/*
===========================================================================

Win32 DbgHelp stack tracing.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "stacktracer.h"

#include <dbghelp.h>
#include <intrin.h>

unsigned char idStackTracer::symbolInfo[ 4096 ];
char idStackTracer::basePath[ 256 ];

bool idStackTracer::GetSymbolName( unsigned int funcAddr, char* symbolName, unsigned long maxSymbolLen ) {
	memset( symbolInfo, 0, sizeof( symbolInfo ) );
	PIMAGEHLP_SYMBOL64 symbol = reinterpret_cast< PIMAGEHLP_SYMBOL64 >( symbolInfo );
	symbol->SizeOfStruct = sizeof( symbolInfo );
	symbol->MaxNameLength = sizeof( symbolInfo ) - sizeof( IMAGEHLP_SYMBOL64 );
	idStr::Copynz( symbolName, "** UNKOWN **", maxSymbolLen );
	symbolName[ maxSymbolLen - 1 ] = '\0';

	DWORD64 displacement = 0;
	if ( SymGetSymFromAddr64( GetCurrentProcess(), funcAddr, &displacement, symbol ) ) {
		return UnDecorateSymbolName( symbol->Name, symbolName, maxSymbolLen, 0x42e2 ) != 0;
	}
	return false;
}

bool idStackTracer::AddPathFromEnvironment( char* path, int maxLen, const char* variable ) {
	char temp[ 1024 ];
	if ( GetEnvironmentVariableA( variable, temp, sizeof( temp ) ) == 0 ) {
		return false;
	}
	strncat( path, ";", maxLen );
	strncat( path, temp, maxLen );
	return true;
}

bool idStackTracer::GetSource( unsigned int address, char* fileName, unsigned long maxFileLen, char* line, unsigned long maxLineLen ) {
	IMAGEHLP_LINE64 lineInfo;
	memset( &lineInfo, 0, sizeof( lineInfo ) );
	lineInfo.SizeOfStruct = sizeof( lineInfo );
	DWORD displacement = 0;

	if ( SymGetLineFromAddr64( GetCurrentProcess(), address, &displacement, &lineInfo ) ) {
		if ( fileName != NULL ) {
			const char* sourceName = lineInfo.FileName;
			const char* relativeName = strstr( sourceName, basePath );
			if ( relativeName == sourceName ) {
				sourceName += strlen( basePath );
			}
			idStr::snPrintf( fileName, maxFileLen, "%s", sourceName );
		}
		if ( line != NULL ) {
			idStr::snPrintf( line, maxLineLen, "%i", lineInfo.LineNumber );
		}
	} else {
		idStr::snPrintf( fileName, maxFileLen, "0x%08x", address );
		if ( line != NULL ) {
			line[ 0 ] = '\0';
		}
	}
	return true;
}

bool idStackTracer::Trace( void* processHandle, void* threadHandle, unsigned __int64 programCounter,
		unsigned __int64 stackPointer, unsigned __int64 framePointer, unsigned int* stack,
		int maxDepth, int skipFuncs ) {
	STACKFRAME64 frame;
	memset( &frame, 0, sizeof( frame ) );
	frame.AddrPC.Offset = programCounter;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrStack.Offset = stackPointer;
	frame.AddrStack.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = framePointer;
	frame.AddrFrame.Mode = AddrModeFlat;

	CONTEXT context;
	memset( &context, 0, sizeof( context ) );
	GetThreadContext( threadHandle, &context );

	int numStackItems = 0;
	if ( maxDepth > 0 ) {
		for ( int i = 0; i < 256; i++ ) {
			bool traced = StackWalk64(
				IMAGE_FILE_MACHINE_I386,
				processHandle,
				threadHandle,
				&frame,
				&context,
				NULL,
				SymFunctionTableAccess64,
				SymGetModuleBase64,
				NULL
			) == TRUE;

			if ( i >= skipFuncs && SymGetModuleBase64( processHandle, frame.AddrPC.Offset ) != 0 ) {
				if ( !traced || frame.AddrFrame.Offset == 0 ) {
					break;
				}
				stack[ numStackItems++ ] = static_cast< unsigned int >( frame.AddrPC.Offset );
			}
			if ( numStackItems >= maxDepth ) {
				return true;
			}
		}
		if ( numStackItems < maxDepth ) {
			memset( stack + numStackItems, 0, sizeof( *stack ) * ( maxDepth - numStackItems ) );
		}
	}
	return true;
}

bool idStackTracer::Trace( void* threadHandle, unsigned int* stack, int maxDepth, int numIgnoreFuncs ) {
	bool suspended = false;
	if ( threadHandle != GetCurrentThread() ) {
		if ( SuspendThread( threadHandle ) == 0 ) {
			return false;
		}
		suspended = true;
	}

	unsigned long stackPointer;
	unsigned long framePointer;
#if defined( _M_IX86 )
	__asm mov stackPointer, esp
	__asm mov framePointer, ebp
#else
	stackPointer = 0;
	framePointer = 0;
#endif
	unsigned int programCounter = reinterpret_cast< unsigned int >( _ReturnAddress() );
	bool result = Trace( GetCurrentProcess(), threadHandle, programCounter, stackPointer, framePointer,
		stack, maxDepth, numIgnoreFuncs );

	if ( suspended ) {
		ResumeThread( threadHandle );
	}
	return result;
}

bool idStackTracer::GetSymbolPath( char* path, int maxLen, const char* appSymbolPath ) {
	strncat( path, ".", maxLen );
	AddPathFromEnvironment( path, maxLen, "_NT_SYMBOL_PATH" );
	AddPathFromEnvironment( path, maxLen, "_NT_ALTERNATE_SYMBOL_PATH" );

	char root[ 256 ];
	root[ 0 ] = '\0';
	if ( AddPathFromEnvironment( root, sizeof( root ), "SYSTEMROOT" ) ) {
		strncat( path, root, maxLen );
		strncat( path, root, maxLen );
		strncat( path, "\\System32", maxLen );
	}

	if ( appSymbolPath != NULL ) {
		strncat( path, ";", maxLen );
		strncat( path, appSymbolPath, maxLen );
		return true;
	}

	char moduleName[ 256 ];
	char drive[ 3 ];
	char directory[ 256 ];
	char fileName[ 256 ];
	char extension[ 256 ];
	GetModuleFileNameA( NULL, moduleName, sizeof( moduleName ) );
	_splitpath( moduleName, drive, directory, fileName, extension );
	int length = static_cast< int >( strlen( directory ) );
	if ( length > 0 && ( directory[ length - 1 ] == '\\' || directory[ length - 1 ] == '/' ) ) {
		directory[ length - 1 ] = '\0';
	}
	strncat( path, ";", maxLen );
	strncat( path, drive, maxLen );
	strncat( path, directory, maxLen );
	return true;
}

bool idStackTracer::Init( const char* appSymbolPath, const char* sourceBasePath ) {
	DWORD options = SymGetOptions();
	SymSetOptions( ( options & ~SYMOPT_UNDNAME ) | SYMOPT_LOAD_LINES );

	char symbolPath[ 2048 ];
	symbolPath[ 0 ] = '\0';
	GetSymbolPath( symbolPath, sizeof( symbolPath ), appSymbolPath );
	if ( sourceBasePath != NULL ) {
		idStr::Copynz( basePath, sourceBasePath, sizeof( basePath ) );
	} else {
		basePath[ 0 ] = '\0';
	}
	return SymInitialize( GetCurrentProcess(), symbolPath, TRUE ) == TRUE;
}
