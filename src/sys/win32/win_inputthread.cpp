// Copyright (C) 2007 Id Software, Inc.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "win_inputthread.h"

extern void IN_Async();

sdInputThread inputThread;
sdInputThread* sys_inputThread = &inputThread;

sdInputThread::sdInputThread() :
	thread( NULL ),
	timer( NULL ) {
}

void sdInputThread::StartThread() {
	if ( thread != NULL ) {
		return;
	}

	timer = CreateWaitableTimerA( NULL, FALSE, NULL );
	if ( timer == NULL ) {
		common->Error( "sdInputThread::StartThread: CreateWaitableTimer failed" );
	}

	LARGE_INTEGER dueTime;
	dueTime.QuadPart = -10000;	// first sample in one millisecond
	if ( !SetWaitableTimer( timer, &dueTime, 1, NULL, NULL, FALSE ) ) {
		CloseHandle( timer );
		timer = NULL;
		common->Error( "sdInputThread::StartThread: SetWaitableTimer failed" );
	}

	thread = new sdThread( this, THREAD_ABOVE_NORMAL, 0x100000 );
	thread->SetName( "Input" );
	if ( !thread->Start( NULL, 0 ) ) {
		common->Error( "sdInputThread::StartThread: failed to start thread" );
	}
}

void sdInputThread::StopThread() {
	if ( thread != NULL ) {
		thread->Stop();
		thread->Join();
		thread->Destroy();
		thread = NULL;
	}
	if ( timer != NULL ) {
		CancelWaitableTimer( timer );
		CloseHandle( timer );
		timer = NULL;
	}
}

unsigned int sdInputThread::Run( void* ) {
	while ( !Terminating() ) {
		const DWORD result = WaitForSingleObject( timer, 100 );
		if ( result != WAIT_OBJECT_0 ) {
			continue;
		}
		if ( common != NULL && common->IsInitialized() ) {
			IN_Async();
		}
	}
	return 0;
}
