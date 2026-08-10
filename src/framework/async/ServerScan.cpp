// Copyright (C) 2007 Id Software, Inc.
//
// Compatibility scanner for the pre-retail async client header.  The final
// ETQW client moves this responsibility into the network service layer.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "AsyncNetwork.h"
#include "ServerScan.h"

namespace {
idServerScan* activeServerScan;
}

idServerScan::idServerScan() :
	scan_state( IDLE ),
	incoming_net( false ),
	incoming_useTimeout( false ),
	incoming_lastTime( 0 ),
	lan_pingtime( -1 ),
	cur_info( 0 ),
	m_pGUI( NULL ),
	listGUI( NULL ),
	m_sort( SORT_PING ),
	m_sortAscending( true ),
	challenge( 0 ),
	endWaitTime( 0 ) {
	activeServerScan = this;
}

void idServerScan::LocalClear() {
	idList< networkServer_t >::Clear();
	net_info.Clear();
	net_servers.Clear();
	m_sortedServers.Clear();
	scan_state = IDLE;
	incoming_net = false;
	incoming_useTimeout = false;
	incoming_lastTime = 0;
	lan_pingtime = -1;
	cur_info = 0;
	endWaitTime = 0;
}

void idServerScan::Clear() {
	LocalClear();
}

void idServerScan::Shutdown() {
	LocalClear();
	m_pGUI = NULL;
	listGUI = NULL;
	if ( activeServerScan == this ) {
		activeServerScan = NULL;
	}
}

void idServerScan::SetupLANScan() {
	LocalClear();
	scan_state = LAN_SCAN;
	lan_pingtime = Sys_Milliseconds();
	challenge = Sys_Milliseconds() ^ 0x6d2b79f5;
}

int idServerScan::InfoResponse( networkServer_t &server ) {
	for ( int i = 0; i < Num(); i++ ) {
		if ( ( *this )[ i ].adr == server.adr ) {
			( *this )[ i ] = server;
			return i;
		}
	}
	Append( server );
	m_sortedServers.Append( Num() - 1 );
	return Num() - 1;
}

void idServerScan::AddServer( int id, const char *serverName ) {
	if ( serverName == NULL ) {
		return;
	}
	inServer_t server;
	memset( &server, 0, sizeof( server ) );
	if ( !Sys_StringToNetAdr( serverName, &server.adr, true ) ) {
		return;
	}
	server.id = id;
	server.time = Sys_Milliseconds();
	net_servers.Append( server );
	incoming_lastTime = server.time;
}

void idServerScan::StartServers( bool timeout ) {
	LocalClear();
	scan_state = WAIT_ON_INIT;
	incoming_net = true;
	incoming_useTimeout = timeout;
	incoming_lastTime = Sys_Milliseconds();
}

void idServerScan::EndServers() {
	incoming_net = false;
	scan_state = net_servers.Num() > 0 ? NET_SCAN : IDLE;
}

void idServerScan::NetScan() {
	scan_state = NET_SCAN;
	cur_info = 0;
}

void idServerScan::RunFrame() {
	const int now = Sys_Milliseconds();
	if ( scan_state == WAIT_ON_INIT && ( !incoming_useTimeout || now - incoming_lastTime >= INCOMING_TIMEOUT ) ) {
		EndServers();
	}
	if ( scan_state == LAN_SCAN && now - lan_pingtime >= REPLY_TIMEOUT ) {
		scan_state = IDLE;
	}
	if ( scan_state == NET_SCAN ) {
		cur_info = net_servers.Num();
		if ( !incoming_net ) {
			scan_state = IDLE;
		}
	}
}

void idServerScan::SetState( scan_state_t state ) {
	scan_state = state;
}

bool idServerScan::GetBestPing( networkServer_t &server ) {
	if ( Num() == 0 ) {
		return false;
	}
	int best = 0;
	for ( int i = 1; i < Num(); i++ ) {
		if ( ( *this )[ i ].ping < ( *this )[ best ].ping ) {
			best = i;
		}
	}
	server = ( *this )[ best ];
	return true;
}

void idServerScan::GUIConfig( idUserInterface *gui, const char *name ) {
	m_pGUI = gui;
}

void idServerScan::GUIUpdateSelected() {
}

void idServerScan::ApplyFilter() {
}

void idServerScan::SetSorting( serverSort_t sort ) {
	if ( m_sort == sort ) {
		m_sortAscending = !m_sortAscending;
	} else {
		m_sort = sort;
		m_sortAscending = true;
	}
}

int idServerScan::GetChallenge() {
	if ( challenge == 0 ) {
		challenge = Sys_Milliseconds() ^ 0x13579bdf;
	}
	return challenge;
}

void idServerScan::EmitGetInfo( netadr_t &server ) {
}

void idServerScan::GUIAdd( int id, const networkServer_t server ) {
}

bool idServerScan::IsFiltered( const networkServer_t server ) {
	return false;
}

int idServerScan::Cmp( const int *a, const int *b ) {
	if ( activeServerScan == NULL || a == NULL || b == NULL ) {
		return 0;
	}
	const networkServer_t& lhs = ( *activeServerScan )[ *a ];
	const networkServer_t& rhs = ( *activeServerScan )[ *b ];
	int result = lhs.ping - rhs.ping;
	return activeServerScan->m_sortAscending ? result : -result;
}
