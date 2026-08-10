// Copyright (C) 2007 Id Software, Inc.
//


#ifndef __WIN_DINPUT_H__
#define __WIN_DINPUT_H__

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

class sdDInputController : public sdController {
public:
					sdDInputController();

	bool			Init( IDirectInputDevice8A*& controllerDevice );
	virtual void	UpdateState();
	virtual int		PollInputEvents();
	virtual int		GetNumEvents() { return numEvents; }
	virtual int		ReturnInputEvent( const int n, int& action, int& value );
	virtual void	EndInputEvents() {}

protected:
	struct event_t {
		int event;
		int value;
	};

	void			PostInputEvent( int event, int value );

private:
	static BOOL CALLBACK EnumObjects( const DIDEVICEOBJECTINSTANCEA* object, void* context );

	int				numEvents;
	event_t			events[ MAX_CONTROLLER_EVENTS ];
	int				numAxes;
	int				axes[ MAX_CONTROLLER_AXES ];
	int				lastFrameTime;
	IDirectInputDevice8A*	controllerDevice;
	DIJOYSTATE2		oldInputState;
};

class sdControllerAPIDInput : public sdControllerAPI {
public:
	virtual void	Init( const int apiIndex );
	virtual void	Shutdown();
	virtual const char* GetName() const { return "DirectInput"; }
	virtual int		GetMaxControllers() const { return 4; }

	static const char* APIName() { return "DirectInput"; }

private:
	static BOOL CALLBACK EnumControllers( const DIDEVICEINSTANCEA* device, void* context );
	static bool		IsXInputDevice( const GUID* productGuid );
};

#if defined( _M_IX86 )
static_assert( sizeof( sdDInputController ) == 0x484, "sdDInputController layout drift" );
static_assert( sizeof( sdControllerAPIDInput ) == 0x08, "sdControllerAPIDInput layout drift" );
#endif

#endif /* !__WIN_DINPUT_H__ */

