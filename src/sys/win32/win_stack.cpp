/*
===========================================================================

Engine-facing Win32 call-stack helpers.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "stacktracer.h"

#include <malloc.h>

bool stackTracerInitialized;

const char* Sys_GetCallStackStr( const address_t* callStack, const int callStackSize ) {
	static char string[ 2048 ];
	static char funcName[ 2048 ];
	int index = 0;
	for ( int i = callStackSize - 1; i >= 0; i-- ) {
		idStackTracer::GetSymbolName( static_cast< unsigned int >( callStack[ i ] ), funcName, sizeof( funcName ) );
		index += sprintf( string + index, " -> %s", funcName );
	}
	return string;
}

const char* Sys_GetFunctionName( const address_t function ) {
	static char string[ 2048 ];
	static char funcName[ 2048 ];
	idStackTracer::GetSymbolName( static_cast< unsigned int >( function ), funcName, sizeof( funcName ) );
	sprintf( string, "%s", funcName );
	return string;
}

void Sys_SetGameOffset( uintptr_t gameAddress ) {
}

void Sys_ShutdownSymbols() {
}

int Sys_GetCurCallStack( address_t* callStack, const int callStackSize ) {
	if ( !stackTracerInitialized ) {
		idStackTracer::Init( NULL, NULL );
		stackTracerInitialized = true;
	}
	idStackTracer::Trace( GetCurrentThread(), reinterpret_cast< unsigned int* >( callStack ), callStackSize, 3 );
	return callStackSize;
}

const char* Sys_GetFunctionSourceFile( const address_t function ) {
	static char string[ 2048 ];
	static char moduleName[ 2048 ];
	idStackTracer::GetSource( static_cast< unsigned int >( function ), moduleName, sizeof( moduleName ), NULL, 0 );
	sprintf( string, "%s", moduleName );
	return string;
}

const char* Sys_GetCurCallStackStr( int depth ) {
	address_t* callStack = static_cast< address_t* >( _alloca( sizeof( address_t ) * depth ) );
	if ( !stackTracerInitialized ) {
		idStackTracer::Init( NULL, NULL );
		stackTracerInitialized = true;
	}
	idStackTracer::Trace( GetCurrentThread(), reinterpret_cast< unsigned int* >( callStack ), depth, 3 );
	return Sys_GetCallStackStr( callStack, depth );
}
