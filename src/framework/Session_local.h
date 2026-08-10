// Copyright (C) 2007 Id Software, Inc.
//
// Private ETQW session declarations reconstructed from the Microsoft PDB.

#ifndef __SESSION_LOCAL_H__
#define __SESSION_LOCAL_H__

struct mapSpawnData_t {
	mapSpawnData_t() : serverIsRanked( false ) {}

	bool			serverIsRanked;
	idDict			serverInfo;
	idDict			syncedCVars;
	idDict			clientSyncedCVars;
	idDict			userInfo[ MAX_ASYNC_CLIENTS ];
	usercmd_t		mapSpawnUsercmd[ MAX_ASYNC_CLIENTS ];
};

enum timeDemo_t {
	TD_NO,
	TD_YES,
	TD_YES_THEN_QUIT
};

class idSessionLocal : public idSession {
public:
	idSessionLocal();
	virtual ~idSessionLocal();

	virtual void			Init();
	virtual void			Shutdown();
	virtual void			Stop();
	virtual void			UpdateScreen( bool outOfSequence = true );
	virtual void			PacifierUpdate();
	virtual void			Frame();
	virtual bool			IsMultiplayer();
	virtual bool			ProcessEvent( const sdSysEvent* event );
	virtual void			StartMenu();
	virtual void			ExitMenu();
	virtual void			GuiFrameEvents( bool outOfSequence );
	virtual void			MessageBox( msgBoxType_t type, const wchar_t* message, const char* titleDef );
	virtual void			SetPlayingSoundWorld();
	virtual void			TimeHitch( int msec );
	virtual bool			MapSpawned();
#ifdef EB_WITH_PB
	virtual const char*		GetCurrentMapName();
#endif

	void					HandleFrameCommand( const idCmdArgs& cmd );
	idSoundWorld*			GetGameSoundWorld();
	idSoundWorld*			GetMenuSoundWorld();
	const char*				GetCurrentMapName();
	void					BackupPersistantPlayerInfo();
	int						GetLocalClientNum();
	void					MoveToNewMap( const char* mapName );
	void					StartNewGame( const char* mapName );
	void					PlayIntroGui();
	void					LoadSession( const char* name );
	void					SaveSession( const char* name );
	bool					CaptureState( const char* imageName, const int width, const int height );
	void					DrawWipeModel();
	void					StartWipe( const char* materialName, bool hold );
	void					CompleteWipe();
	void					ClearWipe();
	void					SetServerInfo( const char* key, const char* value );
	void					FlushServerInfo();
	void					UpdateConsoleName();
	void					SetWaitingForSnapshot( bool set );
	void					SetPureWait( bool set );

	void					Clear();
	void					DrawCmdGraph();
	void					Draw();
	void					StartRecordingRenderDemo( const char* name );
	void					StopRecordingRenderDemo();
	void					StartPlayingRenderDemo( idStr name );
	void					StopPlayingRenderDemo();
	void					CompressDemoFile( const char* scheme, const char* name );
	void					TimeRenderDemo( const char* name, bool twice );
	void					AVIRenderDemo( const char* name );
	void					AVIGame( const char* name );
	void					BeginAVICapture( const char* name );
	void					EndAVICapture();
	void					AdvanceRenderDemo( bool singleFrameOnly );
	void					RunGameTic();
	void					DemoShot( const char* name );
	void					TestGUI( const char* name );
	void					ExecuteMapChange( const char* mapName, int startTime, bool startFadeWipe, bool completeFadeWipe, bool isUserChange );
	void					UnloadMap();

	static idCVar			com_showAngles;
	static idCVar			com_showTics;
	static idCVar			com_minTics;
	static idCVar			com_showDemo;
	static idCVar			com_skipGameDraw;
	static idCVar			com_aviDemoWidth;
	static idCVar			com_aviDemoHeight;
	static idCVar			com_aviDemoSamples;
	static idCVar			com_aviDemoTics;
	static idCVar			com_wipeSeconds;

	int						timeHitch;
	idSoundWorld*			menuSoundWorld;
	bool					insideExecuteMapChange;
	bool					insidePureWait;
	bool					waitingForSnapshot;
	bool					ignorePacifier;
	int						bytesNeededForMapLoad;
	int						lastPacifierTime;
	mapSpawnData_t			mapSpawnData;
	idStr					currentMapName;
	bool					mapSpawned;
	bool					insideUpdateScreen;
	int						latchedTicNumber;
	int						lastGameTic;
	int						lastDemoTic;
	bool					aviCaptureMode;
	idStr					aviDemoShortName;
	float					aviDemoFrameCount;
	int						aviTicStart;
	timeDemo_t				timeDemo;
	int						timeDemoStartTime;
	int						numDemoFrames;
	int						demoTimeOffset;
	renderView_t			currentDemoRenderView;
	const idMaterial*		whiteMaterial;
	const idMaterial*		wipeMaterial;
	int						wipeStartTic;
	int						wipeStopTic;
	bool					wipeHold;
	int						emptyDrawCount;
	sdSignal				asyncSignal;
	idStrList				modsList;
};

#if defined( _WIN32 ) && !defined( _WIN64 )
assert_sizeof( mapSpawnData_t, 0x8fc );
assert_sizeof( idSessionLocal, 0xa80 );
#endif

extern idSessionLocal sessLocal;

#endif /* !__SESSION_LOCAL_H__ */
