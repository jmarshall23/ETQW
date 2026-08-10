// Copyright (C) 2007 Id Software, Inc.
//

#ifndef __WIN_XINPUT_H__
#define __WIN_XINPUT_H__

#include <XInput.h>

class sdXInputController : public sdController {
public:
					sdXInputController();

	bool			Init();
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
	void			ConvertCircleInputToSquare( short& xInput, short& yInput );

	int				numEvents;
	event_t			events[ MAX_CONTROLLER_EVENTS ];
	int				lastFrameTime;
	XINPUT_STATE		oldInputState;
	bool			isGamePad;
};

class sdControllerAPIXInput : public sdControllerAPI {
public:
					sdControllerAPIXInput();
	virtual void	Init( const int apiIndex );
	virtual void	Shutdown();
	virtual const char* GetName() const { return "XInput"; }
	virtual int		GetMaxControllers() const { return XUSER_MAX_COUNT; }

private:
	bool			LoadAPI();
	void			FreeAPI();

	HMODULE			XInputDll;
};

#if defined( _M_IX86 )
static_assert( sizeof( sdXInputController ) == 0x360, "sdXInputController layout drift" );
static_assert( sizeof( sdControllerAPIXInput ) == 0x0c, "sdControllerAPIXInput layout drift" );
#endif

#endif /* !__WIN_XINPUT_H__ */
