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

struct binkHeaderBootstrap_t {
	unsigned int width;
	unsigned int height;
	unsigned int frames;
	unsigned int frameNum;
	unsigned int lastFrameNum;
	unsigned int frameRate;
	unsigned int frameRateDiv;
};

class binkApiBootstrap_t {
public:
	typedef void* ( __stdcall *open_t )( const char*, unsigned int );
	typedef void ( __stdcall *close_t )( void* );
	typedef int ( __stdcall *doFrame_t )( void* );
	typedef int ( __stdcall *copyToBuffer_t )( void*, void*, int, unsigned int, unsigned int, unsigned int, unsigned int );
	typedef void ( __stdcall *nextFrame_t )( void* );
	typedef const char* ( __stdcall *getError_t )();

	binkApiBootstrap_t() :
		module( NULL ),
		open( NULL ),
		close( NULL ),
		doFrame( NULL ),
		copyToBuffer( NULL ),
		nextFrame( NULL ),
		getError( NULL ),
		attemptedLoad( false ) {
	}

	bool Load() {
		if ( attemptedLoad ) {
			return module != NULL;
		}
		attemptedLoad = true;

		char executablePath[ MAX_OSPATH ];
		const DWORD pathLength = GetModuleFileNameA( NULL, executablePath, sizeof( executablePath ) );
		if ( pathLength > 0 && pathLength < sizeof( executablePath ) ) {
			for ( int i = static_cast< int >( pathLength ) - 1; i >= 0; i-- ) {
				if ( executablePath[ i ] == '\\' || executablePath[ i ] == '/' ) {
					executablePath[ i + 1 ] = '\0';
					break;
				}
			}
			idStr dllPath = executablePath;
			dllPath += "binkw32.dll";
			module = LoadLibraryA( dllPath.c_str() );
		}
		if ( module == NULL ) {
			module = LoadLibraryA( "binkw32.dll" );
		}
		if ( module == NULL ) {
			common->Warning( "Bink playback unavailable: could not load binkw32.dll" );
			return false;
		}

		open = reinterpret_cast< open_t >( GetProcAddress( module, "_BinkOpen@8" ) );
		close = reinterpret_cast< close_t >( GetProcAddress( module, "_BinkClose@4" ) );
		doFrame = reinterpret_cast< doFrame_t >( GetProcAddress( module, "_BinkDoFrame@4" ) );
		copyToBuffer = reinterpret_cast< copyToBuffer_t >( GetProcAddress( module, "_BinkCopyToBuffer@28" ) );
		nextFrame = reinterpret_cast< nextFrame_t >( GetProcAddress( module, "_BinkNextFrame@4" ) );
		getError = reinterpret_cast< getError_t >( GetProcAddress( module, "_BinkGetError@0" ) );
		if ( open == NULL || close == NULL || doFrame == NULL || copyToBuffer == NULL || nextFrame == NULL ) {
			common->Warning( "Bink playback unavailable: binkw32.dll has an incompatible API" );
			FreeLibrary( module );
			module = NULL;
			return false;
		}
		return true;
	}

	HMODULE module;
	open_t open;
	close_t close;
	doFrame_t doFrame;
	copyToBuffer_t copyToBuffer;
	nextFrame_t nextFrame;
	getError_t getError;
	bool attemptedLoad;
};

binkApiBootstrap_t binkApi;

bool FindBinkPath( const idSoundShader* shader, idStr& samplePath ) {
	samplePath.Clear();
	if ( shader == NULL || shader->GetTextLength() <= 0 ) {
		return false;
	}

	const int textLength = shader->GetTextLength();
	char* text = static_cast< char* >( Mem_Alloc( textLength + 1 ) );
	shader->GetText( text );
	idParser src;
	src.SetFlags( LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWPATHNAMES );
	src.LoadMemory( text, textLength, shader->GetName() );
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token.Find( ".bik", false ) != -1 ) {
			samplePath = token;
			break;
		}
	}
	Mem_Free( text );
	return !samplePath.IsEmpty();
}

bool ResolveBinkOSPath( const char* samplePath, idStr& osPath ) {
	osPath.Clear();
	if ( samplePath == NULL || samplePath[ 0 ] == '\0' ) {
		return false;
	}

	char executablePath[ MAX_OSPATH ];
	const DWORD pathLength = GetModuleFileNameA( NULL, executablePath, sizeof( executablePath ) );
	if ( pathLength == 0 || pathLength >= sizeof( executablePath ) ) {
		return false;
	}
	for ( int i = static_cast< int >( pathLength ) - 1; i >= 0; i-- ) {
		if ( executablePath[ i ] == '\\' || executablePath[ i ] == '/' ) {
			executablePath[ i + 1 ] = '\0';
			break;
		}
	}

	idStr normalized = samplePath;
	normalized.BackSlashesToSlashes();
	osPath = executablePath;
	osPath += "base/";
	osPath += normalized;
	if ( GetFileAttributesA( osPath.c_str() ) != INVALID_FILE_ATTRIBUTES ) {
		return true;
	}

	// SDK development layouts can put loose assets below runtime/base.
	osPath = executablePath;
	osPath += "runtime/base/";
	osPath += normalized;
	return GetFileAttributesA( osPath.c_str() ) != INVALID_FILE_ATTRIBUTES;
}

class binkMovieBootstrap_t {
public:
	binkMovieBootstrap_t() :
		bink( NULL ),
		videoBuffer( NULL ),
		width( 0 ),
		height( 0 ),
		frameCount( 0 ),
		framesDecoded( 0 ),
		frameMsec( 33 ),
		nextFrameTime( 0 ),
		playing( false ),
		frameReady( false ) {
	}

	~binkMovieBootstrap_t() {
		Close();
	}

	bool Open( const char* samplePath ) {
		Close();
		idStr osPath;
		if ( !ResolveBinkOSPath( samplePath, osPath ) ) {
			common->Warning( "Bink cinematic '%s' was not found below the game base directory", samplePath );
			return false;
		}
		if ( !binkApi.Load() ) {
			return false;
		}

		// 0x4000 is the flag used by the retail sdBinkFile::Open path.
		bink = binkApi.open( osPath.c_str(), 0x4000 );
		if ( bink == NULL ) {
			const char* error = binkApi.getError != NULL ? binkApi.getError() : "unknown error";
			common->Warning( "BinkOpen failed for '%s': %s", osPath.c_str(), error != NULL ? error : "unknown error" );
			return false;
		}

		const binkHeaderBootstrap_t* header = static_cast< const binkHeaderBootstrap_t* >( bink );
		width = static_cast< int >( header->width );
		height = static_cast< int >( header->height );
		frameCount = static_cast< int >( header->frames );
		if ( width <= 0 || height <= 0 || width > 8192 || height > 8192 || frameCount <= 0 ) {
			common->Warning( "Bink cinematic '%s' has invalid dimensions or frame count", osPath.c_str() );
			Close();
			return false;
		}
		if ( header->frameRate > 0 && header->frameRateDiv > 0 ) {
			frameMsec = Max( 1, idMath::Ftoi( 1000.0f * header->frameRateDiv / header->frameRate ) );
		}

		const int lumaSize = width * height;
		const int chromaSize = ( width >> 1 ) * ( height >> 1 );
		videoBuffer = static_cast< byte* >( Mem_Alloc( lumaSize + chromaSize * 2 ) );
		memset( videoBuffer, 0, lumaSize + chromaSize * 2 );
		playing = true;
		nextFrameTime = Sys_Milliseconds();
		if ( !DecodeFrame() ) {
			Close();
			return false;
		}
		common->Printf( "Bink cinematic opened: %s (%dx%d, %d frames)\n", samplePath, width, height, frameCount );
		return true;
	}

	void Close() {
		if ( bink != NULL && binkApi.close != NULL ) {
			binkApi.close( bink );
		}
		bink = NULL;
		if ( videoBuffer != NULL ) {
			Mem_Free( videoBuffer );
		}
		videoBuffer = NULL;
		width = height = frameCount = framesDecoded = 0;
		playing = false;
		frameReady = false;
	}

	bool IsPlaying() const {
		return playing;
	}

	cinData_t ImageForTime() {
		if ( !playing || bink == NULL || videoBuffer == NULL ) {
			return EmptyCinematicData();
		}
		const int now = Sys_Milliseconds();
		if ( frameReady && now >= nextFrameTime ) {
			if ( framesDecoded >= frameCount ) {
				playing = false;
				return EmptyCinematicData();
			}
			if ( !DecodeFrame() ) {
				playing = false;
				return EmptyCinematicData();
			}
		}

		cinData_t data = EmptyCinematicData();
		data.imageWidth = width;
		data.imageHeight = height;
		const int lumaSize = width * height;
		const int chromaSize = ( width >> 1 ) * ( height >> 1 );
		data.image[ 0 ] = videoBuffer;
		// BINKSURFACEYV12 stores Y, V, U. cinData_t is Y, U, V.
		data.image[ 1 ] = videoBuffer + lumaSize + chromaSize;
		data.image[ 2 ] = videoBuffer + lumaSize;
		return data;
	}

private:
	bool DecodeFrame() {
		binkApi.doFrame( bink );
		const unsigned int BINK_SURFACE_YV12 = 15;
		const unsigned int BINK_COPY_ALL = 0x80000000u;
		binkApi.copyToBuffer( bink, videoBuffer, width, height, 0, 0, BINK_COPY_ALL | BINK_SURFACE_YV12 );
		binkApi.nextFrame( bink );
		framesDecoded++;
		frameReady = true;
		nextFrameTime = Sys_Milliseconds() + frameMsec;
		return true;
	}

	void* bink;
	byte* videoBuffer;
	int width;
	int height;
	int frameCount;
	int framesDecoded;
	int frameMsec;
	int nextFrameTime;
	bool playing;
	bool frameReady;
};

class idSoundEmitterBootstrap : public idSoundEmitter {
public:
	explicit idSoundEmitterBootstrap( int value ) :
		index( value ),
		playing( false ),
		hasMovie( false ),
		origin( vec3_origin ),
		listenerId( 0 ) {
		memset( &parms, 0, sizeof( parms ) );
	}
	virtual ~idSoundEmitterBootstrap() {
		movie.Close();
	}

	virtual int Index() const { return index; }
	virtual void Free( bool ) { playing = false; hasMovie = false; movie.Close(); }
	virtual void UpdateEmitter( const idVec3& value, int newListenerId, const soundShaderParms_t* newParms ) {
		origin = value;
		listenerId = newListenerId;
		if ( newParms != NULL ) {
			parms = *newParms;
		}
	}
	virtual int StartSound( const idSoundShader* shader, const soundChannel_t, soundChannel_t, float, int ) {
		playing = shader != NULL;
		hasMovie = false;
		idStr binkPath;
		if ( FindBinkPath( shader, binkPath ) ) {
			hasMovie = true;
			playing = movie.Open( binkPath.c_str() );
		}
		return 0;
	}
	virtual const soundShaderParms_t& GetChannelParms( const soundChannel_t ) { return parms; }
	virtual void ModifySound( const soundChannel_t, const soundShaderParms_t& value ) { parms = value; }
	virtual void StopSound( const soundChannel_t ) { playing = false; hasMovie = false; movie.Close(); }
	virtual void FadeSound( const soundChannel_t, float, float ) {}
	virtual bool CurrentlyPlaying() const { return playing && ( !hasMovie || movie.IsPlaying() ); }
	virtual float CurrentAmplitude() { return 0.0f; }
	virtual cinData_t ImageForTime( const int ) {
		cinData_t data = hasMovie ? movie.ImageForTime() : EmptyCinematicData();
		if ( hasMovie && data.image[ 0 ] == NULL && !movie.IsPlaying() ) {
			playing = false;
		}
		return data;
	}
	virtual void SetChannelOffset( const soundChannel_t, int ) {}

private:
	int index;
	bool playing;
	bool hasMovie;
	idVec3 origin;
	int listenerId;
	soundShaderParms_t parms;
	binkMovieBootstrap_t movie;
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
