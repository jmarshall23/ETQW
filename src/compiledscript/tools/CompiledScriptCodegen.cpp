// Copyright (C) 2007 Id Software, Inc.
//
// Build-time driver for the ETQW DoomScript-to-C++ exporter.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

static bool IsFile( const std::wstring& path ) {
	const DWORD attributes = GetFileAttributesW( path.c_str() );
	return attributes != INVALID_FILE_ATTRIBUTES && ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0;
}

static std::wstring JoinPath( const std::wstring& lhs, const std::wstring& rhs ) {
	if ( lhs.empty() ) {
		return rhs;
	}
	const wchar_t last = lhs[ lhs.size() - 1 ];
	return lhs + ( last == L'\\' || last == L'/' ? L"" : L"\\" ) + rhs;
}

static std::wstring Quote( const std::wstring& value ) {
	return L"\"" + value + L"\"";
}

static bool EnsureDirectory( const std::wstring& path ) {
	if ( CreateDirectoryW( path.c_str(), NULL ) != FALSE ) {
		return true;
	}
	return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool CleanGeneratedFiles( const std::wstring& directory ) {
	WIN32_FIND_DATAW findData;
	const std::wstring pattern = JoinPath( directory, L"Generated_*" );
	HANDLE find = FindFirstFileW( pattern.c_str(), &findData );
	if ( find == INVALID_HANDLE_VALUE ) {
		return GetLastError() == ERROR_FILE_NOT_FOUND;
	}

	bool success = true;
	do {
		if ( ( findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ) {
			continue;
		}
		const std::wstring fileName = JoinPath( directory, findData.cFileName );
		if ( DeleteFileW( fileName.c_str() ) == FALSE ) {
			std::wcerr << L"Unable to remove stale generated file: " << fileName << L"\n";
			success = false;
		}
	} while ( FindNextFileW( find, &findData ) != FALSE );
	FindClose( find );
	return success;
}

static int Usage( void ) {
	std::wcerr << L"Usage: compiledscript_codegen --engine <etqw.exe> --workspace <root> --temp <directory>\n";
	return 2;
}

int wmain( int argc, wchar_t** argv ) {
	std::wstring engine;
	std::wstring workspace;
	std::wstring tempDirectory;
	for ( int i = 1; i < argc; i++ ) {
		if ( i + 1 >= argc ) {
			return Usage();
		}
		const std::wstring option = argv[ i++ ];
		if ( option == L"--engine" ) {
			engine = argv[ i ];
		} else if ( option == L"--workspace" ) {
			workspace = argv[ i ];
		} else if ( option == L"--temp" ) {
			tempDirectory = argv[ i ];
		} else {
			return Usage();
		}
	}

	const std::wstring projectDirectory = JoinPath( JoinPath( workspace, L"src" ), L"compiledscript" );
	const std::wstring generatedDirectory = JoinPath( projectDirectory, L"generated" );
	const std::wstring mainScript = JoinPath( JoinPath( projectDirectory, L"scripts" ), L"main.script" );
	const std::wstring completionMarker = JoinPath( generatedDirectory, L"Generated_Complete.stamp" );
	if ( !IsFile( engine ) || !IsFile( mainScript ) ) {
		std::wcerr << L"Missing exporter input. Engine: " << engine << L", script: " << mainScript << L"\n";
		return 3;
	}
	if ( !EnsureDirectory( tempDirectory ) || !EnsureDirectory( generatedDirectory ) || !CleanGeneratedFiles( generatedDirectory ) ) {
		return 4;
	}

	std::wstring command = Quote( engine );
	command += L" +set fs_basepath " + Quote( workspace );
	command += L" +set fs_devpath " + Quote( workspace );
	command += L" +set fs_savepath " + Quote( tempDirectory );
	command += L" +set fs_game src/compiledscript";
	command += L" +set com_skipRenderer 1";
	command += L" +set net_serverDedicated 1";
	command += L" +set win_allowMultipleInstances 1";
	command += L" +exportScript scripts/main.script";
	command += L" +quit";

	std::vector< wchar_t > mutableCommand( command.begin(), command.end() );
	mutableCommand.push_back( L'\0' );
	STARTUPINFOW startupInfo;
	PROCESS_INFORMATION processInfo;
	ZeroMemory( &startupInfo, sizeof( startupInfo ) );
	ZeroMemory( &processInfo, sizeof( processInfo ) );
	startupInfo.cb = sizeof( startupInfo );

	std::wcout << L"Generating compiled scripts from " << mainScript << L"\n";
	if ( CreateProcessW( engine.c_str(), &mutableCommand[ 0 ], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, workspace.c_str(), &startupInfo, &processInfo ) == FALSE ) {
		std::wcerr << L"Unable to launch ETQW exporter (" << GetLastError() << L")\n";
		return 5;
	}

	for ( ;; ) {
		const DWORD waitResult = WaitForSingleObject( processInfo.hProcess, 20 );
		if ( IsFile( completionMarker ) ) {
			if ( waitResult != WAIT_OBJECT_0 ) {
				// This is a dedicated compiler host. All generated files are closed
				// before the marker is written, so no global game shutdown is needed.
				TerminateProcess( processInfo.hProcess, 0 );
				WaitForSingleObject( processInfo.hProcess, INFINITE );
			}
			break;
		}
		if ( waitResult == WAIT_OBJECT_0 ) {
			break;
		}
		if ( waitResult == WAIT_FAILED ) {
			std::wcerr << L"Unable to wait for the ETQW exporter (" << GetLastError() << L")\n";
			TerminateProcess( processInfo.hProcess, 7 );
			WaitForSingleObject( processInfo.hProcess, INFINITE );
			break;
		}
	}
	DWORD exitCode = 1;
	GetExitCodeProcess( processInfo.hProcess, &exitCode );
	CloseHandle( processInfo.hThread );
	CloseHandle( processInfo.hProcess );
	const std::wstring generatedFunctions = JoinPath( generatedDirectory, L"Generated_GlobalFunctions.cpp" );
	const std::wstring generatedEvents = JoinPath( generatedDirectory, L"Generated_Events.h" );
	if ( !IsFile( completionMarker ) || !IsFile( generatedFunctions ) || !IsFile( generatedEvents ) ) {
		if ( exitCode != 0 ) {
			std::wcerr << L"ETQW script exporter failed with exit code " << exitCode << L"\n";
		}
		std::wcerr << L"Exporter completed without producing the expected generated C++ files.\n";
		return 6;
	}
	if ( exitCode != 0 ) {
		// The legacy headless engine can fault during its unrelated global shutdown.
		// The end-of-export marker proves the compiler completed before that point.
		std::wcerr << L"Warning: ETQW exited with " << exitCode
			<< L" after the script export completed; accepting the verified generated output.\n";
	}

	std::wcout << L"Generated C++ is ready in " << generatedDirectory << L"\n";
	return 0;
}
