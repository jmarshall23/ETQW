/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "NetworkSystem.h"

idNetworkSystem		networkSystemLocal;
idNetworkSystem *	networkSystem = &networkSystemLocal;


/*
==================
idNetworkSystem::ServerSendReliableMessage
==================
*/
void idNetworkSystem::ServerSendReliableMessage( int clientNum, const idBitMsg &msg ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.SendReliableGameMessage( clientNum, msg );
	}
}

#ifdef SD_SUPPORT_REPEATER
/*
==================
idNetworkSystem::RepeaterSendReliableMessage
==================
*/
void idNetworkSystem::RepeaterSendReliableMessage( int clientNum, const idBitMsg& msg, bool ignoreRelays ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.SendReliableGameMessage( clientNum, msg );
	}
}
#endif // SD_SUPPORT_REPEATER

/*
==================
idNetworkSystem::ServerGetClientPing
==================
*/
int idNetworkSystem::ServerGetClientPing( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientPing( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientPrediction
==================
*/
int idNetworkSystem::ServerGetClientPrediction( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientPrediction( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientTimeSinceLastPacket
==================
*/
int idNetworkSystem::ServerGetClientTimeSinceLastPacket( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientTimeSinceLastPacket( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientTimeSinceLastInput
==================
*/
int idNetworkSystem::ServerGetClientTimeSinceLastInput( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientTimeSinceLastInput( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientOutgoingRate
==================
*/
int idNetworkSystem::ServerGetClientOutgoingRate( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientOutgoingRate( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientIncomingRate
==================
*/
int idNetworkSystem::ServerGetClientIncomingRate( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientIncomingRate( clientNum );
	}
	return 0;
}

/*
==================
idNetworkSystem::ServerGetClientIncomingPacketLoss
==================
*/
float idNetworkSystem::ServerGetClientIncomingPacketLoss( int clientNum ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetClientIncomingPacketLoss( clientNum );
	}
	return 0.0f;
}

/*
==================
idNetworkSystem::ClientSendReliableMessage
==================
*/
void idNetworkSystem::ClientSendReliableMessage( const idBitMsg &msg ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		idAsyncNetwork::client.SendReliableGameMessage( msg );
	} else if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.LocalClientSendReliableMessage( msg );
	}
}

/*
==================
idNetworkSystem::ClientGetPrediction
==================
*/
int idNetworkSystem::ClientGetPrediction( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetPrediction();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetTimeSinceLastPacket
==================
*/
int idNetworkSystem::ClientGetTimeSinceLastPacket( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetTimeSinceLastPacket();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetOutgoingRate
==================
*/
int idNetworkSystem::ClientGetOutgoingRate( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetOutgoingRate();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetIncomingRate
==================
*/
int idNetworkSystem::ClientGetIncomingRate( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetIncomingRate();
	}
	return 0;
}

/*
==================
idNetworkSystem::ClientGetIncomingPacketLoss
==================
*/
float idNetworkSystem::ClientGetIncomingPacketLoss( void ) {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetIncomingPacketLoss();
	}
	return 0.0f;
}

/*
===============================================================================

	The public SDK exposes a wider network service vtable than the surviving
	Doom 3 implementation supplied.  Keep the boundary complete while the
	retail repeater, demo, VOIP, and bot transports are reconstructed.

===============================================================================
*/

idNetworkSystem::~idNetworkSystem() {
}

void idNetworkSystem::ServerGetClientNetworkInfo( int clientNum, clientNetworkAddress_t& info ) {
	memset( &info, 0, sizeof( info ) );
}

void idNetworkSystem::ServerGetClientNetId( int clientNum, sdNetClientId& netClientId ) {
}

const usercmd_t* idNetworkSystem::ServerGetClientUserCmd( int clientNum, int frameNum ) {
	return NULL;
}

void idNetworkSystem::ServerKickClient( int clientNum, const char* reason, bool localizedReason ) {
	if ( idAsyncNetwork::server.IsActive() ) {
		idAsyncNetwork::server.DropClient( clientNum, reason != NULL ? reason : "disconnected" );
	}
}

int idNetworkSystem::AllocateClientSlotForBot( int maxPlayersOnServer ) {
	return -1;
}

int idNetworkSystem::ServerSetBotUserCommand( int clientNum, int frameNum, const usercmd_t& cmd ) {
	return 0;
}

int idNetworkSystem::ServerSetBotUserName( int clientNum, const char* playerName ) {
	return 0;
}

const usercmd_t* idNetworkSystem::ClientGetUserCmd( int clientNum, int frameNum ) {
	return NULL;
}

void idNetworkSystem::WriteClientUserCmds( int clientNum, idBitMsg& msg ) {
}

void idNetworkSystem::ReadClientUserCmds( int clientNum, const idBitMsg& msg ) {
}

bool idNetworkSystem::IsDedicated() {
	return cvarSystem != NULL && cvarSystem->GetCVarBool( "net_serverDedicated" );
}

bool idNetworkSystem::IsLANServer() {
	return idAsyncNetwork::server.IsActive() && cvarSystem != NULL && cvarSystem->GetCVarBool( "si_serverDedicated" ) == 0;
}

bool idNetworkSystem::IsActive() {
	return idAsyncNetwork::client.IsActive() || idAsyncNetwork::server.IsActive();
}

bool idNetworkSystem::IsClient() {
	return idAsyncNetwork::client.IsActive();
}

netadr_t idNetworkSystem::ClientGetServerAddress() const {
	netadr_t address;
	memset( &address, 0, sizeof( address ) );
	address.type = NA_BAD;
	return address;
}

netadr_t idNetworkSystem::ServerGetBoundAddress() const {
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetBoundAdr();
	}
	netadr_t address;
	memset( &address, 0, sizeof( address ) );
	address.type = NA_BAD;
	return address;
}

void idNetworkSystem::WriteSound( short* buffer, int numSamples ) {
}

int idNetworkSystem::UpdateSound( float* buffer, int numSpeakers, int numSamples ) {
	return 0;
}

void idNetworkSystem::BeginLevelLoad() {
}

void idNetworkSystem::EndLevelLoad() {
}

void idNetworkSystem::EnableVoip( voiceMode_t mode ) {
}

void idNetworkSystem::DisableVoip() {
}

int idNetworkSystem::GetLastVoiceSentTime() {
	return 0;
}

int idNetworkSystem::GetLastVoiceReceivedTime( int clientIndex ) {
	return 0;
}

int idNetworkSystem::ClientGetFrameTime() {
	return USERCMD_MSEC;
}

int idNetworkSystem::GetDemoState( int& time, int& position, int& length, int& startPosition, int& endPosition, int& cutStartMarker, int& cutEndMarker ) {
	time = position = length = startPosition = endPosition = cutStartMarker = cutEndMarker = 0;
	return 0;
}

const char* idNetworkSystem::GetDemoName() {
	return "";
}

bool idNetworkSystem::CanPlayDemo( const char* fileName ) {
	return false;
}

const idDict& idNetworkSystem::GetUserInfo( int clientNum ) {
	static idDict emptyUserInfo;
	return emptyUserInfo;
}

bool idNetworkSystem::IsRankedServer() {
	return false;
}

void idNetworkSystem::StartSoundTest( int duration ) {
}

bool idNetworkSystem::IsSoundTestActive() {
	return false;
}

bool idNetworkSystem::IsSoundTestPlaybackActive() {
	return false;
}

float idNetworkSystem::GetSoundTestProgress() {
	return 0.0f;
}

voiceMode_t idNetworkSystem::GetVoiceMode() {
	return VO_GLOBAL;
}

void idNetworkSystem::RegisterServerInterest( const netadr_t& address ) {
}

#if !defined( SD_PUBLIC_TOOLS )
bool idNetworkSystem::HTTPEnable( bool enable ) {
	return false;
}
#endif

#ifdef SD_SUPPORT_REPEATER
void idNetworkSystem::RepeaterSetInfo( const idDict& info ) {
}

const idDict& idNetworkSystem::RepeaterGetClientInfo( int clientNum ) {
	static idDict emptyClientInfo;
	return emptyClientInfo;
}

void idNetworkSystem::SetClientRepeaterUserOrigin( const repeaterUserOrigin_t& origin ) {
}
#endif // SD_SUPPORT_REPEATER
