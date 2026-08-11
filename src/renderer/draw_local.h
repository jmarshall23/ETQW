// Copyright (C) 2007 Id Software, Inc.
//
// ETQW renderer back-end private types.  Field names and ownership follow the
// retail PDB drawSurf_s/viewEntity_s/viewLight_s/viewDef_s records.  Engine
// front-end classes are represented by their public parameter blocks until
// idRenderEntityLocal/idRenderLightLocal are made active.

#ifndef __ETQW_DRAW_LOCAL_H__
#define __ETQW_DRAW_LOCAL_H__

#include "RenderWorld_local.h"
#include "ScreenRect.h"
#include "Material.h"
#include "Model.h"

struct viewEntity_s {
	viewEntity_s* next;
	renderEntity_t* entityDef;
	int entityIndex;
	idRenderModel* model;
	idScreenRect scissorRect;
	bool culled;
	bool occtest;
	int occlusionIndex;
	bool weaponDepthHack;
	bool foliageDepthHack;
	float modelDepthHack;
	float weaponDepthHackFOV_x;
	float weaponDepthHackFOV_y;
	float modelMatrix[ 16 ];
	float modelViewMatrix[ 16 ];
	float coverage;
	short minGpuSpec;
	short numInsts;
	sdInstInfo* insts;
	// ETQW allocates a bit set immediately after each viewEntity.  The
	// atmosphere/mega interaction builder marks the model surface ids that are
	// rendered by the combined ambient-lighting path; R_FillDepthAmbient uses
	// it to avoid drawing those surfaces a second time.
	int maxSurfID;
	unsigned int* ambSurf;
	const sdDeclAmbientCubeMap* ambientCubeMap;
	idImage* envCubemap;
};

struct drawSurf_s {
	const srfTriangles_t* geo;
	const viewEntity_s* space;
	const idMaterial* material;
	float sort;
	float* materialRegisters;
	drawSurf_s* nextOnLight;
	idScreenRect scissorRect;
	int dsFlags;
	float shadowProjectDist;
	int surfID;
};

struct viewLight_s {
	viewLight_s* next;
	renderLight_t* lightDef;
	idScreenRect scissorRect;
	bool culled;
	int lightIndex;
	int occlusionIndex;
	bool viewInsideLight;
	bool viewSeesGlobalLightOrigin;
	int viewSeesShadowPlaneBits;
	idVec3 globalLightOrigin;
	idVec3 globalLightDirection;
	idVec3 lightRadius;
	idPlane lightProject[ 4 ];
	idPlane frustum[ 6 ];
	idPlane fogPlane;
	const srfTriangles_t* frustumTris;
	float fadeFraction;
	const idMaterial* material;
	float* lightRegisters;
	idImage* falloffImage;
	float lightRadiusLength;
	drawSurf_s* globalShadows;
	drawSurf_s* localInteractions;
	drawSurf_s* localShadows;
	drawSurf_s* globalInteractions;
	drawSurf_s* translucentInteractions;
	drawSurf_s* mtInteractions;
};

struct viewDef_s {
	renderView_t renderView;
	float projectionMatrix[ 16 ];
	viewEntity_s worldSpace;
	idRenderWorldLocal* renderWorld;
	float floatTime;
	idScreenRect viewport;
	idScreenRect scissor;
	drawSurf_s** drawSurfs;
	int numDrawSurfs;
	viewLight_s* viewLights;
	viewEntity_s* viewEntities;
	viewLight_s* atmosphereLight;
	const sdDeclAtmosphere* atmosphere;
	idPlane frustum[ 6 ];
	int numPlanes;
	bool isSubview;
	bool isMirror;
};

void RB_BuildDrawView( idRenderWorldLocal* renderWorld, const renderView_t* renderView );
void RB_FreeDrawView();
viewDef_s* RB_GetViewDef();
void R_BuildDrawView( idRenderWorldLocal* renderWorld, const renderView_t* renderView );
void R_FreeBuiltDrawView();
viewEntity_s* R_SetEntityDefViewEntity( renderEntity_t* entity, idRenderModel* model, int entityIndex );
viewLight_s* R_SetLightDefViewLight( renderLight_t* light, int lightIndex );
bool R_GlobalShaderOverride( const idMaterial** shader );
const idMaterial* R_RemapShaderBySkin( const idMaterial* shader, const idDeclSkin* customSkin, const idMaterial* customShader );
void R_AddDrawSurf( const srfTriangles_t* triangles, const viewEntity_s* space, const renderEntity_t* renderEntity,
	const idMaterial* material, const idScreenRect& scissor, int surfID );
void R_AddAmbientDrawsurfs( viewEntity_s* space );
void R_AddModelSurfaces();
void R_AddLightSurfaces();
void R_RemoveUnecessaryViewLights();
void R_SetupViewFrustum( viewDef_s* viewDef );
bool R_CullLocalBox( const idBounds& bounds, const float modelMatrix[ 16 ], int numPlanes, const idPlane* planes );
bool R_CullLocalBox( const idBounds& bounds, const idMat3& axis, const idVec3& origin, int numPlanes, const idPlane* planes );
bool R_CullLocalBoxToViewdef( const idBounds& bounds, const float modelMatrix[ 16 ], const viewDef_s* viewDef );
bool R_CullLocalBoxToViewdef( const idBounds& bounds, const idMat3& axis, const idVec3& origin, const viewDef_s* viewDef );
int R_CullLocalBoxWithin( const idBounds& bounds, const float modelMatrix[ 16 ], int numPlanes, const idPlane* planes );
bool R_DistanceVisibility( const idVec3& origin, int maxVisDist, int minVisDist, const viewDef_s* viewDef );

#endif
