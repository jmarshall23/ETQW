// Copyright (C) 2007 Id Software, Inc.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "querytimers.h"
#include "RendererTypesImpl.h"
#include "../libs/qglLib/qgl.h"

QueryTimer timers[ 4096 ];
unsigned int nextTimerID = 0;

idCVar r_showQueryTimers(
	"r_showQueryTimers",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"Show the query_timer extension results.",
	1.0f,
	-1.0f
);

void R_StartQueryTimer( const char* description ) {
	if ( nextTimerID >= 4096 ) {
		common->Warning( "Too many query timers" );
		return;
	}

	QueryTimer& timer = timers[ nextTimerID ];
	timer.active = true;
	idStr::Copynz( timer.description, description != NULL ? description : "",
		sizeof( timer.description ) );

	if ( glConfig.timerQueryAvailable && qglBeginQueryARB != NULL ) {
		qglBeginQueryARB( GL_TIME_ELAPSED_EXT, nextTimerID + 1 );
	} else {
		timer.startTime = sys->Milliseconds();
	}
	++nextTimerID;
}

void R_StopQueryTimer() {
	if ( nextTimerID == 0 ) {
		common->Warning( "Stopped inactive timer" );
		return;
	}

	QueryTimer& timer = timers[ nextTimerID - 1 ];
	if ( !timer.active ) {
		common->Warning( "Stopped inactive timer" );
		return;
	}

	timer.active = false;
	if ( glConfig.timerQueryAvailable && qglEndQueryARB != NULL ) {
		qglEndQueryARB( GL_TIME_ELAPSED_EXT );
	} else {
		timer.startTime = sys->Milliseconds() - timer.startTime;
	}
}

void R_TimerFrame() {
	if ( nextTimerID != 0 ) {
		if ( timers[ nextTimerID - 1 ].active ) {
			R_StopQueryTimer();
		}

		if ( glConfig.timerQueryAvailable && qglGetQueryObjectivARB != NULL ) {
			GLint available = 0;
			do {
				qglGetQueryObjectivARB(
					nextTimerID, GL_QUERY_RESULT_AVAILABLE_ARB, &available );
			} while ( !available );
		}
	}

	if ( r_showQueryTimers.GetBool() ) {
		if ( nextTimerID != 0 ) {
			common->Printf( "--- Query Results ---\n" );
		}
		for ( unsigned int i = 0; i < nextTimerID; ++i ) {
			if ( glConfig.timerQueryAvailable && qglGetQueryObjectui64vEXT != NULL ) {
				GLuint64EXT elapsed = 0;
				qglGetQueryObjectui64vEXT( i + 1, GL_QUERY_RESULT_ARB, &elapsed );
				elapsed /= 1000;
				common->Printf( "Timer: %s (%lli micro-s)\n",
					timers[ i ].description, static_cast< signed __int64 >( elapsed ) );
			} else {
				common->Printf( "Timer: %s (%i ms)\n",
					timers[ i ].description, timers[ i ].startTime );
			}
		}
		if ( nextTimerID != 0 ) {
			common->Printf( "--- --- --- ---\n" );
		}
	}

	nextTimerID = 0;
}
