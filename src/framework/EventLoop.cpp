// Copyright (C) 2007 Id Software, Inc.
//

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "../sys/sys_local.h"
#include "EventLoop.h"

idCVar idEventLoop::com_journal(
	"com_journal",
	"0",
	CVAR_INIT | CVAR_SYSTEM,
	"1 = record journal, 2 = play back journal",
	0,
	2,
	idCmdSystem::ArgCompletion_Integer< 0, 2 >
);

idEventLoop eventLoopLocal;
idEventLoop* eventLoop = &eventLoopLocal;

/*
=================
idEventLoop::idEventLoop
=================
*/
idEventLoop::idEventLoop() :
	com_journalFile( NULL ),
	com_journalDataFile( NULL ),
	initialTimeOffset( 0 ) {
}

/*
=================
idEventLoop::~idEventLoop
=================
*/
idEventLoop::~idEventLoop() {
}

/*
=================
idEventLoop::GetRealEvent
=================
*/
const sdSysEvent* idEventLoop::GetRealEvent() {
	if ( com_journal.GetInteger() == 2 && com_journalFile != NULL ) {
		sdSysEvent* event = const_cast< sdSysEvent* >( sys->GenerateBlankEvent() );
		if ( event == NULL ) {
			common->FatalError( "idEventLoop::GetRealEvent: failed to allocate journal event" );
			return NULL;
		}
		event->Restore( com_journalFile );
		return event;
	}

	const sdSysEvent* event = sys->GetEvent();
	if ( event != NULL && com_journal.GetInteger() == 1 && com_journalFile != NULL ) {
		const_cast< sdSysEvent* >( event )->Save( com_journalFile );
	}
	return event;
}

/*
=================
idEventLoop::PushEvent
=================
*/
void idEventLoop::PushEvent( sdSysEvent* event ) {
	if ( event == NULL ) {
		return;
	}
	event->GetNode().AddToEnd( com_pushedEvents );
}

/*
=================
idEventLoop::GetEvent
=================
*/
const sdSysEvent* idEventLoop::GetEvent() {
	sdSysEvent* event = com_pushedEvents.Next();
	if ( event != NULL ) {
		return event;
	}
	return GetRealEvent();
}

/*
=================
idEventLoop::ProcessEvent
=================
*/
void idEventLoop::ProcessEvent( const sdSysEvent& event ) {
	if ( event.IsConsoleEvent() ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, event.GetCommand() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
		return;
	}

	session->ProcessEvent( &event );

	bool down = false;
	idKey* key = keyInputManager->GetKeyForEvent( event, down );
	if ( key != NULL ) {
		key->SetDown( down );
	}
}

/*
===============
idEventLoop::RunEventLoop
===============
*/
void idEventLoop::RunEventLoop( bool commandExecution ) {
	for ( ;; ) {
		if ( commandExecution ) {
			cmdSystem->ExecuteCommandBuffer( true );
		}

		const sdSysEvent* event = GetEvent();
		if ( event == NULL ) {
			break;
		}

		ProcessEvent( *event );
		sys->FreeEvent( event );
	}
}

/*
=============
idEventLoop::Init
=============
*/
void idEventLoop::Init() {
	initialTimeOffset = Sys_Milliseconds();

	common->StartupVariable( "com_journal" );

	if ( com_journal.GetInteger() == 1 ) {
		common->Printf( "Journaling events\n" );
		com_journalFile = fileSystem->OpenFileWrite( "journal.dat", "fs_savepath" );
		com_journalDataFile = fileSystem->OpenFileWrite( "journaldata.dat", "fs_savepath" );
	} else if ( com_journal.GetInteger() == 2 ) {
		common->Printf( "Replaying journaled events\n" );
		com_journalFile = fileSystem->OpenFileRead( "journal.dat", true, NULL, true );
		com_journalDataFile = fileSystem->OpenFileRead( "journaldata.dat", true, NULL, true );
	}

	if ( com_journal.GetInteger() != 0 && ( com_journalFile == NULL || com_journalDataFile == NULL ) ) {
		if ( com_journalFile != NULL ) {
			fileSystem->CloseFile( com_journalFile );
		}
		if ( com_journalDataFile != NULL ) {
			fileSystem->CloseFile( com_journalDataFile );
		}
		com_journal.SetInteger( 0 );
		com_journalFile = NULL;
		com_journalDataFile = NULL;
		common->Printf( "Couldn't open journal files\n" );
	}
}

/*
=============
idEventLoop::Shutdown
=============
*/
void idEventLoop::Shutdown() {
	if ( com_journalFile != NULL ) {
		fileSystem->CloseFile( com_journalFile );
		com_journalFile = NULL;
	}
	if ( com_journalDataFile != NULL ) {
		fileSystem->CloseFile( com_journalDataFile );
		com_journalDataFile = NULL;
	}
}

/*
================
idEventLoop::Milliseconds
================
*/
int idEventLoop::Milliseconds() {
	return Sys_Milliseconds() - initialTimeOffset;
}

/*
================
idEventLoop::JournalLevel
================
*/
int idEventLoop::JournalLevel() {
	if ( com_journalFile == NULL || com_journalDataFile == NULL ) {
		return 0;
	}
	return com_journal.GetInteger();
}
