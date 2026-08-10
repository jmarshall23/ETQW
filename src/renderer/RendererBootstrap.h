// Copyright (C) 2007 Id Software, Inc.
//
// Symbol-backed ETQW renderer boundary used while the private renderer is
// reconstructed.  The public renderer interfaces and their vtables are from
// the SDK/PDB; the implementation deliberately remains backend-neutral.

#ifndef __RENDERER_BOOTSTRAP_H__
#define __RENDERER_BOOTSTRAP_H__

#include "RenderSystem.h"
#include "RenderWorld.h"
#include "../idlib/threading/ThreadProcess.h"

extern glconfig_t glConfig;

class idRenderWorldLocal : public idRenderWorld {
public:
	idRenderWorldLocal();
	virtual ~idRenderWorldLocal();

	virtual bool InitFromMap( const char *mapName );
	virtual void LinkCullSectorsToArea( int area );

	virtual qhandle_t AddEntityDef( const renderEntity_t *re );
	virtual void UpdateEntityDef( qhandle_t entityHandle, const renderEntity_t *re );
	virtual void FreeEntityDef( qhandle_t entityHandle );
	virtual const renderEntity_t *GetRenderEntity( qhandle_t entityHandle ) const;
	virtual renderEntity_t *GetRenderEntity( qhandle_t entityHandle );

	virtual qhandle_t AddLightDef( const renderLight_t *rlight );
	virtual void UpdateLightDef( qhandle_t lightHandle, const renderLight_t *rlight );
	virtual void FreeLightDef( qhandle_t lightHandle );
	virtual const renderLight_t *GetRenderLight( qhandle_t lightHandle ) const;

	virtual qhandle_t AddEffectDef( const renderEffect_t *reffect, int time );
	virtual bool UpdateEffectDef( qhandle_t effectHandle, const renderEffect_t *reffect, int time );
	virtual void StopEffectDef( qhandle_t effectHandle );
	virtual void RestartEffectDef( qhandle_t effectHandle );
	virtual void FreeEffectDef( qhandle_t effectHandle );
	virtual void FreeStoppedEffectDefs();

	virtual qhandle_t AddOcclusionTestDef( const occlusionTest_t *occtest );
	virtual void UpdateOcclusionTestDef( qhandle_t occtestHandle, const occlusionTest_t *occtest );
	virtual void UpdateOcclusionTestDefViewID( qhandle_t occtestHandle, int viewID );
	virtual bool IsVisibleOcclusionTestDef( qhandle_t occtestHandle );
	virtual void FreeOcclusionTestDef( qhandle_t occtestHandle );
	virtual int CountVisibleOcclusionTestDef( qhandle_t occtestHandle );
	virtual bool IsVisibleEntity( int viewID, int occid );
	virtual void UpdateOcclusionTests();

	virtual void GenerateAllInteractions();
	virtual idRenderModel *GetEntityHandleDynamicModel( qhandle_t entityHandle );

	virtual idRenderModel *CreateDecalModel();
	virtual void AddToProjectedDecal( const idFixedWinding& winding, const idVec3 &projectionOrigin, const bool parallel, const idVec4& color, idRenderModel* model, int entityNum, const idMaterial** onlyMaterials, const int numOnlyMaterials );
	virtual void ResetDecalModel( idRenderModel* model );
	virtual void FinishDecal( idRenderModel* model );
	virtual void ProjectDecalOntoWorld( const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime, const int currentTime );
	virtual void ProjectDecal( qhandle_t entityHandle, const idFixedWinding &winding, const idVec3 &projectionOrigin, const bool parallel, const float fadeDepth, const idMaterial *material, const int startTime, const int currentTime );
	virtual void ProjectOverlay( qhandle_t entityHandle, const idPlane localTextureAxis[2], const idMaterial *material );
	virtual void RemoveDecals( qhandle_t entityHandle );
	virtual void AddCheapDecal( qhandle_t entityHandle, const cheapDecalParameters_t &params, float time );
	virtual void ClearDecals();
	virtual void AddEnvBounds( idVec3 const &origin, idVec3 const &scale, const char *cubemap );

	virtual void SetRenderView( const renderView_t *renderView );
	virtual void RenderScene( const renderView_t *renderView );
	virtual void PerformRenderScene( const renderView_t *renderView );

	virtual int NumPortals() const;
	virtual qhandle_t FindPortal( const idBounds &b ) const;
	virtual void SetPortalState( qhandle_t portal, int blockingBits );
	virtual int GetPortalState( qhandle_t portal );
	virtual void UpdatePortalOccTestView( int viewID );
	virtual bool AreasAreConnected( int areaNum1, int areaNum2, portalConnection_t connection );
	virtual bool AreasAreConnected( int areaNum1, int areaNum2, portalFlags_t flag );
	virtual bool AreasAreConnected( int areaNum1, int areaNum2 );
	virtual int NumAreas() const;
	virtual int PointInArea( const idVec3 &point ) const;
	virtual int BoundsInAreas( const idBounds &bounds, int *areas, int maxAreas ) const;
	virtual int NumPortalsInArea( int areaNum );
	virtual exitPortal_t GetPortal( int areaNum, int portalNum );
	virtual void SetAreaPortalFlags( int areaNum, int flags );
	virtual int GetAreaPortalFlags( int areaNum ) const;
	virtual void SetAreaAmbientCubeMap( int areaNum, const sdDeclAmbientCubeMap *cubeMapDecl );
	virtual const sdDeclAmbientCubeMap *GetAreaAmbientCubeMap( int areaNum );
	virtual void SetCubemapSunProperties( const sdDeclAmbientCubeMap *cubeMapDecl, const idVec3 &sunDir, const idVec3 &sunColor );

	virtual bool ModelTrace( modelTrace_t &trace, qhandle_t entityHandle, const idVec3 &start, const idVec3 &end, const float radius, int surfCollision ) const;
	virtual bool Trace( modelTrace_t &trace, const idVec3 &start, const idVec3 &end, const float radius, bool skipDynamic ) const;
	virtual bool FastWorldTrace( modelTrace_t &trace, const idVec3 &start, const idVec3 &end ) const;

	virtual void StartWritingDemo( idDemoFile *demo );
	virtual void StopWritingDemo();
	virtual bool ProcessDemoCommand( idDemoFile *readDemo, renderView_t *demoRenderView, int *demoTimeOffset );
	virtual void RegenerateWorld();

	virtual void DebugClearLines( int time );
	virtual void DebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifetime, const bool depthTest );
	virtual void DebugArrow( const idVec4 &color, const idVec3 &start, const idVec3 &end, int size, const int lifetime, bool depthTest );
	virtual void DebugWinding( const idVec4 &color, const idWinding &w, const idVec3 &origin, const idMat3 &axis, const int lifetime, const bool depthTest );
	virtual void DebugCircle( const idVec4 &color, const idVec3 &origin, const idVec3 &dir, const float radius, const int numSteps, const int lifetime, const bool depthTest );
	virtual void DebugSphere( const idVec4 &color, const idSphere &sphere, const int lifetime, const bool depthTest );
	virtual void DebugBounds( const idVec4 &color, const idBounds &bounds, const idVec3 &org, const idMat3& axes, const int lifetime );
	virtual void DebugBox( const idVec4 &color, const idBox &box, const int lifetime );
	virtual void DebugFrustum( const idVec4 &color, const idFrustum &frustum, const bool showFromOrigin, const int lifetime );
	virtual void DebugCone( const idVec4 &color, const idVec3 &apex, const idVec3 &dir, float radius1, float radius2, const int lifetime );
	virtual void DebugAxis( const idVec3 &origin, const idMat3 &axis, int lifetime );
	virtual void DebugClearPolygons( int time );
	virtual void DebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest, idImage* image );
	virtual void DrawText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime );

	virtual void SetAtmosphere( const sdDeclAtmosphere* atmosphere );
	virtual const sdDeclAtmosphere* GetAtmosphere() const;
	virtual void SetupMatrices( const renderView_t* renderView, float* projectionMatrix, float* modelViewMatrix, const bool allowJitter );
	virtual void SetMegaTextureSTGrid( const idBounds& bounds, const idVec2* grid, int width, int height );
	virtual atmosLightProjection_t *FindAtmosLightProjection( int lightID );

private:
	void Clear();
	void ClearTrace( modelTrace_t &trace, const idVec3 &end ) const;

	idStr mapName;
	idList< idRenderModel* > localModels;
	idList< renderEntity_t* > entityDefs;
	idList< renderLight_t* > lightDefs;
	idList< renderEffect_t* > effectDefs;
	idList< bool > stoppedEffects;
	idList< occlusionTest_t* > occlusionTests;
	renderView_t currentRenderView;
	bool hasRenderView;
	int areaPortalFlags;
	const sdDeclAmbientCubeMap* ambientCubeMap;
	const sdDeclAtmosphere* atmosphere;
	idDemoFile* writeDemo;
};

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
