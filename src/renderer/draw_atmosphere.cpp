// Copyright (C) 2007 Id Software, Inc.
//
// ETQW atmosphere back end reconstructed from renderer/draw_atmosphere.obj.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "draw_local.h"
#include "tr_render.h"
#include "renderbindings.h"
#include "VertexCache.h"
#include "../decllib/declAtmosphere.h"
#include "../decllib/declRenderBinding.h"
#include "../decllib/declTypeHolder.h"
#include "../framework/DeclSkin.h"

extern idCVar r_megaDrawMethod;
extern idCVar r_skipAtmosphere;

idCVar r_noDoubleAtmosphere( "r_noDoubleAtmosphere", "1", CVAR_RENDERER | CVAR_ARCHIVE, "Uses the stencil buffer to avoid atmosphere-ing" );
idCVar r_AtmospherePostprocess( "r_AtmospherePostprocess", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "Use post processing pass for atmosphere", 0.0f, 0.1f );

namespace {
	const idMaterial* RemapAtmosphereMaterial( viewDef_s* view, const idMaterial* material ) {
		if ( view != NULL && view->renderView.globalSkin != NULL && material != NULL ) {
			return view->renderView.globalSkin->RemapShaderBySkin( material );
		}
		return material;
	}

	bool SurfaceReceivesAtmosphere( const drawSurf_s* surface ) {
		if ( surface == NULL || surface->material == NULL ) return false;
		const idMaterial* material = surface->material;
		if ( material->TestMaterialFlag( MF_NOATMOSPHERE ) ) return false;
		return material->TestMaterialFlag( MF_FORCEATMOSPHERE ) ||
			( material->GetSort() != SS_SUBVIEW && material->Coverage() != MC_TRANSLUCENT && material->ReceivesLighting() );
	}

	void DrawAtmosphereSurface( const drawSurf_s* surface, const idMaterial* atmosphereMaterial, const float* registers ) {
		if ( !SurfaceReceivesAtmosphere( surface ) ) return;
		RB_ARB2_DrawSurfacePass( surface, atmosphereMaterial, registers, RBP_SHADER, NULL );
	}

}

void RB_ARB2_DrawAtmospherePostProcess() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || view->atmosphere == NULL || rbinds == NULL ) return;
	const idMaterial* material = declHolder.FindMaterial( "atmospheres/postprocess", true );
	if ( material == NULL ) return;
	const float fogStart = view->atmosphere->GetFogStart();
	const float fogEnd = view->atmosphere->GetFogEnd();
	const float range = fogEnd - fogStart;
	rbinds->fogDepths->Set( fogStart, fogEnd, 1.0f / range, -fogStart / range );
	RB_DrawFullscreenQuad( material, 0xFFFFFFFF );
}

void RB_ARB2_DrawAtmosphere( drawSurf_s** drawSurfs, int numDrawSurfs ) {
	viewDef_s* view = RB_GetViewDef();
	if ( r_skipAtmosphere.GetBool() || view == NULL || view->atmosphere == NULL ) return;
	if ( r_megaDrawMethod.GetInteger() != 0 && !r_AtmospherePostprocess.GetBool() ) return;
	if ( r_AtmospherePostprocess.GetBool() ) {
		RB_ARB2_DrawAtmospherePostProcess();
		return;
	}

	const idMaterial* material = RemapAtmosphereMaterial( view, view->atmosphere->GetAtmosphereMaterial() );
	if ( material == NULL ) return;
	glScissor( view->viewport.x1 + view->scissor.x1, view->viewport.y1 + view->scissor.y1,
		view->scissor.x2 - view->scissor.x1 + 1, view->scissor.y2 - view->scissor.y1 + 1 );
	idList< float > registers;
	registers.SetNum( Max( material->GetNumRegisters(), 1 ), false );
	material->EvaluateRegisters( registers.Begin(), view->renderView.shaderParms, view, NULL, 0 );
	if ( material->TestMaterialFlag( MF_UPDATECURRENTRENDER ) && !RB_ARB2_HasCurrentRenderCopy() ) {
		RB_ARB2_SetupPostProcessingFrameBuffer();
		RB_ARB2_CopyFramebufferColor();
	}

	glDisable( GL_STENCIL_TEST );
	if ( r_noDoubleAtmosphere.GetBool() ) {
		glClear( GL_STENCIL_BUFFER_BIT );
		glEnable( GL_STENCIL_TEST );
		glStencilFunc( GL_EQUAL, 128, 255 );
		glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	}
	for ( int index = 0; index < numDrawSurfs; ++index ) DrawAtmosphereSurface( drawSurfs[ index ], material, registers.Begin() );
	if ( r_noDoubleAtmosphere.GetBool() ) {
		glStencilFunc( GL_ALWAYS, 128, 255 );
		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	}
	RB_ARB2_ClearSpace();
	glEnable( GL_STENCIL_TEST );
}

void RB_ARB2_DrawAtmosphere( const drawSurf_s* drawSurfs ) {
	viewDef_s* view = RB_GetViewDef();
	if ( r_skipAtmosphere.GetBool() || r_AtmospherePostprocess.GetBool() || view == NULL || view->atmosphere == NULL ) return;
	const idMaterial* material = RemapAtmosphereMaterial( view, view->atmosphere->GetAtmosphereMaterial() );
	if ( material == NULL ) return;
	glScissor( view->viewport.x1 + view->scissor.x1, view->viewport.y1 + view->scissor.y1,
		view->scissor.x2 - view->scissor.x1 + 1, view->scissor.y2 - view->scissor.y1 + 1 );
	idList< float > registers;
	registers.SetNum( Max( material->GetNumRegisters(), 1 ), false );
	material->EvaluateRegisters( registers.Begin(), view->renderView.shaderParms, view, NULL, 0 );
	if ( material->TestMaterialFlag( MF_UPDATECURRENTRENDER ) && !RB_ARB2_HasCurrentRenderCopy() ) {
		RB_ARB2_SetupPostProcessingFrameBuffer();
		RB_ARB2_CopyFramebufferColor();
	}
	glDisable( GL_STENCIL_TEST );
	if ( r_noDoubleAtmosphere.GetBool() ) {
		glClear( GL_STENCIL_BUFFER_BIT );
		glEnable( GL_STENCIL_TEST );
		glStencilFunc( GL_EQUAL, 128, 255 );
		glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
	}
	for ( const drawSurf_s* surface = drawSurfs; surface != NULL; surface = surface->nextOnLight ) DrawAtmosphereSurface( surface, material, registers.Begin() );
	if ( r_noDoubleAtmosphere.GetBool() ) {
		glStencilFunc( GL_ALWAYS, 128, 255 );
		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	}
	RB_ARB2_ClearSpace();
	glEnable( GL_STENCIL_TEST );
}

void RB_ARB2_DrawAtmosphere() {
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) RB_ARB2_DrawAtmosphere( view->drawSurfs, view->numDrawSurfs );
}
