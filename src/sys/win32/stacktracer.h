/*
===========================================================================

Win32 DbgHelp stack tracing.

===========================================================================
*/

#ifndef __STACKTRACER_H__
#define __STACKTRACER_H__

class idStackTracer {
public:
	static bool Init( const char* appSymbolPath, const char* basePath );
	static bool Trace( void* threadHandle, unsigned int* stack, int maxDepth, int numIgnoreFuncs );
	static bool GetSymbolName( unsigned int functionAddress, char* symbolName, unsigned long maxSymbolLen );
	static bool GetSource( unsigned int address, char* fileName, unsigned long maxFileLen, char* line, unsigned long maxLineLen );

private:
	static bool Trace( void* processHandle, void* threadHandle, unsigned __int64 programCounter,
		unsigned __int64 stackPointer, unsigned __int64 framePointer, unsigned int* stack,
		int maxDepth, int skipFuncs );
	static bool AddPathFromEnvironment( char* path, int maxLen, const char* variable );
	static bool GetSymbolPath( char* path, int maxLen, const char* appSymbolPath );

	static unsigned char symbolInfo[ 4096 ];
	static char basePath[ 256 ];
};

#endif /* !__STACKTRACER_H__ */
