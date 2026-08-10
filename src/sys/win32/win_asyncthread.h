/*
===========================================================================

Win32 asynchronous common-services thread.

===========================================================================
*/

#ifndef __WIN_ASYNCTHREAD_H__
#define __WIN_ASYNCTHREAD_H__

#include "../../idlib/threading/ThreadProcess.h"
#include "../../idlib/threading/Thread.h"

class sdAsyncThread : public sdThreadProcess {
public:
					sdAsyncThread();

	void		StartThread();
	void		StopThread();
	virtual unsigned int Run( void* parm );

private:
	sdThread*	thread;
	void*		timer;
};

extern sdAsyncThread asyncThread;
extern sdAsyncThread* sys_asyncThread;

#endif /* !__WIN_ASYNCTHREAD_H__ */
