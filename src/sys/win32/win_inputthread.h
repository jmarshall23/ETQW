// Copyright (C) 2007 Id Software, Inc.

#ifndef __WIN_INPUTTHREAD_H__
#define __WIN_INPUTTHREAD_H__

#include "../../idlib/threading/ThreadProcess.h"
#include "../../idlib/threading/Thread.h"

class sdInputThread : public sdThreadProcess {
public:
					sdInputThread();

	void		StartThread();
	void		StopThread();
	virtual unsigned int Run( void* parm );

private:
	sdThread*	thread;
	void*		timer;
};

extern sdInputThread inputThread;
extern sdInputThread* sys_inputThread;

#endif /* !__WIN_INPUTTHREAD_H__ */
