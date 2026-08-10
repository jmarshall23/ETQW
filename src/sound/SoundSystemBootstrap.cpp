// Copyright (C) 2007 Id Software, Inc.
//
// Compile-first ETQW sound boundary.  It preserves the SDK object lifetime
// and query semantics while the private mixer and hardware backends are
// reconstructed from the retail symbols.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "../idlib/threading/Lock.h"
#include "SoundSystem.h"
#include "SoundWorld.h"
#include "SoundShader.h"

namespace {

idCVar s_volume(
	"s_volume_dB", "0",
	CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT,
	"volume in dB" );
idCVar s_volumeVoIPIn(
	"s_volume_VoIPIn_dB", "0",
	CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT,
	"inbound volume adjust for voip in dB" );
idCVar s_volumeVoIPOut(
	"s_volume_VoIPOut_dB", "0",
	CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT,
	"outbound volume adjust for voip in dB" );
idCVar s_volumeVoIPScale(
	"s_volume_VoIPScale", "0.5",
	CVAR_SOUND | CVAR_ARCHIVE | CVAR_FLOAT,
	"percentage of regular volume to scale game audio to when voip is active" );
idCVar s_numberOfSpeakers(
	"s_numberOfSpeakers", "2",
	CVAR_SOUND | CVAR_ARCHIVE,
	"number of speakers" );
idCVar s_useAdpcmCompression(
	"s_useAdpcmCompression", "1",
	CVAR_SOUND | CVAR_ARCHIVE | CVAR_BOOL,
	"Use adpcm compression on single channel uncompressed samples" );
idCVar s_primaryDevice(
	"s_primaryDevice", "",
	CVAR_SOUND | CVAR_ARCHIVE,
	"sound device for game audio" );
idCVar s_voiceDevice(
	"s_voiceDevice", "",
	CVAR_SOUND | CVAR_ARCHIVE,
	"sound device for voice audio" );
idCVar s_micDevice(
	"s_micDevice", "",
	CVAR_SOUND | CVAR_ARCHIVE,
	"sound device for voice audio input" );

cinData_t EmptyCinematicData() {
	cinData_t data;
	memset( &data, 0, sizeof( data ) );
	return data;
}

class idSoundEmitterBootstrap : public idSoundEmitter {
public:
	explicit idSoundEmitterBootstrap( int value ) :
		index( value ),
		playing( false ),
		origin( vec3_origin ),
		listenerId( 0 ) {
		memset( &parms, 0, sizeof( parms ) );
	}

	virtual int Index() const { return index; }
	virtual void Free( bool ) { playing = false; }
	virtual void UpdateEmitter( const idVec3& value, int newListenerId, const soundShaderParms_t* newParms ) {
		origin = value;
		listenerId = newListenerId;
		if ( newParms != NULL ) {
			parms = *newParms;
		}
	}
	virtual int StartSound( const idSoundShader* shader, const soundChannel_t, soundChannel_t, float, int ) {
		playing = shader != NULL;
		return 0;
	}
	virtual const soundShaderParms_t& GetChannelParms( const soundChannel_t ) { return parms; }
	virtual void ModifySound( const soundChannel_t, const soundShaderParms_t& value ) { parms = value; }
	virtual void StopSound( const soundChannel_t ) { playing = false; }
	virtual void FadeSound( const soundChannel_t, float, float ) {}
	virtual bool CurrentlyPlaying() const { return playing; }
	virtual float CurrentAmplitude() { return 0.0f; }
	virtual cinData_t ImageForTime( const int ) { return EmptyCinematicData(); }
	virtual void SetChannelOffset( const soundChannel_t, int ) {}

private:
	int index;
	bool playing;
	idVec3 origin;
	int listenerId;
	soundShaderParms_t parms;
};

class idSoundWorldBootstrap : public idSoundWorld {
public:
	idSoundWorldBootstrap() :
		listenerPosition( vec3_origin ),
		listenerAxis( mat3_identity ),
		listenerId( 0 ),
		gameTime( 0 ),
		paused( false ),
		muted( false ),
		writeDemo( NULL ) {
	}

	virtual ~idSoundWorldBootstrap() {
		ClearAllSoundEmitters();
	}

	virtual void ClearAllSoundEmitters() {
		for ( int i = 0; i < emitters.Num(); i++ ) {
			delete emitters[ i ];
		}
		emitters.Clear();
	}
	virtual void StopAllSounds() {
		for ( int i = 0; i < emitters.Num(); i++ ) {
			emitters[ i ]->StopSound( SCHANNEL_ANY );
		}
	}
	virtual idSoundEmitter* AllocSoundEmitter() {
		idSoundEmitter* emitter = new idSoundEmitterBootstrap( emitters.Num() + 1 );
		emitters.Append( emitter );
		return emitter;
	}
	virtual idSoundEmitter* EmitterForIndex( int index ) {
		return index > 0 && index <= emitters.Num() ? emitters[ index - 1 ] : NULL;
	}
	virtual void MixLoop( int ) {}
	virtual float CurrentShakeAmplitudeForPosition( const int, const idVec3& ) { return 0.0f; }
	virtual void PlaceListener( const idVec3& origin, const idMat3& axis, const int newListenerId, const int newGameTime ) {
		listenerPosition = origin;
		listenerAxis = axis;
		listenerId = newListenerId;
		gameTime = newGameTime;
	}
	virtual const idVec3& GetListenerPosition() const { return listenerPosition; }
	virtual const idMat3& GetListenerAxis() const { return listenerAxis; }
	virtual void FadeSoundClasses( const int, const float, const float ) {}
	virtual void PlayShaderDirectly( const idSoundShader*, const soundChannel_t, int* length ) {
		if ( length != NULL ) {
			*length = 0;
		}
	}
	virtual void StartWritingDemo( idDemoFile* demo ) { writeDemo = demo; }
	virtual void StopWritingDemo() { writeDemo = NULL; }
	virtual void ProcessDemoCommand( idDemoFile* ) {}
	virtual void Pause() { paused = true; }
	virtual void UnPause() { paused = false; }
	virtual bool IsPaused() { return paused; }
	virtual bool IsMuted() const { return muted; }
	virtual void AVIOpen( const char*, const char* ) {}
	virtual void AVIClose() {}
	virtual void WriteToSaveGame( idFile* ) {}
	virtual void ReadFromSaveGame( idFile* ) {}
	virtual void BeginLevelLoad() { StopAllSounds(); }

private:
	idList< idSoundEmitter* > emitters;
	idVec3 listenerPosition;
	idMat3 listenerAxis;
	int listenerId;
	int gameTime;
	bool paused;
	bool muted;
	idDemoFile* writeDemo;
};

class idSoundSystemBootstrap : public idSoundSystem {
public:
	idSoundSystemBootstrap() :
		initialized( false ),
		hardwareInitialized( false ),
		muted( false ),
		capturing( false ),
		playingWorld( NULL ) {
	}

	virtual ~idSoundSystemBootstrap() {
		Shutdown();
	}

	virtual void Init() {
		cvarSystem->Register( &s_volume );
		cvarSystem->Register( &s_volumeVoIPIn );
		cvarSystem->Register( &s_volumeVoIPOut );
		cvarSystem->Register( &s_volumeVoIPScale );
		cvarSystem->Register( &s_numberOfSpeakers );
		cvarSystem->Register( &s_useAdpcmCompression );
		cvarSystem->Register( &s_primaryDevice );
		cvarSystem->Register( &s_voiceDevice );
		cvarSystem->Register( &s_micDevice );
		initialized = true;
	}
	virtual void Shutdown() {
		playingWorld = NULL;
		for ( int i = 0; i < worlds.Num(); i++ ) {
			delete worlds[ i ];
		}
		worlds.Clear();
		hardwareInitialized = false;
		initialized = false;
	}
	virtual void ClearBuffer() {}
	virtual bool InitHW() {
		hardwareInitialized = true;
		return true;
	}
	virtual bool ShutdownHW() {
		hardwareInitialized = false;
		return true;
	}
	virtual sdLock& GetLock() { return lock; }
	virtual int AsyncUpdate( int time ) { return time; }
	virtual void SetMute( bool value ) { muted = value; }
	virtual bool IsMuted() const { return muted; }
	virtual cinData_t ImageForTime( const int, const bool ) { return EmptyCinematicData(); }
	virtual int GetSoundDecoderInfo( int, soundDecoderInfo_t& ) { return -1; }
	virtual idSoundWorld* AllocSoundWorld( idRenderWorld* ) {
		idSoundWorld* world = new idSoundWorldBootstrap;
		worlds.Append( world );
		return world;
	}
	virtual void FreeSoundWorld( idSoundWorld* world ) {
		if ( playingWorld == world ) {
			playingWorld = NULL;
		}
		for ( int i = 0; i < worlds.Num(); i++ ) {
			if ( worlds[ i ] == world ) {
				delete worlds[ i ];
				worlds.RemoveIndex( i );
				return;
			}
		}
	}
	virtual void SetPlayingSoundWorld( idSoundWorld* world ) { playingWorld = world; }
	virtual idSoundWorld* GetPlayingSoundWorld() { return playingWorld; }
	virtual void BeginLevelLoad() {
		for ( int i = 0; i < worlds.Num(); i++ ) {
			worlds[ i ]->BeginLevelLoad();
		}
	}
	virtual void EndLevelLoad() {}
	virtual void StartCapture() { capturing = true; }
	virtual void StopCapture() { capturing = false; }
	virtual int GetCaptureRate() const { return capturing ? 44100 : 0; }
	virtual bool QuerySpeakers( int numSpeakers ) const { return numSpeakers == 2; }
	virtual void RefreshSoundDevices() {}
	virtual const idWStrList* ListSoundPlaybackDevices() const { return new idWStrList; }
	virtual const idWStrList* ListSoundCaptureDevices() const { return new idWStrList; }
	virtual void FreeDeviceList( const idWStrList* list ) const { delete list; }
	virtual int GetAudioDeviceHash( const wchar_t* name ) const {
		unsigned int hash = 2166136261u;
		if ( name != NULL ) {
			for ( ; *name != L'\0'; name++ ) {
				hash = ( hash ^ static_cast< unsigned int >( towlower( *name ) ) ) * 16777619u;
			}
		}
		return static_cast< int >( hash );
	}
	virtual int GetAudioDeviceHash( const char* name ) const {
		unsigned int hash = 2166136261u;
		if ( name != NULL ) {
			for ( ; *name != '\0'; name++ ) {
				hash = ( hash ^ static_cast< unsigned int >( idStr::ToLower( *name ) ) ) * 16777619u;
			}
		}
		return static_cast< int >( hash );
	}

private:
	sdLock lock;
	idList< idSoundWorld* > worlds;
	bool initialized;
	bool hardwareInitialized;
	bool muted;
	bool capturing;
	idSoundWorld* playingWorld;
};

idSoundSystemBootstrap soundSystemBootstrap;

}

idSoundSystem* soundSystem = &soundSystemBootstrap;
