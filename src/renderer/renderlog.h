// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW PDB type records and the retail executable.

#ifndef __RENDERER_RENDERLOG_H__
#define __RENDERER_RENDERLOG_H__

#include <stdio.h>

class idRenderLog {
public:
					idRenderLog();

	void			StartFrame();
	void			Close();
	int				Active() const { return activeLevel; }
	void			Printf( const char* fmt, ... );
	void			Indent();
	void			Outdent();

private:
	int				activeLevel;
	idStr			indent;
	int				indentLevel;
	FILE*			logFile;
};

extern idRenderLog renderLog;

static_assert( sizeof( idRenderLog ) == 44, "idRenderLog must match the ETQW PDB layout" );

#endif /* !__RENDERER_RENDERLOG_H__ */
