// Copyright (C) 2007 Id Software, Inc.
//
// ETQW asynchronous client bootstrap.  The Doom 3 networking source that
// previously occupied this file used incompatible UI, authentication, bit
// message, and game interfaces.  This implementation restores the ETQW
// control surface first; protocol details are filled from the retail PDB and
// disassembly as the lower networking layers are reconstructed.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../precompiled.h"
#include "AsyncNetwork.h"
#include "../Session_local.h"

static const int CLIENT_CONNECT_RESEND_MSEC = 1000;
static const int CLIENT_EMPTY_RESEND_MSEC = 500;

idAsyncClient::idAsyncClient() :
	active( false ),
	realTime( 0 ),
	clientTime( 0 ),
	clientId( 0 ),
	clientDataChecksum( 0 ),
	clientNum( 0 ),
	clientState( CS_DISCONNECTED ),
	clientPrediction( 0 ),
	clientPredictTime( 0 ),
	serverId( 0 ),
	serverChallenge( 0 ),
	serverMessageSequence( 0 ),
	lastRconTime( 0 ),
	lastConnectTime( -9999 ),
	lastEmptyTime( -9999 ),
	lastPacketTime( -9999 ),
	lastSnapshotTime( -9999 ),
	snapshotSequence( 0 ),
	snapshotGameFrame( 0 ),
	snapshotGameTime( 0 ),
	gameInitId( GAME_INIT_ID_INVALID ),
	gameFrame( 0 ),
	gameTime( 0 ),
	gameTimeResidual( 0 ),
	guiNetMenu( NULL ),
	updateState( UPDATE_NONE ),
	updateSentTime( 0 ),
	updateDirectDownload( false ),
	updateMime( FILE_OPEN ),
	showUpdateMessage( false ),
	dltotal( 0 ),
	dlnow( 0 ),
	lastFrameDelta( 0 ),
	dlRequest( -1 ),
	dlCount( -1 ),
	currentDlSize( 0 ),
	totalDlSize( 0 ) {
	memset( &serverAddress, 0, sizeof( serverAddress ) );
	memset( &lastRconAddress, 0, sizeof( lastRconAddress ) );
	memset( userCmds, 0, sizeof( userCmds ) );
	memset( dlChecksums, 0, sizeof( dlChecksums ) );
	backgroundDownload.completed = true;
}

void idAsyncClient::Clear() {
	active = false;
	realTime = 0;
	clientTime = 0;
	clientId = 0;
	clientDataChecksum = 0;
	clientNum = 0;
	clientState = CS_DISCONNECTED;
	clientPrediction = 0;
	clientPredictTime = 0;
	serverId = 0;
	serverChallenge = 0;
	serverMessageSequence = 0;
	lastConnectTime = -9999;
	lastEmptyTime = -9999;
	lastPacketTime = -9999;
	lastSnapshotTime = -9999;
	snapshotSequence = 0;
	snapshotGameFrame = 0;
	snapshotGameTime = 0;
	gameInitId = GAME_INIT_ID_INVALID;
	gameFrame = 0;
	gameTime = 0;
	gameTimeResidual = 0;
	lastFrameDelta = 0;
	dlRequest = -1;
	dlCount = -1;
	currentDlSize = 0;
	totalDlSize = 0;
	memset( userCmds, 0, sizeof( userCmds ) );
	memset( dlChecksums, 0, sizeof( dlChecksums ) );
	backgroundDownload.completed = true;
}

void idAsyncClient::Shutdown() {
	DisconnectFromServer();
	serverList.Shutdown();
	updateMSG.Clear();
	updateURL.Clear();
	updateFile.Clear();
	updateFallback.Clear();
	backgroundDownload.url.url.Clear();
	dlList.Clear();
	guiNetMenu = NULL;
}

bool idAsyncClient::InitPort() {
	if ( clientPort.GetPort() != 0 ) {
		return true;
	}
	if ( !clientPort.InitForPort( PORT_ANY ) ) {
		common->Printf( "Couldn't open client network port.\n" );
		return false;
	}
	return true;
}

void idAsyncClient::ClosePort() {
	clientPort.Close();
}

void idAsyncClient::ClearPendingPackets() {
	byte buffer[ MAX_MESSAGE_SIZE ];
	netadr_t from;
	int size = 0;
	while ( clientPort.GetPacket( from, buffer, size, sizeof( buffer ) ) ) {
	}
}

void idAsyncClient::ConnectToServer( const netadr_t adr ) {
	session->Stop();
	if ( !InitPort() ) {
		return;
	}

	ClearPendingPackets();
	Clear();
	serverAddress = adr;
	clientId = Sys_Milliseconds() & CONNECTIONLESS_MESSAGE_ID_MASK;
	clientDataChecksum = declManager->GetChecksum();
	clientState = CS_CHALLENGING;
	active = true;
	lastConnectTime = -9999;
	common->Printf( "Connecting to %s\n", Sys_NetAdrToString( serverAddress ) );
}

void idAsyncClient::ConnectToServer( const char* address ) {
	netadr_t adr;
	if ( address == NULL || !Sys_StringToNetAdr( address, &adr, true ) ) {
		common->Printf( "Couldn't resolve server '%s'\n", address != NULL ? address : "" );
		return;
	}
	if ( adr.port == 0 ) {
		adr.port = PORT_SERVER;
	}
	ConnectToServer( adr );
}

void idAsyncClient::Reconnect() {
	if ( serverAddress.type != NA_BAD ) {
		ConnectToServer( serverAddress );
	}
}

void idAsyncClient::DisconnectFromServer() {
	if ( active ) {
		channel.Shutdown();
	}
	Clear();
}

void idAsyncClient::GetServerInfo( const netadr_t adr ) {
	if ( !InitPort() ) {
		return;
	}
	serverAddress = adr;
	common->DPrintf( "requesting server info from %s\n", Sys_NetAdrToString( adr ) );
}

void idAsyncClient::GetServerInfo( const char* address ) {
	netadr_t adr;
	if ( address != NULL && Sys_StringToNetAdr( address, &adr, true ) ) {
		if ( adr.port == 0 ) {
			adr.port = PORT_SERVER;
		}
		GetServerInfo( adr );
	}
}

void idAsyncClient::GetLANServers() {
	if ( InitPort() ) {
		serverList.SetupLANScan();
	}
}

void idAsyncClient::GetNETServers() {
	if ( InitPort() ) {
		serverList.StartServers( true );
		idAsyncNetwork::GetNETServers();
	}
}

void idAsyncClient::ListServers() {
	for ( int i = 0; i < serverList.Num(); ++i ) {
		common->Printf(
			"%3d: %s %dms (%s)\n",
			i,
			serverList[ i ].serverInfo.GetString( "si_name" ),
			serverList[ i ].ping,
			Sys_NetAdrToString( serverList[ i ].adr )
		);
	}
}

void idAsyncClient::ClearServers() {
	serverList.Clear();
}

void idAsyncClient::RemoteConsole( const char* command ) {
	if ( command == NULL || command[ 0 ] == '\0' ) {
		return;
	}
	netadr_t adr;
	if ( !Sys_StringToNetAdr( idAsyncNetwork::clientRemoteConsoleAddress.GetString(), &adr, true ) ) {
		return;
	}
	if ( adr.port == 0 ) {
		adr.port = PORT_SERVER;
	}
	lastRconAddress = adr;
	lastRconTime = Sys_Milliseconds();
	common->DPrintf( "rcon to %s: %s\n", Sys_NetAdrToString( adr ), command );
}

int idAsyncClient::GetPrediction() const {
	return clientPrediction;
}

int idAsyncClient::GetTimeSinceLastPacket() const {
	return active ? realTime - lastPacketTime : 0;
}

int idAsyncClient::GetOutgoingRate() const {
	return active ? channel.GetOutgoingRate() : 0;
}

int idAsyncClient::GetIncomingRate() const {
	return active ? channel.GetIncomingRate() : 0;
}

float idAsyncClient::GetOutgoingCompression() const {
	return active ? channel.GetOutgoingCompression() : 0.0f;
}

float idAsyncClient::GetIncomingCompression() const {
	return active ? channel.GetIncomingCompression() : 0.0f;
}

float idAsyncClient::GetIncomingPacketLoss() const {
	return active ? channel.GetIncomingPacketLoss() : 0.0f;
}

void idAsyncClient::RunFrame() {
	if ( !active ) {
		serverList.RunFrame();
		return;
	}

	UpdateTime( 100 );
	byte buffer[ MAX_MESSAGE_SIZE ];
	netadr_t from;
	int size = 0;
	while ( clientPort.GetPacket( from, buffer, size, sizeof( buffer ) ) ) {
		idBitMsg msg;
		msg.InitRead( buffer, size );
		ProcessMessage( from, msg );
	}

	if ( CheckTimeout() ) {
		DisconnectFromServer();
		session->StartMenu();
		return;
	}

	SetupConnection();
	if ( clientState >= CS_CONNECTED ) {
		SendUsercmdsToServer();
		ProcessReliableServerMessages();
	}
}

void idAsyncClient::SendReliableGameMessage( const idBitMsg& msg ) {
	if ( active && clientState >= CS_CONNECTED ) {
		channel.SendReliableMessage( msg );
	}
}

void idAsyncClient::SendVersionCheck( bool fromMenu ) {
	common->DPrintf( "version check requested%s\n", fromMenu ? " from menu" : "" );
}

bool idAsyncClient::SendAuthCheck( const char* cdkey, const char* xpkey ) {
	return InitPort();
}

void idAsyncClient::PacifierUpdate() {
	if ( active ) {
		RunFrame();
	}
}

void idAsyncClient::DuplicateUsercmds( int frame, int time ) {
	if ( frame <= 0 ) {
		return;
	}
	for ( int client = 0; client < MAX_ASYNC_CLIENTS; ++client ) {
		idAsyncNetwork::DuplicateUsercmd( userCmds[ ( frame - 1 ) & ( MAX_USERCMD_BACKUP - 1 ) ][ client ],
			userCmds[ frame & ( MAX_USERCMD_BACKUP - 1 ) ][ client ], frame, time );
	}
}

void idAsyncClient::SendUserInfoToServer() {
}

void idAsyncClient::SendEmptyToServer( bool force, bool mapLoad ) {
	if ( !active || clientState < CS_CONNECTED ) {
		return;
	}
	if ( !force && realTime - lastEmptyTime < CLIENT_EMPTY_RESEND_MSEC ) {
		return;
	}
	byte buffer[ 16 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteByte( SERVER_UNRELIABLE_MESSAGE_EMPTY );
	msg.WriteBool( mapLoad );
	channel.SendMessage( clientPort, realTime, msg );
	lastEmptyTime = realTime;
}

void idAsyncClient::SendPingResponseToServer( int time ) {
	byte buffer[ 16 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteByte( CLIENT_UNRELIABLE_MESSAGE_PINGRESPONSE );
	msg.WriteLong( time );
	channel.SendMessage( clientPort, realTime, msg );
}

void idAsyncClient::SendUsercmdsToServer() {
	if ( !channel.ReadyToSend( realTime ) ) {
		return;
	}
	const int index = gameFrame & ( MAX_USERCMD_BACKUP - 1 );
	userCmds[ index ][ clientNum ] = usercmdGen->GetDirectUsercmd();

	byte buffer[ MAX_MESSAGE_SIZE ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteByte( CLIENT_UNRELIABLE_MESSAGE_USERCMD );
	msg.WriteLong( gameFrame );
	idAsyncNetwork::WriteUserCmdDelta( msg, userCmds[ index ][ clientNum ], NULL );
	channel.SendMessage( clientPort, realTime, msg );
}

void idAsyncClient::InitGame( int serverGameInitId, int serverGameFrame, int serverGameTime, const idDict& serverSI ) {
	gameInitId = serverGameInitId;
	gameFrame = serverGameFrame;
	gameTime = serverGameTime;
	snapshotGameFrame = serverGameFrame;
	snapshotGameTime = serverGameTime;
	sessLocal.mapSpawnData.serverInfo = serverSI;
	game->SetServerInfo( serverSI );
	clientState = CS_INGAME;
}

void idAsyncClient::ProcessUnreliableServerMessage( const idBitMsg& msg ) {
	const int type = msg.ReadByte();
	if ( type == SERVER_UNRELIABLE_MESSAGE_PING ) {
		SendPingResponseToServer( msg.ReadLong() );
	} else if ( type == SERVER_UNRELIABLE_MESSAGE_EMPTY ) {
		lastPacketTime = realTime;
	}
}

void idAsyncClient::ProcessReliableServerMessages() {
	byte buffer[ MAX_MESSAGE_SIZE ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	while ( channel.GetReliableMessage( msg ) ) {
		const int type = msg.ReadByte();
		switch ( type ) {
			case SERVER_RELIABLE_MESSAGE_DISCONNECT:
				DisconnectFromServer();
				session->StartMenu();
				return;
			case SERVER_RELIABLE_MESSAGE_GAME:
				game->ClientProcessReliableMessage( msg );
				break;
			case SERVER_RELIABLE_MESSAGE_ENTERGAME:
				clientState = CS_INGAME;
				break;
			default:
				break;
		}
	}
}

void idAsyncClient::ProcessChallengeResponseMessage( const netadr_t from, const idBitMsg& msg ) {
	if ( !Sys_CompareNetAdrBase( from, serverAddress ) ) {
		return;
	}
	serverChallenge = msg.ReadLong();
	clientState = CS_CONNECTING;
	lastPacketTime = realTime;
}

void idAsyncClient::ProcessConnectResponseMessage( const netadr_t from, const idBitMsg& msg ) {
	if ( !Sys_CompareNetAdrBase( from, serverAddress ) ) {
		return;
	}
	serverId = msg.ReadLong();
	clientNum = msg.ReadLong();
	channel.Init( from, clientId );
	clientState = CS_CONNECTED;
	lastPacketTime = realTime;
}

void idAsyncClient::ProcessDisconnectMessage( const netadr_t from, const idBitMsg& msg ) {
	if ( Sys_CompareNetAdrBase( from, serverAddress ) ) {
		DisconnectFromServer();
		session->StartMenu();
	}
}

void idAsyncClient::ProcessInfoResponseMessage( const netadr_t from, const idBitMsg& msg ) {
	networkServer_t server;
	memset( &server, 0, sizeof( server ) );
	server.adr = from;
	server.ping = 0;
	serverList.InfoResponse( server );
}

void idAsyncClient::ProcessPrintMessage( const netadr_t from, const idBitMsg& msg ) {
	char text[ 4096 ];
	msg.ReadString( text, sizeof( text ) );
	common->Printf( "%s\n", text );
}

void idAsyncClient::ProcessServersListMessage( const netadr_t from, const idBitMsg& msg ) {
}

void idAsyncClient::ProcessAuthKeyMessage( const netadr_t from, const idBitMsg& msg ) {
}

void idAsyncClient::ProcessVersionMessage( const netadr_t from, const idBitMsg& msg ) {
}

void idAsyncClient::ConnectionlessMessage( const netadr_t from, const idBitMsg& msg ) {
	char command[ 1024 ];
	msg.ReadString( command, sizeof( command ) );
	if ( idStr::Icmp( command, "challengeResponse" ) == 0 ) {
		ProcessChallengeResponseMessage( from, msg );
	} else if ( idStr::Icmp( command, "connectResponse" ) == 0 ) {
		ProcessConnectResponseMessage( from, msg );
	} else if ( idStr::Icmp( command, "disconnect" ) == 0 ) {
		ProcessDisconnectMessage( from, msg );
	} else if ( idStr::Icmp( command, "infoResponse" ) == 0 ) {
		ProcessInfoResponseMessage( from, msg );
	} else if ( idStr::Icmp( command, "print" ) == 0 ) {
		ProcessPrintMessage( from, msg );
	}
}

void idAsyncClient::ProcessMessage( const netadr_t from, idBitMsg& msg ) {
	msg.BeginReading();
	const int id = msg.ReadLong();
	if ( id == CONNECTIONLESS_MESSAGE_ID ) {
		ConnectionlessMessage( from, msg );
		return;
	}
	if ( !active || !Sys_CompareNetAdrBase( from, channel.GetRemoteAddress() ) ) {
		return;
	}
	int sequence = 0;
	if ( channel.Process( from, realTime, msg, sequence ) ) {
		serverMessageSequence = sequence;
		lastPacketTime = realTime;
		ProcessUnreliableServerMessage( msg );
	}
}

void idAsyncClient::SetupConnection() {
	if ( realTime - lastConnectTime < CLIENT_CONNECT_RESEND_MSEC ) {
		return;
	}
	if ( clientState == CS_CHALLENGING ) {
		common->DPrintf( "sending challenge to %s\n", Sys_NetAdrToString( serverAddress ) );
		lastConnectTime = realTime;
	} else if ( clientState == CS_CONNECTING ) {
		common->DPrintf( "sending connect to %s\n", Sys_NetAdrToString( serverAddress ) );
		lastConnectTime = realTime;
	}
}

void idAsyncClient::ProcessPureMessage( const netadr_t from, const idBitMsg& msg ) {
}

bool idAsyncClient::ValidatePureServerChecksums( const netadr_t from, const idBitMsg& msg ) {
	return true;
}

void idAsyncClient::ProcessReliableMessagePure( const idBitMsg& msg ) {
}

const char* idAsyncClient::HandleGuiCommand( const char* cmd ) {
	return idAsyncNetwork::client.HandleGuiCommandInternal( cmd );
}

const char* idAsyncClient::HandleGuiCommandInternal( const char* cmd ) {
	if ( cmd != NULL && ( idStr::Icmp( cmd, "abort" ) == 0 || idStr::Icmp( cmd, "pure_abort" ) == 0 ) ) {
		DisconnectFromServer();
		return "";
	}
	return NULL;
}

void idAsyncClient::SendVersionDLUpdate( int state ) {
	updateState = static_cast< clientUpdateState_t >( state );
}

void idAsyncClient::HandleDownloads() {
}

void idAsyncClient::Idle() {
	SendEmptyToServer();
}

int idAsyncClient::UpdateTime( int clamp ) {
	const int now = Sys_Milliseconds();
	int delta = now - realTime;
	if ( delta < 0 ) {
		delta = 0;
	}
	if ( clamp > 0 && delta > clamp ) {
		delta = clamp;
	}
	realTime = now;
	clientTime += delta;
	gameTimeResidual += delta;
	return delta;
}

void idAsyncClient::ReadLocalizedServerString( const idBitMsg& msg, char* out, int maxLen ) {
	if ( out == NULL || maxLen <= 0 ) {
		return;
	}
	msg.ReadString( out, maxLen );
}

bool idAsyncClient::CheckTimeout() {
	if ( !active || lastPacketTime < 0 ) {
		return false;
	}
	return realTime - lastPacketTime > idAsyncNetwork::clientServerTimeout.GetInteger() * 1000;
}

void idAsyncClient::ProcessDownloadInfoMessage( const netadr_t from, const idBitMsg& msg ) {
}

int idAsyncClient::GetDownloadRequest( const int checksums[ MAX_PURE_PAKS ], int count, int gamePakChecksum ) {
	dlCount = idMath::ClampInt( 0, MAX_PURE_PAKS - 1, count );
	for ( int i = 0; i < dlCount; ++i ) {
		dlChecksums[ i ] = checksums[ i ];
	}
	if ( dlCount < MAX_PURE_PAKS ) {
		dlChecksums[ dlCount ] = 0;
	}
	dlRequest = Sys_Milliseconds();
	return dlRequest;
}
