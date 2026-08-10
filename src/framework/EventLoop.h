// Copyright (C) 2007 Id Software, Inc.
//

#ifndef __EVENTLOOP_H__
#define __EVENTLOOP_H__

class sdSysEvent;

/*
===============================================================================

	The event loop receives system events and dispatches them to the session.
	Event storage is owned by idSys; queued events are linked through the node
	embedded in sdSysEvent.

===============================================================================
*/

class idEventLoop {
public:
							idEventLoop();
							~idEventLoop();

	void					Init();
	void					Shutdown();

	const sdSysEvent*		GetEvent();
	void					RunEventLoop( bool commandExecution = true );
	int						Milliseconds();
	int						JournalLevel();

	idFile*					com_journalFile;
	idFile*					com_journalDataFile;

	void					ProcessEvent( const sdSysEvent& event );

private:
	int						initialTimeOffset;
	idLinkList< sdSysEvent >	com_pushedEvents;

	static idCVar			com_journal;

	const sdSysEvent*		GetRealEvent();
	void					PushEvent( sdSysEvent* event );
};

extern idEventLoop* eventLoop;

#endif /* !__EVENTLOOP_H__ */
