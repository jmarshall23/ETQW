// Copyright (C) 2007 Id Software, Inc.
//
// ETQW back-end triangle rendering.  Function ownership and public boundaries
// are reconstructed from tr_render.obj in the retail Microsoft PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_render.h"
#include "draw_local.h"
#include "Image.h"
#include "Material.h"
#include "Model.h"
#include "VertexCache.h"
#include "RenderWorld.h"
#include "RenderWorld_local.h"
#include "GuiModel.h"
#include "renderbindings.h"
#include "renderbindingmanager.h"
#include "megatexture/MegaTexture.h"
#include "../decllib/declAtmosphere.h"
#include "../decllib/declRenderProgram.h"
#include "../decllib/DeclRenderProgram_opengl.h"
#include "../decllib/declRenderBinding.h"
#include "../libs/qglLib/qgl.h"
#include "../sound/SoundEmitter.h"

#include <GL/gl.h>

extern idCVar r_renderProgramLodDistance;
extern idCVar r_renderProgramLodFade;
extern idCVar r_ambientScale;
extern idCVar r_AtmospherePostprocess;
extern idCVar r_lightScale;
extern idCVar r_stuffFadeStart;
extern idCVar r_stuffFadeEnd;
extern idCVar r_elevateForceClear;
extern idCVar r_multiSamples;
extern idCVar r_showOverDraw;
extern idCVar r_singleTriangle;
extern idCVar r_useIndexBuffers;
extern idCVar r_useScissor;
extern idCVar r_znear;
extern idCVar r_skipDynamicTextures;
extern glconfig_t glConfig;

idCVar r_useIndexHier( "r_useIndexHier", "1", CVAR_RENDERER | CVAR_FLOAT, "" );
idCVar r_depthRangeWeaponHackEnd( "r_depthRangeWeaponHackEnd", "0.3", CVAR_RENDERER | CVAR_FLOAT, "" );
idCVar r_depthRangeWeaponHackScale( "r_depthRangeWeaponHackScale", "0.25", CVAR_RENDERER | CVAR_FLOAT, "" );
idCVar r_depthRangeStartDefault( "r_depthRangeStartDefault", "0.0", CVAR_RENDERER | CVAR_FLOAT, "" );

namespace {

struct immediateViewState_t {
	const renderView_t* renderView;
	float projectionMatrix[ 16 ];
	int viewportWidth;
	int viewportHeight;
};

immediateViewState_t immediateViewState = { NULL, { 0.0f }, 0, 0 };
float currentBindingModelMatrix[ 16 ] = {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

idVec3 GlobalVectorToBindingLocal( const idVec3& global ) {
	return idVec3(
		global.x * currentBindingModelMatrix[ 0 ] + global.y * currentBindingModelMatrix[ 1 ] + global.z * currentBindingModelMatrix[ 2 ],
		global.x * currentBindingModelMatrix[ 4 ] + global.y * currentBindingModelMatrix[ 5 ] + global.z * currentBindingModelMatrix[ 6 ],
		global.x * currentBindingModelMatrix[ 8 ] + global.y * currentBindingModelMatrix[ 9 ] + global.z * currentBindingModelMatrix[ 10 ]
	);
}

void RB_RenderScene( const void* data ) {
	if ( data == NULL ) return;
	const void* const* command = static_cast< const void* const* >( data );
	idRenderWorld* renderWorld = const_cast< idRenderWorld* >( static_cast< const idRenderWorld* >( command[ 0 ] ) );
	const renderView_t* renderView = static_cast< const renderView_t* >( command[ 1 ] );
	if ( renderWorld != NULL && renderView != NULL ) {
		renderWorld->PerformRenderScene( renderView );
	}
}

void RB_EmitGuiModel( const void* data ) {
	if ( data == NULL ) return;
	sdGuiModel* model = *static_cast< sdGuiModel* const* >( data );
	if ( model != NULL ) model->EmitFullScreen( -1 );
}

void RB_Evaluator_UpdateCinematicImageYUV( const sdDeclRenderBinding* target ) {
	if ( target == NULL || rbinds == NULL || globalImages == NULL ) return;
	if ( r_skipDynamicTextures.GetBool() ) {
		target->Set( globalImages->defaultImage );
		rbinds->cinematicU->Set( globalImages->defaultImage );
		rbinds->cinematicV->Set( globalImages->defaultImage );
		return;
	}

	const viewEntity_s* space = RB_GetActiveDrawSpace();
	idSoundEmitter* sound = space != NULL && space->entityDef != NULL ? space->entityDef->referenceSound : NULL;
	viewDef_s* view = RB_GetViewDef();
	if ( sound != NULL && sound->CurrentlyPlaying() && view != NULL ) {
		const int milliseconds = static_cast< int >( ( view->floatTime + view->renderView.nearPlane ) * 1000.0f );
		const cinData_t frame = sound->ImageForTime( milliseconds );
		if ( frame.imageWidth > 0 && frame.imageHeight > 0 &&
				frame.image[ 0 ] != NULL && frame.image[ 1 ] != NULL && frame.image[ 2 ] != NULL ) {
			globalImages->cinematicYImage->UploadScratch( frame.image[ 0 ], frame.imageWidth, frame.imageHeight );
			globalImages->cinematicUImage->UploadScratch( frame.image[ 1 ], frame.imageWidth / 2, frame.imageHeight / 2 );
			globalImages->cinematicVImage->UploadScratch( frame.image[ 2 ], frame.imageWidth / 2, frame.imageHeight / 2 );
			target->Set( globalImages->cinematicYImage );
			rbinds->cinematicU->Set( globalImages->cinematicUImage );
			rbinds->cinematicV->Set( globalImages->cinematicVImage );
			return;
		}
	}

	target->Set( globalImages->blackImage );
	rbinds->cinematicU->Set( globalImages->grayImage );
	rbinds->cinematicV->Set( globalImages->grayImage );
}

void Evaluator_ObjectSpaceSundir( const sdDeclRenderBinding* target ) {
	if ( target == NULL || rbinds == NULL || rbinds->sunDirectionWorld == NULL ) return;
	const idVec4 world = rbinds->sunDirectionWorld->GetVec4();
	const idVec3 local = GlobalVectorToBindingLocal( idVec3( world.x, world.y, world.z ) );
	target->Set( local.x, local.y, local.z, 0.0f );
}

idVec3 FogRotationAxis( int axis ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || view->atmosphere == NULL ) return axis == 2 ? idVec3( 0.0f, 0.0f, 1.0f ) : idVec3( 1.0f, 0.0f, 0.0f );
	const float radians = ( 360.0f - view->atmosphere->GetSunAzimuth() + 90.0f ) * idMath::M_DEG2RAD;
	const float cosine = idMath::Cos( radians );
	const float sine = idMath::Sin( radians );
	if ( axis == 0 ) return idVec3( cosine, sine, 0.0f );
	if ( axis == 1 ) return idVec3( -sine, cosine, 0.0f );
	return idVec3( 0.0f, 0.0f, 1.0f );
}

void Evaluator_FogRotation_x( const sdDeclRenderBinding* target ) {
	const idVec3 local = GlobalVectorToBindingLocal( FogRotationAxis( 0 ) );
	target->Set( local.x, local.y, local.z, 0.0f );
}

void Evaluator_FogRotation_y( const sdDeclRenderBinding* target ) {
	const idVec3 local = GlobalVectorToBindingLocal( FogRotationAxis( 1 ) );
	target->Set( local.x, local.y, local.z, 0.0f );
}

void Evaluator_FogRotation_z( const sdDeclRenderBinding* target ) {
	const idVec3 local = GlobalVectorToBindingLocal( FogRotationAxis( 2 ) );
	target->Set( local.x, local.y, local.z, 0.0f );
}

void RB_SetAtmosphereFrameRenderBindings() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || view->atmosphere == NULL || rbinds == NULL ) return;
	const sdDeclAtmosphere* atmosphere = view->atmosphere;

	rbinds->skyGradientCubeMap->Set( atmosphere->GetSkyGradientImage() );
	rbinds->fogRotation_x->SetEvaluator( Evaluator_FogRotation_x );
	rbinds->fogRotation_y->SetEvaluator( Evaluator_FogRotation_y );
	rbinds->fogRotation_z->SetEvaluator( Evaluator_FogRotation_z );
	const idVec3& sunDirection = atmosphere->GetSunDirection();
	rbinds->sunDirectionWorld->Set( sunDirection.x, sunDirection.y, sunDirection.z, 0.0f );
	rbinds->sunDirection->SetEvaluator( Evaluator_ObjectSpaceSundir );
	const idVec3& sunColor = atmosphere->GetSunColor();
	const float lightScale = r_lightScale.GetFloat();
	rbinds->sunColor->Set( sunColor.x * lightScale, sunColor.y * lightScale, sunColor.z * lightScale, 0.0f );
	rbinds->sunHaloParameters->Set( atmosphere->GetSunHaloScale(), atmosphere->GetSunHaloBias(), 0.0f, 0.0f );

	const sdDeclAtmosphere::postProcessParms_t& post = atmosphere->GetPostProcessParms();
	rbinds->postTint->Set( post.tint.x, post.tint.y, post.tint.z, 0.0f );
	rbinds->postSaturationContrast->Set( post.saturation, post.contrast, 0.0f, 0.0f );
	rbinds->postGlareParameters->Set( post.glareParms );

	const float* modelView = view->worldSpace.modelViewMatrix;
	rbinds->fogUpInView->Set( modelView[ 8 ], modelView[ 9 ], modelView[ 10 ], 0.0f );
	const float fogHeightHalf = atmosphere->GetFogHeightHalf();
	const float fogEye = idMath::Pow( 0.5f, Max( 0.0f, view->renderView.vieworg.z + atmosphere->GetFogHeightOffset() ) / fogHeightHalf );
	rbinds->fogEyePrecalc->Set( fogEye );
	rbinds->fogParams->Set( 1.0f / atmosphere->GetFogDistHalf(),
		1.0f / fogHeightHalf, atmosphere->GetFogHeightOffset(), 0.0f );
	const idVec3& fogColor = atmosphere->GetFogColor();
	rbinds->fogColor->Set( fogColor.x, fogColor.y, fogColor.z, 0.0f );
	if ( r_AtmospherePostprocess.GetBool() ) {
		rbinds->fogDepths->Set( 0.0f, 0.0f, 0.0f, 0.0f );
	} else {
		const float fogStart = atmosphere->GetFogStart();
		const float fogEnd = atmosphere->GetFogEnd();
		const float fogRange = fogEnd - fogStart;
		rbinds->fogDepths->Set( fogStart, fogEnd, 1.0f / fogRange, -fogStart / fogRange );
	}

	const float radians = atmosphere->GetSunAzimuth() * idMath::M_DEG2RAD;
	const float cosine = idMath::Cos( radians );
	const float sine = idMath::Sin( radians );
	const idVec4 axisX( modelView[ 0 ], modelView[ 1 ], modelView[ 2 ], view->renderView.vieworg.x );
	const idVec4 axisY( modelView[ 4 ], modelView[ 5 ], modelView[ 6 ], view->renderView.vieworg.y );
	rbinds->fogViewMatrix_x->Set( axisX * cosine + axisY * sine );
	rbinds->fogViewMatrix_y->Set( axisX * -sine + axisY * cosine );
	rbinds->fogViewMatrix_z->Set( modelView[ 8 ], modelView[ 9 ], modelView[ 10 ], view->renderView.vieworg.z );
}

GLenum BlendSourceForBits( int stateBits ) {
	switch ( stateBits & 0x0F ) {
		case 0x01: return GL_ZERO;
		case 0x03: return GL_DST_COLOR;
		case 0x04: return GL_ONE_MINUS_DST_COLOR;
		case 0x05: return GL_SRC_ALPHA;
		case 0x06: return GL_ONE_MINUS_SRC_ALPHA;
		case 0x07: return GL_DST_ALPHA;
		case 0x08: return GL_ONE_MINUS_DST_ALPHA;
		case 0x09: return GL_SRC_ALPHA_SATURATE;
		default: return GL_ONE;
	}
}

GLenum BlendDestinationForBits( int stateBits ) {
	switch ( stateBits & 0xF0 ) {
		case 0x20: return GL_ONE;
		case 0x30: return GL_SRC_COLOR;
		case 0x40: return GL_ONE_MINUS_SRC_COLOR;
		case 0x50: return GL_SRC_ALPHA;
		case 0x60: return GL_ONE_MINUS_SRC_ALPHA;
		case 0x70: return GL_DST_ALPHA;
		case 0x80: return GL_ONE_MINUS_DST_ALPHA;
		default: return GL_ZERO;
	}
}

void SetCompatibilityState( int stateBits, cullType_t cullType ) {
	// The compatibility path shares the same GL state with the programmable
	// draw_* backend.  Use its cache rather than issuing raw calls (especially
	// glDisable(GL_BLEND), because retail keeps blending enabled and represents
	// "no blend" as ONE,ZERO in the state vector).
	GL_State( stateBits );
	GL_Cull( cullType == CT_INVALID ? CT_TWO_SIDED : cullType );
}

idImage* StageColorImage( const materialStage_t* stage ) {
	idImage* fallback = NULL;
	for ( int i = 0; i < stage->numTextures; ++i ) {
		const stageTexture_t& texture = stage->textures[ i ];
		if ( texture.renderBinding == rbinds->diffuseMap || texture.renderBinding == rbinds->map || texture.renderBinding == rbinds->cinematicY ) return texture.image;
		if ( fallback == NULL && texture.renderBinding != rbinds->bumpMap && texture.renderBinding != rbinds->specularMap &&
			texture.renderBinding != rbinds->bumpDetailMap && texture.renderBinding != rbinds->specDetailMap ) fallback = texture.image;
	}
	return fallback;
}

void GetStageColor( idVec4& color ) {
	color.Set( 1.0f, 1.0f, 1.0f, 1.0f );
	if ( rbinds != NULL && rbinds->diffuseColor != NULL ) color = rbinds->diffuseColor->GetVec4();
}

bool IsOverrideTextureBinding( const sdDeclRenderBinding* binding ) {
	return binding == rbinds->map || binding == rbinds->diffuseMap || binding == rbinds->cinematicY;
}

const void* CachePosition( const vertCache_s* cache, bool indexBuffer ) {
	if ( cache == NULL ) return NULL;
	if ( cache->vbo != 0 && qglBindBufferARB != NULL ) {
		qglBindBufferARB( indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB, cache->vbo );
		return reinterpret_cast< const void* >( cache->offset );
	}
	if ( qglBindBufferARB != NULL ) qglBindBufferARB( indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB, 0 );
	return cache->virtMem != NULL ? static_cast< const byte* >( cache->virtMem ) + cache->offset : NULL;
}

GLenum PrimitiveMode( const srfTriangles_t* triangles ) {
	return triangles->mode == PM_POINTSPRITE ? GL_POINTS : GL_TRIANGLES;
}

void SetInstanceAttributes( const sdInstInfo& instance ) {
	if ( qglMultiTexCoord4fARB == NULL ) return;
	const float byteToFloat = 1.0f / 255.0f;
	const sdInstCommon& inst = instance.inst;
	qglMultiTexCoord4fARB( GL_TEXTURE2_ARB,
		inst.color[ 0 ] * byteToFloat, inst.color[ 1 ] * byteToFloat,
		inst.color[ 2 ] * byteToFloat, inst.color[ 3 ] * byteToFloat );
	qglMultiTexCoord4fARB( GL_TEXTURE5_ARB,
		inst.axis[ 0 ].x, inst.axis[ 1 ].x, inst.axis[ 2 ].x, inst.origin.x );
	qglMultiTexCoord4fARB( GL_TEXTURE6_ARB,
		inst.axis[ 0 ].y, inst.axis[ 1 ].y, inst.axis[ 2 ].y, inst.origin.y );
	qglMultiTexCoord4fARB( GL_TEXTURE7_ARB,
		inst.axis[ 0 ].z, inst.axis[ 1 ].z, inst.axis[ 2 ].z, inst.origin.z );
}

}

void RB_SetCurrentBindingSpace( const float modelMatrix[ 16 ] ) {
	if ( modelMatrix != NULL ) memcpy( currentBindingModelMatrix, modelMatrix, sizeof( currentBindingModelMatrix ) );
}

void RB_BeginDrawingView() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( view->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glViewport( view->viewport.x1, view->viewport.y1,
		view->viewport.x2 - view->viewport.x1 + 1, view->viewport.y2 - view->viewport.y1 + 1 );
	glScissor( view->viewport.x1 + view->scissor.x1, view->viewport.y1 + view->scissor.y1,
		view->scissor.x2 - view->scissor.x1 + 1, view->scissor.y2 - view->scissor.y1 + 1 );

	// Keep the OpenGL driver and the renderer's state vector in lockstep.
	// Retail enters every view through GL_State( GLS_DEPTHFUNC_ALWAYS ); raw
	// state calls here make the following material pass eligible for a false
	// cache hit and leave it rendering with the view-clear state instead.
	GL_State( 0x10000 );

	const int forceClear = view->renderView.forceClear;
	const bool clearColor = forceClear > 1 ||
		( forceClear == 1 && ( r_elevateForceClear.GetInteger() > 1 || r_multiSamples.GetInteger() > 1 ) );
	if ( clearColor && r_showOverDraw.GetInteger() == 0 ) {
		glClearColor( view->renderView.clearColor.x, view->renderView.clearColor.y,
			view->renderView.clearColor.z, view->renderView.clearColor.w );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	if ( view->viewEntities != NULL ) {
		glStencilMask( 0xFF );
		glClearStencil( 1 << Max( glConfig.stencilBits - 1, 0 ) );
		if ( view->renderView.farPlane > 0.0f ) {
			float clearDepth;
			R_TransformEyeZToWin( -view->renderView.farPlane, view->projectionMatrix, clearDepth );
			glClearDepth( clearDepth );
		}
		glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		glEnable( GL_DEPTH_TEST );
		if ( view->renderView.farPlane > 0.0f ) glClearDepth( 1.0 );
	} else {
		glClear( GL_DEPTH_BUFFER_BIT );
		glEnable( GL_DEPTH_TEST );
		glDisable( GL_STENCIL_TEST );
	}
	GL_Cull( CT_FRONT_SIDED );
	GL_EnableVertexAttribs( 0 );
	SD_UnbindRenderProgram();
}

void RB_SetImmediateViewState( const renderView_t* renderView, const float projectionMatrix[ 16 ], int viewportWidth, int viewportHeight ) {
	immediateViewState.renderView = renderView;
	immediateViewState.viewportWidth = Max( 1, viewportWidth );
	immediateViewState.viewportHeight = Max( 1, viewportHeight );
	if ( projectionMatrix != NULL ) {
		memcpy( immediateViewState.projectionMatrix, projectionMatrix, sizeof( immediateViewState.projectionMatrix ) );
	} else {
		memset( immediateViewState.projectionMatrix, 0, sizeof( immediateViewState.projectionMatrix ) );
	}
	RB_SetConstantRenderBindings();
}

void RB_SetConstantRenderBindings() {
	const renderView_t* renderView = immediateViewState.renderView;
	const float* projectionMatrix = immediateViewState.projectionMatrix;
	if ( renderView == NULL || rbinds == NULL ) {
		return;
	}

	rbinds->transposedProjectionMatrix_x->Set( projectionMatrix[ 0 ], projectionMatrix[ 4 ], projectionMatrix[ 8 ], projectionMatrix[ 12 ] );
	rbinds->transposedProjectionMatrix_y->Set( projectionMatrix[ 1 ], projectionMatrix[ 5 ], projectionMatrix[ 9 ], projectionMatrix[ 13 ] );
	rbinds->transposedProjectionMatrix_z->Set( projectionMatrix[ 2 ], projectionMatrix[ 6 ], projectionMatrix[ 10 ], projectionMatrix[ 14 ] );
	rbinds->transposedProjectionMatrix_w->Set( projectionMatrix[ 3 ], projectionMatrix[ 7 ], projectionMatrix[ 11 ], projectionMatrix[ 15 ] );
	rbinds->proj2View->Set( projectionMatrix[ 10 ], projectionMatrix[ 14 ], 0.0f, 0.0f );

	const float projectionX = idMath::Fabs( projectionMatrix[ 0 ] ) > 1.0e-6f ? projectionMatrix[ 0 ] : 1.0f;
	const float projectionY = idMath::Fabs( projectionMatrix[ 5 ] ) > 1.0e-6f ? projectionMatrix[ 5 ] : 1.0f;
	rbinds->pos2View->Set(
		-1.0f / projectionX,
		-1.0f / projectionY,
		2.0f / ( immediateViewState.viewportWidth * projectionX ),
		2.0f / ( immediateViewState.viewportHeight * projectionY )
	);

	const float lodFade = r_renderProgramLodFade.GetFloat();
	rbinds->detailFade->Set( r_renderProgramLodDistance.GetFloat() - lodFade, -1.0f / lodFade, 0.0f, 0.0f );

	const float ambientScale = r_ambientScale.GetFloat();
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL && view->atmosphere != NULL ) {
		RB_SetAtmosphereFrameRenderBindings();
		const idVec4& glareBases = view->atmosphere->GetPostProcessParms().glareBases;
		rbinds->ambientScale->Set( ambientScale, glareBases.x, glareBases.y, 1.0f );
	} else {
		rbinds->ambientScale->Set( ambientScale, ambientScale, ambientScale, ambientScale );
	}
	rbinds->viewOriginWorld->Set( renderView->vieworg.x, renderView->vieworg.y, renderView->vieworg.z, 0.0f );
	rbinds->foliageHackDistance->Set( renderView->foliageDepthHack );
	rbinds->viewDirectionWorld->Set( renderView->viewaxis[ 0 ].x, renderView->viewaxis[ 0 ].y, renderView->viewaxis[ 0 ].z, 0.0f );
	rbinds->viewRightWorld->Set( renderView->viewaxis[ 1 ].x, renderView->viewaxis[ 1 ].y, renderView->viewaxis[ 1 ].z, 0.0f );
	rbinds->viewUpWorld->Set( renderView->viewaxis[ 2 ].x, renderView->viewaxis[ 2 ].y, renderView->viewaxis[ 2 ].z, 0.0f );
	if ( view != NULL && view->viewEntities != NULL ) {
		rbinds->viewMovement->Set(
			renderView->lastViewAxis[ 0 ] * renderView->viewaxis[ 1 ],
			renderView->lastViewAxis[ 0 ] * renderView->viewaxis[ 2 ],
			0.0f,
			0.0f
		);
	}

	const float stuffFadeStart = r_stuffFadeStart.GetFloat();
	const float stuffFadeEnd = r_stuffFadeEnd.GetFloat();
	const float stuffFadeRange = stuffFadeEnd - stuffFadeStart;
	rbinds->stuffParameters->Set( stuffFadeStart, 1.0f / stuffFadeRange, stuffFadeStart / stuffFadeRange, 1.0f );
	rbinds->cinematicY->SetEvaluator( RB_Evaluator_UpdateCinematicImageYUV );

	if ( renderBindingManager != NULL ) {
		renderBindingManager->UpdateInfrequentRenderBindings();
	}
}

void RB_DetermineLightScale() {
	// ETQW removed Doom 3's dynamic over-bright scan.  The retail body only
	// copies r_lightScale into the two back-end scale values; render bindings
	// consume the cvar directly in this reconstructed back end.
}

void RB_GetShaderTextureMatrix( const stageTextureMatrix_t* textureMatrix, const float* materialRegisters, float matrix[ 16 ] ) {
	memset( matrix, 0, sizeof( float ) * 16 );
	matrix[ 0 ] = materialRegisters[ textureMatrix->matrix[ 0 ][ 0 ] ];
	matrix[ 4 ] = materialRegisters[ textureMatrix->matrix[ 0 ][ 1 ] ];
	matrix[ 12 ] = materialRegisters[ textureMatrix->matrix[ 0 ][ 2 ] ];
	if ( matrix[ 12 ] < -40.0f || matrix[ 12 ] > 40.0f ) matrix[ 12 ] -= static_cast< int >( matrix[ 12 ] );
	matrix[ 1 ] = materialRegisters[ textureMatrix->matrix[ 1 ][ 0 ] ];
	matrix[ 5 ] = materialRegisters[ textureMatrix->matrix[ 1 ][ 1 ] ];
	matrix[ 13 ] = materialRegisters[ textureMatrix->matrix[ 1 ][ 2 ] ];
	if ( matrix[ 13 ] < -40.0f || matrix[ 13 ] > 40.0f ) matrix[ 13 ] -= static_cast< int >( matrix[ 13 ] );
	matrix[ 10 ] = matrix[ 15 ] = 1.0f;
}

void RB_DrawElementsImmediate( const srfTriangles_t* triangles ) {
	if ( triangles == NULL || triangles->verts == NULL || triangles->indexes == NULL ) return;
	glBegin( triangles->mode == PM_POINTSPRITE ? GL_POINTS : GL_TRIANGLES );
	for ( int i = 0; i < triangles->numIndexes; ++i ) {
		const int vertexIndex = triangles->indexes[ i ];
		if ( vertexIndex < 0 || vertexIndex >= triangles->numVerts ) continue;
		const idDrawVert& vertex = triangles->verts[ vertexIndex ];
		const idVec2 st = vertex.GetST();
		glTexCoord2fv( st.ToFloatPtr() );
		glVertex3fv( vertex.xyz.ToFloatPtr() );
	}
	glEnd();
}

void RB_DrawElementsWithCounters( const srfTriangles_t* triangles ) {
	if ( triangles == NULL || triangles->numIndexes <= 0 ) return;

	const GLenum mode = PrimitiveMode( triangles );
	if ( triangles->mode == PM_POINTSPRITE ) {
		glEnable( GL_POINT_SPRITE_ARB );
		glPointSize( 64.0f );
	}

	const glIndex_t singleTriangleIndexes[ 3 ] = { 0, 1, 2 };
	const int singleTriangle = r_singleTriangle.GetInteger();
	const void* indexes = triangles->indexes;
	const bool usingIndexBuffer = triangles->indexCache != NULL && r_useIndexBuffers.GetBool();
	if ( triangles->indexCache != NULL && r_useIndexBuffers.GetBool() ) {
		indexes = CachePosition( triangles->indexCache, true );
	} else {
		if ( r_useIndexBuffers.GetBool() && qglBindBufferARB != NULL ) {
			qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
		}
		if ( singleTriangle > 1 ) indexes = singleTriangleIndexes;
	}

	if ( indexes != NULL || usingIndexBuffer ) {
		const viewEntity_s* space = RB_GetActiveDrawSpace();
		if ( space != NULL && space->numInsts > 0 && space->insts != NULL ) {
			const viewDef_s* viewDef = RB_GetViewDef();
			for ( int instanceIndex = 0; instanceIndex < space->numInsts; ++instanceIndex ) {
				const sdInstInfo& instance = space->insts[ instanceIndex ];
				if ( viewDef != NULL && R_CullLocalBoxToViewdef( triangles->bounds,
						instance.inst.axis, instance.inst.origin, viewDef ) ) {
					continue;
				}
				SetInstanceAttributes( instance );
				const int count = singleTriangle != 0 ? Min( triangles->numIndexes, 3 ) : triangles->numIndexes;
				glDrawElements( mode, count, GL_UNSIGNED_SHORT, indexes );
			}
		} else if ( singleTriangle == 0 && triangles->indexTree != NULL && space != NULL && r_useIndexHier.GetBool() ) {
			int ranges[ 50 ];
			const int numRanges = R_GenerateIndexTreeRenderList( ranges, 50, space->modelMatrix, RB_GetViewDef(), triangles );
			for ( int rangeIndex = 0; rangeIndex + 1 < numRanges; rangeIndex += 2 ) {
				const int firstIndex = ranges[ rangeIndex ];
				const int count = ranges[ rangeIndex + 1 ] - firstIndex;
				if ( count <= 0 ) continue;
				const size_t byteOffset = static_cast< size_t >( firstIndex ) * sizeof( glIndex_t );
				const void* rangeIndexes = usingIndexBuffer ?
					reinterpret_cast< const void* >( reinterpret_cast< size_t >( indexes ) + byteOffset ) :
					static_cast< const void* >( static_cast< const byte* >( indexes ) + byteOffset );
				glDrawElements( mode, count, GL_UNSIGNED_SHORT, rangeIndexes );
			}
		} else {
			const int count = singleTriangle != 0 ? Min( triangles->numIndexes, 3 ) : triangles->numIndexes;
			const void* drawIndexes = singleTriangle > 1 && !usingIndexBuffer ? singleTriangleIndexes : indexes;
			glDrawElements( mode, count, GL_UNSIGNED_SHORT, drawIndexes );
		}
	}

	if ( triangles->mode == PM_POINTSPRITE ) glDisable( GL_POINT_SPRITE_ARB );
}

void RB_RenderTriangleSurface( const srfTriangles_t* triangles, const sdDeclRenderProgram* program, int extraState, cullType_t cullType ) {
	if ( triangles == NULL || ( triangles->indexes == NULL && triangles->indexCache == NULL ) ||
			( triangles->verts == NULL && triangles->ambientCache == NULL ) ) return;
	if ( program != NULL && qglVertexAttribPointerARB != NULL && qglEnableVertexAttribArrayARB != NULL && qglDisableVertexAttribArrayARB != NULL ) {
		bool weightCacheModified = false;
		RB_ARB2_SetVertexPointers( triangles, weightCacheModified );
		GL_EnableVertexAttribs( program->GetRequiredVertexAttribs() );
		RB_ARB2_DrawWithProgram( triangles, program, extraState, cullType );
	} else {
		if ( triangles->verts == NULL ) return;
		GL_EnableVertexAttribs( 0 );
		glDisableClientState( GL_VERTEX_ARRAY );
		SD_UnbindRenderProgram();
		SetCompatibilityState( extraState, cullType );
		RB_DrawElementsImmediate( triangles );
	}
}

void RB_T_RenderTriangleSurface( const drawSurf_s* surface, const sdDeclRenderProgram* program,
		int stateBits, cullType_t cullType, int surfaceIndex ) {
	(void)surfaceIndex;
	if ( surface == NULL || surface->space == NULL || surface->space->culled ) return;
	RB_ARB2_SetSpace( surface->space, true );
	RB_RenderTriangleSurface( surface->geo, program, stateBits, cullType );
}

void RB_RenderDrawSurfListWithFunction( drawSurf_s** drawSurfs, int numDrawSurfs,
		drawSurfRenderFunction_t renderFunction, const sdDeclRenderProgram* program,
		int stateBits, cullType_t cullType ) {
	if ( drawSurfs == NULL || numDrawSurfs <= 0 || renderFunction == NULL ) return;
	viewDef_s* viewDef = RB_GetViewDef();
	for ( int surfaceIndex = 0; surfaceIndex < numDrawSurfs; ++surfaceIndex ) {
		const drawSurf_s* surface = drawSurfs[ surfaceIndex ];
		if ( surface == NULL || surface->space == NULL || surface->space->culled ) continue;

		RB_ARB2_SetSpace( surface->space, true );
		if ( r_useScissor.GetBool() && viewDef != NULL ) {
			glScissor(
				viewDef->viewport.x1 + surface->scissorRect.x1,
				viewDef->viewport.y1 + surface->scissorRect.y1,
				surface->scissorRect.x2 - surface->scissorRect.x1 + 1,
				surface->scissorRect.y2 - surface->scissorRect.y1 + 1
			);
		}
		renderFunction( surface, program, stateBits, cullType, surfaceIndex );
	}
	RB_ARB2_ClearSpace();
}

void RB_DrawViewImmediate( const viewDef_s* viewDef ) {
	if ( viewDef == NULL ) return;
	viewDef_s* mutableViewDef = const_cast< viewDef_s* >( viewDef );
	viewDef_s* previousViewDef = RB_SwapViewDefContext( mutableViewDef );
	const int viewportWidth = Max( 1, viewDef->viewport.x2 - viewDef->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, viewDef->viewport.y2 - viewDef->viewport.y1 + 1 );
	RB_SetImmediateViewState( &viewDef->renderView, viewDef->projectionMatrix, viewportWidth, viewportHeight );
	RB_STD_DrawView();
	RB_SwapViewDefContext( previousViewDef );
}

void RB_DrawView( const void* data ) {
	if ( data == NULL ) return;
	RB_DrawViewImmediate( *static_cast< viewDef_s* const* >( data ) );
}

void RB_EnterWeaponDepthHack( float fov_x, float fov_y ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	float projection[ 16 ];
	memcpy( projection, view->projectionMatrix, sizeof( projection ) );
	glDepthRange( 0.0, r_depthRangeWeaponHackEnd.GetFloat() );

	if ( fov_x > 0.0f && fov_y > 0.0f ) {
		const float zNear = Max( r_znear.GetFloat(), 0.001f );
		const float xmax = zNear * idMath::Tan( fov_x * idMath::M_DEG2RAD * 0.5f );
		const float ymax = zNear * idMath::Tan( fov_y * idMath::M_DEG2RAD * 0.5f );
		projection[ 0 ] = zNear / Max( xmax, 0.001f );
		projection[ 5 ] = zNear / Max( ymax, 0.001f );
		projection[ 8 ] = 0.0f;
		projection[ 9 ] = 0.0f;
	}
	projection[ 14 ] *= r_depthRangeWeaponHackScale.GetFloat();
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projection );
	glMatrixMode( GL_MODELVIEW );
}

void RB_EnterModelDepthHack( float depth ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	float projection[ 16 ];
	memcpy( projection, view->projectionMatrix, sizeof( projection ) );
	projection[ 14 ] -= depth;
	glDepthRange( r_depthRangeStartDefault.GetFloat(), 1.0 );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( projection );
	glMatrixMode( GL_MODELVIEW );
}

void RB_LeaveDepthHack() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	glDepthRange( r_depthRangeStartDefault.GetFloat(), 1.0 );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( view->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
}

bool RB_SetupMaterialStage( const materialStage_t* stage, const float* materialRegisters, idImage* overrideImage, idVec4& color, idVec4& matrixS, idVec4& matrixT, const sdDeclRenderProgram* selectedProgram, float texCoordScale ) {
	if ( stage == NULL || materialRegisters == NULL ) return false;
	idMaterial::SetRenderBindings( stage, materialRegisters, texCoordScale );
	const sdDeclRenderProgram* program = selectedProgram != NULL ? selectedProgram : stage->renderProgram;
	if ( program != NULL ) {
		program->Bind();
		program->SetState( stage->drawStateBits, stage->cullType );
		// Texture bindings are declared by the selected alternate, not
		// necessarily by the source interaction program.
		for ( int unit = 0; unit < program->GetNumTextureBindings(); ++unit ) {
			const sdDeclRenderBinding* binding = program->GetTextureBinding( unit );
			if ( binding == NULL ) continue;
			binding->Evaluate();
			idImage* image = overrideImage != NULL && IsOverrideTextureBinding( binding ) ? overrideImage : binding->GetImage();
			if ( image == NULL && globalImages != NULL ) {
				common->Warning( "RB_ARB2_SetupProgram: NULL image for render binding %s", binding->GetName() );
				image = globalImages->defaultImage;
			}
			if ( image != NULL ) image->BindFragment( unit );
		}
		GL_SelectTexture( 0 );
		program->UpdateParameters();
	} else {
		SD_UnbindRenderProgram();
		idImage* image = overrideImage != NULL ? overrideImage : StageColorImage( stage );
		if ( image != NULL && image->texnum != idImage::TEXTURE_NOT_LOADED && !image->defaulted ) {
			glEnable( GL_TEXTURE_2D );
			image->BindFragment( 0 );
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		} else {
			glDisable( GL_TEXTURE_2D );
		}
		SetCompatibilityState( stage->drawStateBits, stage->cullType );
	}
	if ( stage->hasAlphaTest ) {
		glEnable( GL_ALPHA_TEST );
		glAlphaFunc( GL_GREATER, materialRegisters[ stage->alphaTestRegister ] );
	} else glDisable( GL_ALPHA_TEST );
	GetStageColor( color );
	matrixS = rbinds->diffuseMatrix_s->GetVec4();
	matrixT = rbinds->diffuseMatrix_t->GetVec4();
	return true;
}
