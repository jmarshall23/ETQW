// Copyright (C) 2007 Id Software, Inc.
//
// SDL2 event pump adapted from Darklight2 for ETQW's system-event format.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "sys_local.h"
#include "win32/win_local.h"

namespace {

void HandleWindowEvent( const SDL_WindowEvent& event ) {
	if ( win32.sdlWindow != NULL && event.windowID != SDL_GetWindowID( win32.sdlWindow ) ) {
		return;
	}

	switch ( event.event ) {
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			win32.activeApp = true;
			win32.movingWindow = false;
			idKeyInput::ClearStates();
			sys->Keyboard().Activate();
			IN_Frame();
			if ( session != NULL ) {
				session->SetPlayingSoundWorld();
			}
			break;

		case SDL_WINDOWEVENT_FOCUS_LOST:
			win32.activeApp = false;
			win32.movingWindow = false;
			sys->Keyboard().Deactivate();
			Sys_ClearSDLInputEvents();
			IN_Frame();
			break;

		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			if ( sys3D != NULL && event.data1 > 0 && event.data2 > 0 ) {
				sys3D->WindowSizeDragged( event.data1, event.data2 );
			}
			break;

		case SDL_WINDOWEVENT_MINIMIZED:
			win32.activeApp = false;
			IN_Frame();
			break;

		case SDL_WINDOWEVENT_RESTORED:
		case SDL_WINDOWEVENT_MAXIMIZED:
			win32.activeApp = true;
			IN_Frame();
			break;

		case SDL_WINDOWEVENT_CLOSE:
			Sys_Quit();
			break;

		default:
			break;
	}
}

} // namespace

void Sys_ProcessSDLEvents() {
	SDL_Event event;
	while ( SDL_PollEvent( &event ) ) {
		win32.sysMsgTime = event.common.timestamp != 0 ?
			static_cast< int >( event.common.timestamp ) : Sys_Milliseconds();

		switch ( event.type ) {
			case SDL_WINDOWEVENT:
				HandleWindowEvent( event.window );
				break;

			case SDL_KEYDOWN:
				if ( event.key.keysym.sym == SDLK_RETURN &&
					( event.key.keysym.mod & KMOD_ALT ) != 0 ) {
					const bool fullscreen = sys3D != NULL && sys3D->IsFullscreen();
					cvarSystem->SetCVarBool( "r_fullscreen", !fullscreen );
					cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "vid_restart\n" );
					break;
				}
				Sys_QueueSDLKeyEvent( event.key );
				break;

			case SDL_KEYUP:
				Sys_QueueSDLKeyEvent( event.key );
				break;

			case SDL_TEXTINPUT:
				Sys_QueueSDLTextEvent( event.text );
				break;

			case SDL_MOUSEMOTION:
				if ( sys->Mouse().IsActive() ) {
					Sys_QueueSDLMouseMotionEvent( event.motion );
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if ( sys->Mouse().IsActive() ) {
					Sys_QueueSDLMouseButtonEvent( event.button );
				}
				break;

			case SDL_MOUSEWHEEL:
				if ( sys->Mouse().IsActive() ) {
					Sys_QueueSDLMouseWheelEvent( event.wheel );
				}
				break;

			case SDL_QUIT:
				Sys_Quit();
				break;

			default:
				break;
		}
	}
}
