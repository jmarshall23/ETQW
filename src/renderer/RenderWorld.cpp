// Copyright (C) 2007 Id Software, Inc.
//
// ETQW render-world implementation reconstructed under the retail PDB path.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderWorld_local.h"
#include "RendererMetrics.h"
#include "draw_local.h"
#include "Image.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "RenderSystemBackend.h"
#include "GuiModel.h"
#include "DeviceContext.h"
#include "tr_render.h"
#include "../decllib/declSkin.h"
#include "../decllib/declTypeHolder.h"
#include "../decllib/declAmbientCubeMap.h"
#include "../bse/BSE.h"
#include "../sys/sys_render.h"

#include <GL/gl.h>

struct bseEffectState_t {
	bseEffectState_t() : decl( NULL ), model( NULL ), startTime( 0 ), stopTime( 0.0f ), stopped( false ) {
		memset( &entity, 0, sizeof( entity ) );
		entity.axis.Identity();
	}

	rvBSE simulation;
	const rvDeclEffect* decl;
	idRenderModel* model;
	renderEntity_t entity;
	int startTime;
	float stopTime;
	bool stopped;
};

namespace {

void FreeEffectState( bseEffectState_t*& state ) {
	if ( state == NULL ) return;
	if ( state->model != NULL && renderModelManager != NULL ) {
		renderModelManager->FreeModel( state->model );
	}
	delete state;
	state = NULL;
}

bool BSE_SampleModelSurface( const char* modelName, idRandom& random, idVec3& point, idVec3& normal ) {
	idRenderModel* model = renderModelManager != NULL ? renderModelManager->CheckModel( modelName ) : NULL;
	if ( model == NULL || model->IsDefaultModel() || model->IsDynamicModel() != DM_STATIC ) return false;

	int triangleCount = 0;
	for ( int i = 0; i < model->NumSurfaces(); i++ ) {
		const modelSurface_t* surface = model->Surface( i );
		if ( surface != NULL && surface->geometry != NULL && surface->geometry->verts != NULL ) {
			triangleCount += surface->geometry->numIndexes / 3;
		}
	}
	if ( triangleCount <= 0 ) return false;

	int selected = random.RandomInt( triangleCount );
	for ( int i = 0; i < model->NumSurfaces(); i++ ) {
		const modelSurface_t* surface = model->Surface( i );
		if ( surface == NULL || surface->geometry == NULL || surface->geometry->verts == NULL ) continue;
		const srfTriangles_t* tri = surface->geometry;
		const int surfaceTriangles = tri->numIndexes / 3;
		if ( selected >= surfaceTriangles ) {
			selected -= surfaceTriangles;
			continue;
		}
		const idDrawVert& a = tri->verts[tri->indexes[selected * 3 + 0]];
		const idDrawVert& b = tri->verts[tri->indexes[selected * 3 + 1]];
		const idDrawVert& c = tri->verts[tri->indexes[selected * 3 + 2]];
		const float root = idMath::Sqrt( random.RandomFloat() );
		const float baryA = 1.0f - root;
		const float baryB = root * ( 1.0f - random.RandomFloat() );
		const float baryC = 1.0f - baryA - baryB;
		point = a.xyz * baryA + b.xyz * baryB + c.xyz * baryC;
		normal = a.GetNormal() * baryA + b.GetNormal() * baryB + c.GetNormal() * baryC;
		if ( normal.Normalize() == 0.0f ) {
			normal.Cross( b.xyz - a.xyz, c.xyz - a.xyz );
			normal.Normalize();
		}
		return true;
	}
	return false;
}

template< class T >
qhandle_t AddDefinition( idList< T* >& definitions, const T* value ) {
	if ( value == NULL ) {
		return -1;
	}
	for ( int i = 0; i < definitions.Num(); i++ ) {
		if ( definitions[ i ] == NULL ) {
			definitions[ i ] = new T( *value );
			return i;
		}
	}
	definitions.Append( new T( *value ) );
	return definitions.Num() - 1;
}

template< class T >
void UpdateDefinition( idList< T* >& definitions, qhandle_t handle, const T* value ) {
	if ( value == NULL || handle < 0 || handle >= definitions.Num() || definitions[ handle ] == NULL ) {
		return;
	}
	*definitions[ handle ] = *value;
}

template< class T >
void FreeDefinition( idList< T* >& definitions, qhandle_t handle ) {
	if ( handle < 0 || handle >= definitions.Num() ) {
		return;
	}
	delete definitions[ handle ];
	definitions[ handle ] = NULL;
}

template< class T >
void ClearDefinitions( idList< T* >& definitions ) {
	for ( int i = 0; i < definitions.Num(); i++ ) {
		delete definitions[ i ];
	}
	definitions.Clear();
}

template< class T >
T* DefinitionForHandle( idList< T* >& definitions, qhandle_t handle ) {
	if ( handle < 0 || handle >= definitions.Num() ) {
		return NULL;
	}
	return definitions[ handle ];
}

template< class T >
const T* DefinitionForHandle( const idList< T* >& definitions, qhandle_t handle ) {
	if ( handle < 0 || handle >= definitions.Num() ) {
		return NULL;
	}
	return definitions[ handle ];
}

}

idRenderWorldLocal::idRenderWorldLocal() :
	hasRenderView( false ),
	areaPortalFlags( 0 ),
	ambientCubeMap( NULL ),
	atmosphere( NULL ),
	writeDemo( NULL ),
	megaTextureSTGridWidth( 0 ),
	megaTextureSTGridHeight( 0 ) {
	memset( &currentRenderView, 0, sizeof( currentRenderView ) );
	megaTextureBounds.Zero();
	portalAreas.SetNum( 1 );
}

idRenderWorldLocal::~idRenderWorldLocal() {
	Clear();
}

void idRenderWorldLocal::Clear() {
	for ( int i = 0; i < localModels.Num(); ++i ) {
		if ( localModels[ i ] != NULL && renderModelManager != NULL ) {
			renderModelManager->FreeModel( localModels[ i ] );
		}
	}
	localModels.Clear();
	for ( int i = 0; i < entityDynamicModels.Num(); ++i ) {
		delete entityDynamicModels[ i ];
	}
	entityDynamicModels.Clear();
	entityDynamicModelSources.Clear();
	ClearDefinitions( entityDefs );
	ClearDefinitions( lightDefs );
	for ( int i = 0; i < effectStates.Num(); i++ ) FreeEffectState( effectStates[i] );
	effectStates.Clear();
	ClearDefinitions( effectDefs );
	ClearDefinitions( occlusionTests );
	stoppedEffects.Clear();
	mapName.Clear();
	hasRenderView = false;
	areaPortalFlags = 0;
	ambientCubeMap = NULL;
	portalAreas.Clear();
	areaNodes.Clear();
	portalAreas.SetNum( 1 );
	atmosphere = NULL;
	writeDemo = NULL;
	megaTextureBounds.Zero();
	megaTextureSTGrid.Clear();
	megaTextureSTGridWidth = 0;
	megaTextureSTGridHeight = 0;
}

qhandle_t idRenderWorldLocal::AddEntityDef( const renderEntity_t *re ) {
	const qhandle_t handle = AddDefinition( entityDefs, re );
	if ( handle >= 0 ) {
		while ( entityDynamicModels.Num() <= handle ) {
			entityDynamicModels.Append( NULL );
			entityDynamicModelSources.Append( NULL );
		}
		delete entityDynamicModels[ handle ];
		entityDynamicModels[ handle ] = NULL;
		entityDynamicModelSources[ handle ] = NULL;
	}
	return handle;
}

void idRenderWorldLocal::UpdateEntityDef( qhandle_t handle, const renderEntity_t *re ) {
	UpdateDefinition( entityDefs, handle, re );
}

void idRenderWorldLocal::FreeEntityDef( qhandle_t handle ) {
	if ( handle >= 0 && handle < entityDynamicModels.Num() ) {
		delete entityDynamicModels[ handle ];
		entityDynamicModels[ handle ] = NULL;
		entityDynamicModelSources[ handle ] = NULL;
	}
	FreeDefinition( entityDefs, handle );
}

idRenderModel* idRenderWorldLocal::BackendInstantiateDynamicModel( int index,
	idRenderModel* sourceModel, const renderEntity_t* entity ) {
	if ( index < 0 || sourceModel == NULL ) {
		return sourceModel;
	}
	while ( entityDynamicModels.Num() <= index ) {
		entityDynamicModels.Append( NULL );
		entityDynamicModelSources.Append( NULL );
	}
	if ( entityDynamicModelSources[ index ] != sourceModel ) {
		delete entityDynamicModels[ index ];
		entityDynamicModels[ index ] = NULL;
		entityDynamicModelSources[ index ] = sourceModel;
	}
	idRenderModel* result = R_InstantiateDynamicModel( sourceModel, entity,
		entityDynamicModels[ index ] );
	entityDynamicModels[ index ] = result != sourceModel ? result : NULL;
	return result;
}

const renderEntity_t *idRenderWorldLocal::GetRenderEntity( qhandle_t handle ) const {
	return DefinitionForHandle( entityDefs, handle );
}

renderEntity_t *idRenderWorldLocal::GetRenderEntity( qhandle_t handle ) {
	return DefinitionForHandle( entityDefs, handle );
}

qhandle_t idRenderWorldLocal::AddLightDef( const renderLight_t *light ) {
	return AddDefinition( lightDefs, light );
}

void idRenderWorldLocal::UpdateLightDef( qhandle_t handle, const renderLight_t *light ) {
	UpdateDefinition( lightDefs, handle, light );
}

void idRenderWorldLocal::FreeLightDef( qhandle_t handle ) {
	FreeDefinition( lightDefs, handle );
}

const renderLight_t *idRenderWorldLocal::GetRenderLight( qhandle_t handle ) const {
	return DefinitionForHandle( lightDefs, handle );
}

qhandle_t idRenderWorldLocal::AddEffectDef( const renderEffect_t *effect, int time ) {
	const qhandle_t handle = AddDefinition( effectDefs, effect );
	if ( handle >= 0 ) {
		while ( effectStates.Num() <= handle ) effectStates.Append( NULL );
		FreeEffectState( effectStates[handle] );
		effectStates[handle] = new bseEffectState_t;
		effectStates[handle]->startTime = time;
		effectStates[handle]->decl = effect->declEffect;
		effectStates[handle]->simulation.Init( effect->declEffect );
		while ( stoppedEffects.Num() <= handle ) {
			stoppedEffects.Append( false );
		}
		stoppedEffects[ handle ] = false;
	}
	return handle;
}

bool idRenderWorldLocal::UpdateEffectDef( qhandle_t handle, const renderEffect_t *effect, int time ) {
	if ( DefinitionForHandle( effectDefs, handle ) == NULL || effect == NULL ) {
		return false;
	}
	UpdateDefinition( effectDefs, handle, effect );
	if ( handle >= effectStates.Num() || effectStates[handle] == NULL ) return false;
	bseEffectState_t* state = effectStates[handle];
	if ( state->decl != effect->declEffect ) {
		state->decl = effect->declEffect;
		state->simulation.Init( effect->declEffect );
	}
	if ( effect->declEffect == NULL || effect->loop ) return false;
	const float effectTime = time * 0.001f + effect->shaderParms[SHADERPARM_TIMEOFFSET];
	const float duration = effect->declEffect->GetMaxDuration();
	if ( duration <= 0.0f ) return false;
	if ( state->stopped ) {
		return state->stopTime > 0.0f && effectTime > state->stopTime + duration;
	}
	return effectTime > duration;
}

void idRenderWorldLocal::StopEffectDef( qhandle_t handle ) {
	if ( handle >= 0 && handle < stoppedEffects.Num() ) {
		stoppedEffects[ handle ] = true;
		if ( handle < effectStates.Num() && effectStates[handle] != NULL ) {
			bseEffectState_t* state = effectStates[handle];
			state->stopped = true;
			const renderEffect_t* effect = DefinitionForHandle( effectDefs, handle );
			state->stopTime = effect != NULL && hasRenderView ?
				Max( 0.0001f, currentRenderView.time * 0.001f + effect->shaderParms[SHADERPARM_TIMEOFFSET] ) : -1.0f;
		}
	}
}

void idRenderWorldLocal::RestartEffectDef( qhandle_t handle ) {
	if ( handle >= 0 && handle < stoppedEffects.Num() ) {
		stoppedEffects[ handle ] = false;
		if ( handle < effectStates.Num() && effectStates[handle] != NULL ) {
			effectStates[handle]->stopped = false;
			effectStates[handle]->stopTime = 0.0f;
		}
	}
}

void idRenderWorldLocal::FreeEffectDef( qhandle_t handle ) {
	FreeDefinition( effectDefs, handle );
	if ( handle >= 0 && handle < effectStates.Num() ) FreeEffectState( effectStates[handle] );
	if ( handle >= 0 && handle < stoppedEffects.Num() ) {
		stoppedEffects[ handle ] = false;
	}
}

void idRenderWorldLocal::FreeStoppedEffectDefs() {
	for ( int i = 0; i < stoppedEffects.Num(); i++ ) {
		if ( stoppedEffects[ i ] ) {
			FreeEffectDef( i );
		}
	}
}

void idRenderWorldLocal::BackendPrepareEffects( const renderView_t* renderView ) {
	if ( renderView == NULL ) return;
	for ( int i = 0; i < effectStates.Num(); i++ ) {
		bseEffectState_t* state = effectStates[i];
		renderEffect_t* effect = i < effectDefs.Num() ? effectDefs[i] : NULL;
		if ( state == NULL ) continue;
		state->entity.hModel = NULL;
		if ( effect == NULL || effect->declEffect == NULL || renderView->time < state->startTime ) continue;
		if ( effect->suppressSurfaceInViewID != 0 && effect->suppressSurfaceInViewID == renderView->viewID ) continue;
		if ( effect->allowSurfaceInViewID != 0 && effect->allowSurfaceInViewID != renderView->viewID ) continue;

		const float cutOffDistance = effect->maxVisDist > 0.0f ? effect->maxVisDist : effect->declEffect->GetCutOffDistance();
		if ( cutOffDistance > 0.0f && ( renderView->vieworg - effect->origin ).LengthSqr() > cutOffDistance * cutOffDistance ) continue;

		if ( state->decl != effect->declEffect ) {
			state->decl = effect->declEffect;
			state->simulation.Init( effect->declEffect );
		}
		if ( state->model == NULL ) {
			if ( renderModelManager == NULL ) continue;
			state->model = renderModelManager->AllocModel();
			if ( state->model == NULL ) continue;
		}

		const idMat3 inverseAxis = effect->axis.Transpose();
		rvBSEOwner owner;
		memset( &owner, 0, sizeof( owner ) );
		owner.time = renderView->time * 0.001f + effect->shaderParms[SHADERPARM_TIMEOFFSET];
		if ( owner.time < 0.0f ) continue;
		owner.stopTime = state->stopped ? state->stopTime : 0.0f;
		if ( state->stopped && owner.stopTime < 0.0f ) {
			state->stopTime = owner.stopTime = Max( 0.0001f, owner.time );
		}
		owner.diversity = effect->shaderParms[SHADERPARM_DIVERSITY];
		owner.brightness = effect->shaderParms[SHADERPARM_BRIGHTNESS];
		if ( owner.brightness == 0.0f ) owner.brightness = 1.0f;
		owner.attenuation = effect->attenuation == 0.0f ? 1.0f : effect->attenuation;
		owner.color.Set( effect->shaderParms[SHADERPARM_RED], effect->shaderParms[SHADERPARM_GREEN],
			effect->shaderParms[SHADERPARM_BLUE], effect->shaderParms[SHADERPARM_ALPHA] );
		if ( owner.color.x == 0.0f && owner.color.y == 0.0f && owner.color.z == 0.0f && owner.color.w == 0.0f ) owner.color.Set( 1.0f, 1.0f, 1.0f, 1.0f );
		owner.materialColor.Set( effect->materialColor.x, effect->materialColor.y, effect->materialColor.z, owner.color.w );
		if ( effect->materialColor.LengthSqr() == 0.0f ) owner.materialColor = owner.color;
		const idVec3 worldGravity = effect->gravity.LengthSqr() > 0.0f ? effect->gravity : idVec3( 0.0f, 0.0f, -1066.0f );
		owner.gravity = worldGravity * inverseAxis;
		owner.wind = effect->windVector * inverseAxis;
		owner.hasEndOrigin = effect->hasEndOrigin;
		owner.endOrigin = effect->hasEndOrigin ? ( effect->endOrigin - effect->origin ) * inverseAxis : vec3_origin;
		owner.viewOrigin = ( renderView->vieworg - effect->origin ) * inverseAxis;
		owner.modelSampler = BSE_SampleModelSurface;

		idVec3 viewRight = -( renderView->viewaxis[1] * inverseAxis );
		idVec3 viewUp = renderView->viewaxis[2] * inverseAxis;
		viewRight.Normalize();
		viewUp.Normalize();
		idList<rvBSEParticle> particles;
		{
			RENDER_METRIC_SCOPE( "BSE simulate" );
			state->simulation.Service( owner, particles );
		}
		idList<rvBSEParticle> renderParticles;
		{
			RENDER_METRIC_SCOPE( "BSE prepare particles" );
			if ( bse != NULL ) bse->PrepareRender( owner, particles, renderParticles );
			else renderParticles = particles;
		}
		{
			RENDER_METRIC_SCOPE( "BSE build geometry" );
			BSE_BuildRenderModel( state->model, va( "_BSEEffect_%d", i ), renderParticles, owner,
				owner.viewOrigin, viewRight, viewUp );
		}
		if ( state->model->NumSurfaces() <= 0 ) continue;

		renderEntity_t& entity = state->entity;
		memset( &entity, 0, sizeof( entity ) );
		entity.hModel = state->model;
		entity.spawnID = -1;
		entity.mapId = -1;
		entity.bounds = state->model->Bounds( &entity );
		entity.suppressSurfaceInViewID = effect->suppressSurfaceInViewID;
		entity.allowSurfaceInViewID = effect->allowSurfaceInViewID;
		entity.origin = effect->origin;
		entity.axis = effect->axis;
		entity.modelDepthHack = effect->modelDepthHack;
		entity.foliageDepthHack = effect->foliageDepthHack;
		entity.sortOffset = effect->distanceOffset;
		entity.flags.noShadow = true;
		entity.flags.noDynamicInteractions = true;
		entity.flags.weaponDepthHack = effect->weaponDepthHackInViewID == renderView->viewID;
		entity.weaponDepthHackFOV_x = effect->weaponDepthHackFOV_x;
		entity.weaponDepthHackFOV_y = effect->weaponDepthHackFOV_y;
		entity.maxVisDist = cutOffDistance > 0.0f ? idMath::Ftoi( cutOffDistance ) : 0;
		memcpy( entity.shaderParms, effect->shaderParms, sizeof( entity.shaderParms ) );
	}
}

int idRenderWorldLocal::BackendNumPreparedEffects() const {
	return effectStates.Num();
}

renderEntity_t* idRenderWorldLocal::BackendPreparedEffect( int index ) const {
	if ( index < 0 || index >= effectStates.Num() || effectStates[index] == NULL ) return NULL;
	return effectStates[index]->entity.hModel != NULL ? &effectStates[index]->entity : NULL;
}

qhandle_t idRenderWorldLocal::AddOcclusionTestDef( const occlusionTest_t *test ) {
	return AddDefinition( occlusionTests, test );
}

void idRenderWorldLocal::UpdateOcclusionTestDef( qhandle_t handle, const occlusionTest_t *test ) {
	UpdateDefinition( occlusionTests, handle, test );
}

void idRenderWorldLocal::UpdateOcclusionTestDefViewID( qhandle_t handle, int viewID ) {
	occlusionTest_t* test = DefinitionForHandle( occlusionTests, handle );
	if ( test != NULL ) {
		test->view = viewID;
	}
}

bool idRenderWorldLocal::IsVisibleOcclusionTestDef( qhandle_t handle ) {
	if ( DefinitionForHandle( occlusionTests, handle ) == NULL ) return false;
	return R_IsVisibleOcclusionSystem( currentRenderView.viewID, handle ) != 0;
}

void idRenderWorldLocal::FreeOcclusionTestDef( qhandle_t handle ) {
	FreeDefinition( occlusionTests, handle );
}

int idRenderWorldLocal::CountVisibleOcclusionTestDef( qhandle_t handle ) {
	if ( DefinitionForHandle( occlusionTests, handle ) == NULL ) return 0;
	return R_CountVisibleOcclusionSystem( currentRenderView.viewID, handle );
}

bool idRenderWorldLocal::IsVisibleEntity( int viewID, int occid ) {
	return GetRenderEntity( occid ) != NULL && R_IsVisibleEntity( viewID, occid );
}

void idRenderWorldLocal::UpdateOcclusionTests() {
	R_RetirePrevFrameOcclusionSystem();
}

void idRenderWorldLocal::GenerateAllInteractions() {
}

idRenderModel *idRenderWorldLocal::GetEntityHandleDynamicModel( qhandle_t handle ) {
	renderEntity_t* entity = GetRenderEntity( handle );
	return entity != NULL ? entity->hModel : NULL;
}

idRenderModel *idRenderWorldLocal::CreateDecalModel() {
	return NULL;
}

void idRenderWorldLocal::AddToProjectedDecal( const idFixedWinding&, const idVec3&, const bool, const idVec4&, idRenderModel*, int, const idMaterial**, const int ) {
}

void idRenderWorldLocal::ResetDecalModel( idRenderModel* ) {
}

void idRenderWorldLocal::FinishDecal( idRenderModel* ) {
}

void idRenderWorldLocal::ProjectDecalOntoWorld( const idFixedWinding&, const idVec3&, const bool, const float, const idMaterial*, const int, const int ) {
}

void idRenderWorldLocal::ProjectDecal( qhandle_t, const idFixedWinding&, const idVec3&, const bool, const float, const idMaterial*, const int, const int ) {
}

void idRenderWorldLocal::ProjectOverlay( qhandle_t, const idPlane[2], const idMaterial* ) {
}

void idRenderWorldLocal::RemoveDecals( qhandle_t ) {
}

void idRenderWorldLocal::AddCheapDecal( qhandle_t, const cheapDecalParameters_t&, float ) {
}

void idRenderWorldLocal::ClearDecals() {
}

void idRenderWorldLocal::AddEnvBounds( idVec3 const&, idVec3 const&, const char* ) {
}

void idRenderWorldLocal::SetRenderView( const renderView_t *renderView ) {
	if ( renderView == NULL ) {
		hasRenderView = false;
		return;
	}
	currentRenderView = *renderView;
	hasRenderView = true;
}

void idRenderWorldLocal::RenderScene( const renderView_t *renderView ) {
	SetRenderView( renderView );
	PerformRenderScene( renderView );
}

void idRenderWorldLocal::PerformRenderScene( const renderView_t *renderView ) {
	RENDER_METRIC_SCOPE( "Render scene" );
	SetRenderView( renderView );
	if ( renderView == NULL || !glConfig.isInitialized || wglGetCurrentContext() == NULL ) {
		return;
	}

	// Retail emits any fullscreen GUI commands accumulated before a nested
	// render world, then resumes GUI collection after the scene.  This is what
	// keeps a renderWorld window's backdrop behind its models instead of being
	// submitted over them at EndFrame.
	{
		RENDER_METRIC_SCOPE( "Pre-scene GUI flush" );
		guiModel.FlushFrame( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );
	}

	idScreenRect viewport;
	renderSystemBackend.RenderViewToViewport( renderView, &viewport );
	const int viewportWidth = Max( 1, viewport.x2 - viewport.x1 + 1 );
	const int viewportHeight = Max( 1, viewport.y2 - viewport.y1 + 1 );

	glPushAttrib( GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT |
		GL_ENABLE_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT | GL_TEXTURE_BIT |
		GL_VIEWPORT_BIT );
	renderSystem->SetDefaultGLState();
	glViewport( viewport.x1, viewport.y1, viewportWidth, viewportHeight );
	glEnable( GL_SCISSOR_TEST );
	glScissor( viewport.x1, viewport.y1, viewportWidth, viewportHeight );
	glClear( GL_DEPTH_BUFFER_BIT );
	glEnable( GL_DEPTH_TEST );
	// Do not issue depth state behind GL_State's back.  RB_SetDefaultGLState
	// records GLS_DEPTHFUNC_ALWAYS in the ETQW back-end cache; changing only
	// the driver to LEQUAL made RB_BeginDrawingView's following
	// GL_State( GLS_DEPTHFUNC_ALWAYS ) look like a cache hit and left the
	// actual GL depth function at LEQUAL.  Drive both depth func and depth mask
	// through the retail state vector so the first material pass starts from a
	// coherent state.
	GL_State( 0x40000 );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_LIGHTING );
	glShadeModel( GL_SMOOTH );

	{
		RENDER_METRIC_SCOPE( "Build draw view" );
		RB_SetDrawViewContext( this, renderView );
	}
	viewDef_s* viewDef = RB_GetViewDef();
	if ( viewDef == NULL ) {
		glPopAttrib();
		return;
	}

	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadMatrixf( viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadMatrixf( viewDef->worldSpace.modelViewMatrix );
	RB_SetImmediateViewState( renderView, viewDef->projectionMatrix, viewportWidth, viewportHeight );

	{
		RENDER_METRIC_SCOPE( "Draw view" );
		RB_STD_DrawView();
	}
	{
		RENDER_METRIC_SCOPE( "Free draw view" );
		RB_ClearDrawViewContext();
	}

	glDepthMask( GL_TRUE );
	RB_ARB2_ClearSpace();
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopAttrib();
	// glPopAttrib restores driver state but cannot restore the renderer's
	// software cache.  Re-establish the retail default state before GUI or a
	// nested render view submits more commands.
	RB_SetDefaultGLState();
}

int idRenderWorldLocal::NumAreas() const {
	return portalAreas.Num();
}

int idRenderWorldLocal::PointInArea( const idVec3& point ) const {
	if ( areaNodes.Num() == 0 ) return -1;
	int nodeNum = 0;
	for ( ;; ) {
		if ( nodeNum < 0 ) {
			const int areaNum = -1 - nodeNum;
			if ( areaNum < 0 || areaNum >= portalAreas.Num() ) {
				common->Error( "idRenderWorld::PointInArea: area out of range" );
			}
			return areaNum;
		}
		if ( nodeNum >= areaNodes.Num() ) return -1;
		const areaNode_t& node = areaNodes[ nodeNum ];
		nodeNum = node.children[ ( point * node.plane.Normal() + node.plane[ 3 ] ) > 0.0f ? 0 : 1 ];
		if ( nodeNum == 0 ) return -1;
	}
}

namespace {
	void BoundsInAreas_r( const idList< areaNode_t >& nodes, int nodeNum, const idBounds& bounds,
			int* areas, int& numAreas, int maxAreas ) {
		while ( nodeNum != 0 && numAreas < maxAreas ) {
			if ( nodeNum < 0 ) {
				const int areaNum = -1 - nodeNum;
				for ( int index = 0; index < numAreas; ++index ) if ( areas[ index ] == areaNum ) return;
				areas[ numAreas++ ] = areaNum;
				return;
			}
			if ( nodeNum >= nodes.Num() ) return;
			const areaNode_t& node = nodes[ nodeNum ];
			const int side = bounds.PlaneSide( node.plane, 0.1f );
			if ( side == PLANESIDE_FRONT ) nodeNum = node.children[ 0 ];
			else if ( side == PLANESIDE_BACK ) nodeNum = node.children[ 1 ];
			else {
				if ( node.children[ 1 ] != 0 ) BoundsInAreas_r( nodes, node.children[ 1 ], bounds, areas, numAreas, maxAreas );
				nodeNum = node.children[ 0 ];
			}
		}
	}
}

int idRenderWorldLocal::BoundsInAreas( const idBounds& bounds, int *areas, int maxAreas ) const {
	if ( areas == NULL || maxAreas <= 0 || areaNodes.Num() == 0 ) return 0;
	idBounds limited = bounds;
	for ( int axis = 0; axis < 3; ++axis ) {
		if ( limited[ 1 ][ axis ] - limited[ 0 ][ axis ] >= 10000.0f ) {
			const float center = ( limited[ 0 ][ axis ] + limited[ 1 ][ axis ] ) * 0.5f;
			limited[ 0 ][ axis ] = center - 10000.0f;
			limited[ 1 ][ axis ] = center + 10000.0f;
		}
	}
	int numAreas = 0;
	BoundsInAreas_r( areaNodes, 0, limited, areas, numAreas, maxAreas );
	return numAreas;
}

int idRenderWorldLocal::NumPortalsInArea( int ) {
	return 0;
}

exitPortal_t idRenderWorldLocal::GetPortal( int, int ) {
	exitPortal_t portal;
	memset( &portal, 0, sizeof( portal ) );
	return portal;
}

void idRenderWorldLocal::SetAreaPortalFlags( int areaNum, int flags ) {
	if ( areaNum < 0 || areaNum >= portalAreas.Num() ) {
		common->Warning( "idRenderWorld::SetAreaPortalFlags: bad areanum %i", areaNum );
		return;
	}
	portalAreas[ areaNum ].portalFlags = flags;
}

int idRenderWorldLocal::GetAreaPortalFlags( int areaNum ) const {
	if ( areaNum < 0 || areaNum >= portalAreas.Num() ) return 0;
	return portalAreas[ areaNum ].portalFlags;
}

void idRenderWorldLocal::SetAreaAmbientCubeMap( int areaNum, const sdDeclAmbientCubeMap *cubeMapDecl ) {
	if ( areaNum < 0 || areaNum >= portalAreas.Num() ) {
		common->Warning( "idRenderWorld::SetAreaAmbientCubeMap: bad areanum %i", areaNum );
		return;
	}
	portalAreas[ areaNum ].ambientCubeMap = cubeMapDecl;
	if ( areaNum == 0 ) ambientCubeMap = cubeMapDecl;
}

const sdDeclAmbientCubeMap *idRenderWorldLocal::GetAreaAmbientCubeMap( int areaNum ) {
	if ( areaNum < 0 || areaNum >= portalAreas.Num() ) return NULL;
	return portalAreas[ areaNum ].ambientCubeMap;
}

const sdDeclAmbientCubeMap* idRenderWorldLocal::BackendAmbientCubeMapForArea( int areaNum ) const {
	if ( areaNum >= 0 && areaNum < portalAreas.Num() && portalAreas[ areaNum ].ambientCubeMap != NULL ) {
		return portalAreas[ areaNum ].ambientCubeMap;
	}
	return ambientCubeMap;
}

const sdDeclAmbientCubeMap* idRenderWorldLocal::BackendAmbientCubeMapForModel( idRenderModel* model ) const {
	if ( model == NULL ) return ambientCubeMap;
	idList< int >* fixedAreas = model->GetFixedAreas();
	const sdDeclAmbientCubeMap* selected = NULL;
	if ( fixedAreas != NULL ) {
		for ( int index = 0; index < fixedAreas->Num(); ++index ) {
			const sdDeclAmbientCubeMap* candidate = BackendAmbientCubeMapForArea( ( *fixedAreas )[ index ] );
			if ( selected == NULL || ( candidate != NULL && !candidate->IsIndoors() ) ) selected = candidate;
		}
	}
	return selected != NULL ? selected : ambientCubeMap;
}

void idRenderWorldLocal::SetCubemapSunProperties( const sdDeclAmbientCubeMap*, const idVec3&, const idVec3& ) {
}

void idRenderWorldLocal::ClearTrace( modelTrace_t &trace, const idVec3 &end ) const {
	memset( &trace, 0, sizeof( trace ) );
	trace.fraction = 1.0f;
	trace.point = end;
}

bool idRenderWorldLocal::ModelTrace( modelTrace_t &trace, qhandle_t, const idVec3&, const idVec3 &end, const float, int ) const {
	ClearTrace( trace, end );
	return false;
}

bool idRenderWorldLocal::Trace( modelTrace_t &trace, const idVec3&, const idVec3 &end, const float, bool ) const {
	ClearTrace( trace, end );
	return false;
}

bool idRenderWorldLocal::FastWorldTrace( modelTrace_t &trace, const idVec3&, const idVec3 &end ) const {
	ClearTrace( trace, end );
	return false;
}

void idRenderWorldLocal::RegenerateWorld() {
}

/*
================
R_GlobalShaderOverride
================
*/
bool R_GlobalShaderOverride( const idMaterial** shader ) {
	if ( shader == NULL || *shader == NULL || !( *shader )->IsDrawn() ) {
		return false;
	}

	const viewDef_s* view = RB_GetViewDef();
	if ( view != NULL && view->renderView.globalMaterial != NULL ) {
		*shader = view->renderView.globalMaterial;
		return true;
	}

	return false;
}

/*
================
R_RemapShaderBySkin
================
*/
const idMaterial* R_RemapShaderBySkin( const idMaterial* shader, const idDeclSkin* skin, const idMaterial* customShader ) {
	if ( shader == NULL ) {
		return NULL;
	}

	const viewDef_s* view = RB_GetViewDef();
	if ( view != NULL && view->renderView.globalSkin != NULL ) {
		skin = view->renderView.globalSkin;
	}

	// Collision hulls and other originally non-drawn surfaces must never be
	// made visible by a skin or custom material.
	if ( !shader->IsDrawn() ) {
		return shader;
	}

	if ( customShader != NULL ) {
		// Do not apply item-highlight materials to autosprites and other deforms.
		return shader->Deform() == DFRM_NONE ? customShader : NULL;
	}

	return skin != NULL ? skin->RemapShaderBySkin( shader ) : shader;
}

void idRenderWorldLocal::DebugClearLines( int ) {}
void idRenderWorldLocal::DebugLine( const idVec4&, const idVec3&, const idVec3&, const int, const bool ) {}
void idRenderWorldLocal::DebugArrow( const idVec4&, const idVec3&, const idVec3&, int, const int, bool ) {}
void idRenderWorldLocal::DebugWinding( const idVec4&, const idWinding&, const idVec3&, const idMat3&, const int, const bool ) {}
void idRenderWorldLocal::DebugCircle( const idVec4&, const idVec3&, const idVec3&, const float, const int, const int, const bool ) {}
void idRenderWorldLocal::DebugSphere( const idVec4&, const idSphere&, const int, const bool ) {}
void idRenderWorldLocal::DebugBounds( const idVec4&, const idBounds&, const idVec3&, const idMat3&, const int ) {}
void idRenderWorldLocal::DebugBox( const idVec4&, const idBox&, const int ) {}
void idRenderWorldLocal::DebugFrustum( const idVec4&, const idFrustum&, const bool, const int ) {}
void idRenderWorldLocal::DebugCone( const idVec4&, const idVec3&, const idVec3&, float, float, const int ) {}
void idRenderWorldLocal::DebugAxis( const idVec3&, const idMat3&, int ) {}
void idRenderWorldLocal::DebugClearPolygons( int ) {}
void idRenderWorldLocal::DebugPolygon( const idVec4&, const idWinding&, const int, const bool, idImage* ) {}
void idRenderWorldLocal::DrawText( const char*, const idVec3&, float, const idVec4&, const idMat3&, const int, const int ) {}

void idRenderWorldLocal::SetAtmosphere( const sdDeclAtmosphere* value ) {
	atmosphere = value;
}

const sdDeclAtmosphere* idRenderWorldLocal::GetAtmosphere() const {
	return atmosphere;
}

void idRenderWorldLocal::SetupMatrices( const renderView_t* renderView, float* projectionMatrix, float* modelViewMatrix, const bool allowJitter ) {
	if ( renderView == NULL ) {
		return;
	}
	viewDef_s view;
	memset( &view, 0, sizeof( view ) );
	view.renderView = *renderView;
	R_SetupMatrices( &view, allowJitter );
	if ( projectionMatrix != NULL ) {
		memcpy( projectionMatrix, view.projectionMatrix, sizeof( view.projectionMatrix ) );
	}
	if ( modelViewMatrix != NULL ) {
		memcpy( modelViewMatrix, view.worldSpace.modelViewMatrix, sizeof( view.worldSpace.modelViewMatrix ) );
	}
}
