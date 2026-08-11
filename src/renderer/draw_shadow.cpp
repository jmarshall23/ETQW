// Copyright (C) 2007 Id Software, Inc.
//
// ETQW stencil-shadow back end reconstructed from renderer/draw_shadow.obj.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderSystem.h"
#include "VertexCache.h"
#include "draw_local.h"
#include "tr_render.h"
#include "renderbindings.h"
#include "../decllib/declRenderProgram.h"
#include "../libs/qglLib/qgl.h"

extern glconfig_t glConfig;
extern idCVar r_shadows;
extern idCVar r_singleTriangle;
extern idCVar r_useIndexBuffers;
extern idCVar r_useTwoSidedStencil;
extern idCVar r_useExternalShadows;
extern idCVar r_useDepthBoundsTest;
extern idCVar r_useShadowDitherMask;
extern idCVar r_useSampleCoverage;
extern idCVar r_shadowPass;
extern idCVar r_showShadows;
extern idCVar r_shadowPolygonOffset;
extern idCVar r_shadowPolygonFactor;
extern idCVar r_shadowPolygonOffsetMT;
extern idCVar r_shadowPolygonFactorMT;
extern idCVar r_useShadowFastParallel;
extern idCVar r_useShadowInfinite;
extern idCVar r_useScissor;

namespace {
	const GLenum STENCIL_INCR = GL_INCR_WRAP_EXT;
	const GLenum STENCIL_DECR = GL_DECR_WRAP_EXT;

	const void* CachePosition( const vertCache_s* cache, bool indexBuffer ) {
		if ( cache == NULL ) return NULL;
		if ( cache->vbo != 0 && qglBindBufferARB != NULL ) {
			qglBindBufferARB( indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB, cache->vbo );
			return reinterpret_cast< const void* >( cache->offset );
		}
		if ( qglBindBufferARB != NULL ) qglBindBufferARB( indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB, 0 );
		return cache->virtMem != NULL ? static_cast< const byte* >( cache->virtMem ) + cache->offset : NULL;
	}

	int ShadowIndexCount( const drawSurf_s* surface, bool& external ) {
		const srfTriangles_t* triangles = surface->geo;
		external = false;
		const int externalMode = r_useExternalShadows.GetInteger();
		if ( externalMode == 0 ) return triangles->numIndexes;
		if ( externalMode == 2 ) return triangles->numShadowIndexesNoCaps;
		if ( ( surface->dsFlags & 1 ) == 0 ) {
			external = true;
			return triangles->numShadowIndexesNoCaps;
		}
		const viewLight_s* light = RB_GetActiveViewLight();
		if ( light != NULL && !light->viewInsideLight && ( triangles->shadowCapPlaneBits & 0x40 ) == 0 ) {
			external = true;
			if ( ( light->viewSeesShadowPlaneBits & triangles->shadowCapPlaneBits ) != 0 ) {
				return triangles->numShadowIndexesNoFrontCaps;
			}
			return triangles->numShadowIndexesNoCaps;
		}
		return triangles->numIndexes;
	}

	void SetStencilOps( bool mirror, bool external ) {
		const GLenum front = mirror ? GL_BACK : GL_FRONT;
		const GLenum back = mirror ? GL_FRONT : GL_BACK;
		if ( qglStencilOpSeparateATI != NULL && glConfig.atiTwoSidedStencilAvailable ) {
			if ( external ) {
				qglStencilOpSeparateATI( front, GL_KEEP, GL_KEEP, STENCIL_INCR );
				qglStencilOpSeparateATI( back, GL_KEEP, GL_KEEP, STENCIL_DECR );
			} else {
				qglStencilOpSeparateATI( front, GL_KEEP, STENCIL_DECR, GL_KEEP );
				qglStencilOpSeparateATI( back, GL_KEEP, STENCIL_INCR, GL_KEEP );
			}
		}
	}
}

void RB_ARB2_DrawShadowElementsWithCounters( const srfTriangles_t* triangles, int numIndexes ) {
	if ( triangles == NULL || numIndexes <= 0 || ( triangles->indexes == NULL && triangles->indexCache == NULL ) ) return;
	numIndexes = Min( numIndexes, triangles->numIndexes );
	if ( r_singleTriangle.GetBool() ) numIndexes = Min( numIndexes, 3 );
	const void* indexes = triangles->indexes;
	const bool usingIndexBuffer = triangles->indexCache != NULL && r_useIndexBuffers.GetBool();
	if ( usingIndexBuffer ) indexes = CachePosition( triangles->indexCache, true );
	else if ( r_useIndexBuffers.GetBool() && qglBindBufferARB != NULL ) qglBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
	if ( indexes == NULL && !usingIndexBuffer ) return;
	glDrawElements( GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, indexes );
}

void RB_ARB2_DrawShadowSurface( const drawSurf_s* surface, const sdDeclRenderProgram* program, int stateBits ) {
	if ( surface == NULL || surface->geo == NULL || surface->space == NULL || program == NULL ) return;
	const srfTriangles_t* triangles = surface->geo;
	const void* shadowVertices = NULL;
	if ( triangles->shadowCache != NULL ) shadowVertices = CachePosition( triangles->shadowCache, false );
	else shadowVertices = triangles->shadowVertexes;
	if ( shadowVertices == NULL && ( triangles->shadowCache == NULL || triangles->shadowCache->vbo == 0 ) ) return;

	glVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ), shadowVertices );
	RB_ARB2_SetSpace( surface->space, false );
	RB_ARB2_SetupLightSpace( surface );
	if ( r_useScissor.GetBool() && RB_GetViewDef() != NULL ) {
		glScissor(
			surface->scissorRect.x1 + RB_GetViewDef()->viewport.x1,
			surface->scissorRect.y1 + RB_GetViewDef()->viewport.y1,
			surface->scissorRect.x2 - surface->scissorRect.x1 + 1,
			surface->scissorRect.y2 - surface->scissorRect.y1 + 1
		);
	}
	const bool twoSided = ( r_useTwoSidedStencil.GetBool() &&
		( glConfig.twoSidedStencilAvailable || glConfig.atiTwoSidedStencilAvailable ) ) || r_showShadows.GetInteger() != 0;
	RB_ARB2_SetShadowSurfaceContext( surface, true );
	RB_ARB2_SetupProgram( program, stateBits, twoSided ? CT_TWO_SIDED : CT_FRONT_SIDED, NULL );
	RB_ARB2_SetShadowSurfaceContext( NULL, false );

	bool external = false;
	const int numIndexes = ShadowIndexCount( surface, external );
	viewDef_s* view = RB_GetViewDef();
	if ( glConfig.depthBoundsTestAvailable && qglDepthBoundsEXT != NULL && r_useDepthBoundsTest.GetBool() ) {
		qglDepthBoundsEXT( surface->scissorRect.zmin, surface->scissorRect.zmax );
	}

	if ( r_showShadows.GetInteger() != 0 ) {
		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
		glDisable( GL_STENCIL_TEST );
		glColor3f( external ? 1.0f : 0.1f, external ? 0.1f : 1.0f, 0.1f );
		RB_ARB2_DrawShadowElementsWithCounters( triangles, numIndexes );
		glEnable( GL_STENCIL_TEST );
		return;
	}

	if ( r_useTwoSidedStencil.GetBool() && glConfig.twoSidedStencilAvailable && qglActiveStencilFaceEXT != NULL ) {
		const bool mirror = view != NULL && view->isMirror;
		const GLenum frontFace = mirror ? GL_BACK : GL_FRONT;
		const GLenum backFace = mirror ? GL_FRONT : GL_BACK;
		qglActiveStencilFaceEXT( frontFace );
		glStencilOp( GL_KEEP, external ? GL_KEEP : STENCIL_DECR, external ? STENCIL_INCR : GL_KEEP );
		qglActiveStencilFaceEXT( backFace );
		glStencilOp( GL_KEEP, external ? GL_KEEP : STENCIL_INCR, external ? STENCIL_DECR : GL_KEEP );
		glEnable( GL_STENCIL_TEST_TWO_SIDE_EXT );
		RB_ARB2_DrawShadowElementsWithCounters( triangles, numIndexes );
		glDisable( GL_STENCIL_TEST_TWO_SIDE_EXT );
	} else if ( r_useTwoSidedStencil.GetBool() && glConfig.atiTwoSidedStencilAvailable && qglStencilOpSeparateATI != NULL ) {
		SetStencilOps( view != NULL && view->isMirror, external );
		RB_ARB2_DrawShadowElementsWithCounters( triangles, numIndexes );
	} else {
		// CT_FRONT_SIDED culls front faces, so this first draw submits the back
		// faces.  Retail decrements the z-fail count for external volumes (and
		// increments depth-fail for capped volumes) on this draw, then performs
		// the inverse operation for front faces.
		GL_Cull( CT_FRONT_SIDED );
		glStencilOp( GL_KEEP, external ? GL_KEEP : STENCIL_INCR, external ? STENCIL_DECR : GL_KEEP );
		RB_ARB2_DrawShadowElementsWithCounters( triangles, numIndexes );
		GL_Cull( CT_BACK_SIDED );
		glStencilOp( GL_KEEP, external ? GL_KEEP : STENCIL_DECR, external ? STENCIL_INCR : GL_KEEP );
		RB_ARB2_DrawShadowElementsWithCounters( triangles, numIndexes );
	}
}

void RB_ARB2_StencilShadowPass( const drawSurf_s* surfaces, const sdDeclRenderProgram* program,
		const sdDeclRenderProgram* invariantProgram, bool atmosLight, float polyFactor, float polyOffset ) {
	if ( !r_shadows.GetBool() || !r_shadowPass.GetBool() || surfaces == NULL || program == NULL ) return;
	if ( program->GetProgram() != NULL ) program->Bind();
	const int stateBits = r_showShadows.GetInteger() != 0 ? ( r_showShadows.GetInteger() == 2 ? 528672 : 73728 ) : 532224;
	if ( polyFactor != 0.0f || polyOffset != 0.0f ) {
		glPolygonOffset( polyFactor, -polyOffset );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}
	glStencilFunc( GL_ALWAYS, 1, 255 );
	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) glEnable( GL_DEPTH_BOUNDS_TEST_EXT );
	GL_EnableVertexAttribs( 0 );
	for ( const drawSurf_s* surface = surfaces; surface != NULL; surface = surface->nextOnLight ) {
		const sdDeclRenderProgram* selected = invariantProgram != NULL && ( surface->geo->dsFlags & 0x100 ) != 0 ? invariantProgram : program;
		RB_ARB2_DrawShadowSurface( surface, selected, stateBits );
	}
	if ( polyFactor != 0.0f || polyOffset != 0.0f ) glDisable( GL_POLYGON_OFFSET_FILL );
	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) glDisable( GL_DEPTH_BOUNDS_TEST_EXT );
	glStencilFunc( GL_GEQUAL, 128, 255 );
	if ( r_useTwoSidedStencil.GetBool() && glConfig.twoSidedStencilAvailable && qglActiveStencilFaceEXT != NULL ) {
		qglActiveStencilFaceEXT( GL_BACK );
		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
		qglActiveStencilFaceEXT( GL_FRONT );
		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	} else if ( r_useTwoSidedStencil.GetBool() && glConfig.atiTwoSidedStencilAvailable && qglStencilOpSeparateATI != NULL ) {
		qglStencilOpSeparateATI( GL_FRONT, GL_KEEP, GL_KEEP, GL_KEEP );
		qglStencilOpSeparateATI( GL_BACK, GL_KEEP, GL_KEEP, GL_KEEP );
	} else {
		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	}
	RB_ARB2_ClearSpace();
	RB_ARB2_ResetDrawCaches();
}

void RB_ARB2_StencilShadowPass() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || rbinds == NULL ) return;
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next ) {
		RB_ARB2_StencilShadowPass( light->globalShadows, rbinds->shadowProgram, rbinds->shadowInvariantProgram,
			light == view->atmosphereLight, 0.0f, 0.0f );
		RB_ARB2_StencilShadowPass( light->localShadows, rbinds->shadowProgram, rbinds->shadowInvariantProgram,
			light == view->atmosphereLight, 0.0f, 0.0f );
	}
}
