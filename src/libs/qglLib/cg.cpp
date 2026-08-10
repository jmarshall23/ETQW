// Copyright (C) 2007 Id Software, Inc.

#include "precompiled.h"
#pragma hdrstop

#include "qcg.h"

static void* cgDLL = NULL;

CGcontext cgContext = NULL;

PFNCGCREATECONTEXTPROC cgCreateContext = NULL;
PFNCGDESTROYCONTEXTPROC cgDestroyContext = NULL;
PFNCGGETLASTLISTINGPROC cgGetLastListing = NULL;
PFNCGCREATEPROGRAMPROC cgCreateProgram = NULL;
PFNCGDESTROYPROGRAMPROC cgDestroyProgram = NULL;
PFNCGGETPROGRAMSTRINGPROC cgGetProgramString = NULL;
PFNCGGETERRORPROC cgGetError = NULL;
PFNCGGETERRORSTRINGPROC cgGetErrorString = NULL;
PFNCGSETERRORCALLBACKPROC cgSetErrorCallback = NULL;

static void CGErrorCallback( void ) {
	const CGerror error = cgGetError();
	common->Printf( "Cg error (%d): %s\n", error, cgGetErrorString( error ) );
}

bool cgInit( void ) {
	char dllPath[ MAX_OSPATH ];

	if ( cgDLL != NULL ) {
		common->Warning( "Cg is already initialised\n" );
		return false;
	}

	fileSystem->FindDLL( "cg", dllPath, false, false );
	if ( dllPath[ 0 ] == '\0' ) {
		common->Warning( "couldn't load '%s' dynamic library\n", "cg" );
		return false;
	}

	cgDLL = sys->DLL_Load( dllPath, true );
	if ( cgDLL == NULL ) {
		common->Warning( "couldn't load '%s' dynamic library\n", dllPath );
		return false;
	}

#define LOAD_CG_PROC( name, type ) \
	name = reinterpret_cast< type >( sys->DLL_GetProcAddress( cgDLL, #name ) ); \
	if ( name == NULL ) { \
		common->Warning( "Couldn't find proc address for: " #name "\n" ); \
		return false; \
	}

	LOAD_CG_PROC( cgCreateContext, PFNCGCREATECONTEXTPROC );
	LOAD_CG_PROC( cgDestroyContext, PFNCGDESTROYCONTEXTPROC );
	LOAD_CG_PROC( cgGetLastListing, PFNCGGETLASTLISTINGPROC );
	LOAD_CG_PROC( cgCreateProgram, PFNCGCREATEPROGRAMPROC );
	LOAD_CG_PROC( cgDestroyProgram, PFNCGDESTROYPROGRAMPROC );
	LOAD_CG_PROC( cgGetProgramString, PFNCGGETPROGRAMSTRINGPROC );
	LOAD_CG_PROC( cgGetError, PFNCGGETERRORPROC );
	LOAD_CG_PROC( cgGetErrorString, PFNCGGETERRORSTRINGPROC );
	LOAD_CG_PROC( cgSetErrorCallback, PFNCGSETERRORCALLBACKPROC );

#undef LOAD_CG_PROC

	cgContext = cgCreateContext();
	cgSetErrorCallback( CGErrorCallback );
	return true;
}

void cgShutdown( void ) {
	if ( cgContext != NULL ) {
		cgDestroyContext( cgContext );
		cgContext = NULL;
	}

	if ( cgDLL != NULL ) {
		sys->DLL_Unload( cgDLL );
		cgDLL = NULL;
	}
}
