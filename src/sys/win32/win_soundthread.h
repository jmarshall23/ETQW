// Copyright (C) 2007 Id Software, Inc.

#ifndef __WIN_SOUNDTHREAD_H__
#define __WIN_SOUNDTHREAD_H__

#include "../../idlib/threading/ThreadProcess.h"
#include "../../idlib/threading/Thread.h"

class sdSoundThread : public sdThreadProcess {
public:
					sdSoundThread();

	void		StartThread();
	void		StopThread();
	virtual unsigned int Run( void* parm );

private:
	sdThread*	thread;
	void*		timer;
};

extern sdSoundThread soundThread;
extern sdSoundThread* sys_soundThread;

#endif /* !__WIN_SOUNDTHREAD_H__ */
