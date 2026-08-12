// Copyright (C) 2007 Id Software, Inc.
//
// SDL2 keyboard and mouse implementation adapted from Darklight2 for ETQW's
// richer idSys input interfaces. DirectInput and XInput remain available only
// for the controller APIs exposed by the game.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "sys_local.h"
#include "win32/win_local.h"
#include "../idlib/threading/Lock.h"

namespace {

struct sdlKeyboardPoll_t {
	keyNum_t key;
	bool down;

	sdlKeyboardPoll_t() : key( K_INVALID ), down( false ) {}
	sdlKeyboardPoll_t( keyNum_t key_, bool down_ ) : key( key_ ), down( down_ ) {}
};

struct sdlMousePoll_t {
	int action;
	int value;

	sdlMousePoll_t() : action( 0 ), value( 0 ) {}
	sdlMousePoll_t( int action_, int value_ ) : action( action_ ), value( value_ ) {}
};

idList< sdlKeyboardPoll_t > keyboardPolls;
idList< sdlMousePoll_t > mousePolls;
sdLock inputPollLock;
bool inputInitialized;
unsigned int lastTextScanCode;

keyNum_t MapSDLKey( const SDL_Keysym& keysym ) {
	const SDL_Scancode scan = keysym.scancode;
	if ( scan == SDL_SCANCODE_GRAVE ) {
		return K_CONSOLE;
	}
	if ( scan == SDL_SCANCODE_0 ) {
		return K_0;
	}
	if ( scan >= SDL_SCANCODE_1 && scan <= SDL_SCANCODE_9 ) {
		return static_cast< keyNum_t >( K_1 + scan - SDL_SCANCODE_1 );
	}
	if ( scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_Z ) {
		return static_cast< keyNum_t >( K_A + scan - SDL_SCANCODE_A );
	}

	switch ( keysym.sym ) {
		case SDLK_BACKSPACE: return K_BACKSPACE;
		case SDLK_TAB: return K_TAB;
		case SDLK_RETURN: return K_ENTER;
		case SDLK_ESCAPE: return K_ESCAPE;
		case SDLK_SPACE: return K_SPACE;
		case SDLK_EXCLAIM: return K_EXCLAMATION;
		case SDLK_HASH: return K_HASH;
		case SDLK_DOLLAR: return K_DOLLAR;
		case SDLK_AMPERSAND: return K_AMPERSAND;
		case SDLK_QUOTE: return K_APOSTROPHE;
		case SDLK_LEFTPAREN: return K_LEFTPARENTHESIS;
		case SDLK_RIGHTPAREN: return K_RIGHTPARENTHESIS;
		case SDLK_ASTERISK: return K_ASTERISK;
		case SDLK_PLUS: return K_PLUS;
		case SDLK_COMMA: return K_COMMA;
		case SDLK_MINUS: return K_MINUS;
		case SDLK_PERIOD: return K_PERIOD;
		case SDLK_SLASH: return K_SLASH;
		case SDLK_SEMICOLON: return K_SEMICOLON;
		case SDLK_EQUALS: return K_EQUALS;
		case SDLK_LEFTBRACKET: return K_LEFTBRACKET;
		case SDLK_BACKSLASH: return K_BACKSLASH;
		case SDLK_RIGHTBRACKET: return K_RIGHTBRACKET;
		case SDLK_BACKQUOTE: return K_BACKQUOTE;
		case SDLK_APPLICATION: return K_COMMAND;
		case SDLK_CAPSLOCK: return K_CAPSLOCK;
		case SDLK_SCROLLLOCK: return K_SCROLL;
		case SDLK_PAUSE: return K_PAUSE;
		case SDLK_UP: return K_UPARROW;
		case SDLK_DOWN: return K_DOWNARROW;
		case SDLK_LEFT: return K_LEFTARROW;
		case SDLK_RIGHT: return K_RIGHTARROW;
		case SDLK_LGUI: return K_LWIN;
		case SDLK_RGUI: return K_RWIN;
		case SDLK_MENU: return K_MENU;
		case SDLK_LALT: return K_ALT;
		case SDLK_RALT:
		case SDLK_MODE: return K_RIGHT_ALT;
		case SDLK_LCTRL: return K_CTRL;
		case SDLK_RCTRL: return K_RIGHT_CTRL;
		case SDLK_LSHIFT: return K_SHIFT;
		case SDLK_RSHIFT: return K_RIGHT_SHIFT;
		case SDLK_INSERT: return K_INS;
		case SDLK_DELETE: return K_DEL;
		case SDLK_PAGEDOWN: return K_PGDN;
		case SDLK_PAGEUP: return K_PGUP;
		case SDLK_HOME: return K_HOME;
		case SDLK_END: return K_END;
		case SDLK_F1: return K_F1;
		case SDLK_F2: return K_F2;
		case SDLK_F3: return K_F3;
		case SDLK_F4: return K_F4;
		case SDLK_F5: return K_F5;
		case SDLK_F6: return K_F6;
		case SDLK_F7: return K_F7;
		case SDLK_F8: return K_F8;
		case SDLK_F9: return K_F9;
		case SDLK_F10: return K_F10;
		case SDLK_F11: return K_F11;
		case SDLK_F12: return K_F12;
		case SDLK_F13: return K_F13;
		case SDLK_F14: return K_F14;
		case SDLK_F15: return K_F15;
		case SDLK_F16: return K_F16;
		case SDLK_KP_7: return K_KP_HOME;
		case SDLK_KP_8: return K_KP_UPARROW;
		case SDLK_KP_9: return K_KP_PGUP;
		case SDLK_KP_4: return K_KP_LEFTARROW;
		case SDLK_KP_5: return K_KP_5;
		case SDLK_KP_6: return K_KP_RIGHTARROW;
		case SDLK_KP_1: return K_KP_END;
		case SDLK_KP_2: return K_KP_DOWNARROW;
		case SDLK_KP_3: return K_KP_PGDN;
		case SDLK_KP_ENTER: return K_KP_ENTER;
		case SDLK_KP_0: return K_KP_INS;
		case SDLK_KP_PERIOD: return K_KP_DEL;
		case SDLK_KP_DIVIDE: return K_KP_SLASH;
		case SDLK_KP_MINUS: return K_KP_MINUS;
		case SDLK_KP_PLUS: return K_KP_PLUS;
		case SDLK_NUMLOCKCLEAR: return K_KP_NUMLOCK;
		case SDLK_KP_MULTIPLY: return K_KP_STAR;
		case SDLK_KP_EQUALS: return K_KP_EQUALS;
		case SDLK_PRINTSCREEN: return K_PRINT_SCR;
		default: break;
	}

	if ( scan == SDL_SCANCODE_NONUSBACKSLASH ) {
		return K_OEM_102;
	}
	return K_INVALID;
}

unsigned int EventScanCode( const SDL_Keysym& keysym ) {
	// Preserve the Win32 scan code used by ETQW to identify the console key.
	if ( keysym.scancode == SDL_SCANCODE_GRAVE ) {
		return 41;
	}
	return static_cast< unsigned int >( keysym.scancode ) & 0xff;
}

int SDLButtonToETQW( Uint8 button ) {
	switch ( button ) {
		case SDL_BUTTON_LEFT: return M_MOUSE1;
		case SDL_BUTTON_RIGHT: return M_MOUSE2;
		case SDL_BUTTON_MIDDLE: return M_MOUSE3;
		case SDL_BUTTON_X1: return M_MOUSE4;
		case SDL_BUTTON_X2: return M_MOUSE5;
		default:
			if ( button >= 1 && button <= 12 ) {
				return M_MOUSE1 + button - 1;
			}
			return M_INVALID;
	}
}

void SetMouseGrab( bool grab ) {
	if ( win32.sdlWindow == NULL ) {
		win32.mouseGrabbed = false;
		return;
	}
	if ( !grab ) {
		SDL_SetRelativeMouseMode( SDL_FALSE );
		SDL_SetWindowGrab( win32.sdlWindow, SDL_FALSE );
		SDL_ShowCursor( SDL_ENABLE );
		win32.mouseGrabbed = false;
		return;
	}

	SDL_SetWindowGrab( win32.sdlWindow, SDL_TRUE );
	if ( SDL_SetRelativeMouseMode( SDL_TRUE ) != 0 ) {
		// Startup can reach this point just before Windows transfers focus to
		// the newly shown window.  Do not claim success: IN_Frame will retry
		// once the SDL focus event arrives.
		common->DPrintf( "SDL_SetRelativeMouseMode failed: %s\n", SDL_GetError() );
		SDL_SetWindowGrab( win32.sdlWindow, SDL_FALSE );
		SDL_ShowCursor( SDL_ENABLE );
		win32.mouseGrabbed = false;
		return;
	}
	SDL_ShowCursor( SDL_DISABLE );
	win32.mouseGrabbed = SDL_GetRelativeMouseMode() == SDL_TRUE;
}

class idKeyboardSDL : public idKeyboard {
public:
	virtual bool Init() { return true; }
	virtual void Shutdown() { keyboardPolls.Clear(); }
	virtual void Activate() {}
	virtual void Deactivate() {}
	virtual int PollInputEvents( bool ) { return keyboardPolls.Num(); }
	virtual int ReturnInputEvent( const int n, keyNum_t& key, bool& isDown ) {
		if ( n < 0 || n >= keyboardPolls.Num() ) {
			return 0;
		}
		key = keyboardPolls[ n ].key;
		isDown = keyboardPolls[ n ].down;
		return 1;
	}
	virtual void EndInputEvents() { keyboardPolls.SetNum( 0, false ); }

	virtual keyNum_t ConvertScanToKey( unsigned int scanCode ) const {
		SDL_Keysym keysym;
		memset( &keysym, 0, sizeof( keysym ) );
		keysym.scancode = static_cast< SDL_Scancode >( scanCode );
		keysym.sym = SDL_GetKeyFromScancode( keysym.scancode );
		return MapSDLKey( keysym );
	}
	virtual keyNum_t ConvertCharToKey( char ch ) const {
		unsigned char value = static_cast< unsigned char >( ch );
		if ( value >= 'A' && value <= 'Z' ) {
			value = static_cast< unsigned char >( value - 'A' + 'a' );
		}
		return value < K_NUM_KEYS ? static_cast< keyNum_t >( value ) : K_INVALID;
	}
	virtual char ConvertScanToChar( unsigned int scanCode ) const {
		const SDL_Keycode key = SDL_GetKeyFromScancode( static_cast< SDL_Scancode >( scanCode ) );
		return key >= SDLK_SPACE && key <= SDLK_z ? static_cast< char >( key ) : '\0';
	}
	virtual unsigned int ConvertCharToScan( char ch ) const {
		SDL_Keycode key = static_cast< unsigned char >( ch );
		if ( key >= 'A' && key <= 'Z' ) {
			key = key - 'A' + 'a';
		}
		return static_cast< unsigned int >( SDL_GetScancodeFromKey( key ) );
	}
	virtual char ConvertKeyToChar( const keyNum_t keyNum ) const {
		return keyNum >= K_SPACE && keyNum <= K_Z ? static_cast< char >( keyNum ) : '\0';
	}
	virtual bool IsConsoleKey( const sdSysEvent& event ) const {
		return ( event.IsKeyEvent() && event.GetKey() == K_CONSOLE ) ||
			( event.IsCharEvent() && event.GetScanCode() == 41 );
	}
};

class idMouseSDL : public idMouse {
public:
	idMouseSDL() : active( false ) {}
	virtual bool Init() { active = false; return true; }
	virtual void Shutdown() { Deactivate(); mousePolls.Clear(); }
	virtual bool IsActive() const { return active; }
	virtual void Activate() { SetMouseGrab( true ); active = win32.mouseGrabbed; }
	virtual void Deactivate() { SetMouseGrab( false ); active = false; }
	virtual void GrabCursor( bool grab ) {
		win32.mouseReleased = !grab;
		IN_Frame();
	}
	virtual int PollInputEvents( bool ) { return mousePolls.Num(); }
	virtual int ReturnInputEvent( const int n, int& action, int& value ) {
		if ( n < 0 || n >= mousePolls.Num() ) {
			return 0;
		}
		action = mousePolls[ n ].action;
		value = mousePolls[ n ].value;
		return 1;
	}
	virtual void EndInputEvents() { mousePolls.SetNum( 0, false ); }

private:
	bool active;
};

idKeyboardSDL keyboard;
idMouseSDL mouse;

} // namespace

Win32Vars_t win32 = {};
idCVar Win32Vars_t::in_mouse( "in_mouse", "1", CVAR_SYSTEM | CVAR_BOOL, "enable mouse input" );

sdLock& IN_GetPollLock() {
	return inputPollLock;
}

idKeyboard& idSysLocal::Keyboard() {
	return keyboard;
}

idMouse& idSysLocal::Mouse() {
	return mouse;
}

void Sys_QueueSDLKeyEvent( const SDL_KeyboardEvent& event ) {
	const keyNum_t key = MapSDLKey( event.keysym );
	if ( key == K_INVALID ) {
		return;
	}
	const bool down = event.state == SDL_PRESSED;
	const unsigned int scanCode = EventScanCode( event.keysym );
	lastTextScanCode = scanCode;

	sdScopedLock< true > pollLock( inputPollLock );
	keyboardPolls.Append( sdlKeyboardPoll_t( key, down ) );
	sys->QueEvent( SE_KEY, SE_KEY_VALUE( key, scanCode ),
		SE_KEY_VALUE2( down, down && event.repeat != 0 ), 0, NULL );

	if ( down ) {
		int controlChar = 0;
		if ( key == K_BACKSPACE ) {
			controlChar = '\b';
		} else if ( key == K_TAB ) {
			controlChar = '\t';
		} else if ( key == K_ENTER || key == K_KP_ENTER ) {
			controlChar = '\r';
		} else if ( ( event.keysym.mod & KMOD_CTRL ) != 0 && key >= K_A && key <= K_Z ) {
			controlChar = key - K_A + 1;
		}
		if ( controlChar != 0 ) {
			sys->QueEvent( SE_CHAR, scanCode, controlChar, 0, NULL );
		}
	}
}

void Sys_QueueSDLTextEvent( const SDL_TextInputEvent& event ) {
	wchar_t text[ SDL_TEXTINPUTEVENT_TEXT_SIZE ];
	const int count = MultiByteToWideChar( CP_UTF8, 0, event.text, -1, text,
		static_cast< int >( sizeof( text ) / sizeof( text[ 0 ] ) ) );
	if ( count <= 0 ) {
		return;
	}

	sdScopedLock< true > pollLock( inputPollLock );
	for ( int i = 0; i < count - 1; i++ ) {
		sys->QueEvent( SE_CHAR, lastTextScanCode, text[ i ], 0, NULL );
	}
}

void Sys_QueueSDLMouseMotionEvent( const SDL_MouseMotionEvent& event ) {
	if ( event.xrel == 0 && event.yrel == 0 ) {
		return;
	}
	sdScopedLock< true > pollLock( inputPollLock );
	if ( event.xrel != 0 ) {
		mousePolls.Append( sdlMousePoll_t( 8, event.xrel ) );
	}
	if ( event.yrel != 0 ) {
		mousePolls.Append( sdlMousePoll_t( 9, event.yrel ) );
	}
	sys->QueEvent( SE_MOUSE, event.xrel, event.yrel, 0, NULL );
}

void Sys_QueueSDLMouseButtonEvent( const SDL_MouseButtonEvent& event ) {
	const int button = SDLButtonToETQW( event.button );
	if ( button == M_INVALID ) {
		return;
	}
	const bool down = event.state == SDL_PRESSED;
	sdScopedLock< true > pollLock( inputPollLock );
	mousePolls.Append( sdlMousePoll_t( button - M_MOUSE1, down ? 1 : 0 ) );
	sys->QueEvent( SE_MOUSE_BUTTON, button, down ? 1 : 0, 0, NULL );
}

void Sys_QueueSDLMouseWheelEvent( const SDL_MouseWheelEvent& event ) {
	int amount = event.y;
	if ( event.direction == SDL_MOUSEWHEEL_FLIPPED ) {
		amount = -amount;
	}
	if ( amount == 0 ) {
		return;
	}
	sdScopedLock< true > pollLock( inputPollLock );
	mousePolls.Append( sdlMousePoll_t( 10, amount ) );
	const mouseButton_t button = amount > 0 ? M_MWHEELUP : M_MWHEELDOWN;
	for ( int i = abs( amount ); i > 0; i-- ) {
		sys->QueEvent( SE_MOUSE_BUTTON, button, 1, 0, NULL );
		sys->QueEvent( SE_MOUSE_BUTTON, button, 0, 0, NULL );
	}
}

void Sys_ClearSDLInputEvents() {
	sdScopedLock< true > pollLock( inputPollLock );
	keyboardPolls.SetNum( 0, false );
	mousePolls.SetNum( 0, false );
}

void IN_Frame() {
	sdScopedLock< true > pollLock( inputPollLock );
	const bool shouldActivate = inputInitialized && Win32Vars_t::in_mouse.GetBool() &&
		win32.activeApp && !win32.mouseReleased && !win32.movingWindow;
	if ( shouldActivate != mouse.IsActive() ) {
		if ( shouldActivate ) {
			mouse.Activate();
		} else {
			mouse.Deactivate();
		}
	}
}

void IN_ActivateMouse() {
	win32.mouseReleased = false;
	IN_Frame();
}

void IN_DeactivateMouse() {
	win32.mouseReleased = true;
	IN_Frame();
}

void IN_DeactivateMouseIfWindowed() {
	if ( sys3D == NULL || !sys3D->IsFullscreen() ) {
		IN_DeactivateMouse();
	}
}

void Sys_GrabMouseCursor( bool grabIt ) {
	if ( grabIt ) {
		IN_ActivateMouse();
	} else {
		IN_DeactivateMouse();
	}
}

void Sys_InitInput() {
	sdScopedLock< true > pollLock( inputPollLock );
	if ( inputInitialized ) {
		return;
	}
	common->Printf( "\n------- SDL2 Input Initialization -------\n" );
	keyboardPolls.SetGranularity( 64 );
	mousePolls.SetGranularity( 64 );

	win32.hInstance = GetModuleHandleA( NULL );
	win32.hWnd = sys3D != NULL ? reinterpret_cast< HWND >( sys3D->GetGameWindowHandle() ) : NULL;
	if ( win32.g_pdi != NULL ) {
		win32.g_pdi->Release();
		win32.g_pdi = NULL;
	}
	if ( FAILED( DirectInput8Create( win32.hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8A,
		reinterpret_cast< void** >( &win32.g_pdi ), NULL ) ) ) {
		common->Printf( "DirectInput8 controller API unavailable.\n" );
	}

	mouse.Init();
	sys->InitInput();
	SDL_StartTextInput();
	Win32Vars_t::in_mouse.ClearModified();
	inputInitialized = true;
	common->Printf( "-----------------------------------------\n" );
	if ( game != NULL ) {
		game->OnInputInit();
	}
}

void Sys_ShutdownInput() {
	sdScopedLock< true > pollLock( inputPollLock );
	if ( !inputInitialized ) {
		return;
	}
	if ( game != NULL ) {
		game->OnInputShutdown();
	}
	SDL_StopTextInput();
	sys->ShutdownInput();
	mouse.Shutdown();
	if ( win32.g_pdi != NULL ) {
		win32.g_pdi->Release();
		win32.g_pdi = NULL;
	}
	inputInitialized = false;
}
