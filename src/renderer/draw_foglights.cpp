// Copyright (C) 2007 Id Software, Inc.
//
// ETQW fog and blend-light back end.  Function ownership and pass ordering
// follow renderer/draw_foglights.obj from the retail PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "draw_local.h"
#include "tr_render.h"
#include "renderbindings.h"
#include "../decllib/declRenderBinding.h"
#include "../libs/qglLib/qgl.h"

extern idCVar r_skipFogLights;
extern idCVar r_showOverDraw;
extern idCVar r_ignore;
extern idCVar r_useScissor;

namespace {
	idPlane fogPlanes[ 4 ];

	idPlane GlobalPlaneToLocal( const idPlane& global, const float modelMatrix[ 16 ] ) {
		return idPlane(
			global[ 0 ] * modelMatrix[ 0 ] + global[ 1 ] * modelMatrix[ 1 ] + global[ 2 ] * modelMatrix[ 2 ],
			global[ 0 ] * modelMatrix[ 4 ] + global[ 1 ] * modelMatrix[ 5 ] + global[ 2 ] * modelMatrix[ 6 ],
			global[ 0 ] * modelMatrix[ 8 ] + global[ 1 ] * modelMatrix[ 9 ] + global[ 2 ] * modelMatrix[ 10 ],
			global[ 3 ] + global[ 0 ] * modelMatrix[ 12 ] + global[ 1 ] * modelMatrix[ 13 ] + global[ 2 ] * modelMatrix[ 14 ]
		);
	}

	void SetFalloffPlane( const sdDeclRenderBinding* binding, const idPlane& plane ) {
		if ( binding != NULL ) binding->Set( plane.ToFloatPtr() );
	}
}

void R_CalculateBlendPlanesPerLight( viewLight_s* vLight ) {
	if ( vLight == NULL ) return;
	for ( int index = 0; index < 4; ++index ) fogPlanes[ index ] = vLight->lightProject[ index ];
}

void R_CalculateFogPlanesPerLight( viewLight_s* vLight ) {
	viewDef_s* view = RB_GetViewDef();
	if ( vLight == NULL || view == NULL || vLight->material == NULL || vLight->lightRegisters == NULL || vLight->material->GetNumStages() == 0 ) return;

	const materialStage_t* stage = vLight->material->GetStage( 0 );
	float fogDistance = 1.0f;
	if ( stage != NULL && stage->colorVector != NULL ) fogDistance = vLight->lightRegisters[ stage->colorVector->registers[ 3 ] ];
	const float distanceScale = fogDistance > 1.0f ? -0.5f / fogDistance : -0.001f;
	const float* modelView = view->worldSpace.modelViewMatrix;
	fogPlanes[ 0 ].Set( modelView[ 2 ] * distanceScale, modelView[ 6 ] * distanceScale, modelView[ 10 ] * distanceScale, modelView[ 14 ] * distanceScale );
	fogPlanes[ 1 ].Set( modelView[ 0 ] * distanceScale, modelView[ 4 ] * distanceScale, modelView[ 8 ] * distanceScale, modelView[ 12 ] * distanceScale );

	float density = stage != NULL ? vLight->lightRegisters[ stage->specularPowerRegister ] : 0.0f;
	density = density > 0.0f ? density * 0.001f : 0.001f;
	fogPlanes[ 2 ].Set(
		vLight->fogPlane[ 0 ] * density,
		vLight->fogPlane[ 1 ] * density,
		vLight->fogPlane[ 2 ] * density,
		vLight->fogPlane[ 3 ] * density
	);
	fogPlanes[ 3 ].Set( 0.0f, 0.0f, 0.0f,
		view->renderView.vieworg.x * fogPlanes[ 2 ][ 0 ] +
		view->renderView.vieworg.y * fogPlanes[ 2 ][ 1 ] +
		view->renderView.vieworg.z * fogPlanes[ 2 ][ 2 ] + fogPlanes[ 2 ][ 3 ] + 0.5078125f );
}

void R_CalculateFogPlanesPerSpace( const drawSurf_s* surface ) {
	if ( surface == NULL || surface->space == NULL || rbinds == NULL ) return;
	idPlane local = GlobalPlaneToLocal( fogPlanes[ 0 ], surface->space->modelMatrix );
	local[ 3 ] += 0.5f;
	SetFalloffPlane( rbinds->lightFalloff_0, local );
	SetFalloffPlane( rbinds->lightFalloff_1, idPlane( 0.0f, 0.0f, 0.0f, 0.5f ) );
	local = GlobalPlaneToLocal( fogPlanes[ 2 ], surface->space->modelMatrix );
	local[ 3 ] += 0.5078125f;
	SetFalloffPlane( rbinds->lightFalloff_2, local );
	SetFalloffPlane( rbinds->lightFalloff_3, GlobalPlaneToLocal( fogPlanes[ 3 ], surface->space->modelMatrix ) );
}

void R_CalculateBlendPlanesPerSpace( const drawSurf_s* surface ) {
	if ( surface == NULL || surface->space == NULL || rbinds == NULL ) return;
	SetFalloffPlane( rbinds->lightFalloff_0, GlobalPlaneToLocal( fogPlanes[ 0 ], surface->space->modelMatrix ) );
	SetFalloffPlane( rbinds->lightFalloff_1, GlobalPlaneToLocal( fogPlanes[ 1 ], surface->space->modelMatrix ) );
	SetFalloffPlane( rbinds->lightFalloff_2, GlobalPlaneToLocal( fogPlanes[ 2 ], surface->space->modelMatrix ) );
	SetFalloffPlane( rbinds->lightFalloff_3, GlobalPlaneToLocal( fogPlanes[ 3 ], surface->space->modelMatrix ) );
}

void RB_ARB2_FogLights( int phase ) {
	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0 ) return;
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || rbinds == NULL ) return;

	glDisable( GL_STENCIL_TEST );
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next ) {
		if ( light->culled || light->material == NULL || light->lightRegisters == NULL ) continue;
		if ( !light->material->IsFogLight() && !light->material->IsBlendLight() ) continue;
		if ( light->material->GetSort() != static_cast< float >( phase ) ) continue;

		rbinds->fadeFraction->Set( light->fadeFraction );
		if ( light->falloffImage != NULL ) rbinds->lightFalloffMap->Set( light->falloffImage );
		rbinds->lightRadius->Set( light->lightRadius.x, light->lightRadius.y, light->lightRadius.z, 0.0f );
		if ( r_ignore.GetBool() ) {
			if ( r_useScissor.GetBool() ) {
				glScissor(
					light->scissorRect.x1 + view->viewport.x1,
					light->scissorRect.y1 + view->viewport.y1,
					light->scissorRect.x2 - light->scissorRect.x1 + 1,
					light->scissorRect.y2 - light->scissorRect.y1 + 1
				);
			}
			glClear( GL_STENCIL_BUFFER_BIT );
			glEnable( GL_STENCIL_TEST );
			glStencilFunc( GL_EQUAL, 128, 0xFF );
			glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );
		}

		void ( *spaceCallback )( const drawSurf_s* ) = NULL;
		if ( light->material->IsFogLight() ) {
			R_CalculateFogPlanesPerLight( light );
			spaceCallback = R_CalculateFogPlanesPerSpace;
		} else {
			R_CalculateBlendPlanesPerLight( light );
			spaceCallback = R_CalculateBlendPlanesPerSpace;
		}

		for ( drawSurf_s* surface = light->globalInteractions; surface != NULL; surface = surface->nextOnLight ) {
			RB_ARB2_DrawSurfacePass( surface, light->material, light->lightRegisters, RBP_SHADER, spaceCallback );
		}
		for ( drawSurf_s* surface = light->localInteractions; surface != NULL; surface = surface->nextOnLight ) {
			RB_ARB2_DrawSurfacePass( surface, light->material, light->lightRegisters, RBP_SHADER, spaceCallback );
		}
		RB_ARB2_ClearSpace();
		glDisable( GL_STENCIL_TEST );
	}
	RB_ARB2_ClearSpace();
	glEnable( GL_STENCIL_TEST );
}
