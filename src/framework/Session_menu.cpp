// Copyright (C) 2007 Id Software, Inc.
//
// ETQW moved the UI implementation into gamex86.  The engine session owns
// only this narrow forwarding boundary.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "precompiled.h"
#include "Session_local.h"
#include "../decllib/declLocStr.h"

void idSessionLocal::StartMenu() {
	if ( game == NULL || game->IsMainMenuActive() ) {
		return;
	}
	if ( readDemo != NULL ) {
		UnloadMap();
	}
	renderSystem->LockThreads();
	game->ShowMainMenu();
}

void idSessionLocal::ExitMenu() {
	if ( game != NULL ) {
		game->HideMainMenu();
	}
}

void idSessionLocal::GuiFrameEvents( bool outOfSequence ) {
	if ( game != NULL ) {
		game->GuiFrameEvents( outOfSequence );
	}
}

void idSessionLocal::MessageBox( msgBoxType_t type, const wchar_t* message, const char* titleDef ) {
	if ( game == NULL ) {
		return;
	}
	const sdDeclLocStr* title = NULL;
	if ( titleDef != NULL && titleDef[ 0 ] != '\0' ) {
		title = declHolder.FindLocStr( titleDef );
	}
	game->MessageBox( type, message, title );
}
