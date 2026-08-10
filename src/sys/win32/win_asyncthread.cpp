/*
===========================================================================

Win32 asynchronous common-services thread.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "win_asyncthread.h"

sdAsyncThread asyncThread;
sdAsyncThread* sys_asyncThread = &asyncThread;

sdAsyncThread::sdAsyncThread() :
	thread( NULL ),
	timer( NULL ) {
}

void sdAsyncThread::StartThread() {
	timer = CreateWaitableTimerA( NULL, FALSE, NULL );
	if ( timer == NULL ) {
		common->Error( "sdAsyncThread::StartThread : CreateWaitableTimer failed" );
	}

	LARGE_INTEGER dueTime;
	dueTime.QuadPart = 0;
	SetWaitableTimer( timer, &dueTime, 1, NULL, NULL, TRUE );

	thread = new sdThread( this, THREAD_ABOVE_NORMAL, 0x100000 );
	thread->SetName( "Async" );
	if ( !thread->Start( NULL, 0 ) ) {
		common->Error( "sdAsyncThread::StartThread : failed to start thread" );
	}
}

void sdAsyncThread::StopThread() {
	if ( thread != NULL ) {
		thread->Stop();
		thread->Join();
		thread->Destroy();
		thread = NULL;
	}
}

unsigned int sdAsyncThread::Run( void* parm ) {
	Sys_Milliseconds();
	while ( !Terminating() ) {
		if ( WaitForSingleObject( timer, 100 ) != WAIT_OBJECT_0 ) {
			OutputDebugStringA( "sdAsyncThread::ThreadProc : bad wait return" );
		}
		common->Async();
	}
	return 0;
}

#if defined( _M_IX86 )
static_assert( sizeof( sdAsyncThread ) == 0x10, "sdAsyncThread layout drift" );
#endif
