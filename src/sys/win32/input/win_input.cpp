// Copyright (C) 2007 Id Software, Inc.
//


#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../sys_local.h"
#include "../win_local.h"
#include "win_input_mouse.h"
#include "../../../idlib/threading/Lock.h"

Win32Vars_t win32 = {};

idCVar Win32Vars_t::in_mouse( "in_mouse", "1", CVAR_SYSTEM | CVAR_BOOL, "enable mouse input" );

static bool inputInitialized;
static sdLock inputPollLock;

sdLock& IN_GetPollLock() {
	return inputPollLock;
}

/*
========================
IN_InitDirectInput
========================
*/
void IN_InitDirectInput() {
	common->Printf( "Initializing DirectInput8...\n" );

	if ( win32.g_pdi != NULL ) {
		win32.g_pdi->Release();
		win32.g_pdi = NULL;
	}

	win32.hInstance = GetModuleHandleA( NULL );
	win32.hWnd = sys3D != NULL ? reinterpret_cast< HWND >( sys3D->GetGameWindowHandle() ) : NULL;
	win32.activeApp = win32.hWnd != NULL;

	if ( FAILED( DirectInput8Create( win32.hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast< void** >( &win32.g_pdi ), NULL ) ) ) {
		common->Printf( "DirectInput8Create failed.\n" );
	}
}

/*
==================
IN_Frame

Called every frame, even if not generating commands.
==================
*/
void IN_Frame() {
	sdScopedLock< true > pollLock( inputPollLock );

	bool shouldActivate = Win32Vars_t::in_mouse.GetBool();
	if ( win32.mouseReleased || win32.movingWindow || !win32.activeApp ) {
		shouldActivate = false;
	}

	idMouse& mouse = sys->Mouse();
	if ( shouldActivate != mouse.IsActive() ) {
		if ( mouse.IsActive() ) {
			mouse.Deactivate();
		} else {
			mouse.Activate();
		}
	}

	if ( win32.languageChanged ) {
		win32.languageChanged = false;
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec etqwbinds.cfg\n" );
	}
}

/*
==================
IN_Async

Poll devices on the dedicated input thread.  This function only publishes
system events and controller state; usercmd construction and game callbacks
remain on the main thread.
==================
*/
void IN_Async() {
	sdScopedLock< true > pollLock( inputPollLock );
	if ( !inputInitialized ) {
		return;
	}

	idMouse& mouse = sys->Mouse();
	mouse.PollInputEvents( true );
	mouse.EndInputEvents();

	idKeyboard& keyboard = sys->Keyboard();
	keyboard.PollInputEvents( true );
	keyboard.EndInputEvents();

	sdControllerManager& manager = sys->GetControllerManager();
	for ( int i = 0; i < manager.GetMaxControllers(); i++ ) {
		sdController& controller = manager.GetController( i );
		const int eventCount = controller.PollInputEvents();
		for ( int eventIndex = 0; eventIndex < eventCount; eventIndex++ ) {
			int action;
			int value;
			if ( controller.ReturnInputEvent( eventIndex, action, value ) &&
				action >= C_AXIS1 && action <= C_AXIS_MAX ) {
				controller.SetAxis( action - C_AXIS1, value / 32767.0f );
			}
		}
		controller.EndInputEvents();
	}
}

/*
===========
Sys_InitInput
===========
*/
void Sys_InitInput() {
	sdScopedLock< true > pollLock( inputPollLock );
	common->Printf( "\n------- Input Initialization -------\n" );
	IN_InitDirectInput();

	if ( Win32Vars_t::in_mouse.GetBool() ) {
		if ( !sys->Mouse().Init() && m_rawInput.GetBool() ) {
			m_rawInput.SetBool( false );
			sys->Mouse().Init();
		}
		sys->Mouse().GrabCursor( false );
	} else {
		common->Printf( "Mouse control not active.\n" );
	}

	sys->InitInput();
	common->Printf( "------------------------------------\n" );
	Win32Vars_t::in_mouse.ClearModified();

	if ( game != NULL ) {
		game->OnInputInit();
	}
	inputInitialized = true;
}

/*
===========
Sys_ShutdownInput
===========
*/
void Sys_ShutdownInput() {
	sdScopedLock< true > pollLock( inputPollLock );
	if ( !inputInitialized ) {
		return;
	}

	sys->ShutdownInput();
	sys->Mouse().Shutdown();

	if ( win32.g_pdi != NULL ) {
		win32.g_pdi->Release();
		win32.g_pdi = NULL;
	}

	if ( game != NULL ) {
		game->OnInputShutdown();
	}
	inputInitialized = false;
}
