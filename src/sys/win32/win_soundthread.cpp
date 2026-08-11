// Copyright (C) 2007 Id Software, Inc.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "win_soundthread.h"
#include "../../framework/UsercmdGen.h"
#include "../../sound/SoundSystem.h"
#include "../../idlib/threading/Lock.h"

sdSoundThread soundThread;
sdSoundThread* sys_soundThread = &soundThread;

sdSoundThread::sdSoundThread() :
	thread( NULL ),
	timer( NULL ) {
}

void sdSoundThread::StartThread() {
	if ( thread != NULL ) {
		return;
	}

	timer = CreateWaitableTimerA( NULL, FALSE, NULL );
	if ( timer == NULL ) {
		common->Error( "sdSoundThread::StartThread: CreateWaitableTimer failed" );
	}

	LARGE_INTEGER dueTime;
	dueTime.QuadPart = -10000;
	if ( !SetWaitableTimer( timer, &dueTime, USERCMD_MSEC, NULL, NULL, FALSE ) ) {
		CloseHandle( timer );
		timer = NULL;
		common->Error( "sdSoundThread::StartThread: SetWaitableTimer failed" );
	}

	thread = new sdThread( this, THREAD_ABOVE_NORMAL, 0x100000 );
	thread->SetName( "Sound" );
	if ( !thread->Start( NULL, 0 ) ) {
		common->Error( "sdSoundThread::StartThread: failed to start thread" );
	}
}

void sdSoundThread::StopThread() {
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

unsigned int sdSoundThread::Run( void* ) {
	while ( !Terminating() ) {
		const DWORD result = WaitForSingleObject( timer, 100 );
		if ( result != WAIT_OBJECT_0 ) {
			continue;
		}
		if ( common == NULL || !common->IsInitialized() || soundSystem == NULL ) {
			continue;
		}

		// The same lock protects foreground world changes and backend mixing.
		sdScopedLock< true > soundLock( soundSystem->GetLock() );
		soundSystem->AsyncUpdate( Sys_Milliseconds() );
	}
	return 0;
}
