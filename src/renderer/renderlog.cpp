// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from renderer/RenderLog.obj in the original ETQW PDB and
// checked against the shipping 1.5 executable.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "renderlog.h"

#include <stdarg.h>
#include <time.h>

extern idCVar r_logFile;

idRenderLog renderLog;

idRenderLog::idRenderLog() :
	activeLevel( 0 ),
	indentLevel( 0 ),
	logFile( NULL ) {
}

void idRenderLog::Close() {
	if ( logFile != NULL ) {
		common->Printf( "Closing logfile\n" );
		fclose( logFile );
		logFile = NULL;
		activeLevel = 0;
	}
}

void idRenderLog::Printf( const char* fmt, ... ) {
	if ( activeLevel == 0 || logFile == NULL ) {
		return;
	}

	va_list args;
	va_start( args, fmt );
	fprintf( logFile, "%s", indent.c_str() );
	vfprintf( logFile, fmt, args );
	va_end( args );
}

void idRenderLog::StartFrame() {
	if ( logFile != NULL ) {
		r_logFile.SetInteger( r_logFile.GetInteger() - 1 );
		common->Printf( "Frame logged.\n" );
		if ( r_logFile.GetInteger() < 1 ) {
			Close();
		}
		return;
	}

	if ( r_logFile.GetInteger() == 0 ) {
		return;
	}

	indentLevel = 0;
	indent = "";
	activeLevel = 1;

	char qpath[ 128 ];
	char ospath[ 1024 ];
	sprintf( qpath, "renderlog.txt" );
	idStr::Copynz( ospath, fileSystem->RelativePathToOSPath( qpath, "fs_savepath" ), sizeof( ospath ) );
	logFile = fopen( ospath, "wt" );
	common->Printf( "Opening logfile %s\n", qpath );

	if ( logFile != NULL ) {
		time_t aclock;
		time( &aclock );
		fprintf( logFile, "// %s", asctime( localtime( &aclock ) ) );
		fprintf( logFile, "// %s\n\n", cvarSystem->GetCVarString( "si_version" ) );
	}
}

void idRenderLog::Indent() {
	indent += "    ";
	indentLevel++;
}

void idRenderLog::Outdent() {
	if ( indentLevel <= 0 ) {
		indentLevel = 0;
		indent = "";
		return;
	}

	indentLevel--;
	indent.CapLength( indentLevel * 4 );
}
