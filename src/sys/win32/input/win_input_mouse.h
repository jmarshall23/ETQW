// Copyright (C) 2007 Id Software, Inc.
//


#ifndef __WIN_INPUT_MOUSE_H__
#define __WIN_INPUT_MOUSE_H__

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

class idMouseDInput : public idMouse {
public:
					idMouseDInput();
	virtual			~idMouseDInput();

	virtual bool	Init();
	virtual void	Shutdown();
	virtual bool	IsActive() const { return active; }
	virtual void	Activate();
	virtual void	Deactivate();
	virtual void	GrabCursor( bool grab );
	virtual int		PollInputEvents( bool postEvents );
	virtual int		ReturnInputEvent( const int n, int& action, int& value );
	virtual void	EndInputEvents() {}

private:
	void			QueueEvents( int numEvents );

	static const int directInputBufferSize = 256;
	bool			active;
	IDirectInputDevice8A* mouseDevice;
	DIDEVICEOBJECTDATA polled_didod[ directInputBufferSize ];
};

class idMouseRawInput : public idMouse {
public:
					idMouseRawInput();
	virtual			~idMouseRawInput();

	virtual bool	Init();
	virtual void	Shutdown();
	virtual bool	IsActive() const { return active; }
	virtual void	Activate();
	virtual void	Deactivate();
	virtual void	GrabCursor( bool grab );
	virtual int		PollInputEvents( bool ) { return 0; }
	virtual int		ReturnInputEvent( const int, int&, int& ) { return 0; }
	virtual void	EndInputEvents() {}

private:
	bool			LoadAPI();
	void			FreeAPI();

	bool			initialized;
	bool			active;
	HMODULE			user32Dll;
};

extern idCVar m_rawInput;

#if defined( _M_IX86 )
static_assert( sizeof( idMouseDInput ) == 0x140c, "idMouseDInput layout drift" );
static_assert( sizeof( idMouseRawInput ) == 0x0c, "idMouseRawInput layout drift" );
#endif

#endif /* !__WIN_INPUT_MOUSE_H__ */

