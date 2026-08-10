// Copyright (C) 2007 Id Software, Inc.
//
// ETQW standard back-end pass orchestration.  The ordering is recovered from
// renderer/draw_common.obj in the retail PDB (RB_STD_DrawView, RVA 0x405090).

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_render.h"
#include "draw_local.h"
#include "Image.h"
#include "RenderWorld_local.h"
#include "querytimers.h"
#include "../decllib/declTypeHolder.h"
#include "../libs/qglLib/qgl.h"

extern idCVar r_megaDrawMethod;
extern idCVar r_softParticles;
extern idCVar r_AtmospherePostprocess;
extern idCVar r_useSampleCoverage;
extern idCVar r_forceGLFinish;

idCVar r_depthFill(
	"r_depthFill", "0", CVAR_RENDERER,
	"Enable depth only pass"
);

idCVar r_skipShadowViewsBackend(
	"r_skipShadowViewsBackend", "0", CVAR_RENDERER | CVAR_BOOL,
	"Skip the rendering but all other setup is done."
);

#if 0
// Superseded reconstruction scaffolding.  View construction and ownership are
// front-end work (tr_main.cpp/tr_light.cpp), not draw_common.cpp work.  Keep
// this block temporarily as a comparison aid while the remaining front end is
// recovered, but do not compile a second view builder into the back end.
namespace {
	idRenderWorldLocal* drawWorld = NULL;
	const renderView_t* drawView = NULL;
	viewDef_s backendView;
	idList< viewEntity_s* > allocatedViewEntities;
	idList< viewLight_s* > allocatedViewLights;
	idList< drawSurf_s* > allocatedDrawSurfaces;
	idList< float* > allocatedRegisters;
	idList< drawSurf_s* > sortedDrawSurfaces;

	void FinishDrawPhase() {
		if ( r_forceGLFinish.GetInteger() > 1 ) glFinish();
	}

	void SetFullScreenRect( idScreenRect& rect ) {
		// View/scissor rectangles in the ETQW back end are viewport-relative;
		// the viewport origin is added only when issuing glScissor.
		rect.x1 = 0;
		rect.y1 = 0;
		rect.x2 = backendView.viewport.x2 - backendView.viewport.x1;
		rect.y2 = backendView.viewport.y2 - backendView.viewport.y1;
		rect.zmin = 0.0f;
		rect.zmax = 1.0f;
	}

	void SetEntityMatrix( const renderEntity_t* entity, float matrix[ 16 ] ) {
		matrix[ 0 ] = entity != NULL ? entity->axis[ 0 ].x : 1.0f;
		matrix[ 1 ] = entity != NULL ? entity->axis[ 0 ].y : 0.0f;
		matrix[ 2 ] = entity != NULL ? entity->axis[ 0 ].z : 0.0f;
		matrix[ 3 ] = 0.0f;
		matrix[ 4 ] = entity != NULL ? entity->axis[ 1 ].x : 0.0f;
		matrix[ 5 ] = entity != NULL ? entity->axis[ 1 ].y : 1.0f;
		matrix[ 6 ] = entity != NULL ? entity->axis[ 1 ].z : 0.0f;
		matrix[ 7 ] = 0.0f;
		matrix[ 8 ] = entity != NULL ? entity->axis[ 2 ].x : 0.0f;
		matrix[ 9 ] = entity != NULL ? entity->axis[ 2 ].y : 0.0f;
		matrix[ 10 ] = entity != NULL ? entity->axis[ 2 ].z : 1.0f;
		matrix[ 11 ] = 0.0f;
		matrix[ 12 ] = entity != NULL ? entity->origin.x : 0.0f;
		matrix[ 13 ] = entity != NULL ? entity->origin.y : 0.0f;
		matrix[ 14 ] = entity != NULL ? entity->origin.z : 0.0f;
		matrix[ 15 ] = 1.0f;
	}

	void SetEntityModelViewMatrix( const float modelMatrix[ 16 ], float modelViewMatrix[ 16 ] ) {
		float viewMatrix[ 16 ];
		glGetFloatv( GL_MODELVIEW_MATRIX, viewMatrix );
		for ( int column = 0; column < 4; ++column ) {
			for ( int row = 0; row < 4; ++row ) {
				modelViewMatrix[ column * 4 + row ] =
					viewMatrix[ 0 * 4 + row ] * modelMatrix[ column * 4 + 0 ] +
					viewMatrix[ 1 * 4 + row ] * modelMatrix[ column * 4 + 1 ] +
					viewMatrix[ 2 * 4 + row ] * modelMatrix[ column * 4 + 2 ] +
					viewMatrix[ 3 * 4 + row ] * modelMatrix[ column * 4 + 3 ];
			}
		}
	}

	viewEntity_s* AllocViewEntity( renderEntity_t* entity, idRenderModel* model, int entityIndex ) {
		viewEntity_s* space = new viewEntity_s;
		memset( space, 0, sizeof( *space ) );
		space->entityDef = entity;
		space->entityIndex = entityIndex;
		space->occlusionIndex = 0;
		space->model = model;
		space->occtest = entity != NULL && entity->flags.occlusionTest;
		space->coverage = entity != NULL && entity->flags.overridenCoverage ? entity->coverage : 1.0f;
		space->minGpuSpec = entity != NULL ? entity->minGpuSpec : 0;
		space->numInsts = entity != NULL ? entity->numInsts : 0;
		space->insts = entity != NULL ? entity->insts : NULL;
		space->weaponDepthHack = entity != NULL && entity->flags.weaponDepthHack;
		space->foliageDepthHack = entity != NULL && entity->flags.foliageDepthHack;
		space->modelDepthHack = entity != NULL ? entity->modelDepthHack : 0.0f;
		space->weaponDepthHackFOV_x = entity != NULL ? entity->weaponDepthHackFOV_x : 0.0f;
		space->weaponDepthHackFOV_y = entity != NULL ? entity->weaponDepthHackFOV_y : 0.0f;
		space->ambientCubeMap = entity != NULL && entity->ambientCubeMap != NULL ? entity->ambientCubeMap : drawWorld->BackendAmbientCubeMap();
		SetEntityMatrix( entity, space->modelMatrix );
		SetEntityModelViewMatrix( space->modelMatrix, space->modelViewMatrix );
		SetFullScreenRect( space->scissorRect );
		allocatedViewEntities.Append( space );
		return space;
	}

	drawSurf_s* AllocDrawSurface( const modelSurface_t* modelSurface, viewEntity_s* space, const idMaterial* material, const float* shaderParms ) {
		if ( modelSurface == NULL || modelSurface->geometry == NULL || material == NULL || !material->IsDrawn() ) return NULL;
		drawSurf_s* surface = new drawSurf_s;
		memset( surface, 0, sizeof( *surface ) );
		surface->geo = modelSurface->geometry;
		surface->space = space;
		surface->material = material;
		surface->sort = material->GetSort();
		surface->surfID = modelSurface->id;
		SetFullScreenRect( surface->scissorRect );
		float* registers = new float[ Max( material->GetNumRegisters(), 1 ) ];
		material->EvaluateRegisters( registers, shaderParms, &backendView, NULL, 0 );
		surface->materialRegisters = registers;
		allocatedRegisters.Append( registers );
		allocatedDrawSurfaces.Append( surface );
		return surface;
	}

	drawSurf_s* AllocInteractionSurface( const drawSurf_s* source ) {
		drawSurf_s* interaction = new drawSurf_s( *source );
		interaction->nextOnLight = NULL;
		allocatedDrawSurfaces.Append( interaction );
		return interaction;
	}

	int DrawSurfaceSortCompare( drawSurf_s* const* left, drawSurf_s* const* right ) {
		if ( ( *left )->sort < ( *right )->sort ) return -1;
		if ( ( *left )->sort > ( *right )->sort ) return 1;
		return ( *left )->material < ( *right )->material ? -1 : ( *left )->material != ( *right )->material;
	}

	bool SurfaceIntersectsLight( const drawSurf_s* surface, const renderLight_t* light ) {
		if ( surface == NULL || light == NULL || !light->flags.pointLight ) return true;
		idVec3 center = surface->geo->bounds.GetCenter();
		if ( surface->space != NULL ) {
			const float* matrix = surface->space->modelMatrix;
			center.Set(
				center.x * matrix[ 0 ] + center.y * matrix[ 4 ] + center.z * matrix[ 8 ] + matrix[ 12 ],
				center.x * matrix[ 1 ] + center.y * matrix[ 5 ] + center.z * matrix[ 9 ] + matrix[ 13 ],
				center.x * matrix[ 2 ] + center.y * matrix[ 6 ] + center.z * matrix[ 10 ] + matrix[ 14 ]
			);
		}
		const idVec3 delta = center - light->origin;
		const float radius = surface->geo->bounds.GetRadius();
		for ( int axis = 0; axis < 3; ++axis ) {
			if ( idMath::Fabs( delta * light->axis[ axis ] ) > idMath::Fabs( light->lightRadius[ axis ] ) + radius ) return false;
		}
		return true;
	}
}

void RB_SetDrawViewContext( idRenderWorldLocal* renderWorld, const renderView_t* renderView ) {
	drawWorld = renderWorld;
	drawView = renderView;
	RB_BuildDrawView( renderWorld, renderView );
}

void RB_ClearDrawViewContext() {
	RB_FreeDrawView();
	drawWorld = NULL;
	drawView = NULL;
}

idRenderWorldLocal* RB_GetDrawWorld() {
	return drawWorld;
}

const renderView_t* RB_GetDrawView() {
	return drawView;
}

viewDef_s* RB_GetViewDef() {
	return drawWorld != NULL ? &backendView : NULL;
}

void RB_FreeDrawView() {
	R_FreeBuiltDrawView();
#if 0
	for ( int i = 0; i < allocatedRegisters.Num(); ++i ) delete[] allocatedRegisters[ i ];
	for ( int i = 0; i < allocatedDrawSurfaces.Num(); ++i ) delete allocatedDrawSurfaces[ i ];
	for ( int i = 0; i < allocatedViewLights.Num(); ++i ) delete allocatedViewLights[ i ];
	for ( int i = 0; i < allocatedViewEntities.Num(); ++i ) delete allocatedViewEntities[ i ];
	allocatedRegisters.Clear();
	allocatedDrawSurfaces.Clear();
	allocatedViewLights.Clear();
	allocatedViewEntities.Clear();
	sortedDrawSurfaces.Clear();
#endif
	memset( &backendView, 0, sizeof( backendView ) );
}

void RB_BuildDrawView( idRenderWorldLocal* renderWorld, const renderView_t* renderView ) {
	R_BuildDrawView( renderWorld, renderView );
#if 0
	RB_FreeDrawView();
	if ( renderWorld == NULL || renderView == NULL ) return;
	drawWorld = renderWorld;
	drawView = renderView;
	backendView.renderWorld = renderWorld;
	backendView.renderView = *renderView;
	backendView.floatTime = renderView->time * 0.001f;
	backendView.atmosphere = renderWorld->GetAtmosphere();
	glGetFloatv( GL_PROJECTION_MATRIX, backendView.projectionMatrix );
	GLint viewport[ 4 ];
	glGetIntegerv( GL_VIEWPORT, viewport );
	backendView.viewport.x1 = static_cast< short >( viewport[ 0 ] );
	backendView.viewport.y1 = static_cast< short >( viewport[ 1 ] );
	backendView.viewport.x2 = static_cast< short >( viewport[ 0 ] + viewport[ 2 ] - 1 );
	backendView.viewport.y2 = static_cast< short >( viewport[ 1 ] + viewport[ 3 ] - 1 );
	backendView.viewport.zmin = 0.0f;
	backendView.viewport.zmax = 1.0f;
	SetFullScreenRect( backendView.scissor );

	viewEntity_s* worldSpace = AllocViewEntity( NULL, NULL, -1 );
	backendView.worldSpace = *worldSpace;
	backendView.viewEntities = worldSpace;
	viewEntity_s* lastSpace = worldSpace;

	for ( int modelIndex = 0; modelIndex < renderWorld->BackendNumLocalModels(); ++modelIndex ) {
		idRenderModel* model = renderWorld->BackendLocalModel( modelIndex );
		if ( model == NULL || !model->IsStaticWorldModel() ) continue;
		for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex ) {
			const modelSurface_t* modelSurface = model->Surface( surfaceIndex );
			const idMaterial* material = renderView->globalMaterial != NULL ? renderView->globalMaterial : modelSurface->material;
			drawSurf_s* surface = AllocDrawSurface( modelSurface, worldSpace, material, renderView->shaderParms );
			if ( surface != NULL ) sortedDrawSurfaces.Append( surface );
		}
	}

	for ( int entityIndex = 0; entityIndex < renderWorld->BackendNumEntityDefs(); ++entityIndex ) {
		renderEntity_t* entity = renderWorld->BackendEntityDef( entityIndex );
		if ( entity == NULL || entity->hModel == NULL ) continue;
		if ( entity->suppressSurfaceInViewID != 0 && entity->suppressSurfaceInViewID == renderView->viewID ) continue;
		if ( entity->allowSurfaceInViewID != 0 && entity->allowSurfaceInViewID != renderView->viewID ) continue;
		viewEntity_s* space = AllocViewEntity( entity, entity->hModel, entityIndex );
		lastSpace->next = space;
		lastSpace = space;
		for ( int surfaceIndex = 0; surfaceIndex < entity->hModel->NumSurfaces(); ++surfaceIndex ) {
			const modelSurface_t* modelSurface = entity->hModel->Surface( surfaceIndex );
			if ( modelSurface == NULL ) continue;
			if ( modelSurface->id >= 0 && modelSurface->id < MAX_SURFACE_BITS - 1 && entity->hideSurfaceMask.Get( modelSurface->id ) != 0 ) continue;
			const idMaterial* material = renderView->globalMaterial != NULL ? renderView->globalMaterial :
				( entity->customShader != NULL ? entity->customShader : modelSurface->material );
			drawSurf_s* surface = AllocDrawSurface( modelSurface, space, material, entity->shaderParms );
			if ( surface != NULL ) sortedDrawSurfaces.Append( surface );
		}
	}
	sortedDrawSurfaces.Sort( DrawSurfaceSortCompare );
	backendView.drawSurfs = sortedDrawSurfaces.Begin();
	backendView.numDrawSurfs = sortedDrawSurfaces.Num();

	viewLight_s* lastLight = NULL;
	for ( int lightIndex = 0; lightIndex < renderWorld->BackendNumLightDefs(); ++lightIndex ) {
		renderLight_t* light = renderWorld->BackendLightDef( lightIndex );
		if ( light == NULL ) continue;
		if ( light->suppressLightInViewID != 0 && light->suppressLightInViewID == renderView->viewID ) continue;
		if ( light->allowLightInViewID != 0 && light->allowLightInViewID != renderView->viewID ) continue;
		viewLight_s* vLight = new viewLight_s;
		memset( vLight, 0, sizeof( *vLight ) );
		vLight->lightDef = light;
		vLight->lightIndex = lightIndex;
		vLight->occlusionIndex = 0;
		R_DeriveLightData( *light, vLight->lightProject, vLight->globalLightOrigin, vLight->material, vLight->falloffImage );
		vLight->globalLightDirection = light->flags.parallel ? light->lightCenter : light->axis[ 0 ];
		vLight->globalLightDirection.Normalize();
		vLight->lightRadius = light->lightRadius;
		vLight->lightRadiusLength = light->lightRadius.Length();
		vLight->fogPlane = vLight->lightProject[ 3 ];
		vLight->fadeFraction = 1.0f;
		SetFullScreenRect( vLight->scissorRect );
		if ( vLight->material != NULL ) {
			vLight->lightRegisters = new float[ Max( vLight->material->GetNumRegisters(), 1 ) ];
			vLight->material->EvaluateRegisters( vLight->lightRegisters, light->shaderParms, &backendView, light->referenceSound, 0 );
			allocatedRegisters.Append( vLight->lightRegisters );
		}
		for ( int surfaceIndex = 0; surfaceIndex < sortedDrawSurfaces.Num(); ++surfaceIndex ) {
			drawSurf_s* surface = sortedDrawSurfaces[ surfaceIndex ];
			if ( !SurfaceIntersectsLight( surface, light ) ) continue;
			if ( !light->flags.noShadows && vLight->material != NULL && vLight->material->LightCastsShadows() &&
					surface->material->SurfaceCastsShadow() && surface->geo != NULL &&
					( surface->geo->shadowCache != NULL || surface->geo->shadowVertexes != NULL ) ) {
				drawSurf_s* shadow = AllocInteractionSurface( surface );
				if ( surface->space != NULL && surface->space->entityDef != NULL ) {
					shadow->nextOnLight = vLight->localShadows;
					vLight->localShadows = shadow;
				} else {
					shadow->nextOnLight = vLight->globalShadows;
					vLight->globalShadows = shadow;
				}
			}
			if ( vLight->material != NULL && ( vLight->material->IsFogLight() || vLight->material->IsBlendLight() ) ) {
				if ( !surface->material->ReceivesFog() ) continue;
			} else if ( !surface->material->ReceivesLighting() ) {
				continue;
			}
			drawSurf_s* interaction = AllocInteractionSurface( surface );
			if ( surface->material->Coverage() == MC_TRANSLUCENT ) {
				interaction->nextOnLight = vLight->translucentInteractions;
				vLight->translucentInteractions = interaction;
			} else {
				interaction->nextOnLight = vLight->globalInteractions;
				vLight->globalInteractions = interaction;
			}
			if ( surface->material->TestMaterialFlag( MF_HASMEGA ) ) {
				drawSurf_s* megaInteraction = AllocInteractionSurface( surface );
				megaInteraction->nextOnLight = vLight->mtInteractions;
				vLight->mtInteractions = megaInteraction;
				if ( light->flags.atmosphereLight && r_megaDrawMethod.GetInteger() != 0 ) {
					// The terrain/mega interaction is consumed by R_DrawMTInteractions;
					// it must not also enter the ordinary light chain.
					if ( vLight->globalInteractions == interaction ) vLight->globalInteractions = interaction->nextOnLight;
					if ( vLight->translucentInteractions == interaction ) vLight->translucentInteractions = interaction->nextOnLight;
				}
			}
		}
		allocatedViewLights.Append( vLight );
		if ( lastLight != NULL ) lastLight->next = vLight;
		else backendView.viewLights = vLight;
		lastLight = vLight;
		if ( light->flags.atmosphereLight ) backendView.atmosphereLight = vLight;
	}
#endif
}
#endif

namespace {
	void FinishDrawPhase() {
		if ( r_forceGLFinish.GetInteger() > 1 ) glFinish();
	}
}

void RB_BakeTextureMatrixIntoTexgenAligned( idPlane outLightProject[ 2 ], const idPlane inLightProject[ 3 ], const float textureMatrix[ 16 ] ) {
	for ( int component = 0; component < 4; ++component ) {
		outLightProject[ 0 ][ component ] =
			inLightProject[ 0 ][ component ] * textureMatrix[ 0 ] +
			inLightProject[ 1 ][ component ] * textureMatrix[ 4 ] +
			inLightProject[ 2 ][ component ] * textureMatrix[ 12 ];
		outLightProject[ 1 ][ component ] =
			inLightProject[ 0 ][ component ] * textureMatrix[ 1 ] +
			inLightProject[ 1 ][ component ] * textureMatrix[ 5 ] +
			inLightProject[ 2 ][ component ] * textureMatrix[ 13 ];
	}
}

void RB_STD_DrawView() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || view->renderWorld == NULL ) {
		return;
	}

	if ( r_forceGLFinish.GetInteger() != 0 ) glFinish();
	RB_BeginDrawingView();
	RB_ARB2_BeginViewFrame();
	RB_DetermineLightScale();
	RB_SetConstantRenderBindings();
	FinishDrawPhase();
	R_PrevFrameOcclusionSystemUpdateViewEnts();

	if ( r_depthFill.GetInteger() != 0 && view->viewEntities != NULL ) {
		RB_ARB2_DrawDepth();
		FinishDrawPhase();
	}

	R_DrawMTInteractions();
	// Retail executes this for normal views when r_depthFill < 2.  Besides
	// filling depth, it is the pass that selects each interaction program's
	// ambient-lighting alternate and supplies the area's ambient cube maps.
	if ( r_depthFill.GetInteger() < 2 || view->viewEntities == NULL ) {
		R_FillDepthAmbient();
		FinishDrawPhase();
	}

	// Soft particles and the atmosphere post process sample the opaque scene
	// depth.  Retail copies this immediately after the depth/ambient fill and
	// before issuing the previous-frame occlusion bounds.
	if ( ( r_softParticles.GetBool() || r_AtmospherePostprocess.GetBool() ) &&
			view->viewEntities != NULL &&
			globalImages != NULL && globalImages->currentDepthImage != NULL ) {
		const int width = view->viewport.x2 - view->viewport.x1 + 1;
		const int height = view->viewport.y2 - view->viewport.y1 + 1;
		glScissor(
			view->viewport.x1 + view->scissor.x1,
			view->viewport.y1 + view->scissor.y1,
			view->scissor.x2 - view->scissor.x1 + 1,
			view->scissor.y2 - view->scissor.y1 + 1
		);
		globalImages->currentDepthImage->CopyDepthbuffer( 0, 0, width, height );
	}

	R_PrevFrameOcclusionSystem();
	FinishDrawPhase();
	RB_ARB2_FogLights( LS_AMBIENTOCCLUSION );
	FinishDrawPhase();
	const bool sampleCoverage = glConfig.samples > 0 && r_useSampleCoverage.GetBool() && qglSampleCoverageARB != NULL;
	if ( sampleCoverage ) {
		glEnable( GL_SAMPLE_COVERAGE_ARB );
		qglSampleCoverageARB( 1.0f, GL_FALSE );
	}
	RB_ARB2_DrawInteractions();
	FinishDrawPhase();
	if ( sampleCoverage ) glDisable( GL_SAMPLE_COVERAGE_ARB );
	RB_ARB2_FogLights( LS_REFRACTABLE );
	FinishDrawPhase();
	int shaderIndex = RB_ARB2_DrawShaderPasses( view->drawSurfs, view->numDrawSurfs, SS_REFRACTION );
	FinishDrawPhase();
	shaderIndex += RB_ARB2_DrawShaderPasses( view->drawSurfs + shaderIndex,
		view->numDrawSurfs - shaderIndex, SS_ATMOSPHERE );
	FinishDrawPhase();
	RB_ARB2_FogLights( LS_EFFECT );
	FinishDrawPhase();
	RB_ARB2_DrawAtmosphere( view->drawSurfs, view->numDrawSurfs );
	FinishDrawPhase();
	shaderIndex += RB_ARB2_DrawShaderPasses( view->drawSurfs + shaderIndex,
		view->numDrawSurfs - shaderIndex, SS_POST_PROCESS );
	FinishDrawPhase();
	if ( !view->isSubview ) {
		RB_ARB2_ResetPostProcessingFrameBuffer();
		RB_ARB2_DrawShaderPasses( view->drawSurfs + shaderIndex,
			view->numDrawSurfs - shaderIndex, SS_LAST );
		FinishDrawPhase();
	}

	GL_EnableVertexAttribs( 0 );
	if ( qglActiveTextureARB != NULL ) {
		qglActiveTextureARB( GL_TEXTURE0_ARB );
	}
	RB_RenderDebugTools( view->drawSurfs, view->numDrawSurfs );
	R_TimerFrame();
	FinishDrawPhase();
	R_RetirePrevFrameOcclusionSystem();
	FinishDrawPhase();
	if ( r_forceGLFinish.GetInteger() != 0 ) glFinish();
}

// Retail PDB length is one byte: shadow-map subviews are dispatched by the
// light interaction path and this standard-backend hook is intentionally empty.
void RB_STD_DrawShadowView() {
}
