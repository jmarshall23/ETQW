// Copyright (C) 2007 Id Software, Inc.
//
// ETQW asynchronous server bootstrap.  This replaces the incompatible Doom 3
// implementation while the larger AsyncServerBase/Repeater split present in
// the Microsoft PDB is reconstructed.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../precompiled.h"
#include "AsyncNetwork.h"
#include "../Session_local.h"

static const int SERVER_EMPTY_RESEND_MSEC = 500;
static const int SERVER_PING_RESEND_MSEC = 500;

idAsyncServer::idAsyncServer() :
	active( false ),
	realTime( 0 ),
	serverTime( 0 ),
	serverId( 0 ),
	serverDataChecksum( 0 ),
	localClientNum( -1 ),
	gameInitId( 0 ),
	gameFrame( 0 ),
	gameTime( 0 ),
	gameTimeResidual( 0 ),
	nextHeartbeatTime( 0 ),
	nextAsyncStatsTime( 0 ),
	serverReloadingEngine( false ),
	noRconOutput( true ),
	lastAuthTime( 0 ),
	stats_current( 0 ),
	stats_average_sum( 0 ),
	stats_max( 0 ),
	stats_max_index( 0 ) {
	memset( challenges, 0, sizeof( challenges ) );
	memset( userCmds, 0, sizeof( userCmds ) );
	memset( &rconAddress, 0, sizeof( rconAddress ) );
	memset( stats_outrate, 0, sizeof( stats_outrate ) );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		ClearClient( i );
	}
}

bool idAsyncServer::InitPort() {
	if ( serverPort.GetPort() != 0 ) {
		return true;
	}

	const int configuredPort = cvarSystem->GetCVarInteger( "net_port" );
	if ( configuredPort != 0 ) {
		return serverPort.InitForPort( configuredPort );
	}
	for ( int i = 0; i < MAX_SERVER_PORTS; ++i ) {
		if ( serverPort.InitForPort( PORT_SERVER + i ) ) {
			return true;
		}
	}
	common->Printf( "Unable to open server network port.\n" );
	return false;
}

void idAsyncServer::ClosePort() {
	serverPort.Close();
	for ( int i = 0; i < MAX_CHALLENGES; ++i ) {
		challenges[ i ].authReplyPrint.Clear();
	}
}

void idAsyncServer::Spawn() {
	if ( active ) {
		return;
	}

	// Retail stops the frontend/map before it initializes the new server.  In
	// particular, no game client callbacks are made until the requested map has
	// created its rules object.
	session->Stop();
	if ( !InitPort() ) {
		return;
	}

	byte buffer[ MAX_MESSAGE_SIZE ];
	netadr_t from;
	int size = 0;
	while ( serverPort.GetPacket( from, buffer, size, sizeof( buffer ) ) ) {
	}

	memset( challenges, 0, sizeof( challenges ) );
	memset( userCmds, 0, sizeof( userCmds ) );
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		ClearClient( i );
	}

	active = true;
	realTime = Sys_Milliseconds();
	serverTime = realTime;
	serverId = realTime & CONNECTIONLESS_MESSAGE_ID_MASK;
	serverDataChecksum = declManager->GetChecksum();
	localClientNum = -1;
	gameInitId = 1;
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	serverReloadingEngine = false;

	common->Printf( "Server spawned on %s\n", Sys_NetAdrToString( serverPort.GetAdr() ) );
	ExecuteMapChange();
}

void idAsyncServer::Kill() {
	if ( !active ) {
		return;
	}
	if ( game != NULL ) {
		game->OnServerShutdown();
	}
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState != SCS_FREE ) {
			game->ServerClientDisconnect( i );
			ClearClient( i );
		}
	}
	active = false;
	localClientNum = -1;
}

void idAsyncServer::ExecuteMapChange() {
	if ( !active ) {
		return;
	}

	const char* requestedMap = cvarSystem->GetCVarString( "si_map" );
	if ( requestedMap == NULL || requestedMap[ 0 ] == '\0' || game == NULL ) {
		return;
	}

	// OnUserStartMap selects and constructs the sdGameRules instance.  This
	// must precede ServerClientConnect: GUID authentication writes the local
	// client's user group through gameLocal.rules.
	idStr reason;
	idStr mapName;
	const userMapChangeResult_e result = game->OnUserStartMap( requestedMap, reason, mapName );
	if ( result == UMCR_ERROR ) {
		common->Printf( "User Map Start Denied '%s'\n", reason.c_str() );
		return;
	}
	if ( result == UMCR_STOP ) {
		return;
	}

	serverTime = Sys_Milliseconds();
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	memset( userCmds, 0, sizeof( userCmds ) );
	++gameInitId;

	localClientNum = idAsyncNetwork::serverDedicated.GetBool() ? -1 : 0;
	if ( localClientNum >= 0 ) {
		InitLocalClient( localClientNum );
	}

	sessLocal.mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	sessLocal.mapSpawnData.syncedCVars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );
	if ( localClientNum >= 0 ) {
		sessLocal.mapSpawnData.userInfo[ localClientNum ] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
		game->SetClientNum( localClientNum, true );
	}

	sessLocal.ExecuteMapChange( mapName.c_str(), 0, true, true, true );
	if ( localClientNum >= 0 && sessLocal.MapSpawned() ) {
		BeginLocalClient();
	}
}

int idAsyncServer::GetPort() const {
	return serverPort.GetPort();
}

netadr_t idAsyncServer::GetBoundAdr() const {
	return serverPort.GetAdr();
}

int idAsyncServer::GetOutgoingRate() const {
	int rate = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			rate += clients[ i ].channel.GetOutgoingRate();
		}
	}
	return rate;
}

int idAsyncServer::GetIncomingRate() const {
	int rate = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			rate += clients[ i ].channel.GetIncomingRate();
		}
	}
	return rate;
}

bool idAsyncServer::IsClientInGame( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS && clients[ clientNum ].clientState == SCS_INGAME;
}

int idAsyncServer::GetClientPing( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].clientPing : 0;
}

int idAsyncServer::GetClientPrediction( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].clientPrediction : 0;
}

int idAsyncServer::GetClientTimeSinceLastPacket( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? realTime - clients[ clientNum ].lastPacketTime : 0;
}

int idAsyncServer::GetClientTimeSinceLastInput( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? realTime - clients[ clientNum ].lastInputTime : 0;
}

int idAsyncServer::GetClientOutgoingRate( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].channel.GetOutgoingRate() : 0;
}

int idAsyncServer::GetClientIncomingRate( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].channel.GetIncomingRate() : 0;
}

float idAsyncServer::GetClientOutgoingCompression( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].channel.GetOutgoingCompression() : 0.0f;
}

float idAsyncServer::GetClientIncomingCompression( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].channel.GetIncomingCompression() : 0.0f;
}

float idAsyncServer::GetClientIncomingPacketLoss( int clientNum ) const {
	return clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ? clients[ clientNum ].channel.GetIncomingPacketLoss() : 0.0f;
}

int idAsyncServer::GetNumClients() const {
	int count = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			++count;
		}
	}
	return count;
}

int idAsyncServer::GetNumIdleClients() const {
	int count = 0;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED && realTime - clients[ i ].lastInputTime > 30000 ) {
			++count;
		}
	}
	return count;
}

const usercmd_t* idAsyncServer::GetClientUserCmd( int clientNum, int frameNum ) const {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || frameNum < 0 || clients[ clientNum ].clientState < SCS_CONNECTED ) {
		return NULL;
	}
	return &userCmds[ frameNum & ( MAX_USERCMD_BACKUP - 1 ) ][ clientNum ];
}

const idDict& idAsyncServer::GetUserInfo( int clientNum ) const {
	static idDict emptyUserInfo;
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
		return emptyUserInfo;
	}
	return sessLocal.mapSpawnData.userInfo[ clientNum ];
}

int idAsyncServer::AllocateClientSlotForBot( int maxPlayersOnServer ) {
	if ( !active || game == NULL || !sessLocal.MapSpawned() ) {
		return -1;
	}
	const int slotLimit = idMath::ClampInt( 0, MAX_ASYNC_CLIENTS, maxPlayersOnServer );
	int clientNum = -1;
	for ( int i = 0; i < slotLimit; ++i ) {
		if ( clients[ i ].clientState == SCS_FREE ) {
			clientNum = i;
			break;
		}
	}
	if ( clientNum < 0 ) {
		return -1;
	}

	InitClient( clientNum, serverId ^ ( 0x40000000 | clientNum ), 0 );
	clients[ clientNum ].isBot = true;
	clients[ clientNum ].clientState = SCS_INGAME;

	idDict& userInfo = sessLocal.mapSpawnData.userInfo[ clientNum ];
	userInfo.Clear();
	const idStr botName = va( "Bot %d", clientNum );
	userInfo.Set( "ui_name", botName );
	userInfo.Set( "ui_realname", botName );
	userInfo.SetBool( "ui_bot", true );
	game->ValidateUserInfo( clientNum, userInfo );
	game->ServerClientConnect( clientNum );
	game->ServerClientBegin( clientNum, true );
	game->UserInfoChanged( clientNum );
	return clientNum;
}

int idAsyncServer::SetBotUserCommand( int clientNum, int frameNum, const usercmd_t& cmd ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || frameNum < 0 || !clients[ clientNum ].isBot || clients[ clientNum ].clientState != SCS_INGAME ) {
		return 0;
	}
	userCmds[ frameNum & ( MAX_USERCMD_BACKUP - 1 ) ][ clientNum ] = cmd;
	clients[ clientNum ].gameFrame = frameNum;
	clients[ clientNum ].gameTime = cmd.gameTime;
	clients[ clientNum ].lastInputTime = realTime;
	return 1;
}

int idAsyncServer::SetBotUserName( int clientNum, const char* playerName ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || playerName == NULL || playerName[ 0 ] == '\0' || !clients[ clientNum ].isBot || clients[ clientNum ].clientState < SCS_CONNECTED ) {
		return 0;
	}
	idDict& userInfo = sessLocal.mapSpawnData.userInfo[ clientNum ];
	userInfo.Set( "ui_name", playerName );
	userInfo.Set( "ui_realname", playerName );
	userInfo.SetBool( "ui_bot", true );
	game->ValidateUserInfo( clientNum, userInfo );
	game->UserInfoChanged( clientNum );
	SendUserInfoBroadcast( clientNum, userInfo, true );
	return 1;
}

void idAsyncServer::RunFrame() {
	if ( !active ) {
		return;
	}

	const int delta = UpdateTime( 100 );
	gameTimeResidual += delta;
	ProcessConnectionLessMessages();
	CheckClientTimeouts();

	while ( gameTimeResidual >= USERCMD_MSEC ) {
		DuplicateUsercmds( gameFrame, gameTime );
		LocalClientInput();
		game->RunFrame( userCmds[ gameFrame & ( MAX_USERCMD_BACKUP - 1 ) ], USERCMD_MSEC );
		++gameFrame;
		gameTime += USERCMD_MSEC;
		gameTimeResidual -= USERCMD_MSEC;
	}

	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED && !clients[ i ].isBot ) {
			SendSnapshotToClient( i );
			ProcessReliableClientMessages( i );
		}
	}
	MasterHeartbeat();
}

void idAsyncServer::ProcessConnectionLessMessages() {
	byte buffer[ MAX_MESSAGE_SIZE ];
	netadr_t from;
	int size = 0;
	while ( serverPort.GetPacket( from, buffer, size, sizeof( buffer ) ) ) {
		idBitMsg msg;
		msg.InitRead( buffer, size );
		ProcessMessage( from, msg );
	}
}

void idAsyncServer::RemoteConsoleOutput( const char* string ) {
	if ( noRconOutput || string == NULL ) {
		return;
	}
	PrintOOB( rconAddress, SERVER_PRINT_RCON, string );
}

void idAsyncServer::SendReliableGameMessage( int clientNum, const idBitMsg& msg ) {
	if ( clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ) {
		SendReliableMessage( clientNum, msg );
	}
}

void idAsyncServer::SendReliableGameMessageExcluding( int clientNum, const idBitMsg& msg ) {
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( i != clientNum && clients[ i ].clientState >= SCS_CONNECTED ) {
			SendReliableMessage( i, msg );
		}
	}
}

void idAsyncServer::LocalClientSendReliableMessage( const idBitMsg& msg ) {
	if ( localClientNum >= 0 ) {
		game->ServerProcessReliableMessage( localClientNum, msg );
	}
}

void idAsyncServer::MasterHeartbeat( bool force ) {
	if ( !force && realTime < nextHeartbeatTime ) {
		return;
	}
	nextHeartbeatTime = realTime + 5 * 60 * 1000;
}

void idAsyncServer::DropClient( int clientNum, const char* reason ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || clients[ clientNum ].clientState == SCS_FREE ) {
		return;
	}
	common->Printf( "client %d dropped: %s\n", clientNum, reason != NULL ? reason : "" );
	if ( game != NULL ) {
		game->ServerClientDisconnect( clientNum );
	}
	if ( clients[ clientNum ].isBot ) {
		sessLocal.mapSpawnData.userInfo[ clientNum ].Clear();
		ClearClient( clientNum );
		return;
	}
	clients[ clientNum ].clientState = SCS_ZOMBIE;
	clients[ clientNum ].lastPacketTime = realTime;
}

void idAsyncServer::PacifierUpdate() {
	if ( active ) {
		ProcessConnectionLessMessages();
	}
}

void idAsyncServer::UpdateUI( int clientNum ) {
}

void idAsyncServer::UpdateAsyncStatsAvg() {
	const int value = GetOutgoingRate();
	stats_average_sum -= stats_outrate[ stats_current ];
	stats_outrate[ stats_current ] = value;
	stats_average_sum += value;
	if ( value > stats_max ) {
		stats_max = value;
		stats_max_index = stats_current;
	}
	stats_current = ( stats_current + 1 ) % stats_numsamples;
}

void idAsyncServer::GetAsyncStatsAvgMsg( idStr& msg ) {
	msg = va( "out: avg %d B/s, max %d B/s", stats_average_sum / stats_numsamples, stats_max );
}

void idAsyncServer::PrintLocalServerInfo() {
	common->Printf( "server %s: %d clients\n", Sys_NetAdrToString( serverPort.GetAdr() ), GetNumClients() );
}

void idAsyncServer::PrintOOB( const netadr_t to, int opcode, const char* string ) {
	byte buffer[ MAX_MESSAGE_SIZE ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteLong( CONNECTIONLESS_MESSAGE_ID );
	msg.WriteByte( opcode );
	msg.WriteString( string != NULL ? string : "" );
	serverPort.SendPacket( to, msg.GetData(), msg.GetSize() );
}

void idAsyncServer::DuplicateUsercmds( int frame, int time ) {
	if ( frame <= 0 ) {
		return;
	}
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		idAsyncNetwork::DuplicateUsercmd(
			userCmds[ ( frame - 1 ) & ( MAX_USERCMD_BACKUP - 1 ) ][ i ],
			userCmds[ frame & ( MAX_USERCMD_BACKUP - 1 ) ][ i ],
			frame,
			time
		);
	}
}

void idAsyncServer::ClearClient( int clientNum ) {
	serverClient_t& client = clients[ clientNum ];
	client.OS = 0;
	client.clientId = 0;
	client.clientState = SCS_FREE;
	client.clientPrediction = 0;
	client.clientAheadTime = 0;
	client.clientRate = 0;
	client.clientPing = 0;
	client.gameInitSequence = 0;
	client.gameFrame = 0;
	client.gameTime = 0;
	client.lastConnectTime = 0;
	client.lastEmptyTime = 0;
	client.lastPingTime = 0;
	client.lastSnapshotTime = 0;
	client.lastPacketTime = 0;
	client.lastInputTime = 0;
	client.snapshotSequence = 0;
	client.acknowledgeSnapshotSequence = 0;
	client.numDuplicatedUsercmds = 0;
	client.isBot = false;
	client.guid[ 0 ] = '\0';
	client.channel.Shutdown();
}

void idAsyncServer::InitClient( int clientNum, int clientId, int clientRate ) {
	ClearClient( clientNum );
	serverClient_t& client = clients[ clientNum ];
	client.clientId = clientId;
	client.clientRate = clientRate;
	client.clientState = SCS_CONNECTED;
	client.lastConnectTime = realTime;
	client.lastPacketTime = realTime;
	client.channel.SetMaxOutgoingRate( clientRate );
}

void idAsyncServer::InitLocalClient( int clientNum ) {
	InitClient( clientNum, serverId, 0 );
	clients[ clientNum ].channel.Init( serverPort.GetAdr(), serverId );
}

void idAsyncServer::BeginLocalClient() {
	if ( localClientNum >= 0 && clients[ localClientNum ].clientState == SCS_CONNECTED ) {
		idDict& userInfo = sessLocal.mapSpawnData.userInfo[ localClientNum ];
		if ( userInfo.GetString( "ui_name" )[ 0 ] == '\0' ) {
			userInfo.Set( "ui_name", "Player" );
		}
		userInfo.Set( "ui_realname", userInfo.GetString( "ui_name" ) );
		game->ValidateUserInfo( localClientNum, userInfo );
		clients[ localClientNum ].clientState = SCS_INGAME;
		game->ServerClientConnect( localClientNum );
		game->ServerClientBegin( localClientNum, false );
		game->UserInfoChanged( localClientNum );
	}
}

void idAsyncServer::LocalClientInput() {
	if ( localClientNum < 0 || localClientNum >= MAX_ASYNC_CLIENTS ) {
		return;
	}
	usercmd_t& cmd = userCmds[ gameFrame & ( MAX_USERCMD_BACKUP - 1 ) ][ localClientNum ];
	cmd = usercmdGen->GetDirectUsercmd();
	cmd.gameFrame = gameFrame;
	cmd.gameTime = gameTime;
	cmd.duplicateCount = 0;
	clients[ localClientNum ].gameFrame = gameFrame;
	clients[ localClientNum ].gameTime = gameTime;
	clients[ localClientNum ].lastInputTime = realTime;
}

void idAsyncServer::CheckClientTimeouts() {
	const int timeout = idAsyncNetwork::serverClientTimeout.GetInteger() * 1000;
	const int zombieTimeout = idAsyncNetwork::serverZombieTimeout.GetInteger() * 1000;
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState == SCS_ZOMBIE && realTime - clients[ i ].lastPacketTime > zombieTimeout ) {
			ClearClient( i );
		} else if ( clients[ i ].clientState >= SCS_CONNECTED && i != localClientNum && !clients[ i ].isBot &&
			realTime - clients[ i ].lastPacketTime > timeout ) {
			DropClient( i, "timed out" );
		}
	}
}

void idAsyncServer::SendPrintBroadcast( const char* string ) {
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			SendPrintToClient( i, string );
		}
	}
}

void idAsyncServer::SendPrintToClient( int clientNum, const char* string ) {
	byte buffer[ MAX_MESSAGE_SIZE ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteByte( SERVER_RELIABLE_MESSAGE_PRINT );
	msg.WriteString( string != NULL ? string : "" );
	SendReliableMessage( clientNum, msg );
}

void idAsyncServer::SendUserInfoBroadcast( int userInfoNum, const idDict& info, bool sendToAll ) {
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED && ( sendToAll || i != userInfoNum ) ) {
			SendUserInfoToClient( i, userInfoNum, info );
		}
	}
}

void idAsyncServer::SendUserInfoToClient( int clientNum, int userInfoNum, const idDict& info ) {
}

void idAsyncServer::SendSyncedCvarsBroadcast( const idDict& cvars ) {
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		if ( clients[ i ].clientState >= SCS_CONNECTED ) {
			SendSyncedCvarsToClient( i, cvars );
		}
	}
}

void idAsyncServer::SendSyncedCvarsToClient( int clientNum, const idDict& cvars ) {
}

void idAsyncServer::SendApplySnapshotToClient( int clientNum, int sequence ) {
}

bool idAsyncServer::SendEmptyToClient( int clientNum, bool force ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || clients[ clientNum ].clientState < SCS_CONNECTED ) {
		return false;
	}
	serverClient_t& client = clients[ clientNum ];
	if ( !force && realTime - client.lastEmptyTime < SERVER_EMPTY_RESEND_MSEC ) {
		return false;
	}
	byte buffer[ 16 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_EMPTY );
	client.channel.SendMessage( serverPort, realTime, msg );
	client.lastEmptyTime = realTime;
	return true;
}

bool idAsyncServer::SendPingToClient( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS ) {
		return false;
	}
	serverClient_t& client = clients[ clientNum ];
	if ( realTime - client.lastPingTime < SERVER_PING_RESEND_MSEC ) {
		return false;
	}
	byte buffer[ 16 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_PING );
	msg.WriteLong( realTime );
	client.channel.SendMessage( serverPort, realTime, msg );
	client.lastPingTime = realTime;
	return true;
}

void idAsyncServer::SendGameInitToClient( int clientNum ) {
}

bool idAsyncServer::SendSnapshotToClient( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || clients[ clientNum ].clientState < SCS_CONNECTED ) {
		return false;
	}
	if ( realTime - clients[ clientNum ].lastSnapshotTime < idAsyncNetwork::serverSnapshotDelay.GetInteger() ) {
		return false;
	}
	clients[ clientNum ].lastSnapshotTime = realTime;
	return SendEmptyToClient( clientNum, true );
}

void idAsyncServer::ProcessUnreliableClientMessage( int clientNum, const idBitMsg& msg ) {
	const int type = msg.ReadByte();
	if ( type == CLIENT_UNRELIABLE_MESSAGE_PINGRESPONSE ) {
		clients[ clientNum ].clientPing = realTime - msg.ReadLong();
	} else if ( type == CLIENT_UNRELIABLE_MESSAGE_USERCMD ) {
		const int frame = msg.ReadLong();
		usercmd_t& cmd = userCmds[ frame & ( MAX_USERCMD_BACKUP - 1 ) ][ clientNum ];
		idAsyncNetwork::ReadUserCmdDelta( msg, cmd, NULL );
		clients[ clientNum ].lastInputTime = realTime;
	}
}

void idAsyncServer::ProcessReliableClientMessages( int clientNum ) {
	byte buffer[ MAX_MESSAGE_SIZE ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	while ( clients[ clientNum ].channel.GetReliableMessage( msg ) ) {
		const int type = msg.ReadByte();
		if ( type == CLIENT_RELIABLE_MESSAGE_DISCONNECT ) {
			DropClient( clientNum, "disconnected" );
			return;
		}
		if ( type == CLIENT_RELIABLE_MESSAGE_GAME ) {
			game->ServerProcessReliableMessage( clientNum, msg );
		}
	}
}

void idAsyncServer::ProcessChallengeMessage( const netadr_t from, const idBitMsg& msg ) {
}

void idAsyncServer::ProcessConnectMessage( const netadr_t from, const idBitMsg& msg ) {
}

void idAsyncServer::ProcessRemoteConsoleMessage( const netadr_t from, const idBitMsg& msg ) {
	rconAddress = from;
	noRconOutput = false;
}

void idAsyncServer::ProcessGetInfoMessage( const netadr_t from, const idBitMsg& msg ) {
}

bool idAsyncServer::ConnectionlessMessage( const netadr_t from, const idBitMsg& msg ) {
	char command[ 1024 ];
	msg.ReadString( command, sizeof( command ) );
	if ( idStr::Icmp( command, "getChallenge" ) == 0 ) {
		ProcessChallengeMessage( from, msg );
		return true;
	}
	if ( idStr::Icmp( command, "connect" ) == 0 ) {
		ProcessConnectMessage( from, msg );
		return true;
	}
	if ( idStr::Icmp( command, "getInfo" ) == 0 ) {
		ProcessGetInfoMessage( from, msg );
		return true;
	}
	if ( idStr::Icmp( command, "rcon" ) == 0 ) {
		ProcessRemoteConsoleMessage( from, msg );
		return true;
	}
	return false;
}

bool idAsyncServer::ProcessMessage( const netadr_t from, idBitMsg& msg ) {
	msg.BeginReading();
	const int id = msg.ReadLong();
	if ( id == CONNECTIONLESS_MESSAGE_ID ) {
		return ConnectionlessMessage( from, msg );
	}
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		serverClient_t& client = clients[ i ];
		if ( client.clientState < SCS_CONNECTED || client.clientId != id ||
			!Sys_CompareNetAdrBase( from, client.channel.GetRemoteAddress() ) ) {
			continue;
		}
		int sequence = 0;
		if ( client.channel.Process( from, realTime, msg, sequence ) ) {
			client.lastPacketTime = realTime;
			ProcessUnreliableClientMessage( i, msg );
		}
		return true;
	}
	return false;
}

void idAsyncServer::ProcessAuthMessage( const idBitMsg& msg ) {
}

bool idAsyncServer::SendPureServerMessage( const netadr_t to, int OS ) {
	return false;
}

void idAsyncServer::ProcessPureMessage( const netadr_t from, const idBitMsg& msg ) {
}

int idAsyncServer::ValidateChallenge( const netadr_t from, int challenge, int clientId ) {
	for ( int i = 0; i < MAX_CHALLENGES; ++i ) {
		if ( challenges[ i ].challenge == challenge && challenges[ i ].clientId == clientId &&
			Sys_CompareNetAdrBase( challenges[ i ].address, from ) ) {
			return i;
		}
	}
	return -1;
}

bool idAsyncServer::SendReliablePureToClient( int clientNum ) {
	return false;
}

void idAsyncServer::ProcessReliablePure( int clientNum, const idBitMsg& msg ) {
}

bool idAsyncServer::VerifyChecksumMessage( int clientNum, const netadr_t* from, const idBitMsg& msg, idStr& reply, int OS ) {
	reply.Clear();
	return true;
}

void idAsyncServer::SendReliableMessage( int clientNum, const idBitMsg& msg ) {
	if ( clientNum < 0 || clientNum >= MAX_ASYNC_CLIENTS || clients[ clientNum ].clientState < SCS_CONNECTED ) {
		return;
	}
	if ( clients[ clientNum ].isBot ) {
		return;
	}
	if ( !clients[ clientNum ].channel.SendReliableMessage( msg ) ) {
		DropClient( clientNum, "reliable message overflow" );
	}
}

int idAsyncServer::UpdateTime( int clamp ) {
	const int now = Sys_Milliseconds();
	int delta = now - realTime;
	if ( delta < 0 ) {
		delta = 0;
	}
	if ( clamp > 0 && delta > clamp ) {
		delta = clamp;
	}
	realTime = now;
	serverTime += delta;
	return delta;
}

void idAsyncServer::SendEnterGameToClient( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_ASYNC_CLIENTS ) {
		clients[ clientNum ].clientState = SCS_INGAME;
	}
}

void idAsyncServer::ProcessDownloadRequestMessage( const netadr_t from, const idBitMsg& msg ) {
}
