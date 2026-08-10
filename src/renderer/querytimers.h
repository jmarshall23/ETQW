// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the original ETQW PDB and retail executable.

#ifndef __RENDERER_QUERYTIMERS_H__
#define __RENDERER_QUERYTIMERS_H__

struct QueryTimer {
	unsigned int	startTime;
	bool			active;
	char			description[ 128 ];
};

extern QueryTimer timers[ 4096 ];
extern unsigned int nextTimerID;

void R_StartQueryTimer( const char* description );
void R_StopQueryTimer();
void R_TimerFrame();

static_assert( sizeof( QueryTimer ) == 136,
	"QueryTimer must match the ETQW PDB layout" );

#endif /* !__RENDERER_QUERYTIMERS_H__ */
