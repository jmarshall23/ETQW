// Copyright (C) 2007 Id Software, Inc.
//
// ETQW session reconstruction.  Public interfaces are from the released SDK;
// the private layout and control flow are backed by the Microsoft PDB and the
// matching executable's disassembly.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "precompiled.h"
#include "Session_local.h"
#include "../renderer/DeviceContext.h"
#include "../renderer/SurfaceTypeMap.h"

extern int time_gameFrame;
extern volatile int com_ticNumber;
extern void Com_UpdateGameTime();
extern int Com_GetGameTimeLeft();

idCVar idSessionLocal::com_showAngles( "com_showAngles", "0", CVAR_SYSTEM | CVAR_BOOL, "show the user command angle graph" );
idCVar idSessionLocal::com_showTics( "com_showTics", "0", CVAR_SYSTEM | CVAR_BOOL, "show the number of game tics run per frame" );
idCVar idSessionLocal::com_minTics( "com_minTics", "1", CVAR_SYSTEM | CVAR_INTEGER,
	"legacy timing diagnostic; the main-thread clock never fabricates tics", 1, 10 );
idCVar idSessionLocal::com_showDemo( "com_showDemo", "0", CVAR_SYSTEM | CVAR_BOOL, "show render-demo information" );
idCVar idSessionLocal::com_skipGameDraw( "com_skipGameDraw", "0", CVAR_SYSTEM | CVAR_BOOL, "skip the game draw callback" );
idCVar idSessionLocal::com_aviDemoWidth( "com_aviDemoWidth", "256", CVAR_SYSTEM | CVAR_INTEGER, "AVI capture width" );
idCVar idSessionLocal::com_aviDemoHeight( "com_aviDemoHeight", "256", CVAR_SYSTEM | CVAR_INTEGER, "AVI capture height" );
idCVar idSessionLocal::com_aviDemoSamples( "com_aviDemoSamples", "16", CVAR_SYSTEM | CVAR_INTEGER, "AVI capture antialiasing samples" );
idCVar idSessionLocal::com_aviDemoTics( "com_aviDemoTics", "2", CVAR_SYSTEM | CVAR_INTEGER, "game tics per AVI frame", 1, 60 );
idCVar idSessionLocal::com_wipeSeconds( "com_wipeSeconds", "1", CVAR_SYSTEM | CVAR_FLOAT, "duration of screen wipes" );

idSessionLocal sessLocal;
idSession* session = &sessLocal;

static void Session_Map_f( const idCmdArgs& args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: map <map name>\n" );
		return;
	}

	cvarSystem->SetCVarBool( "developer", false );
	sessLocal.StartNewGame( args.Argv( 1 ) );
}

static void Session_DevMap_f( const idCmdArgs& args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "usage: devmap <map name>\n" );
		return;
	}

	cvarSystem->SetCVarBool( "developer", true );
	sessLocal.StartNewGame( args.Argv( 1 ) );
}

static void Session_Disconnect_f( const idCmdArgs& args ) {
	sessLocal.Stop();
	if ( game != NULL && !game->IsMainMenuActive() ) {
		console->Close();
	}
	sessLocal.StartMenu();
}

static void Session_DemoShot_f( const idCmdArgs& args ) {
	sessLocal.DemoShot( args.Argc() > 1 ? args.Argv( 1 ) : "shot" );
}

idSessionLocal::idSessionLocal() :
	timeHitch( 0 ),
	menuSoundWorld( NULL ),
	insideExecuteMapChange( false ),
	insidePureWait( false ),
	waitingForSnapshot( false ),
	ignorePacifier( false ),
	bytesNeededForMapLoad( 0 ),
	lastPacifierTime( 0 ),
	mapSpawned( false ),
	insideUpdateScreen( false ),
	latchedTicNumber( 0 ),
	lastGameTic( 0 ),
	lastDemoTic( -1 ),
	aviCaptureMode( false ),
	aviDemoFrameCount( 0.0f ),
	aviTicStart( 0 ),
	timeDemo( TD_NO ),
	timeDemoStartTime( 0 ),
	numDemoFrames( 0 ),
	demoTimeOffset( 0 ),
	whiteMaterial( NULL ),
	wipeMaterial( NULL ),
	wipeStartTic( 1 ),
	wipeStopTic( 0 ),
	wipeHold( false ),
	emptyDrawCount( 0 ) {
	rw = NULL;
	sw = NULL;
	readDemo = NULL;
	writeDemo = NULL;
	memset( &currentDemoRenderView, 0, sizeof( currentDemoRenderView ) );
	modsList.SetGranularity( 1 );
}

idSessionLocal::~idSessionLocal() {
}

void idSessionLocal::Clear() {
	insideUpdateScreen = false;
	insideExecuteMapChange = false;
	insidePureWait = false;
	waitingForSnapshot = false;
	ignorePacifier = false;
	currentMapName.Clear();
	aviDemoShortName.Clear();
	timeHitch = 0;
	mapSpawned = false;
	aviCaptureMode = false;
	aviDemoFrameCount = 0.0f;
	aviTicStart = 0;
	timeDemo = TD_NO;
	timeDemoStartTime = 0;
	numDemoFrames = 0;
	demoTimeOffset = 0;
	lastPacifierTime = 0;
	latchedTicNumber = 0;
	lastGameTic = 0;
	lastDemoTic = -1;
	emptyDrawCount = 0;
	wipeMaterial = NULL;
	wipeHold = false;
	wipeStopTic = 0;
	wipeStartTic = 1;
	modsList.Clear();
}

void idSessionLocal::Init() {
	common->Printf( "-------- Initializing Session --------\n" );

	cmdSystem->AddCommand( "map", Session_Map_f, CMD_FL_SYSTEM, "loads a map", idCmdSystem::ArgCompletion_StartGame );
	cmdSystem->AddCommand( "devmap", Session_DevMap_f, CMD_FL_SYSTEM, "loads a map in developer mode", idCmdSystem::ArgCompletion_StartGame );
	cmdSystem->AddCommand( "disconnect", Session_Disconnect_f, CMD_FL_SYSTEM, "disconnects from a game" );
	cmdSystem->AddCommand( "demoShot", Session_DemoShot_f, CMD_FL_SYSTEM, "writes a screenshot for a demo" );

	rw = renderSystem->AllocRenderWorld();
	sw = soundSystem->AllocSoundWorld( rw );
	menuSoundWorld = soundSystem->AllocSoundWorld( NULL );
	whiteMaterial = declHolder.FindMaterial( "_white" );

	common->Printf( "session initialized\n" );
	common->Printf( "--------------------------------------\n" );
}

void idSessionLocal::Shutdown() {
	if ( aviCaptureMode ) {
		EndAVICapture();
	}
	Stop();
	surfaceTypeMapManager->Shutdown();

	if ( sw != NULL ) {
		soundSystem->FreeSoundWorld( sw );
		sw = NULL;
	}
	if ( menuSoundWorld != NULL ) {
		soundSystem->FreeSoundWorld( menuSoundWorld );
		menuSoundWorld = NULL;
	}
	if ( rw != NULL ) {
		renderSystem->FreeRenderWorld( rw );
		rw = NULL;
	}

	mapSpawnData.serverInfo.Clear();
	mapSpawnData.syncedCVars.Clear();
	mapSpawnData.clientSyncedCVars.Clear();
	for ( int i = 0; i < MAX_ASYNC_CLIENTS; ++i ) {
		mapSpawnData.userInfo[ i ].Clear();
	}
	Clear();
}

void idSessionLocal::Stop() {
	ClearWipe();
	StopPlayingRenderDemo();
	if ( writeDemo != NULL ) {
		StopRecordingRenderDemo();
	}

	renderSystem->LockThreads();
	UnloadMap();
	renderSystem->UnlockThreads();

	mapSpawnData.clientSyncedCVars.Clear();
	if ( sw != NULL ) {
		sw->StopAllSounds();
	}
	insideUpdateScreen = false;
	insideExecuteMapChange = false;
	insidePureWait = false;
	waitingForSnapshot = false;
}

bool idSessionLocal::ProcessEvent( const sdSysEvent* event ) {
	if ( event == NULL ) {
		return false;
	}
	if ( console->ProcessEvent( event, false ) ) {
		return true;
	}
	if ( game != NULL && game->HandleGuiEvent( event ) ) {
		return true;
	}
	if ( !mapSpawned ) {
		console->ProcessEvent( event, true );
		return true;
	}
	return usercmdGen->ProcessEvent( *event );
}

void idSessionLocal::UpdateScreen( bool outOfSequence ) {
	if ( !Sys_IsWindowVisible() || insideUpdateScreen ) {
		return;
	}

	insideUpdateScreen = true;
	if ( outOfSequence ) {
		GuiFrameEvents( true );
	}

	renderSystem->BeginFrame( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );
	Draw();
	renderSystem->EndFrame();
	insideUpdateScreen = false;
}

void idSessionLocal::PacifierUpdate() {
	if ( networkSystem != NULL && networkSystem->IsDedicated() ) {
		idAsyncNetwork::client.PacifierUpdate();
		return;
	}
	if ( !insideExecuteMapChange || ignorePacifier || insideUpdateScreen ) {
		return;
	}

	const int now = eventLoop->Milliseconds();
	if ( now - lastPacifierTime < 33 ) {
		return;
	}
	lastPacifierTime = now;
	Sys_GenerateEvents();
	UpdateScreen( true );
	idAsyncNetwork::client.PacifierUpdate();
	idAsyncNetwork::server.PacifierUpdate();
	if ( game != NULL ) {
		game->PacifierUpdate();
	}
}

void idSessionLocal::Draw() {
	bool fullScreenEmitStarted = false;

	if ( game != NULL ) {
		if ( insidePureWait ) {
			deviceContext->BeginEmitFullScreen();
			fullScreenEmitStarted = true;
			game->DrawPureWaitScreen();
		} else if ( insideExecuteMapChange || waitingForSnapshot ) {
			deviceContext->BeginEmitFullScreen();
			fullScreenEmitStarted = true;
			game->DrawLoadScreen();
		} else if ( game->IsMainMenuActive() ) {
			deviceContext->BeginEmitFullScreen();
			fullScreenEmitStarted = true;
			game->DrawMainMenu();
			game->DrawSystemUI();
			if ( menuSoundWorld != NULL ) {
				menuSoundWorld->PlaceListener( vec3_origin, mat3_identity, -1, 0 );
			}
		} else if ( mapSpawned ) {
			const bool drewGame = com_skipGameDraw.GetBool() ? false : game->Draw();
			deviceContext->BeginEmitFullScreen();
			fullScreenEmitStarted = true;
			if ( drewGame ) {
				game->Draw2D();
				game->DrawSystemUI();
			}
		} else {
			deviceContext->BeginEmitFullScreen();
			fullScreenEmitStarted = true;
			if ( ++emptyDrawCount > 5 ) {
				emptyDrawCount = 0;
				StartMenu();
			}
		}
	}

	// Retail brackets all of the 2D session drawing, including the developer
	// console, in a full-screen device-context emit.  Apart from selecting the
	// destination, BeginEmitFullScreen resets color and clipping state so that
	// state left by a GUI in the previous frame cannot make overlays invisible.
	if ( !fullScreenEmitStarted ) {
		deviceContext->BeginEmitFullScreen();
	}

	DrawWipeModel();
	DrawCmdGraph();
	console->Draw( false );
	deviceContext->End();
}

void idSessionLocal::Frame() {
	GuiFrameEvents( false );
	latchedTicNumber = com_ticNumber;

	if ( readDemo != NULL ) {
		AdvanceRenderDemo( false );
		return;
	}

	if ( mapSpawned && !idAsyncNetwork::IsActive() ) {
		int tics = latchedTicNumber - lastGameTic;
		if ( tics < 0 ) {
			// A clock reset must not turn into an enormous unsigned catch-up.
			lastGameTic = latchedTicNumber;
			tics = 0;
		} else if ( tics > 10 ) {
			// Bound recovery after a debugger stop or a long OS stall.
			lastGameTic = latchedTicNumber - 10;
			tics = 10;
		}
		if ( com_showTics.GetBool() ) {
			common->Printf( "%i ", tics );
		}
		const bool renderOnlyFrame = tics == 0;
		while ( tics-- > 0 && mapSpawned ) {
			RunGameTic();
		}

		// The old async server called ClientUpdateView between fixed game tics.
		// Without that call, PlayerView treats every foreground render as another
		// full-tic draw and repeatedly offsets the weapon by the same origin delta.
		// That is visible as rapid weapon drift/flicker whenever the player moves.
		if ( renderOnlyFrame && mapSpawned && game != NULL ) {
			const usercmd_t cmd = usercmdGen->GetDirectUsercmd();
			game->ClientUpdateView( cmd, Com_GetGameTimeLeft() );
		}
	}
}

bool idSessionLocal::IsMultiplayer() {
	return idAsyncNetwork::IsActive();
}

void idSessionLocal::SetPlayingSoundWorld() {
	if ( game != NULL && game->IsMainMenuActive() ) {
		soundSystem->SetPlayingSoundWorld( menuSoundWorld );
	} else {
		soundSystem->SetPlayingSoundWorld( sw );
	}
}

void idSessionLocal::TimeHitch( int msec ) {
	timeHitch += msec;
}

bool idSessionLocal::MapSpawned() {
	return mapSpawned;
}

idSoundWorld* idSessionLocal::GetGameSoundWorld() {
	return sw;
}

idSoundWorld* idSessionLocal::GetMenuSoundWorld() {
	return menuSoundWorld;
}

const char* idSessionLocal::GetCurrentMapName() {
	return currentMapName.c_str();
}

int idSessionLocal::GetLocalClientNum() {
	if ( idAsyncNetwork::client.IsActive() ) {
		return idAsyncNetwork::client.GetLocalClientNum();
	}
	if ( idAsyncNetwork::server.IsActive() ) {
		return idAsyncNetwork::server.GetLocalClientNum();
	}
	return 0;
}

void idSessionLocal::BackupPersistantPlayerInfo() {
}

void idSessionLocal::MoveToNewMap( const char* mapName ) {
	ExecuteMapChange( mapName, 0, true, true, true );
}

void idSessionLocal::StartNewGame( const char* mapName ) {
	if ( game == NULL || mapName == NULL || mapName[ 0 ] == '\0' ) {
		return;
	}

	// Selecting a user map also constructs the active sdGameRules instance.
	// The direct devmap path still needs this step even though it does not spawn
	// an idAsyncServer; map initialization and bot state both consume rules.
	idStr reason;
	idStr resolvedMapName;
	const userMapChangeResult_e result = game->OnUserStartMap( mapName, reason, resolvedMapName );
	if ( result == UMCR_ERROR ) {
		common->Printf( "User Map Start Denied '%s'\n", reason.c_str() );
		return;
	}
	if ( result == UMCR_STOP ) {
		return;
	}

	mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	mapSpawnData.userInfo[ 0 ] = *cvarSystem->MoveCVarsToDict( CVAR_USERINFO );
	mapSpawnData.syncedCVars = *cvarSystem->MoveCVarsToDict( CVAR_NETWORKSYNC );
	game->SetClientNum( 0, true );
	ExecuteMapChange( resolvedMapName.c_str(), 0, true, true, true );
}

void idSessionLocal::ExecuteMapChange( const char* mapName, int startTime, bool startFadeWipe, bool completeFadeWipe, bool isUserChange ) {
	if ( game == NULL || rw == NULL || sw == NULL || mapName == NULL || mapName[ 0 ] == '\0' ) {
		return;
	}

	idStr fullMapName = mapName;
	fullMapName.StripFileExtension();
	if ( idStr::Icmpn( fullMapName.c_str(), "maps/", 5 ) != 0 ) {
		fullMapName = "maps/" + fullMapName;
	}
	fullMapName.SetFileExtension( ".world" );

	sys->SetServerInfo( "si_map", mapName );
	if ( startFadeWipe ) {
		StartWipe( "wipe2Material", true );
	}
	console->Close();
	game->HideMainMenu();
	if ( sw->IsPaused() ) {
		sw->UnPause();
	}

	renderSystem->LockThreads();
	UnloadMap();
	const bool newMap = idStr::Icmp( currentMapName.c_str(), fullMapName.c_str() ) != 0;
	currentMapName = fullMapName;
	if ( newMap ) {
		declManager->BeginLevelLoad();
		renderSystem->BeginLevelLoad();
		soundSystem->BeginLevelLoad();
		surfaceTypeMapManager->BeginLevelLoad();
		game->BeginLevelLoad();
		sw->BeginLevelLoad();
	}

	insideExecuteMapChange = true;
	fileSystem->ResetReadCount();
	fileSystem->BeginLevelLoadStatistics();
	if ( networkSystem != NULL ) {
		networkSystem->BeginLevelLoad();
	}

	ignorePacifier = true;
	game->ShowLevelLoadScreen( fullMapName.c_str() );
	ignorePacifier = false;
	if ( completeFadeWipe ) {
		CompleteWipe();
	}
	ClearWipe();
	PacifierUpdate();

	common->Printf( "--------- Map Initialization ---------\n" );
	common->Printf( "Map: %s\n", fullMapName.c_str() );
	if ( !rw->InitFromMap( fullMapName.c_str() ) ) {
		insideExecuteMapChange = false;
		renderSystem->UnlockThreads();
		common->Error( "couldn't load %s", fullMapName.c_str() );
		return;
	}

	usercmdGen->InitForNewMap();
	memset( mapSpawnData.mapSpawnUsercmd, 0, sizeof( mapSpawnData.mapSpawnUsercmd ) );
	game->SetServerInfo( mapSpawnData.serverInfo );
	game->InitFromNewMap(
		fullMapName.c_str(),
		rw,
		sw,
		idAsyncNetwork::server.IsActive(),
		idAsyncNetwork::client.IsActive(),
		Sys_Milliseconds(),
		startTime,
		isUserChange
	);

	// A direct map/devmap is neither an async server nor an async client.  The
	// retail session creates its local player here; without this block the map
	// scripts run, but idGameLocal::Draw has no active viewer and never submits
	// the render world.
	if ( !idAsyncNetwork::server.IsActive() && !idAsyncNetwork::client.IsActive() ) {
		game->SpawnPlayer( 0, false );

		idStr uiName = mapSpawnData.userInfo[ 0 ].GetString( "ui_name", "Player" );
		mapSpawnData.userInfo[ 0 ].Set( "ui_realname", uiName.c_str() );
		game->UserInfoChanged( 0 );
	}

	if ( newMap ) {
		surfaceTypeMapManager->EndLevelLoad();
		renderSystem->EndLevelLoad();
		soundSystem->EndLevelLoad();
		declManager->EndLevelLoad();
		game->EndLevelLoad();
	}
	if ( networkSystem != NULL ) {
		networkSystem->EndLevelLoad();
	}
	renderSystem->LevelStart();
	fileSystem->EndLevelLoadStatistics();
	fileSystem->ReportLevelLoadStatistics();
	rw->GenerateAllInteractions();

	StartWipe( "wipe2Material", false );
	game->HideLevelLoadScreen();
	usercmdGen->Clear();
	latchedTicNumber = com_ticNumber;
	lastGameTic = latchedTicNumber;
	console->ClearNotifyLines();
	insideExecuteMapChange = false;
	soundSystem->SetPlayingSoundWorld( sw );
	if ( sw->IsPaused() ) {
		sw->UnPause();
	}
	mapSpawned = true;
	sys->ClearEvents();
	renderSystem->UnlockThreads();
}

void idSessionLocal::UnloadMap() {
	StopPlayingRenderDemo();
	if ( game != NULL && mapSpawned ) {
		game->MapShutdown();
	}
	if ( writeDemo != NULL ) {
		StopRecordingRenderDemo();
	}
	mapSpawned = false;
}

void idSessionLocal::RunGameTic() {
	// Input devices are polled by the input worker, but command construction and
	// all game callbacks stay on the main thread.
	usercmd_t cmd = usercmdGen->GetDirectUsercmd();
	mapSpawnData.mapSpawnUsercmd[ 0 ] = cmd;
	++lastGameTic;

	const int start = Sys_Milliseconds();
	game->RunFrame( mapSpawnData.mapSpawnUsercmd, USERCMD_MSEC );
	time_gameFrame += Sys_Milliseconds() - start;
}

void idSessionLocal::HandleFrameCommand( const idCmdArgs& cmd ) {
	const char* command = cmd.Argv( 0 );
	if ( idStr::Icmp( command, "map" ) == 0 || idStr::Icmp( command, "devmap" ) == 0 ) {
		ExecuteMapChange( cmd.Argv( 1 ), 0, true, true, true );
	} else if ( idStr::Icmp( command, "disconnect" ) == 0 ) {
		Stop();
		StartMenu();
	}
}

void idSessionLocal::SetServerInfo( const char* key, const char* value ) {
	if ( key == NULL || value == NULL ) {
		return;
	}
	mapSpawnData.serverInfo.Set( key, value );
	if ( game != NULL ) {
		game->SetServerInfo( mapSpawnData.serverInfo );
	}
	UpdateConsoleName();
}

void idSessionLocal::FlushServerInfo() {
	mapSpawnData.serverInfo = *cvarSystem->MoveCVarsToDict( CVAR_SERVERINFO );
	if ( game != NULL ) {
		game->SetServerInfo( mapSpawnData.serverInfo );
	}
	UpdateConsoleName();
}

void idSessionLocal::UpdateConsoleName() {
	const char* name = mapSpawnData.serverInfo.GetString( "si_name", GAME_NAME );
	Sys_SetConsoleName( name );
}

void idSessionLocal::SetWaitingForSnapshot( bool set ) {
	waitingForSnapshot = set;
}

void idSessionLocal::SetPureWait( bool set ) {
	insidePureWait = set;
}

bool idSessionLocal::CaptureState( const char* imageName, const int width, const int height ) {
	if ( imageName == NULL || !renderSystem->IsOpenGLRunning() ) {
		return false;
	}

	console->Close();
	renderSystem->BeginFrame( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );
	renderSystem->CropRenderSize( width, height, true );
	Draw();
	renderSystem->CaptureRenderToImage( imageName );
	renderSystem->UnCrop();
	renderSystem->EndFrame( false );
	return true;
}

void idSessionLocal::StartWipe( const char* materialName, bool hold ) {
	if ( materialName == NULL || !CaptureState( "_scratch", SCREEN_WIDTH, SCREEN_HEIGHT ) ) {
		return;
	}
	wipeMaterial = declHolder.FindMaterial( materialName, false );
	wipeStartTic = com_ticNumber;
	wipeStopTic = wipeStartTic + idMath::Ftoi( com_wipeSeconds.GetFloat() * ( 1000.0f / USERCMD_MSEC ) );
	wipeHold = hold;
}

void idSessionLocal::CompleteWipe() {
	while ( com_ticNumber != 0 && com_ticNumber < wipeStopTic ) {
		// CompleteWipe is a foreground blocking loop. Keep the foreground-owned
		// fixed clock moving while it presents transition frames.
		Com_UpdateGameTime();
		emptyDrawCount = 0;
		UpdateScreen( true );
	}
	if ( com_ticNumber == 0 ) {
		wipeStopTic = 0;
		UpdateScreen( true );
	}
}

void idSessionLocal::ClearWipe() {
	wipeMaterial = NULL;
	wipeStartTic = 1;
	wipeStopTic = 0;
	wipeHold = false;
}

void idSessionLocal::DrawWipeModel() {
	// The retail renderer draws this material through the game device context.
	// Keeping the timing state here preserves the session ABI until that private
	// device-context interface is reconstructed.
	if ( wipeStartTic < wipeStopTic && !wipeHold && com_ticNumber >= wipeStopTic ) {
		ClearWipe();
	}
}

void idSessionLocal::DrawCmdGraph() {
}

void idSessionLocal::PlayIntroGui() {
	StartMenu();
}

void idSessionLocal::LoadSession( const char* name ) {
	common->DPrintf( "LoadSession '%s' is not implemented yet\n", name != NULL ? name : "" );
}

void idSessionLocal::SaveSession( const char* name ) {
	common->DPrintf( "SaveSession '%s' is not implemented yet\n", name != NULL ? name : "" );
}

void idSessionLocal::StartRecordingRenderDemo( const char* name ) {
	common->DPrintf( "StartRecordingRenderDemo '%s' is not implemented yet\n", name != NULL ? name : "" );
}

void idSessionLocal::StopRecordingRenderDemo() {
	writeDemo = NULL;
}

void idSessionLocal::StartPlayingRenderDemo( idStr name ) {
	common->DPrintf( "StartPlayingRenderDemo '%s' is not implemented yet\n", name.c_str() );
}

void idSessionLocal::StopPlayingRenderDemo() {
	readDemo = NULL;
	timeDemo = TD_NO;
}

void idSessionLocal::CompressDemoFile( const char* scheme, const char* name ) {
	common->DPrintf( "CompressDemoFile '%s' '%s' is not implemented yet\n", scheme != NULL ? scheme : "", name != NULL ? name : "" );
}

void idSessionLocal::TimeRenderDemo( const char* name, bool twice ) {
	timeDemo = twice ? TD_YES_THEN_QUIT : TD_YES;
	StartPlayingRenderDemo( name != NULL ? name : "" );
}

void idSessionLocal::AVIRenderDemo( const char* name ) {
	BeginAVICapture( name );
	StartPlayingRenderDemo( name != NULL ? name : "" );
}

void idSessionLocal::AVIGame( const char* name ) {
	BeginAVICapture( name );
}

void idSessionLocal::BeginAVICapture( const char* name ) {
	if ( aviCaptureMode || sw == NULL ) {
		return;
	}
	aviDemoShortName = name != NULL && name[ 0 ] != '\0' ? name : "capture";
	aviDemoFrameCount = 0.0f;
	aviTicStart = 0;
	aviCaptureMode = true;
	sw->AVIOpen( "demos", aviDemoShortName.c_str() );
}

void idSessionLocal::EndAVICapture() {
	if ( !aviCaptureMode ) {
		return;
	}
	if ( sw != NULL ) {
		sw->AVIClose();
	}
	aviCaptureMode = false;
	common->Printf( "captured %i frames for %s.\n", static_cast< int >( aviDemoFrameCount ), aviDemoShortName.c_str() );
}

void idSessionLocal::AdvanceRenderDemo( bool singleFrameOnly ) {
	if ( readDemo == NULL ) {
		return;
	}
	if ( singleFrameOnly || aviCaptureMode || timeDemo != TD_NO ) {
		lastDemoTic = latchedTicNumber;
	}
}

void idSessionLocal::DemoShot( const char* name ) {
	idStr fileName = "demos/";
	fileName += name != NULL && name[ 0 ] != '\0' ? name : "shot";
	fileName.SetFileExtension( ".tga" );
	renderSystem->TakeScreenshot(
		renderSystem->GetScreenWidth(),
		renderSystem->GetScreenHeight(),
		fileName.c_str(),
		1,
		NULL
	);
}

void idSessionLocal::TestGUI( const char* name ) {
	common->DPrintf( "TestGUI '%s' is game-owned in ETQW\n", name != NULL ? name : "" );
}
