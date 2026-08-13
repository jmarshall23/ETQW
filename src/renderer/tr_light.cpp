/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#if 0
// Retained Doom 3 front-end implementation.  ETQW's reconstructed private
// view types and original ETQW function ownership follow this reference block.
#include "tr_local.h"

static const float CHECK_BOUNDS_EPSILON = 1.0f;


/*
===========================================================================================

VERTEX CACHE GENERATORS

===========================================================================================
*/

/*
==================
R_CreateAmbientCache

Create it if needed
==================
*/
bool R_CreateAmbientCache( srfTriangles_t *tri, bool needsLighting ) {
	if ( tri->ambientCache ) {
		return true;
	}
	// we are going to use it for drawing, so make sure we have the tangents and normals
	if ( needsLighting && !tri->tangentsCalculated ) {
		R_DeriveTangents( tri );
	}

	vertexCache.Alloc( tri->verts, tri->numVerts * sizeof( tri->verts[0] ), &tri->ambientCache );
	if ( !tri->ambientCache ) {
		return false;
	}
	return true;
}

/*
==================
R_CreateLightingCache

Returns false if the cache couldn't be allocated, in which case the surface should be skipped.
==================
*/
bool R_CreateLightingCache( const idRenderEntityLocal *ent, const idRenderLightLocal *light, srfTriangles_t *tri ) {
	idVec3		localLightOrigin;

	// fogs and blends don't need light vectors
	if ( light->lightShader->IsFogLight() || light->lightShader->IsBlendLight() ) {
		return true;
	}

	// not needed if we have vertex programs
	if ( tr.backEndRendererHasVertexPrograms ) {
		return true;
	}

	R_GlobalPointToLocal( ent->modelMatrix, light->globalLightOrigin, localLightOrigin );

	int	size = tri->ambientSurface->numVerts * sizeof( lightingCache_t );
	lightingCache_t *cache = (lightingCache_t *)_alloca16( size );

#if 1

	SIMDProcessor->CreateTextureSpaceLightVectors( &cache[0].localLightVector, localLightOrigin,
												tri->ambientSurface->verts, tri->ambientSurface->numVerts, tri->indexes, tri->numIndexes );

#else

	bool *used = (bool *)_alloca16( tri->ambientSurface->numVerts * sizeof( used[0] ) );
	memset( used, 0, tri->ambientSurface->numVerts * sizeof( used[0] ) );

	// because the interaction may be a very small subset of the full surface,
	// it makes sense to only deal with the verts used
	for ( int j = 0; j < tri->numIndexes; j++ ) {
		int i = tri->indexes[j];
		if ( used[i] ) {
			continue;
		}
		used[i] = true;

		idVec3 lightDir;
		const idDrawVert *v;

		v = &tri->ambientSurface->verts[i];

		lightDir = localLightOrigin - v->xyz;

		cache[i].localLightVector[0] = lightDir * v->tangents[0];
		cache[i].localLightVector[1] = lightDir * v->tangents[1];
		cache[i].localLightVector[2] = lightDir * v->normal;
	}

#endif

	vertexCache.Alloc( cache, size, &tri->lightingCache );
	if ( !tri->lightingCache ) {
		return false;
	}
	return true;
}

/*
==================
R_CreatePrivateShadowCache

This is used only for a specific light
==================
*/
void R_CreatePrivateShadowCache( srfTriangles_t *tri ) {
	if ( !tri->shadowVertexes ) {
		return;
	}

	vertexCache.Alloc( tri->shadowVertexes, tri->numVerts * sizeof( *tri->shadowVertexes ), &tri->shadowCache );
}

/*
==================
R_CreateVertexProgramShadowCache

This is constant for any number of lights, the vertex program
takes care of projecting the verts to infinity.
==================
*/
void R_CreateVertexProgramShadowCache( srfTriangles_t *tri ) {
	if ( tri->verts == NULL ) {
		return;
	}

	shadowCache_t *temp = (shadowCache_t *)_alloca16( tri->numVerts * 2 * sizeof( shadowCache_t ) );

#if 1

	SIMDProcessor->CreateVertexProgramShadowCache( &temp->xyz, tri->verts, tri->numVerts );

#else

	int numVerts = tri->numVerts;
	const idDrawVert *verts = tri->verts;
	for ( int i = 0; i < numVerts; i++ ) {
		const float *v = verts[i].xyz.ToFloatPtr();
		temp[i*2+0].xyz[0] = v[0];
		temp[i*2+1].xyz[0] = v[0];
		temp[i*2+0].xyz[1] = v[1];
		temp[i*2+1].xyz[1] = v[1];
		temp[i*2+0].xyz[2] = v[2];
		temp[i*2+1].xyz[2] = v[2];
		temp[i*2+0].xyz[3] = 1.0f;		// on the model surface
		temp[i*2+1].xyz[3] = 0.0f;		// will be projected to infinity
	}

#endif

	vertexCache.Alloc( temp, tri->numVerts * 2 * sizeof( shadowCache_t ), &tri->shadowCache );
}

/*
==================
R_SkyboxTexGen
==================
*/
void R_SkyboxTexGen( drawSurf_t *surf, const idVec3 &viewOrg ) {
	int		i;
	idVec3	localViewOrigin;

	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );

	int numVerts = surf->geo->numVerts;
	int size = numVerts * sizeof( idVec3 );
	idVec3 *texCoords = (idVec3 *) _alloca16( size );

	const idDrawVert *verts = surf->geo->verts;
	for ( i = 0; i < numVerts; i++ ) {
		texCoords[i][0] = verts[i].xyz[0] - localViewOrigin[0];
		texCoords[i][1] = verts[i].xyz[1] - localViewOrigin[1];
		texCoords[i][2] = verts[i].xyz[2] - localViewOrigin[2];
	}

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}

/*
==================
R_WobbleskyTexGen
==================
*/
void R_WobbleskyTexGen( drawSurf_t *surf, const idVec3 &viewOrg ) {
	int		i;
	idVec3	localViewOrigin;

	const int *parms = surf->material->GetTexGenRegisters();

	float	wobbleDegrees = surf->shaderRegisters[ parms[0] ];
	float	wobbleSpeed = surf->shaderRegisters[ parms[1] ];
	float	rotateSpeed = surf->shaderRegisters[ parms[2] ];

	wobbleDegrees = wobbleDegrees * idMath::PI / 180;
	wobbleSpeed = wobbleSpeed * 2 * idMath::PI / 60;
	rotateSpeed = rotateSpeed * 2 * idMath::PI / 60;

	// very ad-hoc "wobble" transform
	float	transform[16];
	float	a = tr.viewDef->floatTime * wobbleSpeed;
	float	s = sin( a ) * sin( wobbleDegrees );
	float	c = cos( a ) * sin( wobbleDegrees );
	float	z = cos( wobbleDegrees );

	idVec3	axis[3];

	axis[2][0] = c;
	axis[2][1] = s;
	axis[2][2] = z;

	axis[1][0] = -sin( a * 2 ) * sin( wobbleDegrees );
	axis[1][2] = -s * sin( wobbleDegrees );
	axis[1][1] = sqrt( 1.0f - ( axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2] ) );

	// make the second vector exactly perpendicular to the first
	axis[1] -= ( axis[2] * axis[1] ) * axis[2];
	axis[1].Normalize();

	// construct the third with a cross
	axis[0].Cross( axis[1], axis[2] );

	// add the rotate
	s = sin( rotateSpeed * tr.viewDef->floatTime );
	c = cos( rotateSpeed * tr.viewDef->floatTime );

	transform[0] = axis[0][0] * c + axis[1][0] * s;
	transform[4] = axis[0][1] * c + axis[1][1] * s;
	transform[8] = axis[0][2] * c + axis[1][2] * s;

	transform[1] = axis[1][0] * c - axis[0][0] * s;
	transform[5] = axis[1][1] * c - axis[0][1] * s;
	transform[9] = axis[1][2] * c - axis[0][2] * s;

	transform[2] = axis[2][0];
	transform[6] = axis[2][1];
	transform[10] = axis[2][2];

	transform[3] = transform[7] = transform[11] = 0.0f;
	transform[12] = transform[13] = transform[14] = 0.0f;

	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );

	int numVerts = surf->geo->numVerts;
	int size = numVerts * sizeof( idVec3 );
	idVec3 *texCoords = (idVec3 *) _alloca16( size );

	const idDrawVert *verts = surf->geo->verts;
	for ( i = 0; i < numVerts; i++ ) {
		idVec3 v;

		v[0] = verts[i].xyz[0] - localViewOrigin[0];
		v[1] = verts[i].xyz[1] - localViewOrigin[1];
		v[2] = verts[i].xyz[2] - localViewOrigin[2];

		R_LocalPointToGlobal( transform, v, texCoords[i] );
	}

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}

/*
=================
R_SpecularTexGen

Calculates the specular coordinates for cards without vertex programs.
=================
*/
static void R_SpecularTexGen( drawSurf_t *surf, const idVec3 &globalLightOrigin, const idVec3 &viewOrg ) {
	const srfTriangles_t *tri;
	idVec3	localLightOrigin;
	idVec3	localViewOrigin;

	R_GlobalPointToLocal( surf->space->modelMatrix, globalLightOrigin, localLightOrigin );
	R_GlobalPointToLocal( surf->space->modelMatrix, viewOrg, localViewOrigin );

	tri = surf->geo;

	// FIXME: change to 3 component?
	int	size = tri->numVerts * sizeof( idVec4 );
	idVec4 *texCoords = (idVec4 *) _alloca16( size );

#if 1

	SIMDProcessor->CreateSpecularTextureCoords( texCoords, localLightOrigin, localViewOrigin,
											tri->verts, tri->numVerts, tri->indexes, tri->numIndexes );

#else

	bool *used = (bool *)_alloca16( tri->numVerts * sizeof( used[0] ) );
	memset( used, 0, tri->numVerts * sizeof( used[0] ) );

	// because the interaction may be a very small subset of the full surface,
	// it makes sense to only deal with the verts used
	for ( int j = 0; j < tri->numIndexes; j++ ) {
		int i = tri->indexes[j];
		if ( used[i] ) {
			continue;
		}
		used[i] = true;

		float ilength;

		const idDrawVert *v = &tri->verts[i];

		idVec3 lightDir = localLightOrigin - v->xyz;
		idVec3 viewDir = localViewOrigin - v->xyz;

		ilength = idMath::RSqrt( lightDir * lightDir );
		lightDir[0] *= ilength;
		lightDir[1] *= ilength;
		lightDir[2] *= ilength;

		ilength = idMath::RSqrt( viewDir * viewDir );
		viewDir[0] *= ilength;
		viewDir[1] *= ilength;
		viewDir[2] *= ilength;

		lightDir += viewDir;

		texCoords[i][0] = lightDir * v->tangents[0];
		texCoords[i][1] = lightDir * v->tangents[1];
		texCoords[i][2] = lightDir * v->normal;
		texCoords[i][3] = 1;
	}

#endif

	surf->dynamicTexCoords = vertexCache.AllocFrameTemp( texCoords, size );
}


//=======================================================================================================

/*
=============
R_SetEntityDefViewEntity

If the entityDef isn't already on the viewEntity list, create
a viewEntity and add it to the list with an empty scissor rect.

This does not instantiate dynamic models for the entity yet.
=============
*/
viewEntity_t *R_SetEntityDefViewEntity( idRenderEntityLocal *def ) {
	viewEntity_t		*vModel;

	if ( def->viewCount == tr.viewCount ) {
		return def->viewEntity;
	}
	def->viewCount = tr.viewCount;

	// set the model and modelview matricies
	vModel = (viewEntity_t *)R_ClearedFrameAlloc( sizeof( *vModel ) );
	vModel->entityDef = def;

	// the scissorRect will be expanded as the model bounds is accepted into visible portal chains
	vModel->scissorRect.Clear();

	// copy the model and weapon depth hack for back-end use
	vModel->modelDepthHack = def->parms.modelDepthHack;
	vModel->weaponDepthHack = def->parms.weaponDepthHack;

	R_AxisToModelMatrix( def->parms.axis, def->parms.origin, vModel->modelMatrix );

	// we may not have a viewDef if we are just creating shadows at entity creation time
	if ( tr.viewDef ) {
		myGlMultMatrix( vModel->modelMatrix, tr.viewDef->worldSpace.modelViewMatrix, vModel->modelViewMatrix );

		vModel->next = tr.viewDef->viewEntitys;
		tr.viewDef->viewEntitys = vModel;
	}

	def->viewEntity = vModel;

	return vModel;
}

/*
====================
R_TestPointInViewLight
====================
*/
static const float INSIDE_LIGHT_FRUSTUM_SLOP = 32;
// this needs to be greater than the dist from origin to corner of near clip plane
static bool R_TestPointInViewLight( const idVec3 &org, const idRenderLightLocal *light ) {
	int		i;
	idVec3	local;

	for ( i = 0 ; i < 6 ; i++ ) {
		float d = light->frustum[i].Distance( org );
		if ( d > INSIDE_LIGHT_FRUSTUM_SLOP ) {
			return false;
		}
	}

	return true;
}

/*
===================
R_PointInFrustum

Assumes positive sides face outward
===================
*/
static bool R_PointInFrustum( idVec3 &p, idPlane *planes, int numPlanes ) {
	for ( int i = 0 ; i < numPlanes ; i++ ) {
		float d = planes[i].Distance( p );
		if ( d > 0 ) {
			return false;
		}
	}
	return true;
}

/*
=============
R_SetLightDefViewLight

If the lightDef isn't already on the viewLight list, create
a viewLight and add it to the list with an empty scissor rect.
=============
*/
viewLight_t *R_SetLightDefViewLight( idRenderLightLocal *light ) {
	viewLight_t *vLight;

	if ( light->viewCount == tr.viewCount ) {
		return light->viewLight;
	}
	light->viewCount = tr.viewCount;

	// add to the view light chain
	vLight = (viewLight_t *)R_ClearedFrameAlloc( sizeof( *vLight ) );
	vLight->lightDef = light;

	// the scissorRect will be expanded as the light bounds is accepted into visible portal chains
	vLight->scissorRect.Clear();

	// calculate the shadow cap optimization states
	vLight->viewInsideLight = R_TestPointInViewLight( tr.viewDef->renderView.vieworg, light );
	if ( !vLight->viewInsideLight ) {
		vLight->viewSeesShadowPlaneBits = 0;
		for ( int i = 0 ; i < light->numShadowFrustums ; i++ ) {
			float d = light->shadowFrustums[i].planes[5].Distance( tr.viewDef->renderView.vieworg );
			if ( d < INSIDE_LIGHT_FRUSTUM_SLOP ) {
				vLight->viewSeesShadowPlaneBits|= 1 << i;
			}
		}
	} else {
		// this should not be referenced in this case
		vLight->viewSeesShadowPlaneBits = 63;
	}

	// see if the light center is in view, which will allow us to cull invisible shadows
	vLight->viewSeesGlobalLightOrigin = R_PointInFrustum( light->globalLightOrigin, tr.viewDef->frustum, 4 );

	// copy data used by backend
	vLight->globalLightOrigin = light->globalLightOrigin;
	vLight->lightProject[0] = light->lightProject[0];
	vLight->lightProject[1] = light->lightProject[1];
	vLight->lightProject[2] = light->lightProject[2];
	vLight->lightProject[3] = light->lightProject[3];
	vLight->fogPlane = light->frustum[5];
	vLight->frustumTris = light->frustumTris;
	vLight->falloffImage = light->falloffImage;
	vLight->lightShader = light->lightShader;
	vLight->shaderRegisters = NULL;		// allocated and evaluated in R_AddLightSurfaces

	// link the view light
	vLight->next = tr.viewDef->viewLights;
	tr.viewDef->viewLights = vLight;

	light->viewLight = vLight;

	return vLight;
}

/*
=================
idRenderWorldLocal::CreateLightDefInteractions

When a lightDef is determined to effect the view (contact the frustum and non-0 light), it will check to
make sure that it has interactions for all the entityDefs that it might possibly contact.

This does not guarantee that all possible interactions for this light are generated, only that
the ones that may effect the current view are generated. so it does need to be called every view.

This does not cause entityDefs to create dynamic models, all work is done on the referenceBounds.

All entities that have non-empty interactions with viewLights will
have viewEntities made for them and be put on the viewEntity list,
even if their surfaces aren't visible, because they may need to cast shadows.

Interactions are usually removed when a entityDef or lightDef is modified, unless the change
is known to not effect them, so there is no danger of getting a stale interaction, we just need to
check that needed ones are created.

An interaction can be at several levels:

Don't interact (but share an area) (numSurfaces = 0)
Entity reference bounds touches light frustum, but surfaces haven't been generated (numSurfaces = -1)
Shadow surfaces have been generated, but light surfaces have not.  The shadow surface may still be empty due to bounds being conservative.
Both shadow and light surfaces have been generated.  Either or both surfaces may still be empty due to conservative bounds.

=================
*/
void idRenderWorldLocal::CreateLightDefInteractions( idRenderLightLocal *ldef ) {
	areaReference_t		*eref;
	areaReference_t		*lref;
	idRenderEntityLocal		*edef;
	portalArea_t	*area;
	idInteraction	*inter;

	for ( lref = ldef->references ; lref ; lref = lref->ownerNext ) {
		area = lref->area;

		// check all the models in this area
		for ( eref = area->entityRefs.areaNext ; eref != &area->entityRefs ; eref = eref->areaNext ) {
			edef = eref->entity;

			// if the entity doesn't have any light-interacting surfaces, we could skip this,
			// but we don't want to instantiate dynamic models yet, so we can't check that on
			// most things

			// if the entity isn't viewed
			if ( tr.viewDef && edef->viewCount != tr.viewCount ) {
				// if the light doesn't cast shadows, skip
				if ( !ldef->lightShader->LightCastsShadows() ) {
					continue;
				}
				// if we are suppressing its shadow in this view, skip
				if ( !r_skipSuppress.GetBool() ) {
					if ( edef->parms.suppressShadowInViewID && edef->parms.suppressShadowInViewID == tr.viewDef->renderView.viewID ) {
						continue;
					}
					if ( edef->parms.suppressShadowInLightID && edef->parms.suppressShadowInLightID == ldef->parms.lightId ) {
						continue;
					}
				}
			}

			// some big outdoor meshes are flagged to not create any dynamic interactions
			// when the level designer knows that nearby moving lights shouldn't actually hit them
			if ( edef->parms.noDynamicInteractions && edef->world->generateAllInteractionsCalled ) {
				continue;
			}

			// if any of the edef's interaction match this light, we don't
			// need to consider it. 
			if ( r_useInteractionTable.GetBool() && this->interactionTable ) {
				// allocating these tables may take several megs on big maps, but it saves 3% to 5% of
				// the CPU time.  The table is updated at interaction::AllocAndLink() and interaction::UnlinkAndFree()
				int index = ldef->index * this->interactionTableWidth + edef->index;
				inter = this->interactionTable[ index ];
				if ( inter ) {
					// if this entity wasn't in view already, the scissor rect will be empty,
					// so it will only be used for shadow casting
					if ( !inter->IsEmpty() ) {
						R_SetEntityDefViewEntity( edef );
					}
					continue;
				}
			} else {
				// scan the doubly linked lists, which may have several dozen entries

				// we could check either model refs or light refs for matches, but it is
				// assumed that there will be less lights in an area than models
				// so the entity chains should be somewhat shorter (they tend to be fairly close).
				for ( inter = edef->firstInteraction; inter != NULL; inter = inter->entityNext ) {
					if ( inter->lightDef == ldef ) {
						break;
					}
				}

				// if we already have an interaction, we don't need to do anything
				if ( inter != NULL ) {
					// if this entity wasn't in view already, the scissor rect will be empty,
					// so it will only be used for shadow casting
					if ( !inter->IsEmpty() ) {
						R_SetEntityDefViewEntity( edef );
					}
					continue;
				}
			}

			//
			// create a new interaction, but don't do any work other than bbox to frustum culling
			//
			idInteraction *inter = idInteraction::AllocAndLink( edef, ldef );

			// do a check of the entity reference bounds against the light frustum,
			// trying to avoid creating a viewEntity if it hasn't been already
			float	modelMatrix[16];
			float	*m;

			if ( edef->viewCount == tr.viewCount ) {
				m = edef->viewEntity->modelMatrix;
			} else {
				R_AxisToModelMatrix( edef->parms.axis, edef->parms.origin, modelMatrix );
				m = modelMatrix;
			}

			if ( R_CullLocalBox( edef->referenceBounds, m, 6, ldef->frustum ) ) {
				inter->MakeEmpty();
				continue;
			}

			// we will do a more precise per-surface check when we are checking the entity

			// if this entity wasn't in view already, the scissor rect will be empty,
			// so it will only be used for shadow casting
			R_SetEntityDefViewEntity( edef );
		}
	}
}

//===============================================================================================================

/*
=================
R_LinkLightSurf
=================
*/
void R_LinkLightSurf( const drawSurf_t **link, const srfTriangles_t *tri, const viewEntity_t *space, 
				   const idRenderLightLocal *light, const idMaterial *shader, const idScreenRect &scissor, bool viewInsideShadow ) {
	drawSurf_t		*drawSurf;

	if ( !space ) {
		space = &tr.viewDef->worldSpace;
	}

	drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );

	drawSurf->geo = tri;
	drawSurf->space = space;
	drawSurf->material = shader;
	drawSurf->scissorRect = scissor;
	drawSurf->dsFlags = 0;
	if ( viewInsideShadow ) {
		drawSurf->dsFlags |= DSF_VIEW_INSIDE_SHADOW;
	}

	if ( !shader ) {
		// shadows won't have a shader
		drawSurf->shaderRegisters = NULL;
	} else {
		// process the shader expressions for conditionals / color / texcoords
		const float *constRegs = shader->ConstantRegisters();
		if ( constRegs ) {
			// this shader has only constants for parameters
			drawSurf->shaderRegisters = constRegs;
		} else {
			// FIXME: share with the ambient surface?
			float *regs = (float *)R_FrameAlloc( shader->GetNumRegisters() * sizeof( float ) );
			drawSurf->shaderRegisters = regs;
			shader->EvaluateRegisters( regs, space->entityDef->parms.shaderParms, tr.viewDef, space->entityDef->parms.referenceSound );
		}

		// calculate the specular coordinates if we aren't using vertex programs
		if ( !tr.backEndRendererHasVertexPrograms && !r_skipSpecular.GetBool() && tr.backEndRenderer != BE_ARB ) {
			R_SpecularTexGen( drawSurf, light->globalLightOrigin, tr.viewDef->renderView.vieworg );
			// if we failed to allocate space for the specular calculations, drop the surface
			if ( !drawSurf->dynamicTexCoords ) {
				return;
			}
		}
	}

	// actually link it in
	drawSurf->nextOnLight = *link;
	*link = drawSurf;
}

/*
======================
R_ClippedLightScissorRectangle
======================
*/
idScreenRect R_ClippedLightScissorRectangle( viewLight_t *vLight ) {
	int i, j;
	const idRenderLightLocal *light = vLight->lightDef;
	idScreenRect r;
	idFixedWinding w;

	r.Clear();

	for ( i = 0 ; i < 6 ; i++ ) {
		const idWinding *ow = light->frustumWindings[i];

		// projected lights may have one of the frustums degenerated
		if ( !ow ) {
			continue;
		}

		// the light frustum planes face out from the light,
		// so the planes that have the view origin on the negative
		// side will be the "back" faces of the light, which must have
		// some fragment inside the portalStack to be visible
		if ( light->frustum[i].Distance( tr.viewDef->renderView.vieworg ) >= 0 ) {
			continue;
		}

		w = *ow;

		// now check the winding against each of the frustum planes
		for ( j = 0; j < 5; j++ ) {
			if ( !w.ClipInPlace( -tr.viewDef->frustum[j] ) ) {
				break;
			}
		}

		// project these points to the screen and add to bounds
		for ( j = 0; j < w.GetNumPoints(); j++ ) {
			idPlane		eye, clip;
			idVec3		ndc;

			R_TransformModelToClip( w[j].ToVec3(), tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );

			if ( clip[3] <= 0.01f ) {
				clip[3] = 0.01f;
			}

			R_TransformClipToDevice( clip, tr.viewDef, ndc );

			float windowX = 0.5f * ( 1.0f + ndc[0] ) * ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 );
			float windowY = 0.5f * ( 1.0f + ndc[1] ) * ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 );

			if ( windowX > tr.viewDef->scissor.x2 ) {
				windowX = tr.viewDef->scissor.x2;
			} else if ( windowX < tr.viewDef->scissor.x1 ) {
				windowX = tr.viewDef->scissor.x1;
			}
			if ( windowY > tr.viewDef->scissor.y2 ) {
				windowY = tr.viewDef->scissor.y2;
			} else if ( windowY < tr.viewDef->scissor.y1 ) {
				windowY = tr.viewDef->scissor.y1;
			}

			r.AddPoint( windowX, windowY );
		}
	}

	// add the fudge boundary
	r.Expand();

	return r;
}

/*
==================
R_CalcLightScissorRectangle

The light screen bounds will be used to crop the scissor rect during
stencil clears and interaction drawing
==================
*/
int	c_clippedLight, c_unclippedLight;

idScreenRect	R_CalcLightScissorRectangle( viewLight_t *vLight ) {
	idScreenRect	r;
	srfTriangles_t *tri;
	idPlane			eye, clip;
	idVec3			ndc;

	if ( vLight->lightDef->parms.pointLight ) {
		idBounds bounds;
		idRenderLightLocal *lightDef = vLight->lightDef;
		tr.viewDef->viewFrustum.ProjectionBounds( idBox( lightDef->parms.origin, lightDef->parms.lightRadius, lightDef->parms.axis ), bounds );
		return R_ScreenRectFromViewFrustumBounds( bounds );
	}

	if ( r_useClippedLightScissors.GetInteger() == 2 ) {
		return R_ClippedLightScissorRectangle( vLight );
	}

	r.Clear();

	tri = vLight->lightDef->frustumTris;
	for ( int i = 0 ; i < tri->numVerts ; i++ ) {
		R_TransformModelToClip( tri->verts[i].xyz, tr.viewDef->worldSpace.modelViewMatrix,
			tr.viewDef->projectionMatrix, eye, clip );

		// if it is near clipped, clip the winding polygons to the view frustum
		if ( clip[3] <= 1 ) {
			c_clippedLight++;
			if ( r_useClippedLightScissors.GetInteger() ) {
				return R_ClippedLightScissorRectangle( vLight );
			} else {
				r.x1 = r.y1 = 0;
				r.x2 = ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 ) - 1;
				r.y2 = ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 ) - 1;
				return r;
			}
		}

		R_TransformClipToDevice( clip, tr.viewDef, ndc );

		float windowX = 0.5f * ( 1.0f + ndc[0] ) * ( tr.viewDef->viewport.x2 - tr.viewDef->viewport.x1 );
		float windowY = 0.5f * ( 1.0f + ndc[1] ) * ( tr.viewDef->viewport.y2 - tr.viewDef->viewport.y1 );

		if ( windowX > tr.viewDef->scissor.x2 ) {
			windowX = tr.viewDef->scissor.x2;
		} else if ( windowX < tr.viewDef->scissor.x1 ) {
			windowX = tr.viewDef->scissor.x1;
		}
		if ( windowY > tr.viewDef->scissor.y2 ) {
			windowY = tr.viewDef->scissor.y2;
		} else if ( windowY < tr.viewDef->scissor.y1 ) {
			windowY = tr.viewDef->scissor.y1;
		}

		r.AddPoint( windowX, windowY );
	}

	// add the fudge boundary
	r.Expand();

	c_unclippedLight++;

	return r;
}

/*
=================
R_AddLightSurfaces

Calc the light shader values, removing any light from the viewLight list
if it is determined to not have any visible effect due to being flashed off or turned off.

Adds entities to the viewEntity list if they are needed for shadow casting.

Add any precomputed shadow volumes.

Removes lights from the viewLights list if they are completely
turned off, or completely off screen.

Create any new interactions needed between the viewLights
and the viewEntitys due to game movement
=================
*/
void R_AddLightSurfaces( void ) {
	viewLight_t		*vLight;
	idRenderLightLocal *light;
	viewLight_t		**ptr;

	// go through each visible light, possibly removing some from the list
	ptr = &tr.viewDef->viewLights;
	while ( *ptr ) {
		vLight = *ptr;
		light = vLight->lightDef;

		const idMaterial	*lightShader = light->lightShader;
		if ( !lightShader ) {
			common->Error( "R_AddLightSurfaces: NULL lightShader" );
		}

		// see if we are suppressing the light in this view
		if ( !r_skipSuppress.GetBool() ) {
			if ( light->parms.suppressLightInViewID
			&& light->parms.suppressLightInViewID == tr.viewDef->renderView.viewID ) {
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
			if ( light->parms.allowLightInViewID 
			&& light->parms.allowLightInViewID != tr.viewDef->renderView.viewID ) {
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
		}

		// evaluate the light shader registers
		float *lightRegs =(float *)R_FrameAlloc( lightShader->GetNumRegisters() * sizeof( float ) );
		vLight->shaderRegisters = lightRegs;
		lightShader->EvaluateRegisters( lightRegs, light->parms.shaderParms, tr.viewDef, light->parms.referenceSound );

		// if this is a purely additive light and no stage in the light shader evaluates
		// to a positive light value, we can completely skip the light
		if ( !lightShader->IsFogLight() && !lightShader->IsBlendLight() ) {
			int lightStageNum;
			for ( lightStageNum = 0 ; lightStageNum < lightShader->GetNumStages() ; lightStageNum++ ) {
				const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

				// ignore stages that fail the condition
				if ( !lightRegs[ lightStage->conditionRegister ] ) {
					continue;
				}

				const int *registers = lightStage->color.registers;

				// snap tiny values to zero to avoid lights showing up with the wrong color
				if ( lightRegs[ registers[0] ] < 0.001f ) {
					lightRegs[ registers[0] ] = 0.0f;
				}
				if ( lightRegs[ registers[1] ] < 0.001f ) {
					lightRegs[ registers[1] ] = 0.0f;
				}
				if ( lightRegs[ registers[2] ] < 0.001f ) {
					lightRegs[ registers[2] ] = 0.0f;
				}

				// FIXME:	when using the following values the light shows up bright red when using nvidia drivers/hardware
				//			this seems to have been fixed ?
				//lightRegs[ registers[0] ] = 1.5143074e-005f;
				//lightRegs[ registers[1] ] = 1.5483369e-005f;
				//lightRegs[ registers[2] ] = 1.7014690e-005f;

				if ( lightRegs[ registers[0] ] > 0.0f ||
						lightRegs[ registers[1] ] > 0.0f ||
							lightRegs[ registers[2] ] > 0.0f ) {
					break;
				}
			}
			if ( lightStageNum == lightShader->GetNumStages() ) {
				// we went through all the stages and didn't find one that adds anything
				// remove the light from the viewLights list, and change its frame marker
				// so interaction generation doesn't think the light is visible and
				// create a shadow for it
				*ptr = vLight->next;
				light->viewCount = -1;
				continue;
			}
		}

		if ( r_useLightScissors.GetBool() ) {
			// calculate the screen area covered by the light frustum
			// which will be used to crop the stencil cull
			idScreenRect scissorRect = R_CalcLightScissorRectangle( vLight );
			// intersect with the portal crossing scissor rectangle
			vLight->scissorRect.Intersect( scissorRect );

			if ( r_showLightScissors.GetBool() ) {
				R_ShowColoredScreenRect( vLight->scissorRect, light->index );
			}
		}

#if 0
		// this never happens, because CullLightByPortals() does a more precise job
		if ( vLight->scissorRect.IsEmpty() ) {
			// this light doesn't touch anything on screen, so remove it from the list
			*ptr = vLight->next;
			continue;
		}
#endif

		// this one stays on the list
		ptr = &vLight->next;

		// if we are doing a soft-shadow novelty test, regenerate the light with
		// a random offset every time
		if ( r_lightSourceRadius.GetFloat() != 0.0f ) {
			for ( int i = 0 ; i < 3 ; i++ ) {
				light->globalLightOrigin[i] += r_lightSourceRadius.GetFloat() * ( -1 + 2 * (rand()&0xfff)/(float)0xfff );
			}
		}

		// create interactions with all entities the light may touch, and add viewEntities
		// that may cast shadows, even if they aren't directly visible.  Any real work
		// will be deferred until we walk through the viewEntities
		tr.viewDef->renderWorld->CreateLightDefInteractions( light );
		tr.pc.c_viewLights++;

		// fog lights will need to draw the light frustum triangles, so make sure they
		// are in the vertex cache
		if ( lightShader->IsFogLight() ) {
			if ( !light->frustumTris->ambientCache ) {
				if ( !R_CreateAmbientCache( light->frustumTris, false ) ) {
					// skip if we are out of vertex memory
					continue;
				}
			}
			// touch the surface so it won't get purged
			vertexCache.Touch( light->frustumTris->ambientCache );
		}

		// add the prelight shadows for the static world geometry
		if ( light->parms.prelightModel && r_useOptimizedShadows.GetBool() ) {

			if ( !light->parms.prelightModel->NumSurfaces() ) {
				common->Error( "no surfs in prelight model '%s'", light->parms.prelightModel->Name() );
			}

			srfTriangles_t	*tri = light->parms.prelightModel->Surface( 0 )->geometry;
			if ( !tri->shadowVertexes ) {
				common->Error( "R_AddLightSurfaces: prelight model '%s' without shadowVertexes", light->parms.prelightModel->Name() );
			}

			// these shadows will all have valid bounds, and can be culled normally
			if ( r_useShadowCulling.GetBool() ) {
				if ( R_CullLocalBox( tri->bounds, tr.viewDef->worldSpace.modelMatrix, 5, tr.viewDef->frustum ) ) {
					continue;
				}
			}

			// if we have been purged, re-upload the shadowVertexes
			if ( !tri->shadowCache ) {
				R_CreatePrivateShadowCache( tri );
				if ( !tri->shadowCache ) {
					continue;
				}
			}

			// touch the shadow surface so it won't get purged
			vertexCache.Touch( tri->shadowCache );

			if ( !tri->indexCache && r_useIndexBuffers.GetBool() ) {
				vertexCache.Alloc( tri->indexes, tri->numIndexes * sizeof( tri->indexes[0] ), &tri->indexCache, true );
			}
			if ( tri->indexCache ) {
				vertexCache.Touch( tri->indexCache );
			}

			R_LinkLightSurf( &vLight->globalShadows, tri, NULL, light, NULL, vLight->scissorRect, true /* FIXME? */ );
		}
	}
}

//===============================================================================================================

/*
==================
R_IssueEntityDefCallback
==================
*/
bool R_IssueEntityDefCallback( idRenderEntityLocal *def ) {
	bool update;
	idBounds	oldBounds;

	if ( r_checkBounds.GetBool() ) {
		oldBounds = def->referenceBounds;
	}

	def->archived = false;		// will need to be written to the demo file
	tr.pc.c_entityDefCallbacks++;
	if ( tr.viewDef ) {
		update = def->parms.callback( &def->parms, &tr.viewDef->renderView );
	} else {
		update = def->parms.callback( &def->parms, NULL );
	}

	if ( !def->parms.hModel ) {
		common->Error( "R_IssueEntityDefCallback: dynamic entity callback didn't set model" );
	}

	if ( r_checkBounds.GetBool() ) {
		if (	oldBounds[0][0] > def->referenceBounds[0][0] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][1] > def->referenceBounds[0][1] + CHECK_BOUNDS_EPSILON ||
				oldBounds[0][2] > def->referenceBounds[0][2] + CHECK_BOUNDS_EPSILON ||
				oldBounds[1][0] < def->referenceBounds[1][0] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][1] < def->referenceBounds[1][1] - CHECK_BOUNDS_EPSILON ||
				oldBounds[1][2] < def->referenceBounds[1][2] - CHECK_BOUNDS_EPSILON ) {
			common->Printf( "entity %i callback extended reference bounds\n", def->index );
		}
	}

	return update;
}

/*
===================
R_EntityDefDynamicModel

Issues a deferred entity callback if necessary.
If the model isn't dynamic, it returns the original.
Returns the cached dynamic model if present, otherwise creates
it and any necessary overlays
===================
*/
idRenderModel *R_EntityDefDynamicModel( idRenderEntityLocal *def ) {
	bool callbackUpdate;

	// allow deferred entities to construct themselves
	if ( def->parms.callback ) {
		callbackUpdate = R_IssueEntityDefCallback( def );
	} else {
		callbackUpdate = false;
	}

	idRenderModel *model = def->parms.hModel;

	if ( !model ) {
		common->Error( "R_EntityDefDynamicModel: NULL model" );
	}

	if ( model->IsDynamicModel() == DM_STATIC ) {
		def->dynamicModel = NULL;
		def->dynamicModelFrameCount = 0;
		return model;
	}

	// continously animating models (particle systems, etc) will have their snapshot updated every single view
	if ( callbackUpdate || ( model->IsDynamicModel() == DM_CONTINUOUS && def->dynamicModelFrameCount != tr.frameCount ) ) {
		R_ClearEntityDefDynamicModel( def );
	}

	// if we don't have a snapshot of the dynamic model, generate it now
	if ( !def->dynamicModel ) {

		// instantiate the snapshot of the dynamic model, possibly reusing memory from the cached snapshot
		def->cachedDynamicModel = model->InstantiateDynamicModel( &def->parms, tr.viewDef, def->cachedDynamicModel );

		if ( def->cachedDynamicModel ) {

			// add any overlays to the snapshot of the dynamic model
			if ( def->overlay && !r_skipOverlays.GetBool() ) {
				def->overlay->AddOverlaySurfacesToModel( def->cachedDynamicModel );
			} else {
				idRenderModelOverlay::RemoveOverlaySurfacesFromModel( def->cachedDynamicModel );
			}

			if ( r_checkBounds.GetBool() ) {
				idBounds b = def->cachedDynamicModel->Bounds();
				if (	b[0][0] < def->referenceBounds[0][0] - CHECK_BOUNDS_EPSILON ||
						b[0][1] < def->referenceBounds[0][1] - CHECK_BOUNDS_EPSILON ||
						b[0][2] < def->referenceBounds[0][2] - CHECK_BOUNDS_EPSILON ||
						b[1][0] > def->referenceBounds[1][0] + CHECK_BOUNDS_EPSILON ||
						b[1][1] > def->referenceBounds[1][1] + CHECK_BOUNDS_EPSILON ||
						b[1][2] > def->referenceBounds[1][2] + CHECK_BOUNDS_EPSILON ) {
					common->Printf( "entity %i dynamic model exceeded reference bounds\n", def->index );
				}
			}
		}

		def->dynamicModel = def->cachedDynamicModel;
		def->dynamicModelFrameCount = tr.frameCount;
	}

	// set model depth hack value
	if ( def->dynamicModel && model->DepthHack() != 0.0f && tr.viewDef ) {
		idPlane eye, clip;
		idVec3 ndc;
		R_TransformModelToClip( def->parms.origin, tr.viewDef->worldSpace.modelViewMatrix, tr.viewDef->projectionMatrix, eye, clip );
		R_TransformClipToDevice( clip, tr.viewDef, ndc );
		def->parms.modelDepthHack = model->DepthHack() * ( 1.0f - ndc.z );
	}

	// FIXME: if any of the surfaces have deforms, create a frame-temporary model with references to the
	// undeformed surfaces.  This would allow deforms to be light interacting.

	return def->dynamicModel;
}

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf( const srfTriangles_t *tri, const viewEntity_t *space, const renderEntity_t *renderEntity,
					const idMaterial *shader, const idScreenRect &scissor ) {
	drawSurf_t		*drawSurf;
	const float		*shaderParms;
	static float	refRegs[MAX_EXPRESSION_REGISTERS];	// don't put on stack, or VC++ will do a page touch
	float			generatedShaderParms[MAX_ENTITY_SHADER_PARMS];

	drawSurf = (drawSurf_t *)R_FrameAlloc( sizeof( *drawSurf ) );
	drawSurf->geo = tri;
	drawSurf->space = space;
	drawSurf->material = shader;
	drawSurf->scissorRect = scissor;
	drawSurf->sort = shader->GetSort() + tr.sortOffset;
	drawSurf->dsFlags = 0;

	// bumping this offset each time causes surfaces with equal sort orders to still
	// deterministically draw in the order they are added
	tr.sortOffset += 0.000001f;

	// if it doesn't fit, resize the list
	if ( tr.viewDef->numDrawSurfs == tr.viewDef->maxDrawSurfs ) {
		drawSurf_t	**old = tr.viewDef->drawSurfs;
		int			count;

		if ( tr.viewDef->maxDrawSurfs == 0 ) {
			tr.viewDef->maxDrawSurfs = INITIAL_DRAWSURFS;
			count = 0;
		} else {
			count = tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] );
			tr.viewDef->maxDrawSurfs *= 2;
		}
		tr.viewDef->drawSurfs = (drawSurf_t **)R_FrameAlloc( tr.viewDef->maxDrawSurfs * sizeof( tr.viewDef->drawSurfs[0] ) );
		memcpy( tr.viewDef->drawSurfs, old, count );
	}
	tr.viewDef->drawSurfs[tr.viewDef->numDrawSurfs] = drawSurf;
	tr.viewDef->numDrawSurfs++;

	// process the shader expressions for conditionals / color / texcoords
	const float	*constRegs = shader->ConstantRegisters();
	if ( constRegs ) {
		// shader only uses constant values
		drawSurf->shaderRegisters = constRegs;
	} else {
		float *regs = (float *)R_FrameAlloc( shader->GetNumRegisters() * sizeof( float ) );
		drawSurf->shaderRegisters = regs;

		// a reference shader will take the calculated stage color value from another shader
		// and use that for the parm0-parm3 of the current shader, which allows a stage of
		// a light model and light flares to pick up different flashing tables from
		// different light shaders
		if ( renderEntity->referenceShader ) {
			// evaluate the reference shader to find our shader parms
			const shaderStage_t *pStage;

			renderEntity->referenceShader->EvaluateRegisters( refRegs, renderEntity->shaderParms, tr.viewDef, renderEntity->referenceSound );
			pStage = renderEntity->referenceShader->GetStage(0);

			memcpy( generatedShaderParms, renderEntity->shaderParms, sizeof( generatedShaderParms ) );
			generatedShaderParms[0] = refRegs[ pStage->color.registers[0] ];
			generatedShaderParms[1] = refRegs[ pStage->color.registers[1] ];
			generatedShaderParms[2] = refRegs[ pStage->color.registers[2] ];

			shaderParms = generatedShaderParms;
		} else {
			// evaluate with the entityDef's shader parms
			shaderParms = renderEntity->shaderParms;
		}

		float oldFloatTime;
		int oldTime;

		if ( space->entityDef && space->entityDef->parms.timeGroup ) {
			oldFloatTime = tr.viewDef->floatTime;
			oldTime = tr.viewDef->renderView.time;

			tr.viewDef->floatTime = game->GetTimeGroupTime( space->entityDef->parms.timeGroup ) * 0.001;
			tr.viewDef->renderView.time = game->GetTimeGroupTime( space->entityDef->parms.timeGroup );
		}

		shader->EvaluateRegisters( regs, shaderParms, tr.viewDef, renderEntity->referenceSound );

		if ( space->entityDef && space->entityDef->parms.timeGroup ) {
			tr.viewDef->floatTime = oldFloatTime;
			tr.viewDef->renderView.time = oldTime;
		}
	}

	// check for deformations
	R_DeformDrawSurf( drawSurf );

	// skybox surfaces need a dynamic texgen
	switch( shader->Texgen() ) {
		case TG_SKYBOX_CUBE:
			R_SkyboxTexGen( drawSurf, tr.viewDef->renderView.vieworg );
			break;
		case TG_WOBBLESKY_CUBE:
			R_WobbleskyTexGen( drawSurf, tr.viewDef->renderView.vieworg );
			break;
	}

	// check for gui surfaces
	idUserInterface	*gui = NULL;

	if ( !space->entityDef ) {
		gui = shader->GlobalGui();
	} else {
		int guiNum = shader->GetEntityGui() - 1;
		if ( guiNum >= 0 && guiNum < MAX_RENDERENTITY_GUI ) {
			gui = renderEntity->gui[ guiNum ];
		}
		if ( gui == NULL ) {
			gui = shader->GlobalGui();
		}
	}

	if ( gui ) {
		// force guis on the fast time
		float oldFloatTime;
		int oldTime;

		oldFloatTime = tr.viewDef->floatTime;
		oldTime = tr.viewDef->renderView.time;

		tr.viewDef->floatTime = game->GetTimeGroupTime( 1 ) * 0.001;
		tr.viewDef->renderView.time = game->GetTimeGroupTime( 1 );

		idBounds ndcBounds;

		if ( !R_PreciseCullSurface( drawSurf, ndcBounds ) ) {
			// did we ever use this to forward an entity color to a gui that didn't set color?
//			memcpy( tr.guiShaderParms, shaderParms, sizeof( tr.guiShaderParms ) );
			R_RenderGuiSurf( gui, drawSurf );
		}

		tr.viewDef->floatTime = oldFloatTime;
		tr.viewDef->renderView.time = oldTime;
	}

	// we can't add subviews at this point, because that would
	// increment tr.viewCount, messing up the rest of the surface
	// adds for this view
}

/*
===============
R_AddAmbientDrawsurfs

Adds surfaces for the given viewEntity
Walks through the viewEntitys list and creates drawSurf_t for each surface of
each viewEntity that has a non-empty scissorRect
===============
*/
static void R_AddAmbientDrawsurfs( viewEntity_t *vEntity ) {
	int					i, total;
	idRenderEntityLocal	*def;
	srfTriangles_t		*tri;
	idRenderModel		*model;
	const idMaterial	*shader;

	def = vEntity->entityDef;

	if ( def->dynamicModel ) {
		model = def->dynamicModel;
	} else {
		model = def->parms.hModel;
	}

	// add all the surfaces
	total = model->NumSurfaces();
	for ( i = 0 ; i < total ; i++ ) {
		const modelSurface_t	*surf = model->Surface( i );

		// for debugging, only show a single surface at a time
		if ( r_singleSurface.GetInteger() >= 0 && i != r_singleSurface.GetInteger() ) {
			continue;
		}

		tri = surf->geometry;
		if ( !tri ) {
			continue;
		}
		if ( !tri->numIndexes ) {
			continue;
		}
		shader = surf->shader;
		shader = R_RemapShaderBySkin( shader, def->parms.customSkin, def->parms.customShader );

		R_GlobalShaderOverride( &shader );

		if ( !shader ) {	
			continue;
		}
		if ( !shader->IsDrawn() ) {
			continue;
		}

		// debugging tool to make sure we are have the correct pre-calculated bounds
		if ( r_checkBounds.GetBool() ) {
			int j, k;
			for ( j = 0 ; j < tri->numVerts ; j++ ) {
				for ( k = 0 ; k < 3 ; k++ ) {
					if ( tri->verts[j].xyz[k] > tri->bounds[1][k] + CHECK_BOUNDS_EPSILON
						|| tri->verts[j].xyz[k] < tri->bounds[0][k] - CHECK_BOUNDS_EPSILON ) {
						common->Printf( "bad tri->bounds on %s:%s\n", def->parms.hModel->Name(), shader->GetName() );
						break;
					}
					if ( tri->verts[j].xyz[k] > def->referenceBounds[1][k] + CHECK_BOUNDS_EPSILON
						|| tri->verts[j].xyz[k] < def->referenceBounds[0][k] - CHECK_BOUNDS_EPSILON ) {
						common->Printf( "bad referenceBounds on %s:%s\n", def->parms.hModel->Name(), shader->GetName() );
						break;
					}
				}
				if ( k != 3 ) {
					break;
				}
			}
		}

		if ( !R_CullLocalBox( tri->bounds, vEntity->modelMatrix, 5, tr.viewDef->frustum ) ) {

			def->visibleCount = tr.viewCount;

			// make sure we have an ambient cache
			if ( !R_CreateAmbientCache( tri, shader->ReceivesLighting() ) ) {
				// don't add anything if the vertex cache was too full to give us an ambient cache
				return;
			}
			// touch it so it won't get purged
			vertexCache.Touch( tri->ambientCache );

			if ( r_useIndexBuffers.GetBool() && !tri->indexCache ) {
				vertexCache.Alloc( tri->indexes, tri->numIndexes * sizeof( tri->indexes[0] ), &tri->indexCache, true );
			}
			if ( tri->indexCache ) {
				vertexCache.Touch( tri->indexCache );
			}

			// add the surface for drawing
			R_AddDrawSurf( tri, vEntity, &vEntity->entityDef->parms, shader, vEntity->scissorRect );

			// ambientViewCount is used to allow light interactions to be rejected
			// if the ambient surface isn't visible at all
			tri->ambientViewCount = tr.viewCount;
		}
	}

	// add the lightweight decal surfaces
	for ( idRenderModelDecal *decal = def->decals; decal; decal = decal->Next() ) {
		decal->AddDecalDrawSurf( vEntity );
	}
}

/*
==================
R_CalcEntityScissorRectangle
==================
*/
idScreenRect R_CalcEntityScissorRectangle( viewEntity_t *vEntity ) {
	idBounds bounds;
	idRenderEntityLocal *def = vEntity->entityDef;

	tr.viewDef->viewFrustum.ProjectionBounds( idBox( def->referenceBounds, def->parms.origin, def->parms.axis ), bounds );

	return R_ScreenRectFromViewFrustumBounds( bounds );
}

/*
===================
R_AddModelSurfaces

Here is where dynamic models actually get instantiated, and necessary
interactions get created.  This is all done on a sort-by-model basis
to keep source data in cache (most likely L2) as any interactions and
shadows are generated, since dynamic models will typically be lit by
two or more lights.
===================
*/
void R_AddModelSurfaces( void ) {
	viewEntity_t		*vEntity;
	idInteraction		*inter, *next;
	idRenderModel		*model;

	// clear the ambient surface list
	tr.viewDef->numDrawSurfs = 0;
	tr.viewDef->maxDrawSurfs = 0;	// will be set to INITIAL_DRAWSURFS on R_AddDrawSurf

	// go through each entity that is either visible to the view, or to
	// any light that intersects the view (for shadows)
	for ( vEntity = tr.viewDef->viewEntitys; vEntity; vEntity = vEntity->next ) {

		if ( r_useEntityScissors.GetBool() ) {
			// calculate the screen area covered by the entity
			idScreenRect scissorRect = R_CalcEntityScissorRectangle( vEntity );
			// intersect with the portal crossing scissor rectangle
			vEntity->scissorRect.Intersect( scissorRect );

			if ( r_showEntityScissors.GetBool() ) {
				R_ShowColoredScreenRect( vEntity->scissorRect, vEntity->entityDef->index );
			}
		}

		float oldFloatTime;
		int oldTime;

		game->SelectTimeGroup( vEntity->entityDef->parms.timeGroup );

		if ( vEntity->entityDef->parms.timeGroup ) {
			oldFloatTime = tr.viewDef->floatTime;
			oldTime = tr.viewDef->renderView.time;

			tr.viewDef->floatTime = game->GetTimeGroupTime( vEntity->entityDef->parms.timeGroup ) * 0.001;
			tr.viewDef->renderView.time = game->GetTimeGroupTime( vEntity->entityDef->parms.timeGroup );
		}

		if ( tr.viewDef->isXraySubview && vEntity->entityDef->parms.xrayIndex == 1 ) {
			if ( vEntity->entityDef->parms.timeGroup ) {
				tr.viewDef->floatTime = oldFloatTime;
				tr.viewDef->renderView.time = oldTime;
			}
			continue;
		} else if ( !tr.viewDef->isXraySubview && vEntity->entityDef->parms.xrayIndex == 2 ) {
			if ( vEntity->entityDef->parms.timeGroup ) {
				tr.viewDef->floatTime = oldFloatTime;
				tr.viewDef->renderView.time = oldTime;
			}
			continue;
		}

		// add the ambient surface if it has a visible rectangle
		if ( !vEntity->scissorRect.IsEmpty() ) {
			model = R_EntityDefDynamicModel( vEntity->entityDef );
			if ( model == NULL || model->NumSurfaces() <= 0 ) {
				if ( vEntity->entityDef->parms.timeGroup ) {
					tr.viewDef->floatTime = oldFloatTime;
					tr.viewDef->renderView.time = oldTime;
				}
				continue;
			}

			R_AddAmbientDrawsurfs( vEntity );
			tr.pc.c_visibleViewEntities++;
		} else {
			tr.pc.c_shadowViewEntities++;
		}

		//
		// for all the entity / light interactions on this entity, add them to the view
		//
		if ( tr.viewDef->isXraySubview ) {
			if ( vEntity->entityDef->parms.xrayIndex == 2 ) {
				for ( inter = vEntity->entityDef->firstInteraction; inter != NULL && !inter->IsEmpty(); inter = next ) {
					next = inter->entityNext;
					if ( inter->lightDef->viewCount != tr.viewCount ) {
						continue;
					}
					inter->AddActiveInteraction();
				}
			}
		} else {
			// all empty interactions are at the end of the list so once the
			// first is encountered all the remaining interactions are empty
			for ( inter = vEntity->entityDef->firstInteraction; inter != NULL && !inter->IsEmpty(); inter = next ) {
				next = inter->entityNext;

				// skip any lights that aren't currently visible
				// this is run after any lights that are turned off have already
				// been removed from the viewLights list, and had their viewCount cleared
				if ( inter->lightDef->viewCount != tr.viewCount ) {
					continue;
				}
				inter->AddActiveInteraction();
			}
		}

		if ( vEntity->entityDef->parms.timeGroup ) {
			tr.viewDef->floatTime = oldFloatTime;
			tr.viewDef->renderView.time = oldTime;
		}

	}
}

/*
=====================
R_RemoveUnecessaryViewLights
=====================
*/
void R_RemoveUnecessaryViewLights( void ) {
	viewLight_t		*vLight;

	// go through each visible light
	for ( vLight = tr.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		// if the light didn't have any lit surfaces visible, there is no need to
		// draw any of the shadows.  We still keep the vLight for debugging
		// draws
		if ( !vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions ) {
			vLight->localShadows = NULL;
			vLight->globalShadows = NULL;
		}
	}

	if ( r_useShadowSurfaceScissor.GetBool() ) {
		// shrink the light scissor rect to only intersect the surfaces that will actually be drawn.
		// This doesn't seem to actually help, perhaps because the surface scissor
		// rects aren't actually the surface, but only the portal clippings.
		for ( vLight = tr.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
			const drawSurf_t	*surf;
			idScreenRect	surfRect;

			if ( !vLight->lightShader->LightCastsShadows() ) {
				continue;
			}

			surfRect.Clear();

			for ( surf = vLight->globalInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}
			for ( surf = vLight->localShadows ; surf ; surf = surf->nextOnLight ) {
				const_cast<drawSurf_t *>(surf)->scissorRect.Intersect( surfRect );
			}

			for ( surf = vLight->localInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}
			for ( surf = vLight->globalShadows ; surf ; surf = surf->nextOnLight ) {
				const_cast<drawSurf_t *>(surf)->scissorRect.Intersect( surfRect );
			}

			for ( surf = vLight->translucentInteractions ; surf ; surf = surf->nextOnLight ) {
				surfRect.Union( surf->scissorRect );
			}

			vLight->scissorRect.Intersect( surfRect );
		}
	}
}
#endif

#include "draw_local.h"
#include "tr_render.h"
#include "RendererJobs.h"
#include "RendererMetrics.h"
#include "RenderSystemBackend.h"
#include "VulkanBackend.h"
#include "Image.h"
#include "Material.h"
#include "Model.h"
#include "Model_Stuff.h"
#include "VertexCache.h"
#include "../decllib/declTypeHolder.h"
#include "../libs/qglLib/qgl.h"

extern idCVar r_megaDrawMethod;
extern idCVar r_skipWaterFogLights;
extern idCVar r_useMaxVisDist;
extern idCVar com_gpuSpec;

namespace {
	idBlockAlloc< viewEntity_s, 256 > frontEndViewEntityAllocator;
	idBlockAlloc< viewLight_s, 128 > frontEndViewLightAllocator;
	idBlockAlloc< drawSurf_s, 1024 > frontEndDrawSurfaceAllocator;
	idBlockAlloc< srfTriangles_t, 256 > frontEndInteractionGeometryAllocator;
	struct interactionGeometryRecord_t {
		srfTriangles_t* geometry;
		int firstIndex;
	};
	idList< viewEntity_s* > frontEndViewEntities;
	idList< viewLight_s* > frontEndViewLights;
	idList< drawSurf_s* > frontEndDrawSurfaces;
	idList< float* > frontEndRegisters;
	idList< drawSurf_s* > sortedDrawSurfaces;
	idList< glIndex_t > frontEndInteractionIndexes;
	idList< interactionGeometryRecord_t > frontEndInteractionGeometry;
	idList< idRenderModel* > frontEndDynamicModels;
	struct ambientSurfaceCandidate_t {
		viewEntity_s*			space;
		const modelSurface_t*	modelSurface;
		bool				visible;
	};
	struct ambientSurfaceAnalysisContext_t {
		const viewDef_s*		view;
		ambientSurfaceCandidate_t*	candidates;
	};
	idList< viewEntity_s* > frontEndModelSpaces;
	idList< ambientSurfaceCandidate_t > frontEndSurfaceCandidates;
	viewEntity_s* lastViewEntity = NULL;
	viewLight_s* lastViewLight = NULL;
	struct materialRegisterCacheEntry_t {
		const idMaterial*	material;
		const float*		shaderParms;
		idSoundEmitter*	referenceSound;
		float*			registers;
	};
	const int MATERIAL_REGISTER_CACHE_SIZE = 256;
	materialRegisterCacheEntry_t materialRegisterCache[
		MATERIAL_REGISTER_CACHE_SIZE ];

	void AnalyzeAmbientSurfaceCandidates( void* opaqueContext, int firstItem,
		int itemCount, int workerIndex ) {
		ambientSurfaceAnalysisContext_t* context =
			static_cast< ambientSurfaceAnalysisContext_t* >( opaqueContext );
		for ( int index = firstItem; index < firstItem + itemCount; ++index ) {
			ambientSurfaceCandidate_t& candidate = context->candidates[ index ];
			viewEntity_s* space = candidate.space;
			const modelSurface_t* modelSurface = candidate.modelSurface;
			candidate.visible = space != NULL && !space->culled &&
				modelSurface != NULL && modelSurface->geometry != NULL &&
				!R_CullLocalBoxToViewdef( modelSurface->geometry->bounds,
					space->modelMatrix, context->view );
			if ( candidate.visible && space->entityDef != NULL &&
				modelSurface->id >= 0 && modelSurface->id < MAX_SURFACE_BITS - 1 &&
				space->entityDef->hideSurfaceMask.Get( modelSurface->id ) != 0 ) {
				candidate.visible = false;
			}
		}
	}

	int MaterialRegisterCacheIndex( const idMaterial* material,
		const float* shaderParms, const idSoundEmitter* referenceSound ) {
		const size_t materialKey = reinterpret_cast< size_t >( material );
		const size_t parmsKey = reinterpret_cast< size_t >( shaderParms );
		const size_t soundKey = reinterpret_cast< size_t >( referenceSound );
		return static_cast< int >( ( ( materialKey >> 4 ) ^ ( parmsKey >> 5 ) ^
			( soundKey >> 4 ) ) & ( MATERIAL_REGISTER_CACHE_SIZE - 1 ) );
	}

	idVec3 MatrixTransformPoint( const float matrix[ 16 ], const idVec3& point ) {
		return idVec3(
			matrix[ 0 ] * point.x + matrix[ 4 ] * point.y + matrix[ 8 ] * point.z + matrix[ 12 ],
			matrix[ 1 ] * point.x + matrix[ 5 ] * point.y + matrix[ 9 ] * point.z + matrix[ 13 ],
			matrix[ 2 ] * point.x + matrix[ 6 ] * point.y + matrix[ 10 ] * point.z + matrix[ 14 ]
		);
	}

	void SetFullScreenRect( idScreenRect& rect ) {
		viewDef_s* view = RB_GetViewDef();
		if ( view == NULL ) {
			rect.Clear();
			return;
		}
		rect.x1 = 0;
		rect.y1 = 0;
		rect.x2 = view->viewport.x2 - view->viewport.x1;
		rect.y2 = view->viewport.y2 - view->viewport.y1;
		rect.zmin = 0.0f;
		rect.zmax = 1.0f;
	}

	idScreenRect ScreenRectForBounds( const idBounds& bounds, const float modelMatrix[ 16 ] ) {
		idScreenRect fullScreen;
		SetFullScreenRect( fullScreen );
		viewDef_s* view = RB_GetViewDef();
		if ( view == NULL || bounds.IsCleared() ) return fullScreen;

		idBounds worldBounds;
		worldBounds.FromTransformedBounds( bounds, modelMatrix );
		if ( worldBounds.ContainsPoint( view->renderView.vieworg ) ) return fullScreen;

		idScreenRect rect;
		rect.Clear();
		const float width = static_cast< float >( Max( 1, view->viewport.x2 - view->viewport.x1 ) );
		const float height = static_cast< float >( Max( 1, view->viewport.y2 - view->viewport.y1 ) );
		for ( int corner = 0; corner < 8; ++corner ) {
			const idVec3 localPoint(
				bounds[ ( corner >> 0 ) & 1 ].x,
				bounds[ ( corner >> 1 ) & 1 ].y,
				bounds[ ( corner >> 2 ) & 1 ].z
			);
			const idVec3 worldPoint = MatrixTransformPoint( modelMatrix, localPoint );
			const idVec3 eyePoint = MatrixTransformPoint( view->worldSpace.modelViewMatrix, worldPoint );
			const float clipX = eyePoint.x * view->projectionMatrix[ 0 ] + eyePoint.y * view->projectionMatrix[ 4 ] +
				eyePoint.z * view->projectionMatrix[ 8 ] + view->projectionMatrix[ 12 ];
			const float clipY = eyePoint.x * view->projectionMatrix[ 1 ] + eyePoint.y * view->projectionMatrix[ 5 ] +
				eyePoint.z * view->projectionMatrix[ 9 ] + view->projectionMatrix[ 13 ];
			const float clipZ = eyePoint.x * view->projectionMatrix[ 2 ] + eyePoint.y * view->projectionMatrix[ 6 ] +
				eyePoint.z * view->projectionMatrix[ 10 ] + view->projectionMatrix[ 14 ];
			const float clipW = eyePoint.x * view->projectionMatrix[ 3 ] + eyePoint.y * view->projectionMatrix[ 7 ] +
				eyePoint.z * view->projectionMatrix[ 11 ] + view->projectionMatrix[ 15 ];
			// A bound crossing the eye plane needs conservative full-screen clipping.
			if ( clipW <= 0.001f ) return fullScreen;
			const float inverseW = 1.0f / clipW;
			rect.AddPoint( ( clipX * inverseW * 0.5f + 0.5f ) * width,
				( clipY * inverseW * 0.5f + 0.5f ) * height );
			const float depth = clipZ * inverseW * 0.5f + 0.5f;
			rect.zmin = Min( rect.zmin, depth );
			rect.zmax = Max( rect.zmax, depth );
		}
		rect.Expand();
		rect.Intersect( view->scissor );
		return rect;
	}

	void SetEntityMatrix( const renderEntity_t* entity, float matrix[ 16 ] ) {
		memset( matrix, 0, sizeof( float ) * 16 );
		if ( entity == NULL ) {
			matrix[ 0 ] = matrix[ 5 ] = matrix[ 10 ] = matrix[ 15 ] = 1.0f;
			return;
		}
		matrix[ 0 ] = entity->axis[ 0 ].x;
		matrix[ 1 ] = entity->axis[ 0 ].y;
		matrix[ 2 ] = entity->axis[ 0 ].z;
		matrix[ 4 ] = entity->axis[ 1 ].x;
		matrix[ 5 ] = entity->axis[ 1 ].y;
		matrix[ 6 ] = entity->axis[ 1 ].z;
		matrix[ 8 ] = entity->axis[ 2 ].x;
		matrix[ 9 ] = entity->axis[ 2 ].y;
		matrix[ 10 ] = entity->axis[ 2 ].z;
		matrix[ 12 ] = entity->origin.x;
		matrix[ 13 ] = entity->origin.y;
		matrix[ 14 ] = entity->origin.z;
		matrix[ 15 ] = 1.0f;
	}

	void MultiplyModelView( const float viewMatrix[ 16 ], const float modelMatrix[ 16 ], float modelViewMatrix[ 16 ] ) {
		for ( int column = 0; column < 4; ++column ) {
			for ( int row = 0; row < 4; ++row ) {
				modelViewMatrix[ column * 4 + row ] =
					viewMatrix[ row ] * modelMatrix[ column * 4 ] +
					viewMatrix[ 4 + row ] * modelMatrix[ column * 4 + 1 ] +
					viewMatrix[ 8 + row ] * modelMatrix[ column * 4 + 2 ] +
					viewMatrix[ 12 + row ] * modelMatrix[ column * 4 + 3 ];
			}
		}
	}

	drawSurf_s* AllocInteractionSurface( const drawSurf_s* source ) {
		drawSurf_s* interaction = frontEndDrawSurfaceAllocator.Alloc();
		*interaction = *source;
		interaction->nextOnLight = NULL;
		frontEndDrawSurfaces.Append( interaction );
		return interaction;
	}

	int DrawSurfaceSortCompare( drawSurf_s* const* left, drawSurf_s* const* right ) {
		if ( ( *left )->sort < ( *right )->sort ) return -1;
		if ( ( *left )->sort > ( *right )->sort ) return 1;
		if ( ( *left )->material < ( *right )->material ) return -1;
		return ( *left )->material != ( *right )->material;
	}

	bool SurfaceIntersectsLight( const drawSurf_s* surface, const viewLight_s* light ) {
		if ( surface == NULL || surface->geo == NULL || surface->space == NULL || light == NULL ) return false;
		return !R_CullLocalBox( surface->geo->bounds, surface->space->modelMatrix, 6, light->frustum );
	}

	const srfTriangles_t* CreateLightInteractionGeometry( const drawSurf_s* surface, const viewLight_s* light ) {
		if ( surface == NULL || surface->geo == NULL || surface->space == NULL || light == NULL ) return NULL;
		const srfTriangles_t* source = surface->geo;
		if ( source->verts == NULL || source->indexes == NULL || source->numIndexes < 3 ) return source;

		// A fully enclosed surface can share the resident ambient IBO.  Only a
		// surface crossing the light boundary needs a transient index subset.
		if ( R_CullLocalBoxWithin( source->bounds, surface->space->modelMatrix, 6, light->frustum ) < 0 ) {
			return source;
		}

		idPlane localPlanes[ 6 ];
		const float* matrix = surface->space->modelMatrix;
		for ( int planeIndex = 0; planeIndex < 6; ++planeIndex ) {
			const idPlane& worldPlane = light->frustum[ planeIndex ];
			const idVec3& normal = worldPlane.Normal();
			localPlanes[ planeIndex ].Normal().Set(
				normal.x * matrix[ 0 ] + normal.y * matrix[ 1 ] + normal.z * matrix[ 2 ],
				normal.x * matrix[ 4 ] + normal.y * matrix[ 5 ] + normal.z * matrix[ 6 ],
				normal.x * matrix[ 8 ] + normal.y * matrix[ 9 ] + normal.z * matrix[ 10 ]
			);
			localPlanes[ planeIndex ][ 3 ] = worldPlane[ 3 ] +
				normal.x * matrix[ 12 ] + normal.y * matrix[ 13 ] + normal.z * matrix[ 14 ];
		}

		const int firstIndex = frontEndInteractionIndexes.Num();
		const float clipEpsilon = 0.1f;
		for ( int index = 0; index + 2 < source->numIndexes; index += 3 ) {
			const glIndex_t i0 = source->indexes[ index + 0 ];
			const glIndex_t i1 = source->indexes[ index + 1 ];
			const glIndex_t i2 = source->indexes[ index + 2 ];
			if ( i0 >= source->numVerts || i1 >= source->numVerts || i2 >= source->numVerts ) continue;
			bool outside = false;
			for ( int planeIndex = 0; planeIndex < 6; ++planeIndex ) {
				const idPlane& plane = localPlanes[ planeIndex ];
				if ( plane.Distance( source->verts[ i0 ].xyz ) > clipEpsilon &&
					 plane.Distance( source->verts[ i1 ].xyz ) > clipEpsilon &&
					 plane.Distance( source->verts[ i2 ].xyz ) > clipEpsilon ) {
					outside = true;
					break;
				}
			}
			if ( outside ) continue;
			frontEndInteractionIndexes.Append( i0 );
			frontEndInteractionIndexes.Append( i1 );
			frontEndInteractionIndexes.Append( i2 );
		}

		const int numIndexes = frontEndInteractionIndexes.Num() - firstIndex;
		if ( numIndexes == 0 ) return NULL;
		if ( numIndexes == source->numIndexes ) {
			frontEndInteractionIndexes.SetNum( firstIndex, false );
			return source;
		}

		srfTriangles_t* geometry = frontEndInteractionGeometryAllocator.Alloc();
		*geometry = *source;
		geometry->ambientSurface = const_cast< srfTriangles_t* >( source );
		geometry->indexes = NULL;
		geometry->indexCache = NULL;
		geometry->numIndexes = numIndexes;
		geometry->numAllocedIndices = 0;
		interactionGeometryRecord_t record;
		record.geometry = geometry;
		record.firstIndex = firstIndex;
		frontEndInteractionGeometry.Append( record );
		return geometry;
	}

	void FinalizeLightInteractionGeometry() {
		if ( frontEndInteractionGeometry.Num() == 0 ) return;
		glIndex_t* indexes = frontEndInteractionIndexes.Begin();
		for ( int i = 0; i < frontEndInteractionGeometry.Num(); ++i ) {
			interactionGeometryRecord_t& record = frontEndInteractionGeometry[ i ];
			record.geometry->indexes = indexes + record.firstIndex;
			record.geometry->indexCache = vertexCache.AllocFrameTemp( record.geometry->indexes,
				record.geometry->numIndexes * sizeof( record.geometry->indexes[ 0 ] ) );
			if ( record.geometry->indexCache != NULL ) {
				record.geometry->indexCache->indexBuffer = true;
			}
		}
	}

	bool SurfaceSharesLightArea( const drawSurf_s* surface, const viewLight_s* light ) {
		if ( surface == NULL || surface->space == NULL || light == NULL || light->lightDef == NULL ||
				light->lightDef->numAreas <= 0 || light->lightDef->flags.atmosphereLight ) {
			return true;
		}

		const int* surfaceAreas = NULL;
		int numSurfaceAreas = 0;
		if ( surface->space->entityDef != NULL && surface->space->entityDef->numAreas > 0 ) {
			surfaceAreas = surface->space->entityDef->areas;
			numSurfaceAreas = surface->space->entityDef->numAreas;
		} else if ( surface->space->model != NULL ) {
			const idList< int >* fixedAreas = surface->space->model->GetFixedAreas();
			if ( fixedAreas != NULL && fixedAreas->Num() > 0 ) {
				surfaceAreas = fixedAreas->Begin();
				numSurfaceAreas = fixedAreas->Num();
			}
		}
		if ( surfaceAreas == NULL ) return true;

		for ( int surfaceAreaIndex = 0; surfaceAreaIndex < numSurfaceAreas; ++surfaceAreaIndex ) {
			for ( int lightAreaIndex = 0; lightAreaIndex < light->lightDef->numAreas; ++lightAreaIndex ) {
				if ( surfaceAreas[ surfaceAreaIndex ] == light->lightDef->areas[ lightAreaIndex ] ) return true;
			}
		}
		return false;
	}

	bool IsSkippedWaterFog( const idMaterial* material ) {
		return r_skipWaterFogLights.GetBool() && material != NULL && material->IsFogLight() &&
			idStr::Icmpn( material->GetName(), "fogs/waterFog", 13 ) == 0;
	}

	bool EntityVisibleBeforeSnapshot( renderEntity_t* entity, idRenderModel* model,
		const viewDef_s* view, idBounds& visibilityBounds ) {
		visibilityBounds.Clear();
		if ( entity == NULL || model == NULL || view == NULL ||
			entity->drawSpec > com_gpuSpec.GetInteger() ) {
			return false;
		}
		if ( entity->numInsts > 0 ) {
			return true;
		}
		visibilityBounds = !entity->bounds.IsCleared() ?
			entity->bounds : model->Bounds( entity );
		if ( entity->maxVisDist > 0 && r_useMaxVisDist.GetInteger() > 0 ) {
			const int requestedDistance = r_useMaxVisDist.GetInteger();
			const int maxVisDist = requestedDistance > 1 ?
				requestedDistance : entity->maxVisDist;
			const idVec3 center = visibilityBounds.IsCleared() ?
				vec3_origin : visibilityBounds.GetCenter();
			if ( !R_DistanceVisibility( entity->origin + center, maxVisDist,
				entity->minVisDist, view ) ) {
				return false;
			}
		}
		if ( !visibilityBounds.IsCleared() ) {
			float modelMatrix[ 16 ];
			SetEntityMatrix( entity, modelMatrix );
			if ( R_CullLocalBoxToViewdef( visibilityBounds, modelMatrix, view ) ) {
				return false;
			}
		}
		return true;
	}
}

viewEntity_s* R_SetEntityDefViewEntity( renderEntity_t* entity, idRenderModel* model,
	int entityIndex, const idBounds* suppliedBounds ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return NULL;
	float modelMatrix[ 16 ];
	SetEntityMatrix( entity, modelMatrix );
	const int numInsts = entity != NULL ? entity->numInsts : 0;
	idBounds modelBounds;
	modelBounds.Clear();
	if ( suppliedBounds != NULL && !suppliedBounds->IsCleared() ) {
		modelBounds = *suppliedBounds;
	} else if ( model != NULL ) {
		modelBounds = model->Bounds( entity );
	} else if ( entity != NULL ) {
		modelBounds = entity->bounds;
	}
	if ( entity != NULL ) {
		if ( entity->drawSpec > com_gpuSpec.GetInteger() ) {
			return NULL;
		}
		if ( entity->maxVisDist > 0 && r_useMaxVisDist.GetInteger() > 0 && numInsts <= 0 ) {
			const int maxVisDist = r_useMaxVisDist.GetInteger() > 1 ? r_useMaxVisDist.GetInteger() : entity->maxVisDist;
			const idVec3 center = modelBounds.IsCleared() ? vec3_origin : modelBounds.GetCenter();
			if ( !R_DistanceVisibility( entity->origin + center, maxVisDist, entity->minVisDist, view ) ) {
				return NULL;
			}
		}
	}
	if ( model != NULL && numInsts <= 0 && !modelBounds.IsCleared() ) {
		if ( R_CullLocalBoxToViewdef( modelBounds, modelMatrix, view ) ) {
			return NULL;
		}
	}
	viewEntity_s* space = frontEndViewEntityAllocator.Alloc();
	memset( space, 0, sizeof( *space ) );
	space->entityDef = entity;
	space->entityIndex = entityIndex;
	space->model = model;
	space->occtest = entity != NULL && entity->flags.occlusionTest;
	space->coverage = entity != NULL && entity->flags.overridenCoverage ? entity->coverage : 1.0f;
	space->minGpuSpec = entity != NULL ? entity->minGpuSpec : 0;
	space->numInsts = numInsts;
	space->insts = entity != NULL ? entity->insts : NULL;
	// Retail reserves at least 128 surface ids and an additional 33 ids beyond
	// the model's surface count, then stores the bit set through ambSurf.
	space->maxSurfID = Max( model != NULL ? model->NumSurfaces() + 33 : 128, 128 );
	if ( !R_UseVulkanBackend() ) {
		space->ambSurf = new unsigned int[ ( space->maxSurfID + 31 ) >> 5 ];
		memset( space->ambSurf, 0,
			sizeof( unsigned int ) * ( ( space->maxSurfID + 31 ) >> 5 ) );
	}
	space->weaponDepthHack = entity != NULL && entity->flags.weaponDepthHack;
	space->foliageDepthHack = entity != NULL && entity->flags.foliageDepthHack;
	space->modelDepthHack = entity != NULL ? entity->modelDepthHack : 0.0f;
	space->weaponDepthHackFOV_x = entity != NULL ? entity->weaponDepthHackFOV_x : 0.0f;
	space->weaponDepthHackFOV_y = entity != NULL ? entity->weaponDepthHackFOV_y : 0.0f;
	if ( entity != NULL && entity->ambientCubeMap != NULL ) {
		space->ambientCubeMap = entity->ambientCubeMap;
	} else if ( model != NULL && entity == NULL ) {
		space->ambientCubeMap = view->renderWorld->BackendAmbientCubeMapForModel( model );
	} else if ( entity != NULL ) {
		space->ambientCubeMap = view->renderWorld->BackendAmbientCubeMapForArea( view->renderWorld->PointInArea( entity->origin ) );
	} else {
		space->ambientCubeMap = view->renderWorld->BackendAmbientCubeMap();
	}
	memcpy( space->modelMatrix, modelMatrix, sizeof( modelMatrix ) );
	MultiplyModelView( view->worldSpace.modelViewMatrix, space->modelMatrix, space->modelViewMatrix );
	space->scissorRect = model != NULL && !modelBounds.IsCleared() ?
		ScreenRectForBounds( modelBounds, space->modelMatrix ) : view->scissor;
	space->culled = space->scissorRect.IsEmpty();
	if ( lastViewEntity != NULL ) lastViewEntity->next = space;
	else view->viewEntities = space;
	lastViewEntity = space;
	frontEndViewEntities.Append( space );
	return space;
}

viewEntity_s* R_SetEntityDefViewEntity( renderEntity_t* entity,
	idRenderModel* model, int entityIndex ) {
	return R_SetEntityDefViewEntity( entity, model, entityIndex, NULL );
}

viewLight_s* R_SetLightDefViewLight( renderLight_t* light, int lightIndex ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || light == NULL ) return NULL;
	if ( light->drawSpec > com_gpuSpec.GetInteger() ) return NULL;
	if ( light->maxVisDist > 0 && r_useMaxVisDist.GetInteger() > 0 ) {
		const int maxVisDist = r_useMaxVisDist.GetInteger() > 1 ? r_useMaxVisDist.GetInteger() : light->maxVisDist;
		if ( !R_DistanceVisibility( light->origin, maxVisDist, 0, view ) ) return NULL;
	}
	viewLight_s* vLight = frontEndViewLightAllocator.Alloc();
	memset( vLight, 0, sizeof( *vLight ) );
	vLight->lightDef = light;
	vLight->lightIndex = lightIndex;
	R_DeriveLightData( *light, vLight->lightProject, vLight->globalLightOrigin, vLight->material, vLight->falloffImage );
	R_RenderLightFrustum( *light, vLight->frustum );
	vLight->frustumTris = R_PolytopeSurface( 6, vLight->frustum, NULL );
	vLight->culled = vLight->frustumTris == NULL || vLight->frustumTris->bounds.IsCleared() ||
		R_CullLocalBoxToViewdef( vLight->frustumTris->bounds, view->worldSpace.modelMatrix, view );
	vLight->globalLightDirection = light->flags.parallel ? light->lightCenter : light->axis[ 0 ];
	vLight->globalLightDirection.Normalize();
	vLight->lightRadius = light->lightRadius;
	vLight->lightRadiusLength = light->lightRadius.Length();
	// ETQW copies the derived light's rear frustum plane into viewLight::fogPlane.
	// This is not the raw falloff projection plane: frustum[ 5 ] includes the
	// far-edge offset and normalization performed by R_SetLightFrustum.  Feeding
	// lightProject[ 3 ] to the fog-enter calculation makes large water fog lights
	// saturate across their volume, which paints Valley with the fog's exact
	// ( 0.21, 0.20, 0.12 ) colour.
	vLight->fogPlane = vLight->frustum[ 5 ];
	vLight->fadeFraction = 1.0f;
	const float identityMatrix[ 16 ] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	vLight->scissorRect = vLight->frustumTris != NULL ? ScreenRectForBounds( vLight->frustumTris->bounds, identityMatrix ) : view->scissor;
	vLight->culled = vLight->culled || vLight->scissorRect.IsEmpty();
	if ( light->flags.pointLight ) {
		const idVec3 delta = view->renderView.vieworg - light->origin;
		vLight->viewInsideLight = true;
		for ( int axis = 0; axis < 3; ++axis ) {
			if ( idMath::Fabs( delta * light->axis[ axis ] ) > idMath::Fabs( light->lightRadius[ axis ] ) ) {
				vLight->viewInsideLight = false;
				break;
			}
		}
	}
	if ( vLight->material != NULL ) {
		const float* constantRegisters = vLight->material->ConstantRegisters( light->shaderParms, view );
		if ( constantRegisters != NULL ) {
			vLight->lightRegisters = const_cast< float* >( constantRegisters );
		} else {
			vLight->lightRegisters = new float[ Max( vLight->material->GetNumRegisters(), 1 ) ];
			vLight->material->EvaluateRegisters( vLight->lightRegisters, light->shaderParms, view, light->referenceSound, 0 );
			frontEndRegisters.Append( vLight->lightRegisters );
		}
	}
	if ( lastViewLight != NULL ) lastViewLight->next = vLight;
	else view->viewLights = vLight;
	lastViewLight = vLight;
	frontEndViewLights.Append( vLight );
	if ( light->flags.atmosphereLight ) view->atmosphereLight = vLight;
	return vLight;
}

drawSurf_s* R_AddDrawSurf( const srfTriangles_t* triangles, const viewEntity_s* space,
		const renderEntity_t* renderEntity, const idMaterial* material,
		const idScreenRect& scissor, int surfID,
		const float* reusedMaterialRegisters = NULL ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || triangles == NULL || space == NULL || material == NULL ||
		!material->IsDrawn() ) return NULL;
	drawSurf_s* surface = frontEndDrawSurfaceAllocator.Alloc();
	memset( surface, 0, sizeof( *surface ) );
	surface->geo = triangles;
	surface->space = space;
	surface->material = material;
	surface->sort = material->GetSort();
	surface->surfID = surfID;
	surface->dsFlags = triangles->dsFlags;
	surface->scissorRect = scissor;
	const float* shaderParms = renderEntity != NULL ? renderEntity->shaderParms : view->renderView.shaderParms;
	idSoundEmitter* referenceSound = renderEntity != NULL ? renderEntity->referenceSound : NULL;
	const int registerCacheIndex = MaterialRegisterCacheIndex( material,
		shaderParms, referenceSound );
	materialRegisterCacheEntry_t& registerCache =
		materialRegisterCache[ registerCacheIndex ];
	const bool registerCacheHit = registerCache.material == material &&
		registerCache.shaderParms == shaderParms &&
		registerCache.referenceSound == referenceSound;
	const float* constantRegisters = reusedMaterialRegisters != NULL ?
		reusedMaterialRegisters : ( registerCacheHit ? registerCache.registers :
		material->ConstantRegisters( shaderParms, view ) );
	if ( constantRegisters != NULL ) {
		surface->materialRegisters = const_cast< float* >( constantRegisters );
	} else {
		float* registers = new float[ Max( material->GetNumRegisters(), 1 ) ];
		material->EvaluateRegisters( registers, shaderParms, view, referenceSound, 0 );
		surface->materialRegisters = registers;
		frontEndRegisters.Append( registers );
	}
	registerCache.material = material;
	registerCache.shaderParms = shaderParms;
	registerCache.referenceSound = referenceSound;
	registerCache.registers = surface->materialRegisters;
	frontEndDrawSurfaces.Append( surface );
	sortedDrawSurfaces.Append( surface );
	return surface;
}

void R_CommitAmbientDrawsurf( viewEntity_s* space,
		const modelSurface_t* modelSurface, bool streamDynamicVertices,
		const idMaterial*& previousMaterial, const float*& previousMaterialRegisters ) {
	if ( space == NULL || modelSurface == NULL || modelSurface->geometry == NULL ) return;
	srfTriangles_t* geometry = modelSurface->geometry;
	const idMaterial* material = modelSurface->material;
	if ( space->entityDef != NULL ) {
		material = R_RemapShaderBySkin( material, space->entityDef->customSkin,
			space->entityDef->customShader );
	}
	R_GlobalShaderOverride( &material );
	// Allocations, cache residency, shader evaluation, and list insertion stay
	// on the render thread.  Workers only decide which immutable candidates are
	// visible.
	if ( streamDynamicVertices && !geometry->hardwareSkinnedSurface &&
		geometry->verts != NULL && geometry->numVerts > 0 ) {
		geometry->ambientCache = vertexCache.AllocFrameTemp( geometry->verts,
			geometry->numVerts * sizeof( geometry->verts[ 0 ] ) );
	} else if ( geometry->ambientCache == NULL && geometry->verts != NULL &&
		geometry->numVerts > 0 ) {
		vertexCache.Alloc( geometry->verts,
			geometry->numVerts * sizeof( geometry->verts[ 0 ] ),
			&geometry->ambientCache );
	}
	if ( geometry->indexCache == NULL && geometry->indexes != NULL &&
		geometry->numIndexes > 0 ) {
		vertexCache.Alloc( geometry->indexes,
			geometry->numIndexes * sizeof( geometry->indexes[ 0 ] ),
			&geometry->indexCache, true );
	}
	if ( geometry->ambientCache != NULL && geometry->ambientCache->tag != TAG_TEMP ) {
		vertexCache.Touch( geometry->ambientCache );
	}
	if ( geometry->indexCache != NULL ) vertexCache.Touch( geometry->indexCache );
	if ( geometry->weightCache != NULL ) vertexCache.Touch( geometry->weightCache );
	const float* reusedRegisters = material == previousMaterial ?
		previousMaterialRegisters : NULL;
	drawSurf_s* drawSurface = R_AddDrawSurf( geometry, space, space->entityDef,
		material, space->scissorRect, modelSurface->id, reusedRegisters );
	previousMaterial = material;
	previousMaterialRegisters = drawSurface != NULL ?
		drawSurface->materialRegisters : NULL;
}

void R_AddAmbientDrawsurfs( viewEntity_s* space ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || space == NULL || space->model == NULL || space->culled ) return;
	const bool streamDynamicVertices = R_UseVulkanBackend() &&
		idStr::Icmp( space->model->Name(), "_MD5_Snapshot_" ) == 0;
	const idMaterial* previousMaterial = NULL;
	const float* previousMaterialRegisters = NULL;
	for ( int surfaceIndex = 0; surfaceIndex < space->model->NumSurfaces(); ++surfaceIndex ) {
		const modelSurface_t* modelSurface = space->model->Surface( surfaceIndex );
		if ( modelSurface == NULL || modelSurface->geometry == NULL ) continue;
		if ( R_CullLocalBoxToViewdef( modelSurface->geometry->bounds,
			space->modelMatrix, view ) ) continue;
		if ( space->entityDef != NULL && modelSurface->id >= 0 &&
			modelSurface->id < MAX_SURFACE_BITS - 1 &&
			space->entityDef->hideSurfaceMask.Get( modelSurface->id ) != 0 ) continue;
		R_CommitAmbientDrawsurf( space, modelSurface, streamDynamicVertices,
			previousMaterial, previousMaterialRegisters );
	}
}

void R_AddModelSurfaces() {
	viewDef_s* view = RB_GetViewDef();
	idRenderWorldLocal* world = RB_GetDrawWorld();
	if ( view == NULL || world == NULL ) return;
	if ( !R_UseVulkanBackend() ) {
		for ( int modelIndex = 0; modelIndex < world->BackendNumLocalModels(); ++modelIndex ) {
			idRenderModel* model = world->BackendLocalModel( modelIndex );
			if ( model == NULL || !model->IsStaticWorldModel() ) continue;
			viewEntity_s* modelSpace = R_SetEntityDefViewEntity( NULL, model, -1 );
			if ( modelSpace != NULL ) R_AddAmbientDrawsurfs( modelSpace );
		}
		for ( viewEntity_s* entity = view->viewEntities; entity != NULL;
			entity = entity->next ) {
			if ( entity->entityDef != NULL ) R_AddAmbientDrawsurfs( entity );
		}
		sortedDrawSurfaces.Sort( DrawSurfaceSortCompare );
		view->drawSurfs = sortedDrawSurfaces.Begin();
		view->numDrawSurfs = sortedDrawSurfaces.Num();
		return;
	}

	frontEndModelSpaces.SetNum( 0, false );
	{
		RENDER_METRIC_SCOPE( "Collect model spaces" );
		for ( int modelIndex = 0; modelIndex < world->BackendNumLocalModels(); ++modelIndex ) {
			idRenderModel* model = world->BackendLocalModel( modelIndex );
			if ( model == NULL || !model->IsStaticWorldModel() ) continue;
			viewEntity_s* modelSpace = R_SetEntityDefViewEntity( NULL, model, -1 );
			if ( modelSpace != NULL ) frontEndModelSpaces.Append( modelSpace );
		}
		for ( viewEntity_s* entity = view->viewEntities; entity != NULL; entity = entity->next ) {
			if ( entity->entityDef != NULL ) frontEndModelSpaces.Append( entity );
		}
	}

	int candidateCount = 0;
	for ( int spaceIndex = 0; spaceIndex < frontEndModelSpaces.Num(); ++spaceIndex ) {
		viewEntity_s* space = frontEndModelSpaces[ spaceIndex ];
		if ( space != NULL && space->model != NULL && !space->culled ) {
			candidateCount += space->model->NumSurfaces();
		}
	}
	frontEndSurfaceCandidates.SetNum( candidateCount );
	int candidateIndex = 0;
	for ( int spaceIndex = 0; spaceIndex < frontEndModelSpaces.Num(); ++spaceIndex ) {
		viewEntity_s* space = frontEndModelSpaces[ spaceIndex ];
		if ( space == NULL || space->model == NULL || space->culled ) continue;
		for ( int surfaceIndex = 0; surfaceIndex < space->model->NumSurfaces(); ++surfaceIndex ) {
			ambientSurfaceCandidate_t& candidate =
				frontEndSurfaceCandidates[ candidateIndex++ ];
			candidate.space = space;
			candidate.modelSurface = space->model->Surface( surfaceIndex );
			candidate.visible = false;
		}
	}
	ambientSurfaceAnalysisContext_t analysisContext;
	analysisContext.view = view;
	analysisContext.candidates = frontEndSurfaceCandidates.Begin();
	rendererJobs.ParallelFor( candidateCount, 64, "Analyze model surfaces",
		AnalyzeAmbientSurfaceCandidates, &analysisContext );

	{
		RENDER_METRIC_SCOPE( "Commit model surfaces" );
		viewEntity_s* previousSpace = NULL;
		const idMaterial* previousMaterial = NULL;
		const float* previousMaterialRegisters = NULL;
		bool streamDynamicVertices = false;
		for ( int index = 0; index < candidateCount; ++index ) {
			ambientSurfaceCandidate_t& candidate = frontEndSurfaceCandidates[ index ];
			if ( candidate.space != previousSpace ) {
				previousSpace = candidate.space;
				previousMaterial = NULL;
				previousMaterialRegisters = NULL;
				streamDynamicVertices = previousSpace != NULL && previousSpace->model != NULL &&
					idStr::Icmp( previousSpace->model->Name(), "_MD5_Snapshot_" ) == 0;
			}
			if ( !candidate.visible ) continue;
			R_CommitAmbientDrawsurf( candidate.space, candidate.modelSurface,
				streamDynamicVertices, previousMaterial, previousMaterialRegisters );
		}
	}
	{
		RENDER_METRIC_SCOPE( "Sort draw surfaces" );
		sortedDrawSurfaces.Sort( DrawSurfaceSortCompare );
	}
	view->drawSurfs = sortedDrawSurfaces.Begin();
	view->numDrawSurfs = sortedDrawSurfaces.Num();
}

void R_AddLightSurfaces() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	// The Vulkan raster path shades ambient draw surfaces directly and ray
	// tracing consumes view-light metadata, not the legacy per-light interaction
	// chains.  Building and triangle-clipping those chains was pure CPU work.
	if ( R_UseVulkanBackend() ) return;
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next ) {
		if ( light->culled || light->material == NULL || IsSkippedWaterFog( light->material ) ) continue;
		for ( int surfaceIndex = 0; surfaceIndex < sortedDrawSurfaces.Num(); ++surfaceIndex ) {
			drawSurf_s* surface = sortedDrawSurfaces[ surfaceIndex ];
			if ( !SurfaceSharesLightArea( surface, light ) ) continue;
			if ( !SurfaceIntersectsLight( surface, light ) ) continue;
			const bool local = surface->space != NULL && surface->space->entityDef != NULL;
			if ( !light->lightDef->flags.noShadows && light->material->LightCastsShadows() &&
					surface->material->SurfaceCastsShadow() && surface->geo != NULL &&
					( surface->geo->shadowCache != NULL || surface->geo->shadowVertexes != NULL ) ) {
				drawSurf_s* shadow = AllocInteractionSurface( surface );
				shadow->scissorRect = light->scissorRect;
				if ( local ) {
					shadow->nextOnLight = light->localShadows;
					light->localShadows = shadow;
				} else {
					shadow->nextOnLight = light->globalShadows;
					light->globalShadows = shadow;
				}
			}

			if ( light->material->IsFogLight() || light->material->IsBlendLight() ) {
				if ( !surface->material->ReceivesFog() ) continue;
			} else if ( !surface->material->ReceivesLighting() ) {
				continue;
			}
			idScreenRect interactionScissor = surface->scissorRect;
			interactionScissor.Intersect( light->scissorRect );
			if ( interactionScissor.IsEmpty() ) continue;
			const srfTriangles_t* interactionGeometry = CreateLightInteractionGeometry( surface, light );
			if ( interactionGeometry == NULL ) continue;
			drawSurf_s* interaction = AllocInteractionSurface( surface );
			interaction->geo = interactionGeometry;
			interaction->scissorRect = interactionScissor;
			if ( surface->material->Coverage() == MC_TRANSLUCENT ||
					surface->material->TestMaterialFlag( MF_TRANSLUCENTINTERACTION ) ) {
				interaction->nextOnLight = light->translucentInteractions;
				light->translucentInteractions = interaction;
			} else if ( light->lightDef->flags.atmosphereLight && r_megaDrawMethod.GetInteger() != 0 ) {
				interaction->nextOnLight = light->mtInteractions;
				light->mtInteractions = interaction;

				viewEntity_s* mutableSpace = const_cast< viewEntity_s* >( surface->space );
				if ( mutableSpace != NULL && mutableSpace->ambSurf != NULL &&
						surface->surfID >= 0 && surface->surfID < mutableSpace->maxSurfID ) {
					mutableSpace->ambSurf[ surface->surfID >> 5 ] |= 1u << ( surface->surfID & 31 );
				}
			} else if ( local ) {
				interaction->nextOnLight = light->localInteractions;
				light->localInteractions = interaction;
			} else {
				interaction->nextOnLight = light->globalInteractions;
				light->globalInteractions = interaction;
			}
		}
	}
	FinalizeLightInteractionGeometry();
}

void R_RemoveUnecessaryViewLights() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	// Keep all culled/derived light records for Vulkan ray-tracing light upload.
	// With no legacy interaction chains they would otherwise all look empty.
	if ( R_UseVulkanBackend() ) return;
	viewLight_s** link = &view->viewLights;
	while ( *link != NULL ) {
		viewLight_s* light = *link;
		const bool empty = light->globalShadows == NULL && light->localShadows == NULL &&
			light->globalInteractions == NULL && light->localInteractions == NULL &&
			light->translucentInteractions == NULL && light->mtInteractions == NULL;
		if ( empty && light != view->atmosphereLight ) {
			*link = light->next;
			continue;
		}
		link = &light->next;
	}
}

void R_FreeBuiltDrawView() {
	for ( int index = 0; index < frontEndRegisters.Num(); ++index ) delete[] frontEndRegisters[ index ];
	for ( int index = 0; index < frontEndDrawSurfaces.Num(); ++index ) {
		frontEndDrawSurfaceAllocator.Free( frontEndDrawSurfaces[ index ] );
	}
	for ( int index = 0; index < frontEndViewLights.Num(); ++index ) {
		R_FreePolytopeSurface( const_cast< srfTriangles_t* >( frontEndViewLights[ index ]->frustumTris ) );
		frontEndViewLights[ index ]->frustumTris = NULL;
		frontEndViewLightAllocator.Free( frontEndViewLights[ index ] );
	}
	for ( int index = 0; index < frontEndViewEntities.Num(); ++index ) {
		delete[] frontEndViewEntities[ index ]->ambSurf;
		frontEndViewEntities[ index ]->ambSurf = NULL;
		frontEndViewEntityAllocator.Free( frontEndViewEntities[ index ] );
	}
	for ( int index = 0; index < frontEndInteractionGeometry.Num(); ++index ) {
		frontEndInteractionGeometryAllocator.Free( frontEndInteractionGeometry[ index ].geometry );
	}
	for ( int index = 0; index < frontEndDynamicModels.Num(); ++index ) {
		delete frontEndDynamicModels[ index ];
	}
	frontEndRegisters.Clear();
	frontEndDrawSurfaces.Clear();
	frontEndViewLights.Clear();
	frontEndViewEntities.Clear();
	frontEndInteractionGeometry.Clear();
	frontEndDynamicModels.Clear();
	frontEndInteractionIndexes.Clear();
	sortedDrawSurfaces.Clear();
	frontEndModelSpaces.SetNum( 0, false );
	frontEndSurfaceCandidates.SetNum( 0, false );
	memset( materialRegisterCache, 0, sizeof( materialRegisterCache ) );
	lastViewEntity = NULL;
	lastViewLight = NULL;
}

void R_BuildDrawView( idRenderWorldLocal* renderWorld, const renderView_t* renderView ) {
	RENDER_METRIC_SCOPE( "Front end build" );
	viewDef_s* view = RB_GetViewDef();
	R_FreeBuiltDrawView();
	if ( view == NULL ) return;
	memset( view, 0, sizeof( *view ) );
	if ( renderWorld == NULL || renderView == NULL ) return;
	view->renderWorld = renderWorld;
	view->renderView = *renderView;
	view->floatTime = renderView->time * 0.001f;
	view->atmosphere = renderWorld->GetAtmosphere();
	R_SetupExpressionMemory( view );
	renderSystemBackend.RenderViewToViewport( renderView, &view->viewport );
	R_SetupMatrices( view, true );
	view->worldSpace.ambientCubeMap = renderWorld->BackendAmbientCubeMap();
	SetFullScreenRect( view->scissor );
	view->worldSpace.scissorRect = view->scissor;
	{
		RENDER_METRIC_SCOPE( "Prepare effects" );
		renderWorld->BackendPrepareEffects( renderView );
	}

	{
		RENDER_METRIC_SCOPE( "Prepare entities and dynamic models" );
	for ( int entityIndex = 0; entityIndex < renderWorld->BackendNumEntityDefs(); ++entityIndex ) {
		renderEntity_t* entity = renderWorld->BackendEntityDef( entityIndex );
		if ( entity == NULL ) continue;
		if ( entity->suppressSurfaceInViewID != 0 && entity->suppressSurfaceInViewID == renderView->viewID ) continue;
		if ( entity->allowSurfaceInViewID != 0 && entity->allowSurfaceInViewID != renderView->viewID ) continue;
		if ( entity->callback != NULL ) {
			float modelMatrix[ 16 ];
			SetEntityMatrix( entity, modelMatrix );
			idBounds callbackBounds;
			callbackBounds.Clear();
			if ( !entity->bounds.IsCleared() ) callbackBounds = entity->bounds;
			else if ( entity->hModel != NULL ) callbackBounds = entity->hModel->Bounds( entity );
			if ( !callbackBounds.IsCleared() && R_CullLocalBoxToViewdef( callbackBounds, modelMatrix, view ) ) continue;
			if ( entity->maxVisDist > 0 && r_useMaxVisDist.GetInteger() > 0 ) {
				const int maxVisDist = r_useMaxVisDist.GetInteger() > 1 ? r_useMaxVisDist.GetInteger() : entity->maxVisDist;
				const idVec3 visibilityOrigin = entity->origin + ( callbackBounds.IsCleared() ? vec3_origin : callbackBounds.GetCenter() );
				if ( !R_DistanceVisibility( visibilityOrigin, maxVisDist, entity->minVisDist, view ) ) continue;
			}
			int lastModifiedGameTime = 0;
			entity->callback( entity, renderView, lastModifiedGameTime );
		}
		if ( entity->hModel == NULL ) continue;
		idBounds visibilityBounds;
		if ( !EntityVisibleBeforeSnapshot( entity, entity->hModel, view,
			visibilityBounds ) ) {
			continue;
		}
		idRenderModel* drawModel = entity->hModel;
		if ( R_GetStuffModelSnapshot( entity->hModel, entity, view, drawModel ) && drawModel == NULL ) continue;
		idRenderModel* dynamicModel = renderWorld->BackendInstantiateDynamicModel(
			entityIndex, drawModel, entity );
		if ( dynamicModel == NULL ) continue;
		if ( dynamicModel != drawModel ) {
			drawModel = dynamicModel;
		}
		R_SetEntityDefViewEntity( entity, drawModel, entityIndex,
			&visibilityBounds );
	}
	}
	{
		RENDER_METRIC_SCOPE( "Prepare effects geometry" );
	for ( int effectIndex = 0; effectIndex < renderWorld->BackendNumPreparedEffects(); ++effectIndex ) {
		renderEntity_t* effectEntity = renderWorld->BackendPreparedEffect( effectIndex );
		if ( effectEntity == NULL || effectEntity->hModel == NULL ) continue;
		R_SetEntityDefViewEntity( effectEntity, effectEntity->hModel, -1 - effectIndex );
	}
	}
	if ( renderWorld->BackendNumLocalModels() > 0 || view->viewEntities != NULL ) {
		view->worldSpace.next = view->viewEntities;
		view->viewEntities = &view->worldSpace;
		lastViewEntity = frontEndViewEntities.Num() > 0 ? frontEndViewEntities[ frontEndViewEntities.Num() - 1 ] : &view->worldSpace;
	}
	{
		RENDER_METRIC_SCOPE( "Add model surfaces" );
		R_AddModelSurfaces();
	}

	{
		RENDER_METRIC_SCOPE( "Prepare lights" );
	for ( int lightIndex = 0; lightIndex < renderWorld->BackendNumLightDefs(); ++lightIndex ) {
		renderLight_t* light = renderWorld->BackendLightDef( lightIndex );
		if ( light == NULL ) continue;
		if ( light->suppressLightInViewID != 0 && light->suppressLightInViewID == renderView->viewID ) continue;
		if ( light->allowLightInViewID != 0 && light->allowLightInViewID != renderView->viewID ) continue;
		R_SetLightDefViewLight( light, lightIndex );
	}
	}
	{
		RENDER_METRIC_SCOPE( "Add light surfaces" );
		R_AddLightSurfaces();
	}
	{
		RENDER_METRIC_SCOPE( "Cull empty view lights" );
		R_RemoveUnecessaryViewLights();
	}
}
