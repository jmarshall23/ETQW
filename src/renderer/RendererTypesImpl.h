// Copyright (C) 2007 Id Software, Inc.
//
// Private ETQW renderer implementation types reconstructed from the retail PDB.

#ifndef __RENDERER_TYPES_IMPL_H__
#define __RENDERER_TYPES_IMPL_H__

#include "RenderSystem.h"
#include "RenderWorld_local.h"
#include "../idlib/threading/ThreadProcess.h"

extern glconfig_t glConfig;

class idRenderSystemLocal : public idRenderSystem, public sdThreadProcess {
public:
	idRenderSystemLocal();
	virtual ~idRenderSystemLocal();

	virtual void Init();
	virtual void Shutdown();
	virtual void ShutdownOpenGL();
	virtual bool IsOpenGLRunning() const;
	virtual int GetScreenWidth() const;
	virtual int GetScreenHeight() const;
	virtual idRenderWorld *AllocRenderWorld();
	virtual void FreeRenderWorld( idRenderWorld *rw );
	virtual void BeginLevelLoad();
	virtual void EndLevelLoad();
	virtual void LevelStart();
	virtual void DrawChar( int charWidth, int charHeight, int x, int y, int ch, const idMaterial *material );
	virtual void DrawStringExt( int charWidth, int charHeight, int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material );
	virtual void DrawSmallChar( int x, int y, int ch, const idMaterial *material );
	virtual void DrawSmallStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material );
	virtual void DrawBigChar( int x, int y, int ch, const idMaterial *material );
	virtual void DrawBigStringExt( int x, int y, const char *string, const idVec4 &setColor, bool forceColor, const idMaterial *material );
	virtual void WriteDemoPics();
	virtual void DrawDemoPics();
	virtual void BeginFrame( int windowWidth, int windowHeight );
	virtual void EndFrame( bool swapBuffers );
	virtual void SetCaptureBuffer( sdFrameBuffer* frameBuffer );
	virtual sdFrameBuffer* GetCaptureBuffer();
	virtual bool TakeScreenshot( int width, int height, const char *fileName, int samples, renderView_s *ref, bool useOffscreenContext, bool flip );
	virtual void CropRenderSize( int width, int height, bool makePowerOfTwo );
	virtual void CaptureRenderToImage( const char *imageName, int faceNum, copyBuffer_t buffer );
	virtual void SetFrameBuffer( sdFrameBuffer *frameBuffer );
	virtual void UnCrop();
	virtual void GetCardCaps( bool &oldCard );
	virtual bool UploadImage( const char* imageName, const byte* data, int width, int height, bool generateMipMaps, bool copy );
	virtual void BindImage( textureType_t target, GLuint image );
	virtual void SetGLState( int stateVector );
	virtual void SetGLTexEnv( int env );
	virtual void SelectTextureUnit( int unit );
	virtual void SetDefaultGLState();
	virtual void SetGL2D();
	virtual void SetCull( int cullType );
	virtual FILE* GetLogFileHandle();
	virtual void SetLogFileHandle( FILE* file );
	virtual void LoadImage( const char *name, byte **pic, int *width, int *height, unsigned *timestamp, bool makePowerOf2 );
	virtual void FlushGLErrors( bool forcePrint );
	virtual int CheckGLForErrors( bool forcePrint );
	virtual idRenderModel* InstantiateDynamicModel( idRenderModel* model, renderEntity_t* ent );
	virtual const glconfig_t& GLConfig() const;
	virtual void SyncRenderSystem();
	virtual bool BeginRenderSync();
	virtual void EndRenderSync();
	virtual idImage *LoadImageFromFile( const char *filename, imageParams_t &ip );
	virtual bool IsDisplayModeAvailable( int width, int height ) const;
	virtual int GetNumMSAAModes() const;
	virtual const char *GetMSAAMode( int idx, int &val ) const;
	virtual bool IsMSAACountAvailable( int msaa ) const;
	virtual void LockThreads();
	virtual void UnlockThreads();
	virtual int GetDoubleBufferIndex();
	virtual int GetSyncNum();
	virtual bool IsSMPEnabled();
	virtual void FreeOcclussionQueries();
	virtual int RegisterPtr( void *ptr );
	virtual void UnregisterPtr( int uid );
	virtual void* PtrForUID( int uid );

	virtual unsigned int Run( void* parm );

private:
	bool initialized;
	bool openGLRunning;
	bool synced;
	bool threadsLocked;
	int windowWidth;
	int windowHeight;
	int syncNum;
	int doubleBufferIndex;
	sdFrameBuffer* captureBuffer;
	sdFrameBuffer* frameBuffer;
	FILE* logFile;
	idList< idRenderWorld* > worlds;
	idList< void* > registeredPtrs;
};

extern idRenderSystemLocal tr;

#endif

