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
	static bool Trace( void* threadHandle, UINT_PTR* stack, int maxDepth, int numIgnoreFuncs );
	static bool GetSymbolName( UINT_PTR functionAddress, char* symbolName, unsigned long maxSymbolLen );
	static bool GetSource( UINT_PTR address, char* fileName, unsigned long maxFileLen, char* line, unsigned long maxLineLen );

private:
	static bool Trace( void* processHandle, void* threadHandle, CONTEXT& context,
		UINT_PTR* stack, int maxDepth, int skipFuncs );
	static bool AddPathFromEnvironment( char* path, int maxLen, const char* variable );
	static bool GetSymbolPath( char* path, int maxLen, const char* appSymbolPath );

	static unsigned char symbolInfo[ 4096 ];
	static char basePath[ 256 ];
};

#endif /* !__STACKTRACER_H__ */
