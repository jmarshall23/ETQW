// Copyright (C) 2007 Id Software, Inc.
//
// ETQW common-system reconstruction.  The public interface comes from the
// released SDK; the private object layout and behaviour are backed by the
// Microsoft PDB and the matching retail executable disassembly.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "precompiled.h"
#include "AdManager.h"
#include "GraphManager.h"
#include "NotificationSystem.h"
#include "../decllib/declLocStr.h"
#include "../renderer/DeviceContext.h"
#include "../renderer/renderbindingmanager.h"
#include "../bse/BSEInterface.h"
#include "../libs/AASLib/AASFileManager.h"
#include "../sdnet/SDNet.h"
#include "../sys/sys_render.h"

#define MAX_PRINT_MSG_SIZE 4096
#define MAX_WARNING_LIST 256
#define MAX_CONSOLE_LINES 32

enum errorParm_t {
	ERP_NONE,
	ERP_FATAL,
	ERP_DROP,
	ERP_DISCONNECT
};

struct versionString_t {
	versionString_t() {
		idStr::snPrintf(
			string,
			sizeof( string ),
			"%s %d.%d.%d.%d %s %s",
			GAME_NAME,
			ENGINE_VERSION_MAJOR,
			ENGINE_VERSION_MINOR,
			ENGINE_SRC_REVISION,
			ENGINE_MEDIA_REVISION,
			BUILD_STRING,
			__DATE__
		);
	}

	char string[ 256 ];
};

static versionString_t versionString;

idCVar com_version( "si_version", versionString.string, CVAR_SYSTEM | CVAR_ROM | CVAR_SERVERINFO, "engine version" );
idCVar com_skipRenderer( "com_skipRenderer", "0", CVAR_BOOL | CVAR_SYSTEM, "skip renderer initialization" );
idCVar com_machineSpec( "com_machineSpec", "-1", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM, "hardware quality classification", -1, 3 );
idCVar com_gpuSpec( "com_gpuSpec", "3", CVAR_INTEGER | CVAR_ARCHIVE | CVAR_SYSTEM,
	"hardware classification, -1 = not detected, 0 = low quality, 1 = medium quality, 2 = high quality, 3 = ultra quality" );
idCVar com_purgeAll( "com_purgeAll", "0", CVAR_BOOL | CVAR_ARCHIVE | CVAR_SYSTEM, "purge all media between level loads" );
idCVar com_memoryMarker( "com_memoryMarker", "-1", CVAR_INTEGER | CVAR_SYSTEM | CVAR_INIT, "memory statistics marker" );
idCVar com_preciseTic( "com_preciseTic", "1", CVAR_BOOL | CVAR_SYSTEM, "run exact user command ticks" );
idCVar com_asyncInput( "com_asyncInput", "0", CVAR_BOOL | CVAR_SYSTEM, "sample input from the async thread" );
idCVar com_asyncSound( "com_asyncSound", "1", CVAR_INTEGER | CVAR_SYSTEM, "0: inline, 1: async sound update", 0, 1 );
idCVar com_forceGenericSIMD( "com_forceGenericSIMD", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "force generic SIMD" );
idCVar com_developer( "developer", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "developer mode" );
idCVar com_allowConsole( "com_allowConsole", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "allow console toggling" );
idCVar com_speeds( "com_speeds", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "show engine timings" );
idCVar com_showFPS( "com_showFPS", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "show frame rate" );
idCVar com_showMemoryUsage( "com_showMemoryUsage", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "show memory usage" );
idCVar com_showAsyncStats( "com_showAsyncStats", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "show async statistics" );
idCVar com_showSoundDecoders( "com_showSoundDecoders", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "show sound decoders" );
idCVar com_timestampPrints( "com_timestampPrints", "0", CVAR_INTEGER | CVAR_SYSTEM, "prefix prints with a timestamp", 0, 2 );
idCVar com_timescale( "timescale", "1", CVAR_FLOAT | CVAR_SYSTEM, "scales game time", 0.1f, 10.0f );
idCVar com_logFile( "logFile", "0", CVAR_INTEGER | CVAR_SYSTEM | CVAR_NOCHEAT, "1: log, 2: force flush", 0, 2 );
idCVar com_logFileName( "logFileName", "console.log", CVAR_SYSTEM | CVAR_NOCHEAT, "console log file name" );
idCVar com_logTimeStamps( "logTimeStamps", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "timestamp console log lines" );
idCVar com_makingBuild( "com_makingBuild", "0", CVAR_BOOL | CVAR_SYSTEM, "building generated data" );
idCVar com_updateLoadSize( "com_updateLoadSize", "0", CVAR_BOOL | CVAR_SYSTEM | CVAR_NOCHEAT, "update map load sizes" );
idCVar com_videoRam( "com_videoRam", "64", CVAR_INTEGER | CVAR_SYSTEM | CVAR_NOCHEAT | CVAR_ARCHIVE, "detected video memory" );
idCVar com_product_lang_ext( "com_product_lang_ext", "1", CVAR_INTEGER | CVAR_SYSTEM | CVAR_ARCHIVE, "language file extension" );

int time_gameFrame;
int time_gameDraw;
int time_frontend;
int time_backend;

int com_frameTime;
int com_frameNumber;
volatile int com_ticNumber;
int com_editors;
bool com_editorActive;

#ifdef _WIN32
HWND com_hwndMsg = NULL;
bool com_outputMsg = false;
unsigned int com_msgID = static_cast< unsigned int >( -1 );
#endif

idGame* game = NULL;
idGameEdit* gameEdit = NULL;

static idCmdArgs com_consoleLines[ MAX_CONSOLE_LINES ];
static int com_numConsoleLines;

// The retail table is a 26-entry, 20-byte array at r_vidModes.  Aspect values
// are the SDK's 4:3, 16:9, 16:10 and 5:4 indices.
static vidmode_t r_vidModes[ 26 ] = {
	{ "Mode  0: 320x240",   320,  240, 0, false },
	{ "Mode  1: 400x300",   400,  300, 0, false },
	{ "Mode  2: 512x384",   512,  384, 0, false },
	{ "Mode  3: 640x480",   640,  480, 0, false },
	{ "Mode  4: 800x600",   800,  600, 0, false },
	{ "Mode  5: 1024x768", 1024,  768, 0, false },
	{ "Mode  6: 1152x864", 1152,  864, 0, false },
	{ "Mode  7: 1280x960", 1280,  960, 0, false },
	{ "Mode  8: 1600x1200",1600, 1200, 0, false },
	{ "Mode  9: 2048x1536",2048, 1536, 0, false },
	{ "Mode 10: 856x480",   856,  480, 1, false },
	{ "Mode 11: 1024x576", 1024,  576, 1, false },
	{ "Mode 12: 1280x720", 1280,  720, 1, false },
	{ "Mode 13: 1360x768", 1360,  768, 1, false },
	{ "Mode 14: 1366x768", 1366,  768, 1, false },
	{ "Mode 15: 1600x900", 1600,  900, 1, false },
	{ "Mode 16: 1920x1080",1920, 1080, 1, false },
	{ "Mode 17: 2560x1440",2560, 1440, 1, false },
	{ "Mode 18: 1024x640", 1024,  640, 2, false },
	{ "Mode 19: 1280x800", 1280,  800, 2, false },
	{ "Mode 20: 1440x900", 1440,  900, 2, false },
	{ "Mode 21: 1680x1050",1680, 1050, 2, false },
	{ "Mode 22: 1920x1200",1920, 1200, 2, false },
	{ "Mode 23: 2560x1600",2560, 1600, 2, false },
	{ "Mode 24: 1280x1024",1280, 1024, 3, false },
	{ "Mode 25: 2560x2048",2560, 2048, 3, false }
};

class idCommonLocal : public idCommon {
public:
	idCommonLocal();

	virtual void			Init( int argc, const char** argv, const char* cmdline );
	virtual void			Shutdown();
	virtual void			Quit();
	virtual bool			IsInitialized() const;
	virtual void			Frame();
	virtual void			Async();
	virtual bool			StartupVariable( const char* match );
	virtual void			ClearStartupVariable( const char* match );
	virtual void			WriteConfigToFile( const char* filename, bool writeBindings, bool writeCVars );
	virtual void			WriteFlaggedCVarsToFile( const char* filename, int flags, const char* setCmd );
	virtual void			BeginRedirect( char* buffer, int buffersize, void* user, void ( *flush )( void*, const char* ) );
	virtual void			EndRedirect();
	virtual void			SetRefreshOnPrint( bool set );
	virtual void			Printf( const char* fmt, ... );
	virtual void			TPrintf( const char* fmt, ... );
	virtual void			VPrintf( const char* fmt, va_list args );
	virtual void			DPrintf( const char* fmt, ... );
	virtual void			Warning( const char* fmt, ... );
	virtual void			TWarning( const char* fmt, ... );
	virtual void			DWarning( const char* fmt, ... );
	virtual void			PrintWarnings();
	virtual void			ClearWarnings( const char* reason );
	virtual void			Error( const char* fmt, ... );
	virtual void			FatalError( const char* fmt, ... );
	virtual void			PrintLoadingMessage( const char* msg );
	virtual void			EnableWarnings();
	virtual void			DisableWarnings();
	virtual void			PacifierUpdate();
	virtual void			UpdateLevelLoadScreen( const wchar_t* status );
	virtual const idLangDict* GetLanguageDict();
	virtual idWStr			LocalizeText( const char* declName, const idWStrList& arguments );
	virtual idWStr			LocalizeText( const sdDeclLocStr* loc, const idWStrList& arguments );
	virtual int				GetNumVideoModes() const;
	virtual vidmode_t&		GetVideoMode( int index ) const;
	virtual idSoundWorld*	GetGameSoundWorld();
	virtual idSoundWorld*	GetMenuSoundWorld();
	virtual void			WriteConfigs();

private:
	void					ParseCommandLine( int argc, const char** argv );
	bool					AddStartupCommands();
	bool					SafeMode();
	void					InitCommands();
	void					InitGame( bool resetConfigs );
	void					LoadGameDLL();
	void					UnloadGameDLL();
	void					InitLanguageDict( bool reload );
	void					InitSIMD();
	void					SingleAsyncTic();
	void					WriteConfiguration();
	void					CloseLogFile();
	void					DumpWarnings();

	bool					com_fullyInitialized;
	bool					refreshOnPrint;
	int						com_errorEntered;
	bool					com_shuttingDown;
	idFile*					logFile;
	char					errorMessage[ MAX_PRINT_MSG_SIZE ];
	char*					rd_buffer;
	int						rd_buffersize;
	void*					rd_user;
	void					( *rd_flush )( void*, const char* );
	idStr					warningCaption;
	idStrList				warningList;
	idStrList				errorList;
	bool					noWarnings;
	void*					gameDLL;
	idLangDict				languageDict;
	int						nextConfigWriteTime;
};

#if defined( _WIN32 ) && !defined( _WIN64 )
assert_sizeof( idCommonLocal, 0x109c );
#endif

static idCommonLocal commonLocal;
idCommon* common = &commonLocal;

idCommonLocal::idCommonLocal() :
	com_fullyInitialized( false ),
	refreshOnPrint( false ),
	com_errorEntered( ERP_NONE ),
	com_shuttingDown( false ),
	logFile( NULL ),
	rd_buffer( NULL ),
	rd_buffersize( 0 ),
	rd_user( NULL ),
	rd_flush( NULL ),
	noWarnings( false ),
	gameDLL( NULL ),
	nextConfigWriteTime( 0 ) {
	errorMessage[ 0 ] = '\0';
	warningList.SetGranularity( 1 );
	errorList.SetGranularity( 1 );
}

void idCommonLocal::BeginRedirect( char* buffer, int buffersize, void* user, void ( *flush )( void*, const char* ) ) {
	if ( buffer == NULL || buffersize <= 0 || flush == NULL ) {
		return;
	}
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_user = user;
	rd_flush = flush;
	rd_buffer[ 0 ] = '\0';
}

void idCommonLocal::EndRedirect() {
	if ( rd_flush != NULL && rd_buffer != NULL && rd_buffer[ 0 ] != '\0' ) {
		rd_flush( rd_user, rd_buffer );
	}
	rd_user = NULL;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

void idCommonLocal::SetRefreshOnPrint( bool set ) {
	refreshOnPrint = set;
}

void idCommonLocal::CloseLogFile() {
	if ( logFile != NULL && fileSystem != NULL ) {
		com_logFile.SetInteger( 0 );
		fileSystem->CloseFile( logFile );
		logFile = NULL;
	}
}

void idCommonLocal::VPrintf( const char* fmt, va_list args ) {
	if ( fmt == NULL ) {
		return;
	}

	char msg[ MAX_PRINT_MSG_SIZE ];
	int prefixLength = 0;
	if ( cvarSystem != NULL && cvarSystem->IsInitialized() && com_timestampPrints.GetInteger() != 0 ) {
		int stamp = Sys_Milliseconds();
		if ( com_timestampPrints.GetInteger() == 1 ) {
			stamp /= 1000;
		}
		prefixLength = idStr::snPrintf( msg, sizeof( msg ), "[%i]", stamp );
		if ( prefixLength < 0 ) {
			prefixLength = 0;
		}
	}
	if ( idStr::vsnPrintf( msg + prefixLength, sizeof( msg ) - prefixLength, fmt, args ) < 0 ) {
		msg[ sizeof( msg ) - 2 ] = '\n';
		msg[ sizeof( msg ) - 1 ] = '\0';
	}

	if ( rd_buffer != NULL ) {
		const int messageLength = idStr::Length( msg );
		const int currentLength = idStr::Length( rd_buffer );
		if ( currentLength + messageLength > rd_buffersize - 1 ) {
			rd_flush( rd_user, rd_buffer );
			rd_buffer[ 0 ] = '\0';
		}
		if ( messageLength <= rd_buffersize - 1 ) {
			idStr::Append( rd_buffer, rd_buffersize, msg );
		} else {
			rd_flush( rd_user, msg );
		}
		return;
	}

	if ( console != NULL ) {
		console->Print( msg );
	}

	char plain[ MAX_PRINT_MSG_SIZE ];
	idStr::Copynz( plain, msg, sizeof( plain ) );
	idStr::RemoveColors( plain );
	Sys_Printf( "%s", plain );

	static bool logFileFailed = false;
	static bool openingLogFile = false;
	if ( cvarSystem != NULL && cvarSystem->IsInitialized() &&
			com_logFile.GetInteger() != 0 && !logFileFailed &&
			fileSystem != NULL && fileSystem->IsInitialized() ) {
		if ( logFile == NULL && !openingLogFile ) {
			openingLogFile = true;
			const char* name = com_logFileName.GetString()[ 0 ] != '\0' ? com_logFileName.GetString() : "console.log";
			logFile = fileSystem->OpenFileWrite( name, "fs_userpath" );
			openingLogFile = false;
			if ( logFile == NULL ) {
				logFileFailed = true;
				Sys_Printf( "failed to open log file '%s'\n", name );
			} else if ( com_logFile.GetInteger() > 1 ) {
				logFile->ForceFlush();
			}
		}
		if ( logFile != NULL ) {
			logFile->Write( plain, idStr::Length( plain ) );
			logFile->Flush();
		}
	}

	if ( com_errorEntered != ERP_FATAL && refreshOnPrint && session != NULL ) {
		session->UpdateScreen( true );
	}
}

void idCommonLocal::Printf( const char* fmt, ... ) {
	va_list args;
	va_start( args, fmt );
	VPrintf( fmt, args );
	va_end( args );
}

void idCommonLocal::TPrintf( const char* fmt, ... ) {
	const bool oldRefresh = refreshOnPrint;
	refreshOnPrint = false;
	va_list args;
	va_start( args, fmt );
	VPrintf( fmt, args );
	va_end( args );
	refreshOnPrint = oldRefresh;
}

void idCommonLocal::DPrintf( const char* fmt, ... ) {
	if ( cvarSystem == NULL || !cvarSystem->IsInitialized() || !com_developer.GetBool() ) {
		return;
	}
	char msg[ MAX_PRINT_MSG_SIZE ];
	va_list args;
	va_start( args, fmt );
	idStr::vsnPrintf( msg, sizeof( msg ), fmt, args );
	va_end( args );
	const bool oldRefresh = refreshOnPrint;
	refreshOnPrint = false;
	Printf( S_COLOR_GRAY "%s", msg );
	refreshOnPrint = oldRefresh;
}

static void FormatCommonMessage( char* target, int targetSize, const char* fmt, va_list args ) {
	idStr::vsnPrintf( target, targetSize, fmt, args );
	target[ targetSize - 1 ] = '\0';
}

void idCommonLocal::Warning( const char* fmt, ... ) {
	if ( noWarnings ) {
		return;
	}
	char msg[ MAX_PRINT_MSG_SIZE ];
	va_list args;
	va_start( args, fmt );
	FormatCommonMessage( msg, sizeof( msg ), fmt, args );
	va_end( args );
	Printf( S_COLOR_YELLOW "WARNING: %s\n", msg );
	if ( warningList.Num() < MAX_WARNING_LIST ) {
		warningList.AddUnique( msg );
	}
}

void idCommonLocal::TWarning( const char* fmt, ... ) {
	if ( noWarnings ) {
		return;
	}
	char msg[ MAX_PRINT_MSG_SIZE ];
	va_list args;
	va_start( args, fmt );
	FormatCommonMessage( msg, sizeof( msg ), fmt, args );
	va_end( args );
	const bool oldRefresh = refreshOnPrint;
	refreshOnPrint = false;
	Printf( S_COLOR_YELLOW "WARNING: %s\n", msg );
	refreshOnPrint = oldRefresh;
	if ( warningList.Num() < MAX_WARNING_LIST ) {
		warningList.AddUnique( msg );
	}
}

void idCommonLocal::DWarning( const char* fmt, ... ) {
	if ( cvarSystem == NULL || !cvarSystem->IsInitialized() || !com_developer.GetBool() || noWarnings ) {
		return;
	}
	char msg[ MAX_PRINT_MSG_SIZE ];
	va_list args;
	va_start( args, fmt );
	FormatCommonMessage( msg, sizeof( msg ), fmt, args );
	va_end( args );
	Printf( S_COLOR_YELLOW "WARNING: %s\n", msg );
}

void idCommonLocal::PrintWarnings() {
	if ( warningList.Num() == 0 ) {
		return;
	}
	warningList.Sort();
	Printf( "------------- Warnings ---------------\n" );
	Printf( "during %s...\n", warningCaption.c_str() );
	for ( int i = 0; i < warningList.Num(); ++i ) {
		Printf( S_COLOR_YELLOW "WARNING: %s\n", warningList[ i ].c_str() );
	}
	Printf( "%s%d warnings\n", warningList.Num() >= MAX_WARNING_LIST ? "more than " : "", warningList.Num() );
}

void idCommonLocal::ClearWarnings( const char* reason ) {
	warningCaption = reason != NULL ? reason : "";
	warningList.Clear();
}

void idCommonLocal::DumpWarnings() {
	if ( warningList.Num() == 0 || fileSystem == NULL || !fileSystem->IsInitialized() ) {
		return;
	}
	idFile* file = fileSystem->OpenFileWrite( "warnings.txt", "fs_savepath" );
	if ( file == NULL ) {
		return;
	}
	file->Printf( "Warnings during %s\n\n", warningCaption.c_str() );
	warningList.Sort();
	for ( int i = 0; i < warningList.Num(); ++i ) {
		file->Printf( "WARNING: %s\n", warningList[ i ].c_str() );
	}
	errorList.Sort();
	for ( int j = 0; j < errorList.Num(); ++j ) {
		file->Printf( "ERROR: %s\n", errorList[ j ].c_str() );
	}
	file->ForceFlush();
	fileSystem->CloseFile( file );
}

void idCommonLocal::Error( const char* fmt, ... ) {
	refreshOnPrint = false;
	if ( com_errorEntered == ERP_FATAL ) {
		Sys_Quit();
	}
	com_errorEntered = ERP_DROP;
	va_list args;
	va_start( args, fmt );
	FormatCommonMessage( errorMessage, sizeof( errorMessage ), fmt, args );
	va_end( args );
	errorList.AddUnique( errorMessage );
	if ( session != NULL ) {
		session->Stop();
	}
	Printf( "********************\nERROR: %s\n********************\n", errorMessage );
	com_errorEntered = ERP_NONE;
	throw idException( errorMessage );
}

void idCommonLocal::FatalError( const char* fmt, ... ) {
	if ( com_errorEntered != ERP_NONE ) {
		Sys_Printf( "FATAL: recursive error:\n%s\n", errorMessage );
		Sys_Quit();
	}
	com_errorEntered = ERP_FATAL;
	va_list args;
	va_start( args, fmt );
	FormatCommonMessage( errorMessage, sizeof( errorMessage ), fmt, args );
	va_end( args );
	Printf( "********************\nFATAL ERROR: %s\n********************\n", errorMessage );
	Sys_SetFatalError( errorMessage );
	Shutdown();
	Sys_Error( "%s", errorMessage );
}

void idCommonLocal::EnableWarnings() {
	noWarnings = false;
}

void idCommonLocal::DisableWarnings() {
	noWarnings = true;
}

void idCommonLocal::ParseCommandLine( int argc, const char** argv ) {
	for ( int i = 0; i < MAX_CONSOLE_LINES; ++i ) {
		com_consoleLines[ i ].Clear();
	}
	com_numConsoleLines = 0;
	for ( int arg = 0; arg < argc && com_numConsoleLines < MAX_CONSOLE_LINES; ++arg ) {
		const char* value = argv[ arg ];
		if ( value == NULL ) {
			continue;
		}
		if ( value[ 0 ] == '+' ) {
			++com_numConsoleLines;
			if ( com_numConsoleLines > MAX_CONSOLE_LINES ) {
				com_numConsoleLines = MAX_CONSOLE_LINES;
				break;
			}
			com_consoleLines[ com_numConsoleLines - 1 ].AppendArg( value + 1 );
		} else {
			if ( com_numConsoleLines == 0 ) {
				com_numConsoleLines = 1;
			}
			com_consoleLines[ com_numConsoleLines - 1 ].AppendArg( value );
		}
	}
}

bool idCommonLocal::StartupVariable( const char* match ) {
	bool found = false;
	if ( cvarSystem == NULL ) {
		return false;
	}
	for ( int i = 0; i < com_numConsoleLines; ++i ) {
		idCmdArgs& args = com_consoleLines[ i ];
		if ( args.Argc() < 3 || idStr::Icmp( args.Argv( 0 ), "set" ) != 0 ) {
			continue;
		}
		if ( match == NULL || idStr::Icmp( args.Argv( 1 ), match ) == 0 ) {
			cvarSystem->SetCVarString( args.Argv( 1 ), args.Argv( 2 ) );
			found = true;
		}
	}
	return found;
}

void idCommonLocal::ClearStartupVariable( const char* match ) {
	if ( match == NULL ) {
		return;
	}
	for ( int i = 0; i < com_numConsoleLines; ++i ) {
		idCmdArgs& args = com_consoleLines[ i ];
		if ( args.Argc() >= 2 && idStr::Icmp( args.Argv( 0 ), "set" ) == 0 &&
				idStr::Icmp( args.Argv( 1 ), match ) == 0 ) {
			args.Clear();
		}
	}
}

bool idCommonLocal::AddStartupCommands() {
	bool added = false;
	for ( int i = 0; i < com_numConsoleLines; ++i ) {
		if ( com_consoleLines[ i ].Argc() == 0 ) {
			continue;
		}
		if ( idStr::Icmpn( com_consoleLines[ i ].Argv( 0 ), "set", 3 ) != 0 ) {
			added = true;
		}
		// Preserve the command name and the already-tokenized arguments.  Using
		// Args() here drops Argv( 0 ), turning "+spawnServer valley" into the
		// unknown command "valley".
		cmdSystem->BufferCommandArgs( CMD_EXEC_APPEND, com_consoleLines[ i ] );
	}
	return added;
}

bool idCommonLocal::SafeMode() {
	for ( int i = 0; i < com_numConsoleLines; ++i ) {
		const char* command = com_consoleLines[ i ].Argv( 0 );
		if ( idStr::Icmp( command, "safe" ) == 0 || idStr::Icmp( command, "cvar_restart" ) == 0 ) {
			com_consoleLines[ i ].Clear();
			return true;
		}
	}
	return false;
}

static void WriteCFGHeader( idFile* file ) {
	file->Printf( "// *********************************************************\n" );
	file->Printf( "// This file is managed by ETQW and will be overwritten\n" );
	file->Printf( "// Put custom commands and bindings in autoexec.cfg\n" );
	file->Printf( "// *********************************************************\n\n" );
}

void idCommonLocal::WriteFlaggedCVarsToFile( const char* filename, int flags, const char* setCmd ) {
	idFile* file = fileSystem->OpenFileWrite( filename, "fs_savepath" );
	if ( file == NULL ) {
		Printf( "Couldn't write %s.\n", filename );
		return;
	}
	cvarSystem->WriteFlaggedVariables( flags, setCmd, file );
	fileSystem->CloseFile( file );
}

void idCommonLocal::WriteConfigToFile( const char* filename, bool writeBindings, bool writeCVars ) {
	idFile* file = fileSystem->OpenFileWrite( filename, "fs_userpath" );
	if ( file == NULL ) {
		Printf( "Couldn't write %s.\n", filename );
		return;
	}
	if ( writeBindings ) {
		WriteCFGHeader( file );
		idKeyInput::WriteBindings( file );
	}
	if ( writeCVars ) {
		WriteCFGHeader( file );
		cvarSystem->WriteFlaggedVariables( CVAR_ARCHIVE, "seta", file );
	}
	fileSystem->CloseFile( file );
}

void idCommonLocal::WriteConfiguration() {
	if ( !com_fullyInitialized || cvarSystem == NULL || fileSystem == NULL ) {
		return;
	}
	if ( ( cvarSystem->GetModifiedFlags() & CVAR_ARCHIVE ) == 0 ) {
		return;
	}
	cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );
	WriteConfigToFile( BINDING_FILE, true, false );
	WriteConfigToFile( CONFIG_FILE, false, true );
}

void idCommonLocal::WriteConfigs() {
	WriteConfiguration();
}

const idLangDict* idCommonLocal::GetLanguageDict() {
	return &languageDict;
}

void idCommonLocal::InitLanguageDict( bool reload ) {
	languageDict.Clear();
	if ( fileSystem == NULL || !fileSystem->IsInitialized() || cvarSystem == NULL ) {
		return;
	}
	StartupVariable( "sys_lang" );
	const char* language = cvarSystem->GetCVarString( "sys_lang" );
	idStr path = va( "localization/%s/strings", language != NULL && language[ 0 ] != '\0' ? language : "english" );
	idFileList* files = fileSystem->ListFilesTree( path, ".lang", true );
	if ( files != NULL ) {
		for ( int i = 0; i < files->GetNumFiles(); ++i ) {
			languageDict.Load( files->GetFile( i ), false );
		}
		fileSystem->FreeFileList( files );
	}
	if ( reload && game != NULL ) {
		game->OnLanguageInit();
	}
}

idWStr idCommonLocal::LocalizeText( const sdDeclLocStr* loc, const idWStrList& arguments ) {
	if ( loc == NULL ) {
		return idWStr();
	}
	idWStr result;
	if ( !loc->Format( result, arguments ) ) {
		Warning( "Failed to localize '%s'", loc->GetName() );
	}
	return result;
}

idWStr idCommonLocal::LocalizeText( const char* declName, const idWStrList& arguments ) {
	if ( declName == NULL || declName[ 0 ] == '\0' ) {
		return idWStr();
	}
	const sdDeclLocStr* loc = declHolder.FindLocStr( declName, false );
	if ( loc == NULL ) {
		return idWStr( va( L"###%hs###", declName ) );
	}
	return LocalizeText( loc, arguments );
}

int idCommonLocal::GetNumVideoModes() const {
	return _arraycount( r_vidModes );
}

vidmode_t& idCommonLocal::GetVideoMode( int index ) const {
	if ( index < 0 || index >= _arraycount( r_vidModes ) ) {
		index = 3;
	}
	return r_vidModes[ index ];
}

idSoundWorld* idCommonLocal::GetGameSoundWorld() {
	return session != NULL ? session->sw : NULL;
}

idSoundWorld* idCommonLocal::GetMenuSoundWorld() {
	return session != NULL ? session->sw : NULL;
}

void idCommonLocal::PacifierUpdate() {
	if ( session != NULL ) {
		session->PacifierUpdate();
	}
}

void idCommonLocal::UpdateLevelLoadScreen( const wchar_t* status ) {
	if ( game != NULL ) {
		game->UpdateLevelLoadScreen( status );
	}
}

void idCommonLocal::PrintLoadingMessage( const char* msg ) {
	if ( msg != NULL && msg[ 0 ] != '\0' ) {
		Printf( "%s\n", msg );
	}
	PacifierUpdate();
}

void idCommonLocal::InitSIMD() {
	idSIMD::InitProcessor( GAME_NAME, com_forceGenericSIMD.GetBool() );
	com_forceGenericSIMD.ClearModified();
}

void idCommonLocal::SingleAsyncTic() {
	const int milliseconds = Sys_Milliseconds();
	if ( usercmdGen != NULL && com_asyncInput.GetBool() ) {
		usercmdGen->UsercmdInterrupt();
	}
	if ( soundSystem != NULL && com_asyncSound.GetInteger() == 1 ) {
		soundSystem->AsyncUpdate( milliseconds );
	}
	++com_ticNumber;
}

void idCommonLocal::Async() {
	if ( com_shuttingDown || !com_fullyInitialized ) {
		return;
	}
	static int lastTicMsec = 0;
	const int now = Sys_Milliseconds();
	if ( lastTicMsec == 0 ) {
		lastTicMsec = now - USERCMD_MSEC;
	}
	int ticMsec = USERCMD_MSEC;
	const float scale = com_timescale.GetFloat();
	if ( scale != 1.0f ) {
		ticMsec = Max( 1, static_cast< int >( ticMsec / scale ) );
	}
	if ( !com_preciseTic.GetBool() ) {
		SingleAsyncTic();
		lastTicMsec = now;
		return;
	}
	while ( lastTicMsec + ticMsec <= now ) {
		SingleAsyncTic();
		lastTicMsec += ticMsec;
	}
}

void idCommonLocal::Frame() {
	if ( !com_fullyInitialized || com_shuttingDown ) {
		return;
	}
	try {
		Sys_FPU_EnableExceptions( 0 );
		Sys_GenerateEvents();
		const int now = Sys_Milliseconds();
		if ( now >= nextConfigWriteTime ) {
			WriteConfiguration();
			nextConfigWriteTime = now + 2000;
		}
		if ( com_forceGenericSIMD.IsModified() ) {
			InitSIMD();
		}
		if ( eventLoop != NULL ) {
			eventLoop->RunEventLoop( true );
		}
		com_frameTime = com_ticNumber * USERCMD_MSEC;
		if ( networkService != NULL ) {
			networkService->RunFrame();
		}
		if ( game != NULL ) {
			// ETQW has a per-host-frame game callback in addition to the
			// fixed-tic RunFrame overload driven by idSessionLocal.
			game->RunFrame();
		}
		idAsyncNetwork::RunFrame();
		if ( session != NULL ) {
			if ( !idAsyncNetwork::IsActive() ) {
				session->Frame();
			} else if ( networkSystem != NULL && networkSystem->IsDedicated() && renderSystem != NULL ) {
				renderSystem->SyncRenderSystem();
			}
			session->UpdateScreen( false );
		}
		++com_frameNumber;
		idLib::frameNumber = com_frameNumber;
	} catch ( idException& ) {
	}
}

static void Com_Quit_f( const idCmdArgs& ) {
	common->Quit();
}

static void Com_WriteConfig_f( const idCmdArgs& args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "Usage: writeConfig <filename>\n" );
		return;
	}
	idStr filename = args.Argv( 1 );
	filename.DefaultFileExtension( ".cfg" );
	common->WriteConfigToFile( filename, true, true );
}

static void Com_ListModes_f( const idCmdArgs& ) {
	common->Printf( "\n" );
	for ( int i = 0; i < common->GetNumVideoModes(); ++i ) {
		const vidmode_t& mode = common->GetVideoMode( i );
		common->Printf( "%s%s\n", mode.description, mode.available ? "" : " - Not available for fullscreen" );
	}
	common->Printf( "\n" );
}

void idCommonLocal::InitCommands() {
	cmdSystem->AddCommand( "quit", Com_Quit_f, CMD_FL_SYSTEM, "quits the game" );
	cmdSystem->AddCommand( "exit", Com_Quit_f, CMD_FL_SYSTEM, "exits the game" );
	cmdSystem->AddCommand( "writeConfig", Com_WriteConfig_f, CMD_FL_SYSTEM, "writes a configuration file" );
	cmdSystem->AddCommand( "listModes", Com_ListModes_f, CMD_FL_RENDERER, "lists video modes" );
}

void idCommonLocal::LoadGameDLL() {
	if ( gameDLL != NULL || game != NULL ) {
		return;
	}

	char dllPath[ MAX_OSPATH ];
	dllPath[ 0 ] = '\0';
	fileSystem->FindDLL( "game", dllPath, true, true );
	if ( dllPath[ 0 ] == '\0' ) {
		idStr localDLL = Sys_EXEPath();
		localDLL.StripFilename();
		localDLL.AppendPath( "gamex86.dll" );
		if ( fileSystem->FileExistsExplicit( localDLL ) ) {
			idStr::Copynz( dllPath, localDLL, sizeof( dllPath ) );
		}
	}
	if ( dllPath[ 0 ] == '\0' ) {
		FatalError( "couldn't find game dynamic library" );
		return;
	}

	DPrintf( "Loading game DLL: '%s'\n", dllPath );
	gameDLL = sys->DLL_Load( dllPath, true );
	if ( gameDLL == NULL ) {
		FatalError( "couldn't load game dynamic library '%s'", dllPath );
		return;
	}

	GetGameAPI_t getGameAPI = reinterpret_cast< GetGameAPI_t >( sys->DLL_GetProcAddress( gameDLL, "GetGameAPI" ) );
	if ( getGameAPI == NULL ) {
		UnloadGameDLL();
		FatalError( "couldn't find game DLL API" );
		return;
	}

	gameImport_t gameImport;
	memset( &gameImport, 0, sizeof( gameImport ) );
	gameImport.version = GAME_API_VERSION;
	gameImport.sys = sys;
	gameImport.common = common;
	gameImport.cmdSystem = cmdSystem;
	gameImport.cvarSystem = cvarSystem;
	gameImport.fileSystem = fileSystem;
	gameImport.networkSystem = networkSystem;
	gameImport.renderSystem = renderSystem;
	gameImport.deviceContext = deviceContext;
	gameImport.soundSystem = soundSystem;
	gameImport.renderModelManager = renderModelManager;
	gameImport.declManager = declManager;
	gameImport.collisionModelManager = collisionModelManager;
	gameImport.AASFileManager = AASFileManager;
	gameImport.bse = bse;
#ifndef _XENON
	gameImport.networkService = networkService;
#endif
	gameImport.adManager = adManager;
	gameImport.keyInputManager = keyInputManager;
	gameImport.notificationSystem = notificationSystem;
	idDict::GetGlobalPools( gameImport.globalKeys, gameImport.globalValues );
	gameImport.stringAllocator = idStr::GetStringAllocator();
	gameImport.wideStringAllocator = idWStr::GetStringAllocator();
	gameImport.graphManager = graphManager;

	gameExport_t* gameExport = getGameAPI( &gameImport );
	if ( gameExport == NULL || gameExport->version != GAME_API_VERSION ) {
		const int foundVersion = gameExport != NULL ? gameExport->version : -1;
		UnloadGameDLL();
		FatalError(
			"Wrong Game DLL API version (expected '%i' but found '%i').",
			GAME_API_VERSION,
			foundVersion
		);
		return;
	}

	game = gameExport->game;
	gameEdit = gameExport->gameEdit;
	if ( game == NULL ) {
		UnloadGameDLL();
		FatalError( "game DLL returned a NULL game interface" );
		return;
	}

	game->Init();
	idKeyInput::SetupBinds();
}

void idCommonLocal::UnloadGameDLL() {
	game = NULL;
	gameEdit = NULL;
	if ( gameDLL != NULL ) {
		sys->DLL_Unload( gameDLL );
		gameDLL = NULL;
	}
}

void idCommonLocal::InitGame( bool resetConfigs ) {
	( void )resetConfigs;

	fileSystem->Init( false );
	declManager->Init();
	renderBindingManager->Init();

	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec default.cfg\n" );
	if ( !SafeMode() ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec " BINDING_FILE "\n" );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec " CONFIG_FILE "\n" );
	}
	cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "exec autoexec.cfg\n" );
	cmdSystem->ExecuteCommandBuffer();
	StartupVariable( NULL );
	cvarSystem->ClearModifiedFlags( CVAR_ARCHIVE );

	if ( networkService != NULL ) {
		networkService->Init();
	}
	if ( !com_skipRenderer.GetBool() && ( networkSystem == NULL || !networkSystem->IsDedicated() ) ) {
		glimpParms_t parms;
		memset( &parms, 0, sizeof( parms ) );
		const int modeIndex = cvarSystem->GetCVarInteger( "r_mode" );
		if ( modeIndex >= 0 && modeIndex < GetNumVideoModes() ) {
			const vidmode_t& mode = GetVideoMode( modeIndex );
			parms.width = mode.width;
			parms.height = mode.height;
		} else {
			parms.width = Max( 320, cvarSystem->GetCVarInteger( "r_customWidth" ) );
			parms.height = Max( 240, cvarSystem->GetCVarInteger( "r_customHeight" ) );
		}
		parms.fullScreen = cvarSystem->GetCVarBool( "r_fullscreen" );
		parms.stereo = false;
		parms.displayHz = cvarSystem->GetCVarInteger( "r_displayRefresh" );
		parms.multiSamples = multiSampleParms( Max( 0, cvarSystem->GetCVarInteger( "r_multiSamples" ) ) );
		parms.pixelAspect = 1.0f;
		parms.fullscreenAvail = true;
		if ( !parms.fullScreen ) {
			sys3D->ConstrainToPrimaryMonitor( parms.width, parms.height );
		}
		sys3D->InitContext( parms );
		if ( sys3D->GetGameRenderContext() == NULL || sys3D->GetGameWindowHandle() == NULL ) {
			common->FatalError(
				"Failed to create the ETQW game window/OpenGL context (%dx%d, fullscreen %d)",
				parms.width,
				parms.height,
				parms.fullScreen ? 1 : 0
			);
		}
		Sys_PumpEvents();
	}

	renderSystem->Init();
	InitLanguageDict( false );
	collisionModelManager->Init();
	console->LoadGraphics();
	eventLoop->Init();
	usercmdGen->Init();
	soundSystem->Init();
	if ( networkSystem == NULL || !networkSystem->IsDedicated() ) {
		soundSystem->InitHW();
	}
	Sys_InitInput();
	idAsyncNetwork::Init();
	session->Init();
	if ( graphManager != NULL ) {
		graphManager->Init();
	}
	if ( adManager != NULL ) {
		adManager->Init();
	}
	if ( bse != NULL ) {
		bse->Init();
	}

	LoadGameDLL();

	if ( sys3D != NULL && sys3D->GetGameRenderContext() != NULL ) {
		Sys_ShowWindow( true );
	}
	sys->PostGameInit();
	nextConfigWriteTime = Sys_Milliseconds() + 2000;
}

void idCommonLocal::Init( int argc, const char** argv, const char* cmdline ) {
	if ( com_fullyInitialized ) {
		return;
	}
	idLib::sys = sys;
	idLib::common = common;
	idLib::cvarSystem = cvarSystem;
	idLib::fileSystem = fileSystem;
	idLib::Init();
	ClearWarnings( GAME_NAME " initialization" );

	idCmdArgs commandLine;
	if ( cmdline != NULL && cmdline[ 0 ] != '\0' ) {
		commandLine.TokenizeString( cmdline, true );
		argv = commandLine.GetArgs( &argc );
	}
	ParseCommandLine( argc, argv );

	cmdSystem->Init();
	cvarSystem->Init();
	idCVar::RegisterStaticVars();
	// The main-menu GUI installs an onCVarChanged callback for this engine
	// variable during game DLL initialization.  Keep the retail ownership in
	// Common and make the cross-DLL registration explicit.
	cvarSystem->Register( &com_machineSpec );
	idKeyInput::Init();
	console->Init();
	Sys_Init();
	Sys_InitNetworking();
	StartupVariable( NULL );
	Printf( "%s\n", versionString.string );
	InitCommands();
	InitSIMD();
	InitGame( false );
	AddStartupCommands();
	cmdSystem->ExecuteCommandBuffer();
	com_fullyInitialized = true;
	Printf( "------------- ETQW initialized -------------\n" );
}

void idCommonLocal::Shutdown() {
	if ( com_shuttingDown ) {
		return;
	}
	com_shuttingDown = true;
	WriteConfiguration();
	com_fullyInitialized = false;

	if ( session != NULL ) {
		session->Shutdown();
	}
	if ( game != NULL ) {
		game->Shutdown();
	}
	UnloadGameDLL();
	if ( bse != NULL ) {
		bse->Shutdown();
	}
	if ( adManager != NULL ) {
		adManager->Shutdown();
	}
	if ( graphManager != NULL ) {
		graphManager->Shutdown();
	}
	if ( networkService != NULL ) {
		networkService->Shutdown();
	}
	idAsyncNetwork::Shutdown();
	Sys_ShutdownInput();
	if ( soundSystem != NULL ) {
		soundSystem->ShutdownHW();
		soundSystem->Shutdown();
	}
	if ( usercmdGen != NULL ) {
		usercmdGen->Shutdown();
	}
	if ( eventLoop != NULL ) {
		eventLoop->Shutdown();
	}
	if ( collisionModelManager != NULL ) {
		collisionModelManager->Shutdown();
	}
	if ( renderSystem != NULL ) {
		renderSystem->Shutdown();
	}
	if ( sys3D != NULL ) {
		sys3D->Shutdown();
	}
	if ( declManager != NULL ) {
		renderBindingManager->Shutdown();
		declManager->Shutdown();
	}
	if ( fileSystem != NULL && fileSystem->IsInitialized() ) {
		DumpWarnings();
		CloseLogFile();
		fileSystem->Shutdown( false );
	}
	Sys_ShutdownNetworking();
	Sys_Shutdown();
	if ( console != NULL ) {
		console->Shutdown();
	}
	idKeyInput::Shutdown();
	if ( cvarSystem != NULL && cvarSystem->IsInitialized() ) {
		cvarSystem->Shutdown();
	}
	if ( cmdSystem != NULL ) {
		cmdSystem->Shutdown();
	}
	languageDict.Clear();
	idLib::ShutDown();
}

void idCommonLocal::Quit() {
	if ( com_errorEntered == ERP_NONE ) {
		Shutdown();
	}
	Sys_Quit();
}

bool idCommonLocal::IsInitialized() const {
	return com_fullyInitialized;
}
