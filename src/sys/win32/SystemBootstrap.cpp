// Copyright (C) 2007 Id Software, Inc.
//
// ETQW Win32 platform boundary reconstructed against the public SDK.  This
// replaces the incompatible Doom 3 Win32 implementation in the executable
// while the retail platform units are recovered from symbols.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../sys_local.h"
#include "../sys_render.h"
#include "win_asyncthread.h"

#include <winsock2.h>
#include <direct.h>
#include <io.h>
#include <locale.h>
#include <sys/stat.h>
#include <process.h>
#include <shellapi.h>

extern idSysLocal sysLocal;
extern const char *sysLanguageNames[];
extern void PQ_Init();
extern void PQ_ShutDown();
extern void Sys_CollectPerformanceData();
extern sdPerformanceQuery* Sys_GetPerformanceQuery( sdPerformanceQueryType pqType );
extern void IN_Frame();

namespace {

const int SYSTEM_MAX_PRINT_MSG = 4096;
const int SYSTEM_MAX_OS_PATH = 256;

volatile LONG quitRequested;
LARGE_INTEGER timerFrequency;
LARGE_INTEGER timerBase;
bool networkingInitialized;

class sdIMEBootstrap : public sdIME {
public:
	sdIMEBootstrap() : enabled( false ) {}
	virtual bool Init() { return true; }
	virtual void Shutdown() { enabled = false; }
	virtual bool LangSupportsIME() const { return false; }
	virtual void Enable( bool value ) { enabled = value; }
	virtual bool IsEnabled() const { return enabled; }
	virtual void FinalizeString( bool ) {}
	virtual int GetCursorChars() const { return 0; }
	virtual bool IsReadingWindowActive() const { return false; }
	virtual bool IsHorizontalReading() const { return true; }
	virtual bool VerticalCandidateLine() const { return false; }
	virtual state_e GetState() const { return enabled ? IME_STATE_ON : IME_STATE_OFF; }
	virtual const wchar_t* GetIndicator() const { return L"A"; }
	virtual bool IsCandidateListActive() const { return false; }
	virtual const wchar_t* GetCandidate( const unsigned int ) const { return L""; }
	virtual int GetCandidateCount() const { return 0; }
	virtual int GetCandidateSelection() const { return 0; }
	virtual const wchar_t* GetCompositionString() const { return L""; }
	virtual const byte* GetCompositionStringAttributes() const { return NULL; }
	virtual const lang_e GetLanguage() const { return IME_LANG_NEUTRAL; }
	virtual const lang_e GetPrimaryLanguage() const { return IME_LANG_NEUTRAL; }
private:
	bool enabled;
};

sdIMEBootstrap ime;

void FillSysTime( const tm& source, sysTime_t& target ) {
	target.tm_sec = source.tm_sec;
	target.tm_min = source.tm_min;
	target.tm_hour = source.tm_hour;
	target.tm_mday = source.tm_mday;
	target.tm_mon = source.tm_mon;
	target.tm_year = source.tm_year;
	target.tm_wday = source.tm_wday;
	target.tm_yday = source.tm_yday;
	target.tm_isdst = source.tm_isdst;
}

time_t MakeTime( const sysTime_t& source ) {
	tm value;
	memset( &value, 0, sizeof( value ) );
	value.tm_sec = source.tm_sec;
	value.tm_min = source.tm_min;
	value.tm_hour = source.tm_hour;
	value.tm_mday = source.tm_mday;
	value.tm_mon = source.tm_mon;
	value.tm_year = source.tm_year;
	value.tm_isdst = source.tm_isdst;
	return mktime( &value );
}

void SockaddrFromNetadr( const netadr_t& address, sockaddr_in& socketAddress ) {
	memset( &socketAddress, 0, sizeof( socketAddress ) );
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons( address.port );
	memcpy( &socketAddress.sin_addr, address.ip, sizeof( address.ip ) );
	if ( address.type == NA_BROADCAST ) {
		socketAddress.sin_addr.s_addr = INADDR_BROADCAST;
	} else if ( address.type == NA_LOOPBACK ) {
		socketAddress.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	}
}

void NetadrFromSockaddr( const sockaddr_in& socketAddress, netadr_t& address ) {
	address.type = ( ntohl( socketAddress.sin_addr.s_addr ) == INADDR_LOOPBACK ) ? NA_LOOPBACK : NA_IP;
	memcpy( address.ip, &socketAddress.sin_addr, sizeof( address.ip ) );
	address.port = ntohs( socketAddress.sin_port );
}

}

sdSysEvent::~sdSysEvent() {
	FreeData();
}

void sdSysEvent::Init( sysEventType_t eventType, int eventValue, int eventValue2, int eventPtrLength, void* eventPtr ) {
	FreeData();
	type = eventType;
	value = eventValue;
	value2 = eventValue2;
	ptrLength = eventPtrLength;
	ptr = eventPtr;
	node.SetOwner( this );
}

void sdSysEvent::FreeData() {
	if ( ptr != NULL ) {
		Mem_Free( ptr );
		ptr = NULL;
	}
	ptrLength = 0;
}

idSysLocal::~idSysLocal() {
	ClearEvents();
}

void idSysLocal::Init() {
	QueryPerformanceFrequency( &timerFrequency );
	QueryPerformanceCounter( &timerBase );
	PQ_Init();
	sys_asyncThread->StartThread();
}

void idSysLocal::PostGameInit() {
}

void idSysLocal::Shutdown() {
	ClearEvents();
	PQ_ShutDown();
	sys_asyncThread->StopThread();
}

void idSysLocal::GetCPUInfo( cpuInfo_t& info ) {
	Sys_CPUInfo( info );
}

void idSysLocal::Sleep( int msec ) {
	Sys_Sleep( msec );
}

int idSysLocal::Milliseconds() {
	return Sys_Milliseconds();
}

time_t idSysLocal::RealTime( sysTime_t* value ) {
	return Sys_RealTime( value );
}

const char* idSysLocal::TimeToSystemStr( const sysTime_t& value ) {
	return Sys_TimeToSystemStr( value );
}

const char* idSysLocal::TimeAndDateToSystemStr( const sysTime_t& value ) {
	return Sys_TimeAndDateToSystemStr( value );
}

time_t idSysLocal::TimeDiff( const sysTime_t& from, const sysTime_t& to ) {
	return Sys_TimeDiff( from, to );
}

void idSysLocal::SecondsToTime( const time_t value, sysTime_t& out, bool localTime ) {
	Sys_SecondsToTime( value, out, localTime );
}

const char *idSysLocal::TimeToStr( const sysTime_t& value ) {
	return Sys_TimeToStr( value );
}

const char *idSysLocal::SecondsToStr( const time_t value, bool localTime ) {
	return Sys_SecondsToStr( value, localTime );
}

void idSysLocal::GetCurrentMemoryStatus( sysMemoryStats_t &stats ) {
	Sys_GetCurrentMemoryStatus( stats );
}

void idSysLocal::GetExeLaunchMemoryStatus( sysMemoryStats_t &stats ) {
	Sys_GetExeLaunchMemoryStatus( stats );
}

void idSysLocal::GetProcessMemoryStatus( sysProcessMemoryStats_t &stats ) {
	Sys_GetProcessMemoryStatus( stats );
}

const char *idSysLocal::GetFunctionName( const address_t function ) {
	return Sys_GetFunctionName( function );
}

const char *idSysLocal::GetFunctionSourceFile( const address_t function ) {
	return Sys_GetFunctionSourceFile( function );
}

const char *idSysLocal::EXEPath() {
	return Sys_EXEPath();
}

long idSysLocal::File_TimeStamp( FILE* file ) {
	return Sys_FileTimeStamp( file );
}

int idSysLocal::File_Stat( const char* OSPath ) {
	struct _stat status;
	return OSPath != NULL ? _stat( OSPath, &status ) : -1;
}

const sdSysEvent* idSysLocal::GenerateBlankEvent() {
	sdSysEvent* event = eventAllocator.Alloc();
	event->Init( SE_NONE, 0, 0, 0, NULL );
	return event;
}

const sdSysEvent* idSysLocal::GenerateCharEvent( int ch ) {
	sdSysEvent* event = eventAllocator.Alloc();
	event->Init( SE_CHAR, 0, ch, 0, NULL );
	return event;
}

const sdSysEvent* idSysLocal::GenerateKeyEvent( keyNum_t key, bool down ) {
	sdSysEvent* event = eventAllocator.Alloc();
	event->Init( SE_KEY, SE_KEY_VALUE( key, key ), SE_KEY_VALUE2( down, false ), 0, NULL );
	return event;
}

const sdSysEvent* idSysLocal::GenerateGuiEvent( int guiValue ) {
	sdSysEvent* event = eventAllocator.Alloc();
	event->Init( SE_GUI, guiValue, 0, 0, NULL );
	return event;
}

void idSysLocal::FreeEvent( const sdSysEvent* event ) {
	if ( event == NULL ) {
		return;
	}
	sdSysEvent* mutableEvent = const_cast< sdSysEvent* >( event );
	mutableEvent->GetNode().Remove();
	mutableEvent->FreeData();
	eventAllocator.Free( mutableEvent );
}

const sdSysEvent* idSysLocal::GetEvent() {
	ProcessOSEvents();
	sdSysEvent* event = eventQue.Next();
	if ( event == NULL ) {
		// The retail ETQW implementation returns NULL when the queue is empty.
		// GenerateBlankEvent is reserved for journal playback.  Returning a
		// blank event here keeps idEventLoop::RunEventLoop spinning forever.
		return NULL;
	}
	event->GetNode().Remove();
	return event;
}

void idSysLocal::QueEvent( sysEventType_t type, int value, int value2, int ptrLength, void *ptr ) {
	sdSysEvent* event = eventAllocator.Alloc();
	event->Init( type, value, value2, ptrLength, ptr );
	event->GetNode().AddToEnd( eventQue );
}

void idSysLocal::ClearEvents() {
	while ( !eventQue.IsListEmpty() ) {
		FreeEvent( eventQue.Next() );
	}
}

void idSysLocal::OpenURL( const char *url, bool quit ) {
	if ( url != NULL ) {
		ShellExecuteA( NULL, "open", url, NULL, NULL, SW_SHOWNORMAL );
	}
	if ( quit ) {
		Sys_Quit();
	}
}

void idSysLocal::StartProcess( const char *exePath, bool quit ) {
	if ( exePath != NULL ) {
		STARTUPINFOA startup;
		PROCESS_INFORMATION process;
		memset( &startup, 0, sizeof( startup ) );
		memset( &process, 0, sizeof( process ) );
		startup.cb = sizeof( startup );
		char commandLine[ SYSTEM_MAX_OS_PATH * 2 ];
		idStr::Copynz( commandLine, exePath, sizeof( commandLine ) );
		if ( CreateProcessA( NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process ) ) {
			CloseHandle( process.hThread );
			CloseHandle( process.hProcess );
		}
	}
	if ( quit ) {
		Sys_Quit();
	}
}

int idSysLocal::MessageBox( const char* title, const char* buffer, messageBoxType_t type ) {
	return Sys_MessageBox( title, buffer, type );
}

void idSysLocal::ProcessOSEvents() {
	MSG message;
	while ( PeekMessage( &message, NULL, 0, 0, PM_REMOVE ) ) {
		if ( message.message == WM_QUIT ) {
			InterlockedExchange( &quitRequested, 1 );
		}
		TranslateMessage( &message );
		DispatchMessage( &message );
	}
}

sdPerformanceQuery* idSysLocal::GetPerformanceQuery( sdPerformanceQueryType pqType ) {
	return Sys_GetPerformanceQuery( pqType );
}

void idSysLocal::CollectPerformanceData() {
	Sys_CollectPerformanceData();
}

idWStr idSysLocal::GetClipboardData() {
	wchar_t* data = Sys_GetClipboardData();
	idWStr result = data != NULL ? data : L"";
	if ( data != NULL ) {
		Mem_Free( data );
	}
	return result;
}

void idSysLocal::SetClipboardData( const wchar_t *string ) {
	Sys_SetClipboardData( string );
}

void idSysLocal::SetServerInfo( const char*, const char* ) {
}

void idSysLocal::FlushServerInfo() {
}

void idSysLocal::InitInput() {
	Keyboard().Init();
	ime.Init();
	controllerManager.Init();
}

void idSysLocal::ShutdownInput() {
	controllerManager.Shutdown();
	ime.Shutdown();
	Keyboard().Shutdown();
}

sdIME& idSysLocal::IME() {
	return ime;
}

void idSysLocal::SetSystemLocale() {
	Sys_SetSystemLocale();
}

void idSysLocal::SetDefaultLocale() {
	Sys_SetDefaultLocale();
}

sdLogitechLCDSystem* idSysLocal::GetLCDSystem() {
	return NULL;
}

const char *idSysLocal::NetAdrToString( const netadr_t& address ) const {
	return Sys_NetAdrToString( address );
}

bool idSysLocal::IsLANAddress( const netadr_t& address ) const {
	return Sys_IsLANAddress( address );
}

bool idSysLocal::StringToNetAdr( const char *text, netadr_t *address, bool doDNSResolve ) const {
	return Sys_StringToNetAdr( text, address, doDNSResolve );
}

int idSysLocal::GetGUID( unsigned char* guid, const int len ) const {
	if ( guid == NULL || len <= 0 ) {
		return 0;
	}
	LARGE_INTEGER counter;
	QueryPerformanceCounter( &counter );
	unsigned int state = static_cast< unsigned int >( counter.LowPart ^ GetCurrentProcessId() ^ GetCurrentThreadId() );
	for ( int i = 0; i < len; i++ ) {
		state = state * 1664525u + 1013904223u;
		guid[ i ] = static_cast< unsigned char >( state >> 24 );
	}
	return len;
}

void Sys_Init() {
	sysLocal.Init();
}

void Sys_Shutdown() {
	sysLocal.Shutdown();
}

void Sys_Error( const char *error, ... ) {
	char text[ SYSTEM_MAX_PRINT_MSG ];
	va_list args;
	va_start( args, error );
	idStr::vsnPrintf( text, sizeof( text ), error, args );
	va_end( args );
	OutputDebugStringA( text );
	fprintf( stderr, "%s\n", text );
	fflush( stderr );
	MessageBoxA( NULL, text, GAME_NAME, MB_OK | MB_ICONERROR );
	ExitProcess( 1 );
}

void Sys_Quit() {
	InterlockedExchange( &quitRequested, 1 );
	PostQuitMessage( 0 );
}

bool Sys_AlreadyRunning() {
	return false;
}

void Sys_CPUInfo( cpuInfo_t& info ) {
	SYSTEM_INFO systemInfo;
	GetSystemInfo( &systemInfo );
	info.logicalNum = Max( 1, static_cast< int >( systemInfo.dwNumberOfProcessors ) );
	info.physicalNum = info.logicalNum;
	info.hyperThreadedStatus = HT_CANNOT_DETECT;
}

void Sys_Printf( const char *message, ... ) {
	char text[ SYSTEM_MAX_PRINT_MSG ];
	va_list args;
	va_start( args, message );
	idStr::vsnPrintf( text, sizeof( text ), message, args );
	va_end( args );
	OutputDebugStringA( text );
	fputs( text, stdout );
	fflush( stdout );
}

void Sys_DebugPrintf( const char *message, ... ) {
	va_list args;
	va_start( args, message );
	Sys_DebugVPrintf( message, args );
	va_end( args );
}

void Sys_DebugVPrintf( const char *message, va_list args ) {
	char text[ SYSTEM_MAX_PRINT_MSG ];
	idStr::vsnPrintf( text, sizeof( text ), message, args );
	OutputDebugStringA( text );
}

void Sys_Sleep( int msec ) {
	Sleep( msec > 0 ? msec : 0 );
}

int Sys_Milliseconds() {
	if ( timerFrequency.QuadPart == 0 ) {
		QueryPerformanceFrequency( &timerFrequency );
		QueryPerformanceCounter( &timerBase );
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter( &now );
	return static_cast< int >( ( now.QuadPart - timerBase.QuadPart ) * 1000 / timerFrequency.QuadPart );
}

unsigned long Sys_TimeBase() {
	return static_cast< unsigned long >( time( NULL ) );
}

double Sys_GetClockTicks() {
	LARGE_INTEGER value;
	QueryPerformanceCounter( &value );
	return static_cast< double >( value.QuadPart );
}

double Sys_GetClockTicksNoFlush() {
	return Sys_GetClockTicks();
}

double Sys_ClockTicksPerSecond() {
	if ( timerFrequency.QuadPart == 0 ) {
		QueryPerformanceFrequency( &timerFrequency );
	}
	return static_cast< double >( timerFrequency.QuadPart );
}

time_t Sys_RealTime( sysTime_t* value ) {
	const time_t now = time( NULL );
	if ( value != NULL ) {
		tm local;
		localtime_s( &local, &now );
		FillSysTime( local, *value );
	}
	return now;
}

time_t Sys_TimeDiff( const sysTime_t& from, const sysTime_t& to ) {
	return MakeTime( to ) - MakeTime( from );
}

void Sys_SecondsToTime( time_t value, sysTime_t& out, bool localTime ) {
	tm converted;
	if ( localTime ) {
		localtime_s( &converted, &value );
	} else {
		gmtime_s( &converted, &value );
	}
	FillSysTime( converted, out );
}

const char* Sys_TimeToStr( const sysTime_t& value ) {
	static char text[ 64 ];
	idStr::snPrintf( text, sizeof( text ), "%02d:%02d:%02d", value.tm_hour, value.tm_min, value.tm_sec );
	return text;
}

const char* Sys_SecondsToStr( const time_t value, bool localTime ) {
	sysTime_t converted;
	Sys_SecondsToTime( value, converted, localTime );
	return Sys_TimeToStr( converted );
}

const char* Sys_TimeToSystemStr( const sysTime_t& value ) {
	static char text[ 64 ];
	idStr::snPrintf( text, sizeof( text ), "%04d-%02d-%02d %02d:%02d:%02d", value.tm_year + 1900, value.tm_mon + 1, value.tm_mday, value.tm_hour, value.tm_min, value.tm_sec );
	return text;
}

const char* Sys_TimeAndDateToSystemStr( const sysTime_t& value ) {
	return Sys_TimeToSystemStr( value );
}

cpuid_t Sys_GetProcessorId() {
	return CPUID_GENERIC;
}

cpuid_t Sys_GetCPUId() {
	return Sys_GetProcessorId();
}

const char *Sys_GetProcessorString() {
	return "generic x86";
}

bool Sys_FPU_StackIsEmpty() { return true; }
void Sys_FPU_ClearStack() {}
const char *Sys_FPU_GetState() { return "FPU state unavailable"; }
void Sys_FPU_EnableExceptions( int ) {}
void Sys_FPU_SetPrecision( int ) {}
void Sys_FPU_SetRounding( int ) {}
void Sys_FPU_SetFTZ( bool ) {}
void Sys_FPU_SetDAZ( bool ) {}
int Sys_GetSystemRam() {
	MEMORYSTATUSEX status;
	status.dwLength = sizeof( status );
	GlobalMemoryStatusEx( &status );
	return static_cast< int >( status.ullTotalPhys >> 20 );
}
int Sys_GetVideoRam() { return 0; }
bool Sys_GetGfxDeviceIdentification( idStr &vendorID, idStr &deviceID ) { vendorID.Clear(); deviceID.Clear(); return false; }

int Sys_GetDriveFreeSpace( const char *path ) {
	ULARGE_INTEGER available;
	if ( path == NULL || !GetDiskFreeSpaceExA( path, &available, NULL, NULL ) ) {
		return 0;
	}
	return static_cast< int >( available.QuadPart >> 20 );
}

void Sys_GetCurrentMemoryStatus( sysMemoryStats_t &stats ) {
	MEMORYSTATUSEX status;
	status.dwLength = sizeof( status );
	GlobalMemoryStatusEx( &status );
	memset( &stats, 0, sizeof( stats ) );
	stats.memoryLoad = status.dwMemoryLoad;
	stats.totalPhysical = static_cast< int >( status.ullTotalPhys >> 10 );
	stats.availPhysical = static_cast< int >( status.ullAvailPhys >> 10 );
	stats.totalPageFile = static_cast< int >( status.ullTotalPageFile >> 10 );
	stats.availPageFile = static_cast< int >( status.ullAvailPageFile >> 10 );
	stats.totalVirtual = static_cast< int >( status.ullTotalVirtual >> 10 );
	stats.availVirtual = static_cast< int >( status.ullAvailVirtual >> 10 );
	stats.availExtendedVirtual = static_cast< int >( status.ullAvailExtendedVirtual >> 10 );
}

void Sys_GetExeLaunchMemoryStatus( sysMemoryStats_t &stats ) {
	Sys_GetCurrentMemoryStatus( stats );
}

void Sys_GetProcessMemoryStatus( sysProcessMemoryStats_t &stats ) {
	memset( &stats, 0, sizeof( stats ) );
	Sys_GetCurrentMemoryStatus( stats.globalStats );
}

void Sys_SetPhysicalWorkMemory( int, int ) {}

const char *Sys_GetCurCallStackAddressStr( int ) { return ""; }

void *Sys_DLL_Load( const char *dllName, bool ) {
	return dllName != NULL ? LoadLibraryA( dllName ) : NULL;
}

void *Sys_DLL_GetProcAddress( void* dllHandle, const char *procName ) {
	return dllHandle != NULL && procName != NULL ? reinterpret_cast< void* >( GetProcAddress( static_cast< HMODULE >( dllHandle ), procName ) ) : NULL;
}

void Sys_DLL_Unload( void* dllHandle ) {
	if ( dllHandle != NULL ) {
		FreeLibrary( static_cast< HMODULE >( dllHandle ) );
	}
}

void Sys_GenerateEvents() {
	sysLocal.ProcessOSEvents();
	IN_Frame();
}

void Sys_PumpEvents() {
	sysLocal.ProcessOSEvents();
}

void Sys_ShowWindow( bool show ) {
	if ( sys3D == NULL ) {
		return;
	}
	if ( show ) {
		sys3D->ShowGameWindow();
	} else {
		sys3D->HideGameWindow();
	}
}

bool Sys_IsWindowVisible() {
	if ( sys3D == NULL ) {
		return false;
	}
	const HWND window = sys3D->GetGameWindowHandle();
	return window != NULL && IsWindowVisible( window ) != FALSE && !sys3D->IsMinimized();
}

bool Sys_IsWindowFocused() {
	if ( sys3D == NULL ) {
		return false;
	}
	const HWND window = sys3D->GetGameWindowHandle();
	return window != NULL && GetForegroundWindow() == window;
}
void Sys_ShowConsole( int, bool ) {}
void Sys_UpdateConsole() {}

void Sys_Mkdir( const char *path ) {
	if ( path != NULL ) {
		_mkdir( path );
	}
}

int Sys_Rmdir( const char *path ) {
	return path != NULL ? _rmdir( path ) : -1;
}

bool Sys_CopyFile( const char* fromOSPath, const char* toOSPath, bool overwrite ) {
	return fromOSPath != NULL && toOSPath != NULL && CopyFileA( fromOSPath, toOSPath, overwrite ? FALSE : TRUE ) != FALSE;
}

long Sys_FileTimeStamp( FILE *file ) {
	if ( file == NULL ) {
		return 0;
	}
	struct _stat status;
	return _fstat( _fileno( file ), &status ) == 0 ? static_cast< long >( status.st_mtime ) : 0;
}

const char *Sys_DefaultCDPath() { return ""; }

const char *Sys_DefaultBasePath() {
	static char path[ SYSTEM_MAX_OS_PATH ];
	_getcwd( path, sizeof( path ) );
	path[ sizeof( path ) - 1 ] = '\0';
	return path;
}

const char *Sys_DefaultSavePath() { return Sys_DefaultBasePath(); }
const char *Sys_DefaultUserPath() { return Sys_DefaultBasePath(); }

const char *Sys_EXEPath() {
	static char path[ SYSTEM_MAX_OS_PATH ];
	GetModuleFileNameA( NULL, path, sizeof( path ) );
	path[ sizeof( path ) - 1 ] = '\0';
	return path;
}

void Sys_ShowSplashScreen( bool ) {}

void Sys_GetDesktopSize( int& width, int& height ) {
	width = GetSystemMetrics( SM_CXSCREEN );
	height = GetSystemMetrics( SM_CYSCREEN );
}

int Sys_ListFiles( const char *directory, const char *extension, idList< idStr > &list ) {
	list.Clear();
	if ( directory == NULL ) {
		return -1;
	}
	const bool directoriesOnly = extension != NULL && extension[ 0 ] == '/' && extension[ 1 ] == '\0';
	idStr search = directory;
	search.AppendPath( directoriesOnly ? "*" : va( "*%s", extension != NULL ? extension : "" ) );
	_finddata_t findInfo;
	intptr_t findHandle = _findfirst( search.c_str(), &findInfo );
	if ( findHandle == -1 ) {
		return -1;
	}
	do {
		const bool isDirectory = ( findInfo.attrib & _A_SUBDIR ) != 0;
		if ( isDirectory == directoriesOnly && idStr::Cmp( findInfo.name, "." ) != 0 && idStr::Cmp( findInfo.name, ".." ) != 0 ) {
			list.Append( findInfo.name );
		}
	} while ( _findnext( findHandle, &findInfo ) == 0 );
	_findclose( findHandle );
	return list.Num();
}

void Sys_SetFatalError( const char * ) {}
void Sys_DoPreferences() {}
int Sys_CPUCount( int &logicalNum, int &physicalNum ) {
	cpuInfo_t info;
	Sys_CPUInfo( info );
	logicalNum = info.logicalNum;
	physicalNum = info.physicalNum;
	return info.hyperThreadedStatus;
}
int Sys_GenerateDiag( const char * ) { return 0; }
const char* Sys_GetEnv( const char* name ) { return name != NULL ? getenv( name ) : NULL; }
FILE* Sys_TempFile() { return tmpfile(); }

wchar_t *Sys_GetClipboardData() {
	if ( !OpenClipboard( NULL ) ) {
		return NULL;
	}
	HANDLE handle = GetClipboardData( CF_UNICODETEXT );
	const wchar_t* source = handle != NULL ? static_cast< const wchar_t* >( GlobalLock( handle ) ) : NULL;
	wchar_t* result = NULL;
	if ( source != NULL ) {
		const size_t bytes = ( wcslen( source ) + 1 ) * sizeof( wchar_t );
		result = static_cast< wchar_t* >( Mem_Alloc( bytes ) );
		memcpy( result, source, bytes );
		GlobalUnlock( handle );
	}
	CloseClipboard();
	return result;
}

void Sys_SetClipboardData( const wchar_t *string ) {
	if ( string == NULL || !OpenClipboard( NULL ) ) {
		return;
	}
	EmptyClipboard();
	const size_t bytes = ( wcslen( string ) + 1 ) * sizeof( wchar_t );
	HGLOBAL handle = GlobalAlloc( GMEM_MOVEABLE, bytes );
	if ( handle != NULL ) {
		void* target = GlobalLock( handle );
		memcpy( target, string, bytes );
		GlobalUnlock( handle );
		SetClipboardData( CF_UNICODETEXT, handle );
	}
	CloseClipboard();
}

idPort::idPort() :
	netSocket( -1 ),
	silent( false ),
	packetsRead( 0 ),
	bytesRead( 0 ),
	packetsWritten( 0 ),
	bytesWritten( 0 ) {
	memset( &bound_to, 0, sizeof( bound_to ) );
	bound_to.type = NA_BAD;
}

idPort::~idPort() {
	Close();
}

bool idPort::InitForPort( int portNumber ) {
	Close();
	SOCKET socketHandle = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( socketHandle == INVALID_SOCKET ) {
		return false;
	}
	u_long nonBlocking = 1;
	ioctlsocket( socketHandle, FIONBIO, &nonBlocking );
	BOOL broadcast = TRUE;
	setsockopt( socketHandle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast< const char* >( &broadcast ), sizeof( broadcast ) );
	sockaddr_in address;
	memset( &address, 0, sizeof( address ) );
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( portNumber == PORT_ANY ? 0 : portNumber );
	if ( bind( socketHandle, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) == SOCKET_ERROR ) {
		closesocket( socketHandle );
		return false;
	}
	int addressLength = sizeof( address );
	getsockname( socketHandle, reinterpret_cast< sockaddr* >( &address ), &addressLength );
	netSocket = static_cast< int >( socketHandle );
	bound_to.type = NA_IP;
	memset( bound_to.ip, 0, sizeof( bound_to.ip ) );
	bound_to.port = ntohs( address.sin_port );
	return true;
}

void idPort::Close() {
	if ( netSocket != -1 ) {
		closesocket( static_cast< SOCKET >( netSocket ) );
		netSocket = -1;
	}
	bound_to.type = NA_BAD;
	bound_to.port = 0;
}

bool idPort::GetPacket( netadr_t &from, void *data, int &size, int maxSize ) {
	size = 0;
	if ( netSocket == -1 || data == NULL || maxSize <= 0 ) {
		return false;
	}
	sockaddr_in address;
	int addressLength = sizeof( address );
	const int received = recvfrom( static_cast< SOCKET >( netSocket ), static_cast< char* >( data ), maxSize, 0, reinterpret_cast< sockaddr* >( &address ), &addressLength );
	if ( received == SOCKET_ERROR ) {
		return false;
	}
	NetadrFromSockaddr( address, from );
	size = received;
	packetsRead++;
	bytesRead += received;
	return true;
}

bool idPort::GetPacketBlocking( netadr_t &from, void *data, int &size, int maxSize, int timeout ) {
	if ( netSocket == -1 ) {
		return false;
	}
	fd_set readSet;
	FD_ZERO( &readSet );
	FD_SET( static_cast< SOCKET >( netSocket ), &readSet );
	timeval wait;
	wait.tv_sec = timeout / 1000;
	wait.tv_usec = ( timeout % 1000 ) * 1000;
	if ( select( 0, &readSet, NULL, NULL, &wait ) <= 0 ) {
		return false;
	}
	return GetPacket( from, data, size, maxSize );
}

void idPort::SendPacket( const netadr_t to, const void *data, int size ) {
	if ( silent || netSocket == -1 || data == NULL || size <= 0 ) {
		return;
	}
	sockaddr_in address;
	SockaddrFromNetadr( to, address );
	const int sent = sendto( static_cast< SOCKET >( netSocket ), static_cast< const char* >( data ), size, 0, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) );
	if ( sent != SOCKET_ERROR ) {
		packetsWritten++;
		bytesWritten += sent;
	}
}

bool Sys_StringToNetAdr( const char *text, netadr_t *address, bool doDNSResolve ) {
	if ( text == NULL || address == NULL ) {
		return false;
	}
	char host[ 256 ];
	idStr::Copynz( host, text, sizeof( host ) );
	char* portText = strrchr( host, ':' );
	int port = 0;
	if ( portText != NULL ) {
		*portText++ = '\0';
		port = atoi( portText );
	}
	memset( address, 0, sizeof( *address ) );
	address->port = static_cast< unsigned short >( port );
	if ( idStr::Icmp( host, "localhost" ) == 0 ) {
		address->type = NA_LOOPBACK;
		address->ip[ 0 ] = 127;
		address->ip[ 3 ] = 1;
		return true;
	}
	unsigned long parsed = inet_addr( host );
	if ( parsed == INADDR_NONE && doDNSResolve ) {
		hostent* entry = gethostbyname( host );
		if ( entry != NULL && entry->h_addr_list[ 0 ] != NULL ) {
			memcpy( &parsed, entry->h_addr_list[ 0 ], sizeof( parsed ) );
		}
	}
	if ( parsed == INADDR_NONE ) {
		address->type = NA_BAD;
		return false;
	}
	address->type = NA_IP;
	memcpy( address->ip, &parsed, sizeof( address->ip ) );
	return true;
}

const char *Sys_NetAdrToString( const netadr_t& address ) {
	static char text[ 64 ];
	if ( address.type == NA_LOOPBACK ) {
		idStr::snPrintf( text, sizeof( text ), "localhost:%u", address.port );
	} else {
		idStr::snPrintf( text, sizeof( text ), "%u.%u.%u.%u:%u", address.ip[ 0 ], address.ip[ 1 ], address.ip[ 2 ], address.ip[ 3 ], address.port );
	}
	return text;
}

bool Sys_NetAdrToHostName( const netadr_t& address, char** text ) {
	if ( text == NULL ) {
		return false;
	}
	*text = const_cast< char* >( Sys_NetAdrToString( address ) );
	return true;
}

bool Sys_IsLANAddress( const netadr_t& address ) {
	if ( address.type == NA_LOOPBACK ) {
		return true;
	}
	return address.type == NA_IP &&
		( address.ip[ 0 ] == 10 ||
		  ( address.ip[ 0 ] == 172 && address.ip[ 1 ] >= 16 && address.ip[ 1 ] <= 31 ) ||
		  ( address.ip[ 0 ] == 192 && address.ip[ 1 ] == 168 ) );
}

bool Sys_CompareNetAdrBase( const netadr_t& a, const netadr_t& b ) {
	return a.type == b.type && memcmp( a.ip, b.ip, sizeof( a.ip ) ) == 0;
}

int Sys_GetLocalIPCount() { return 1; }
const char *Sys_GetLocalIP( int i ) { return i == 0 ? "127.0.0.1" : NULL; }

void Sys_InitNetworking() {
	if ( networkingInitialized ) {
		return;
	}
	WSADATA data;
	networkingInitialized = WSAStartup( MAKEWORD( 2, 2 ), &data ) == 0;
}

void Sys_ShutdownNetworking() {
	if ( networkingInitialized ) {
		WSACleanup();
		networkingInitialized = false;
	}
}

int Sys_MessageBox( const char* title, const char* buffer, messageBoxType_t type ) {
	UINT flags = MB_OK;
	if ( type == MB_WARNING ) {
		flags |= MB_ICONWARNING;
	} else if ( type == MB_FATALERROR ) {
		flags |= MB_ICONERROR;
	} else {
		flags |= MB_ICONINFORMATION;
	}
	return MessageBoxA( NULL, buffer != NULL ? buffer : "", title != NULL ? title : GAME_NAME, flags );
}

sdLogitechLCDSystem* Sys_GetLogitechLCDSystem() { return NULL; }
void Sys_SetConsoleName( const char* ) {}

bool Sys_GetHTTPProxyAddress( char proxy[ MAX_PROXY_LENGTH ] ) {
	if ( proxy == NULL ) {
		return false;
	}
	const char* value = getenv( "HTTP_PROXY" );
	idStr::Copynz( proxy, value != NULL ? value : "", MAX_PROXY_LENGTH );
	return value != NULL && value[ 0 ] != '\0';
}

void Sys_SetSystemLocale() { setlocale( LC_ALL, "" ); }
void Sys_SetDefaultLocale() { setlocale( LC_ALL, "C" ); }

int Sys_GetLanguageIndex( const char* langName ) {
	for ( int i = 0; sysLanguageNames[ i ] != NULL; i++ ) {
		if ( idStr::Icmp( sysLanguageNames[ i ], langName ) == 0 ) {
			return i;
		}
	}
	return 0;
}

int WINAPI WinMain( HINSTANCE, HINSTANCE, LPSTR commandLine, int ) {
	const char* argv[] = { GAME_NAME };
	common->Init( 1, argv, commandLine != NULL ? commandLine : "" );
	// Retail ETQW performs this handoff after common initialization.  The
	// renderer creates the window before input is initialized, and input
	// deliberately starts with its mouse-release latch set.  Showing and
	// focusing the finished game window here produces the activation event
	// that clears that latch.  This is especially important when Visual Studio
	// owns the foreground window while launching the process under F5.
	if ( sys3D != NULL && sys3D->GetGameWindowHandle() != NULL ) {
		sys3D->ShowGameWindow();
		HWND gameWindow = reinterpret_cast< HWND >( sys3D->GetGameWindowHandle() );
		SetForegroundWindow( gameWindow );
		SetActiveWindow( gameWindow );
		SetFocus( gameWindow );
		// If the window already owned keyboard focus before Sys_InitInput ran,
		// SetFocus does not emit WM_SETFOCUS again.  Input initialization leaves
		// mouseReleased set deliberately, so clear it explicitly after the entire
		// engine is initialized instead of depending on another focus message.
		sys->Mouse().GrabCursor( true );
	}
	while ( InterlockedCompareExchange( &quitRequested, 0, 0 ) == 0 ) {
		common->Frame();
		Sys_Sleep( 1 );
	}
	common->Shutdown();
	return 0;
}
