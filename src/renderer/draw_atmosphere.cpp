/*
===========================================================================

	ETQW atmospheric extinction draw pass, adapted to the Darklight GLSL
	backend.  The original renderer performed the same depth-equal,
	premultiplied-alpha pass over opaque scene geometry.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "../decllib/declAtmosphere.h"

static void RB_GLSL_DrawAtmosphereSurface( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *material = surf->material;
	if ( !tri || tri->isBSE || !tri->numIndexes || !material ) {
		return;
	}

	// The atmosphere pass uses DEPTHFUNC_EQUAL, so perforated materials reuse
	// the alpha-tested coverage written by the depth prepass.  Empty portions
	// of foliage cards therefore remain untouched while the leaves receive fog.
	if ( material->Coverage() == MC_TRANSLUCENT || material->IsPortalSky() ) {
		return;
	}

	GL_Cull( material->GetCullType() );
	const idDrawVert *ambient = RB_BindDrawVertBuffer( tri );
	qglVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), ambient->xyz.ToFloatPtr() );

	const float *matrix = surf->space->modelMatrix;
	idVec4 parm;
	parm.Set( matrix[0], matrix[4], matrix[8], matrix[12] );
	R_SetGLSLProgramEnvParameter( GL_VERTEX_SHADER, 0, parm.ToFloatPtr() );
	parm.Set( matrix[1], matrix[5], matrix[9], matrix[13] );
	R_SetGLSLProgramEnvParameter( GL_VERTEX_SHADER, 1, parm.ToFloatPtr() );
	parm.Set( matrix[2], matrix[6], matrix[10], matrix[14] );
	R_SetGLSLProgramEnvParameter( GL_VERTEX_SHADER, 2, parm.ToFloatPtr() );

	RB_DrawElementsWithCounters( tri );
}

/*
==================
RB_GLSL_DrawAtmosphere

Matches ETQW's draw_atmosphere.cpp surface pass.  Extinction is evaluated
from each visible surface point and composited without changing depth.
==================
*/
void RB_GLSL_DrawAtmosphere( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( r_skipAtmosphere.GetBool() || !backEnd.viewDef->viewEntitys ) {
		return;
	}

	idRenderWorldLocal *renderWorld = backEnd.viewDef->renderWorld;
	const sdDeclAtmosphere *atmosphere = renderWorld ? renderWorld->GetAtmosphere() : NULL;
	if ( !atmosphere || !atmosphere->GetSkyGradientImage() ) {
		return;
	}
	if ( !R_BindGLSLProgram( GLSLPROG_ATMOSPHERE ) ) {
		return;
	}

	RB_LogComment( "---------- RB_GLSL_DrawAtmosphere ----------\n" );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA |
		GLS_DEPTHMASK | GLS_DEPTHFUNC_EQUAL );

	const float fogDistHalf = Max( atmosphere->GetFogDistHalf(), 1.0f );
	const float fogHeightHalf = Max( atmosphere->GetFogHeightHalf(), 1.0f );
	idVec4 parm( 1.0f / fogDistHalf, 1.0f / fogHeightHalf,
		atmosphere->GetFogHeightOffset(), r_atmosScale.GetFloat() );
	R_SetGLSLProgramEnvParameter( GL_FRAGMENT_SHADER, 0, parm.ToFloatPtr() );
	parm.Set( atmosphere->GetFogColor().x, atmosphere->GetFogColor().y,
		atmosphere->GetFogColor().z, 1.0f );
	R_SetGLSLProgramEnvParameter( GL_FRAGMENT_SHADER, 1, parm.ToFloatPtr() );
	parm.Set( atmosphere->GetSunDirection().x, atmosphere->GetSunDirection().y,
		atmosphere->GetSunDirection().z, 0.0f );
	R_SetGLSLProgramEnvParameter( GL_FRAGMENT_SHADER, 2, parm.ToFloatPtr() );
	parm.Set( atmosphere->GetSunColor().x, atmosphere->GetSunColor().y,
		atmosphere->GetSunColor().z, 1.0f );
	R_SetGLSLProgramEnvParameter( GL_FRAGMENT_SHADER, 3, parm.ToFloatPtr() );
	parm.Set( atmosphere->GetSunHaloScale(), atmosphere->GetSunHaloBias(), 0.0f, 0.0f );
	R_SetGLSLProgramEnvParameter( GL_FRAGMENT_SHADER, 4, parm.ToFloatPtr() );
	parm.Set( backEnd.viewDef->renderView.vieworg.x, backEnd.viewDef->renderView.vieworg.y,
		backEnd.viewDef->renderView.vieworg.z, 1.0f );
	R_SetGLSLProgramEnvParameter( GL_FRAGMENT_SHADER, 5, parm.ToFloatPtr() );

	GL_SelectTexture( 0 );
	atmosphere->GetSkyGradientImage()->Bind();
	RB_RenderDrawSurfListWithFunction( drawSurfs, numDrawSurfs, RB_GLSL_DrawAtmosphereSurface );

	globalImages->BindNull();
	R_UnbindGLSLProgram();
}
