// Copyright (C) 2007 Id Software, Inc.

#ifndef __ETQW_TR_RENDER_H__
#define __ETQW_TR_RENDER_H__

#include "RendererEnums.h"

class idRenderModel;
class idRenderWorldLocal;
class idImage;
class idMaterial;
class idVec4;
class idPlane;
class sdDeclRenderProgram;
struct renderEntity_t;
struct renderView_s;
struct srfTriangles_t;
struct stageTextureMatrix_t;
struct materialStage_t;
struct renderLight_t;
struct drawSurf_s;
struct viewEntity_s;
struct viewLight_s;
struct viewDef_s;
struct srfIndexTree_s;
class sdGuiModel;

typedef renderView_s renderView_t;

void GL_SelectTexture( int unit );
void GL_Cull( int cullType );
void GL_TexEnv( int env );
void GL_State( int stateVector );
void RB_SetDefaultGLState();
void RB_SetGL2D();

// The retail back end keeps these modes in file-local draw_new.cpp state.
// Naming them here lets the reconstructed front end submit the same surface
// to the depth, ambient, interaction, and ordinary shader phases without
// reviving the incompatible Quake 4 backEnd/viewDef structures.
enum rbDrawPass_t {
	RBP_DEPTH,
	RBP_AMBIENT,
	RBP_INTERACTION,
	RBP_SHADER
};

void RB_GetShaderTextureMatrix( const stageTextureMatrix_t* textureMatrix, const float* materialRegisters, float matrix[ 16 ] );
void RB_BakeTextureMatrixIntoTexgenAligned( idPlane outLightProject[ 2 ], const idPlane inLightProject[ 3 ], const float textureMatrix[ 16 ] );
void R_TransformEyeZToWin( float srcZ, const float* projectionMatrix, float& dstZ );
void R_GenerateViewMatrix( const idMat3& axis, const idVec3& origin, float* const out );
void R_SetViewMatrix( viewDef_s* viewDef );
void R_SetupProjection( viewDef_s* viewDef, bool allowJitter );
void R_SetupMatrices( viewDef_s* viewDef, bool allowJitter );
int R_GenerateIndexTreeRenderList( int* list, int maxLength, const float modelMatrix[ 16 ], const viewDef_s* viewDef, const srfTriangles_t* triangles );
srfTriangles_t* R_PolytopeSurface( int numPlanes, const idPlane* planes, idWinding** windings );
void R_FreePolytopeSurface( srfTriangles_t* triangles );
void RB_DrawElementsImmediate( const srfTriangles_t* triangles );
void RB_DrawElementsWithCounters( const srfTriangles_t* triangles );
void GL_EnableVertexAttribs( int requiredVertexAttribBits );
void RB_SetImmediateViewState( const renderView_t* renderView, const float projectionMatrix[ 16 ], int viewportWidth, int viewportHeight );
void RB_SetConstantRenderBindings();
void RB_SetCurrentBindingSpace( const float modelMatrix[ 16 ] );
void RB_BeginDrawingView();
void RB_DetermineLightScale();
void RB_EnterWeaponDepthHack( float fov_x, float fov_y );
void RB_EnterModelDepthHack( float depth );
void RB_LeaveDepthHack();
void RB_ARB2_ClearSpace();
void RB_ARB2_ResetDrawCaches();
const viewEntity_s* RB_GetActiveDrawSpace();
viewLight_s* RB_GetActiveViewLight();
void RB_ARB2_SetShadowSurfaceContext( const drawSurf_s* surface, bool active );
void RB_ARB2_SetSpace( const float modelMatrix[ 16 ], const idVec3& globalViewOrigin, float coverage = 1.0f );
void RB_ARB2_SetupLightSpace( const renderLight_t* light, const float modelMatrix[ 16 ] );
void R_DeriveLightData( const renderLight_t& light, idPlane lightProject[ 4 ], idVec3& globalLightOrigin, const idMaterial*& lightMaterial, idImage*& falloffImage );
bool RB_ARB2_UseStage( const materialStage_t* stage, const float* materialRegisters, rbDrawPass_t pass );
bool RB_ARB2_UseStage( const materialStage_t* stage, const float* materialRegisters );
const sdDeclRenderProgram* RB_ARB2_SelectProgram( const sdDeclRenderProgram* program, rbDrawPass_t pass, const renderEntity_t* entity, const idMaterial* material );
const sdDeclRenderProgram* RB_ARB2_SelectProgram( const sdDeclRenderProgram* program );
void RB_RenderTriangleSurface( const srfTriangles_t* triangles, const sdDeclRenderProgram* program, int extraState, cullType_t cullType );
typedef void ( *drawSurfRenderFunction_t )( const drawSurf_s* surface, const sdDeclRenderProgram* program, int stateBits, cullType_t cullType, int surfaceIndex );
void RB_RenderScene( const void* data );
void RB_EmitGuiModel( const void* data );
void RB_T_RenderTriangleSurface( const drawSurf_s* surface, const sdDeclRenderProgram* program, int stateBits, cullType_t cullType, int surfaceIndex );
void RB_RenderDrawSurfListWithFunction( drawSurf_s** drawSurfs, int numDrawSurfs, drawSurfRenderFunction_t renderFunction, const sdDeclRenderProgram* program, int stateBits, cullType_t cullType );
void RB_DrawViewImmediate( const viewDef_s* viewDef );
void RB_DrawView( const void* data );
bool RB_SetupMaterialStage( const materialStage_t* stage, const float* materialRegisters, idImage* overrideImage, idVec4& color, idVec4& matrixS, idVec4& matrixT, const sdDeclRenderProgram* selectedProgram = NULL, float texCoordScale = 1.0f );
void RB_SetDrawViewContext( idRenderWorldLocal* renderWorld, const renderView_t* renderView );
void RB_ClearDrawViewContext();
idRenderWorldLocal* RB_GetDrawWorld();
const renderView_t* RB_GetDrawView();
viewDef_s* RB_GetViewDef();
viewDef_s* RB_SwapViewDefContext( viewDef_s* viewDef );
void RB_STD_DrawView();
void RB_STD_DrawShadowView();
void RB_RenderDebugTools( drawSurf_s** drawSurfs, int numDrawSurfs );
void RB_ARB2_DrawDepth();
void R_DrawMTInteractions();
void R_FillDepthAmbient();
void RB_ARB2_DrawInteractions();
int RB_ARB2_DrawShaderPasses( int phase );
void RB_ARB2_FogLights( int phase );
void R_CalculateBlendPlanesPerLight( viewLight_s* vLight );
void R_CalculateFogPlanesPerLight( viewLight_s* vLight );
void R_CalculateFogPlanesPerSpace( const drawSurf_s* surface );
void R_CalculateBlendPlanesPerSpace( const drawSurf_s* surface );
void RB_ARB2_DrawAtmosphere();
void RB_ARB2_DrawAtmospherePostProcess();
void RB_ARB2_DrawAtmosphere( drawSurf_s** drawSurfs, int numDrawSurfs );
void RB_ARB2_DrawAtmosphere( const drawSurf_s* drawSurfs );
void RB_DrawFullscreenQuad( const idMaterial* material, unsigned int color );
void RB_ARB2_StencilShadowPass();
void RB_ARB2_DrawShadowElementsWithCounters( const srfTriangles_t* triangles, int numIndexes );
void RB_ARB2_DrawShadowSurface( const drawSurf_s* surface, const sdDeclRenderProgram* program, int stateBits );
void RB_ARB2_StencilShadowPass( const drawSurf_s* surfaces, const sdDeclRenderProgram* program, const sdDeclRenderProgram* invariantProgram, bool atmosLight, float polyFactor, float polyOffset );
void R_PrevFrameOcclusionSystemUpdateViewEnts();
void R_PrevFrameOcclusionSystem();
void R_RetireOcclusionQueries();
void R_FreeOcclussionQueries();
int R_GatherQueryResults( drawSurf_s** drawSurfs, int numDrawSurfs, bool phase );
void R_ExecOccQueries( drawSurf_s** drawSurfs, int numDrawSurfs );
void R_ExecOccQueriesForLights();
void R_GatherOccQueriesForLights();
void R_GatherOccQueriesForGameTests();
void R_ExecOccQueriesForGameTests();
void R_RetirePrevFrameOcclusionSystem();
int R_IsVisibleOcclusionSystem( int viewID, int occid );
int R_CountVisibleOcclusionSystem( int viewID, int occid );
bool R_IsVisibleEntity( int viewID, int occid );
void R_PrevFrameOcclusionSystemUpdateViewEnts( drawSurf_s** drawSurfs, int numDrawSurfs );
void R_PrevFrameOcclusionSystem( drawSurf_s** drawSurfs, int numDrawSurfs );
void RB_DrawBoundsFilled( const idBounds& bounds );

void RB_ARB2_SetupPostProcessingFrameBuffer();
void RB_ARB2_CopyFramebufferColor();
bool RB_ARB2_HasCurrentRenderCopy();
void RB_ARB2_BeginViewFrame();
void RB_ARB2_ResetPostProcessingFrameBuffer();
void RB_ARB2_SetupProgram( const sdDeclRenderProgram* program, int stateBits, cullType_t cullType, const srfTriangles_t* triangles );
void RB_ARB2_DrawWithProgram( const srfTriangles_t* triangles, const sdDeclRenderProgram* program, int stateBits, cullType_t cullType );
void RB_ARB2_SetupProgram_Simple( const sdDeclRenderProgram* program, int stateBits, cullType_t cullType, const srfTriangles_t* triangles );
void RB_ARB2_SetVertexPointers( const srfTriangles_t* triangles, bool& weightCacheModified );
void RB_ARB2_DrawSurface( const drawSurf_s* surface, const idMaterial* material, const float* materialRegisters, void ( *customSpaceCallback )( const drawSurf_s* ) );
void RB_ARB2_DrawSurfacePass( const drawSurf_s* surface, const idMaterial* material, const float* materialRegisters, rbDrawPass_t pass, void ( *customSpaceCallback )( const drawSurf_s* ) );
void RB_ARB2_DrawSurface_Simple( const drawSurf_s* surface, const idMaterial* material, const float* materialRegisters );
void RB_ARB2_CreateDrawInteractions( const drawSurf_s* surface, bool ambient );
void RB_ARB2_SetSpace( const viewEntity_s* space, bool useSampleCoverage );
void RB_ARB2_SetupLightSpace( const drawSurf_s* surface );
int RB_ARB2_DrawShaderPasses( drawSurf_s** drawSurfs, int numDrawSurfs, int phase );
void R_FillDepthAmbient( drawSurf_s** drawSurfs, int numDrawSurfs );
void RB_ARB2_DrawDepth( drawSurf_s** drawSurfs, int numDrawSurfs );

#endif
