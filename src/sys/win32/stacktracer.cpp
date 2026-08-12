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

bool idStackTracer::GetSymbolName( UINT_PTR funcAddr, char* symbolName, unsigned long maxSymbolLen ) {
	memset( symbolInfo, 0, sizeof( symbolInfo ) );
	PIMAGEHLP_SYMBOL64 symbol = reinterpret_cast< PIMAGEHLP_SYMBOL64 >( symbolInfo );
	symbol->SizeOfStruct = sizeof( IMAGEHLP_SYMBOL64 );
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

bool idStackTracer::GetSource( UINT_PTR address, char* fileName, unsigned long maxFileLen, char* line, unsigned long maxLineLen ) {
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
		idStr::snPrintf( fileName, maxFileLen, "0x%016llx", static_cast< unsigned long long >( address ) );
		if ( line != NULL ) {
			line[ 0 ] = '\0';
		}
	}
	return true;
}

bool idStackTracer::Trace( void* processHandle, void* threadHandle, CONTEXT& context,
		UINT_PTR* stack, int maxDepth, int skipFuncs ) {
	STACKFRAME64 frame;
	memset( &frame, 0, sizeof( frame ) );
#if defined( _M_X64 )
	const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
	frame.AddrPC.Offset = context.Rip;
	frame.AddrStack.Offset = context.Rsp;
	frame.AddrFrame.Offset = context.Rbp;
#else
	const DWORD machineType = IMAGE_FILE_MACHINE_I386;
	frame.AddrPC.Offset = context.Eip;
	frame.AddrStack.Offset = context.Esp;
	frame.AddrFrame.Offset = context.Ebp;
#endif
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrStack.Mode = AddrModeFlat;
	frame.AddrFrame.Mode = AddrModeFlat;

	int numStackItems = 0;
	if ( maxDepth > 0 ) {
		for ( int i = 0; i < 256; i++ ) {
			bool traced = StackWalk64(
				machineType,
				processHandle,
				threadHandle,
				&frame,
				&context,
				NULL,
				SymFunctionTableAccess64,
				SymGetModuleBase64,
				NULL
			) == TRUE;

			if ( !traced || frame.AddrPC.Offset == 0 ) {
				break;
			}
			if ( i >= skipFuncs && SymGetModuleBase64( processHandle, frame.AddrPC.Offset ) != 0 ) {
				stack[ numStackItems++ ] = static_cast< UINT_PTR >( frame.AddrPC.Offset );
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

bool idStackTracer::Trace( void* threadHandle, UINT_PTR* stack, int maxDepth, int numIgnoreFuncs ) {
	HANDLE thread = static_cast< HANDLE >( threadHandle );
	const bool currentThread = thread == GetCurrentThread();
	bool suspended = false;
	if ( !currentThread ) {
		if ( SuspendThread( thread ) == static_cast< DWORD >( -1 ) ) {
			return false;
		}
		suspended = true;
	}

	CONTEXT context;
	memset( &context, 0, sizeof( context ) );
	if ( currentThread ) {
		RtlCaptureContext( &context );
	} else {
		context.ContextFlags = CONTEXT_FULL;
		if ( GetThreadContext( thread, &context ) == FALSE ) {
			ResumeThread( thread );
			return false;
		}
	}
	bool result = Trace( GetCurrentProcess(), thread, context, stack, maxDepth, numIgnoreFuncs );

	if ( suspended ) {
		ResumeThread( thread );
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
