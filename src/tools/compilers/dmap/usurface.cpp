/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "dmap.h"


#define	 TEXTURE_OFFSET_EQUAL_EPSILON	0.005
#define	 TEXTURE_VECTOR_EQUAL_EPSILON	0.001

/*
===============
AddTriListToArea

The triList is appended to the apropriate optimzeGroup_t,
creating a new one if needed.
The entire list is assumed to come from the same planar primitive
===============
*/
static void AddTriListToArea( uEntity_t *e, mapTri_t *triList, int planeNum, int areaNum, textureVectors_t *texVec ) {
	uArea_t		*area;
	optimizeGroup_t	*group;
	int			i, j;

	if ( !triList ) {
		return;
	}

	area = &e->areas[areaNum];
	for ( group = area->groups ; group ; group = group->nextGroup ) {
		if ( group->material == triList->material
			&& group->planeNum == planeNum
			&& group->mergeGroup == triList->mergeGroup ) {
			// check the texture vectors
			for ( i = 0 ; i < 2 ; i++ ) {
				for ( j = 0 ; j < 3 ; j++ ) {
					if ( idMath::Fabs( texVec->v[i][j] - group->texVec.v[i][j] ) > TEXTURE_VECTOR_EQUAL_EPSILON ) {
						break;
					}
				}
				if ( j != 3 ) {
					break;
				}
				if ( idMath::Fabs( texVec->v[i][3] - group->texVec.v[i][3] ) > TEXTURE_OFFSET_EQUAL_EPSILON ) {
					break;
				}
			}
			if ( i == 2 ) {
				break;	// exact match
			} else {
				// different texture offsets
				i = 1;	// just for debugger breakpoint
			}
		}
	}

	if ( !group ) {
		group = (optimizeGroup_t *)Mem_Alloc( sizeof( *group ) );
		memset( group, 0, sizeof( *group ) );
		group->planeNum = planeNum;
		group->mergeGroup = triList->mergeGroup;
		group->material = triList->material;
		group->nextGroup = area->groups;
		group->texVec = *texVec;
		area->groups = group;
	}

	group->triList = MergeTriLists( group->triList, triList );
}

/*
===================
TexVecForTri
===================
*/
static void TexVecForTri( textureVectors_t *texVec, mapTri_t *tri ) {
	float	area, inva;
	idVec3	temp;
	idVec5	d0, d1;
	idDrawVert	*a, *b, *c;

	a = &tri->v[0];
	b = &tri->v[1];
	c = &tri->v[2];

	d0[0] = b->xyz[0] - a->xyz[0];
	d0[1] = b->xyz[1] - a->xyz[1];
	d0[2] = b->xyz[2] - a->xyz[2];
	const idVec2 aST = a->GetST();
	const idVec2 bST = b->GetST();
	const idVec2 cST = c->GetST();
	d0[3] = bST[0] - aST[0];
	d0[4] = bST[1] - aST[1];

	d1[0] = c->xyz[0] - a->xyz[0];
	d1[1] = c->xyz[1] - a->xyz[1];
	d1[2] = c->xyz[2] - a->xyz[2];
	d1[3] = cST[0] - aST[0];
	d1[4] = cST[1] - aST[1];

	area = d0[3] * d1[4] - d0[4] * d1[3];
	inva = 1.0 / area;

    temp[0] = (d0[0] * d1[4] - d0[4] * d1[0]) * inva;
    temp[1] = (d0[1] * d1[4] - d0[4] * d1[1]) * inva;
    temp[2] = (d0[2] * d1[4] - d0[4] * d1[2]) * inva;
	temp.Normalize();
	texVec->v[0].ToVec3() = temp;
	texVec->v[0][3] = tri->v[0].xyz * texVec->v[0].ToVec3() - aST[0];

    temp[0] = (d0[3] * d1[0] - d0[0] * d1[3]) * inva;
    temp[1] = (d0[3] * d1[1] - d0[1] * d1[3]) * inva;
    temp[2] = (d0[3] * d1[2] - d0[2] * d1[3]) * inva;
	temp.Normalize();
	texVec->v[1].ToVec3() = temp;
	texVec->v[1][3] = tri->v[0].xyz * texVec->v[0].ToVec3() - aST[1];
}


/*
=================
TriListForSide
=================
*/
//#define	SNAP_FLOAT_TO_INT	8
#define	SNAP_FLOAT_TO_INT	256
#define	SNAP_INT_TO_FLOAT	(1.0/SNAP_FLOAT_TO_INT)

mapTri_t *TriListForSide( const side_t *s, const idWinding *w ) {
	int				i, j;
	idDrawVert		*dv;
	mapTri_t		*tri, *triList;
	const idVec3		*vec;
	const idMaterial	*si;

	si = s->material;

	// skip any generated faces
	if ( !si ) {
		return NULL;
	}

	// don't create faces for non-visible sides
	if ( !si->SurfaceCastsShadow() && !si->IsDrawn() ) {
		return NULL;
	}

	if ( 1 ) {
		// triangle fan using only the outer verts
		// this gives the minimum triangle count,
		// but may have some very distended triangles
		triList = NULL;
		for ( i = 2 ; i < w->GetNumPoints() ; i++ ) {
			tri = AllocTri();
			tri->material = si;	
			tri->next = triList;
			triList = tri;

			for ( j = 0 ; j < 3 ; j++ ) {
				if ( j == 0 ) {
					vec = &((*w)[0]).ToVec3();
				} else if ( j == 1 ) {
					vec = &((*w)[i-1]).ToVec3();
				} else {
					vec = &((*w)[i]).ToVec3();
				}

				dv = tri->v + j;
#if 0
				// round the xyz to a given precision
				for ( k = 0 ; k < 3 ; k++ ) {
					dv->xyz[k] = SNAP_INT_TO_FLOAT * floor( vec[k] * SNAP_FLOAT_TO_INT + 0.5 );
				}
#else
				VectorCopy( *vec, dv->xyz );
#endif
				
				// calculate texture s/t from brush primitive texture matrix
				dv->SetST(
					DotProduct( dv->xyz, s->texVec.v[0] ) + s->texVec.v[0][3],
					DotProduct( dv->xyz, s->texVec.v[1] ) + s->texVec.v[1][3]
				);

				// copy normal
				const idVec3 normal = dmapGlobals.mapPlanes[s->planenum].Normal();
				dv->SetNormal( normal );
				if ( normal.Length() < 0.9 || normal.Length() > 1.1 ) {
					common->Error( "Bad normal in TriListForSide" );
				}
			}
		}
	} else {
		// triangle fan from central point, more verts and tris, but less distended
		// I use this when debugging some tjunction problems
		triList = NULL;
		for ( i = 0 ; i < w->GetNumPoints() ; i++ ) {
			idVec3	midPoint;

			tri = AllocTri();
			tri->material = si;	
			tri->next = triList;
			triList = tri;

			for ( j = 0 ; j < 3 ; j++ ) {
				if ( j == 0 ) {
					vec = &midPoint;
					midPoint = w->GetCenter();
				} else if ( j == 1 ) {
					vec = &((*w)[i]).ToVec3();
				} else {
					vec = &((*w)[(i+1)%w->GetNumPoints()]).ToVec3();
				}

				dv = tri->v + j;

				VectorCopy( *vec, dv->xyz );
				
				// calculate texture s/t from brush primitive texture matrix
				dv->SetST(
					DotProduct( dv->xyz, s->texVec.v[0] ) + s->texVec.v[0][3],
					DotProduct( dv->xyz, s->texVec.v[1] ) + s->texVec.v[1][3]
				);

				// copy normal
				const idVec3 normal = dmapGlobals.mapPlanes[s->planenum].Normal();
				dv->SetNormal( normal );
				if ( normal.Length() < 0.9f || normal.Length() > 1.1f ) {
					common->Error( "Bad normal in TriListForSide" );
				}
			}
		}
	}

	// set merge groups if needed, to prevent multiple sides from being
	// merged into a single surface in the case of gui shaders, mirrors, and autosprites
	if ( s->material->IsDiscrete() ) {
		for ( tri = triList ; tri ; tri = tri->next ) {
			tri->mergeGroup = (void *)s;
		}
	}

	return triList;
}

//=================================================================================

/*
====================
ClipSideByTree_r

Adds non-opaque leaf fragments to the convex hull
====================
*/
static void ClipSideByTree_r( idWinding *w, side_t *side, node_t *node ) {
	idWinding		*front, *back;

	if ( !w ) {
		return;
	}

	if ( node->planenum != PLANENUM_LEAF ) {
		if ( side->planenum == node->planenum ) {
			ClipSideByTree_r( w, side, node->children[0] );
			return;
		}
		if ( side->planenum == ( node->planenum ^ 1) ) {
			ClipSideByTree_r( w, side, node->children[1] );
			return;
		}

		w->Split( dmapGlobals.mapPlanes[ node->planenum ], ON_EPSILON, &front, &back );
		delete w;

		ClipSideByTree_r( front, side, node->children[0] );
		ClipSideByTree_r( back, side, node->children[1] );

		return;
	}

	// if opaque leaf, don't add
	if ( !node->opaque ) {
		if ( !side->visibleHull ) {
			side->visibleHull = w->Copy();
		}
		else {
			side->visibleHull->AddToConvexHull( w, dmapGlobals.mapPlanes[ side->planenum ].Normal() );
		}
	}

	delete w;
	return;
}


/*
=====================
ClipSidesByTree

Creates side->visibleHull for all visible sides

The visible hull for a side will consist of the convex hull of
all points in non-opaque clusters, which allows overlaps
to be trimmed off automatically.
=====================
*/
void ClipSidesByTree( uEntity_t *e ) {
	uBrush_t		*b;
	int				i;
	idWinding		*w;
	side_t			*side;
	primitive_t		*prim;

	common->Printf( "----- ClipSidesByTree -----\n");

	for ( prim = e->primitives ; prim ; prim = prim->next ) {
		b = prim->brush;
		if ( !b ) {
			// FIXME: other primitives!
			continue;
		}
		for ( i = 0 ; i < b->numsides ; i++ ) {
			side = &b->sides[i];
			if ( !side->winding) {
				continue;
			}
			w = side->winding->Copy();
			side->visibleHull = NULL;
			ClipSideByTree_r( w, side, e->tree->headnode );
			// for debugging, we can choose to use the entire original side
			// but we skip this if the side was completely clipped away
			if ( side->visibleHull && dmapGlobals.noClipSides ) {
				delete side->visibleHull;
				side->visibleHull = side->winding->Copy();
			}
		}
	}
}



//=================================================================================

/*
====================
ClipTriIntoTree_r

This is used for adding curve triangles
The winding will be freed before it returns
====================
*/
void ClipTriIntoTree_r( idWinding *w, mapTri_t *originalTri, uEntity_t *e, node_t *node ) {
	idWinding		*front, *back;

	if ( !w ) {
		return;
	}

	if ( node->planenum != PLANENUM_LEAF ) {
		w->Split( dmapGlobals.mapPlanes[ node->planenum ], ON_EPSILON, &front, &back );
		delete w;

		ClipTriIntoTree_r( front, originalTri, e, node->children[0] );
		ClipTriIntoTree_r( back, originalTri, e, node->children[1] );

		return;
	}

	// if opaque leaf, don't add
	if ( !node->opaque && node->area >= 0 ) {
		mapTri_t	*list;
		int			planeNum;
		idPlane		plane;
		textureVectors_t	texVec;

		list = WindingToTriList( w, originalTri );

		PlaneForTri( originalTri, plane );
		planeNum = FindFloatPlane( plane );

		TexVecForTri( &texVec, originalTri );

		AddTriListToArea( e, list, planeNum, node->area, &texVec );
	}

	delete w;
	return;
}



//=============================================================

/*
====================
CheckWindingInAreas_r

Returns the area number that the winding is in, or
-2 if it crosses multiple areas.

====================
*/
static int CheckWindingInAreas_r( const idWinding *w, node_t *node ) {
	idWinding		*front, *back;

	if ( !w ) {
		return -1;
	}

	if ( node->planenum != PLANENUM_LEAF ) {
		int		a1, a2;
#if 0
		if ( side->planenum == node->planenum ) {
			return CheckWindingInAreas_r( w, node->children[0] );
		}
		if ( side->planenum == ( node->planenum ^ 1) ) {
			return CheckWindingInAreas_r( w, node->children[1] );
		}
#endif
		w->Split( dmapGlobals.mapPlanes[ node->planenum ], ON_EPSILON, &front, &back );

		a1 = CheckWindingInAreas_r( front, node->children[0] );
		delete front;
		a2 = CheckWindingInAreas_r( back, node->children[1] );
		delete back;

		if ( a1 == -2 || a2 == -2 ) {
			return -2;	// different
		}
		if ( a1 == -1 ) {
			return a2;	// one solid
		}
		if ( a2 == -1 ) {
			return a1;	// one solid
		}

		if ( a1 != a2 ) {
			return -2;	// cross areas
		}
		return a1;
	}

	return node->area;
}



/*
====================
PutWindingIntoAreas_r

Clips a winding down into the bsp tree, then converts
the fragments to triangles and adds them to the area lists
====================
*/
static void PutWindingIntoAreas_r( uEntity_t *e, const idWinding *w, side_t *side, node_t *node ) {
	idWinding		*front, *back;
	int				area;

	if ( !w ) {
		return;
	}

	if ( node->planenum != PLANENUM_LEAF ) {
		if ( side->planenum == node->planenum ) {
			PutWindingIntoAreas_r( e, w, side, node->children[0] );
			return;
		}
		if ( side->planenum == ( node->planenum ^ 1) ) {
			PutWindingIntoAreas_r( e, w, side, node->children[1] );
			return;
		}

		// see if we need to split it
		// adding the "noFragment" flag to big surfaces like sky boxes
		// will avoid potentially dicing them up into tons of triangles
		// that take forever to optimize back together
		if ( !dmapGlobals.fullCarve || side->material->NoFragment() ) {
			area = CheckWindingInAreas_r( w, node );
			if ( area >= 0 ) {
				mapTri_t	*tri;

				// put in single area
				tri = TriListForSide( side, w );
				AddTriListToArea( e, tri, side->planenum, area, &side->texVec );
				return;
			}
		}

		w->Split( dmapGlobals.mapPlanes[ node->planenum ], ON_EPSILON, &front, &back );

		PutWindingIntoAreas_r( e, front, side, node->children[0] );
		if ( front ) {
			delete front;
		}

		PutWindingIntoAreas_r( e, back, side, node->children[1] );
		if ( back ) {
			delete back;
		}

		return;
	}

	// if opaque leaf, don't add
	if ( node->area >= 0 && !node->opaque ) {
		mapTri_t	*tri;

		tri = TriListForSide( side, w );
		AddTriListToArea( e, tri, side->planenum, node->area, &side->texVec );
	}
}

/*
==================
AddMapTriToAreas

Used for curves and inlined models
==================
*/
void AddMapTriToAreas( mapTri_t *tri, uEntity_t *e ) {
	int				area;
	idWinding		*w;

	// skip degenerate triangles from pinched curves
	if ( MapTriArea( tri ) <= 0 ) {
		return;
	}

	if ( dmapGlobals.fullCarve ) {
		// always fragment into areas
		w = WindingForTri( tri );
		ClipTriIntoTree_r( w, tri, e, e->tree->headnode );
		return;
	}

	w = WindingForTri( tri );
	area = CheckWindingInAreas_r( w, e->tree->headnode );
	delete w;
	if ( area == -1 ) {
		return;
	}
	if ( area >= 0 ) {
		mapTri_t	*newTri;
		idPlane		plane;
		int			planeNum;
		textureVectors_t	texVec;

		// put in single area
		newTri = CopyMapTri( tri );
		newTri->next = NULL;

		PlaneForTri( tri, plane );
		planeNum = FindFloatPlane( plane );

		TexVecForTri( &texVec, newTri );

		AddTriListToArea( e, newTri, planeNum, area, &texVec );
	} else {
		// fragment into areas
		w = WindingForTri( tri );
		ClipTriIntoTree_r( w, tri, e, e->tree->headnode );
	}
}

/*
=====================
PutPrimitivesInAreas

=====================
*/
void PutPrimitivesInAreas( uEntity_t *e ) {
	uBrush_t		*b;
	int				i;
	side_t			*side;
	primitive_t		*prim;
	mapTri_t		*tri;

	common->Printf( "----- PutPrimitivesInAreas -----\n");

	// allocate space for surface chains for each area
	e->areas = (uArea_t *)Mem_Alloc( e->numAreas * sizeof( e->areas[0] ) );
	memset( e->areas, 0, e->numAreas * sizeof( e->areas[0] ) );

	// for each primitive, clip it to the non-solid leafs
	// and divide it into different areas
	for ( prim = e->primitives ; prim ; prim = prim->next ) {
		b = prim->brush;

		if ( !b ) {
			// add curve triangles
			for ( tri = prim->tris ; tri ; tri = tri->next ) {
				AddMapTriToAreas( tri, e );
			}
			continue;
		}

		// clip in brush sides
		for ( i = 0 ; i < b->numsides ; i++ ) {
			side = &b->sides[i];
			if ( !side->visibleHull ) {
				continue;
			}
			PutWindingIntoAreas_r( e, side->visibleHull, side, e->tree->headnode );
		}
	}


	// optionally inline some of the func_static models
	if ( dmapGlobals.entityNum == 0 ) {
		for ( int eNum = 1 ; eNum < dmapGlobals.num_entities ; eNum++ ) {
			uEntity_t *entity = &dmapGlobals.uEntities[eNum];
			const char *className = entity->mapEntity->epairs.GetString( "classname" );
			if ( idStr::Icmp( className, "func_static" ) ) {
				continue;
			}
			if ( !Dmap_ShouldInlineStatic( entity->mapEntity ) ) {
				continue;
			}
			const char *modelName = entity->mapEntity->epairs.GetString( "model" );
			if ( !modelName || !modelName[0] ) {
				continue;
			}
			renderEntity_t renderEntity;
			gameEdit->ParseSpawnArgsToRenderEntity( entity->mapEntity->epairs, renderEntity );
			idRenderModel *model = renderEntity.hModel;
			if ( !model || model->IsDefaultModel() || model->IsDynamicModel() != DM_STATIC ) {
				continue;
			}

			common->Printf( "inlining %s.\n", entity->mapEntity->epairs.GetString( "name" ) );

			for ( i = 0 ; i < model->NumSurfaces() ; i++ ) {
				const modelSurface_t *surface = model->Surface( i );
				const srfTriangles_t *tri = surface->geometry;
				const idMaterial *material = R_RemapShaderBySkin( surface->material,
					renderEntity.customSkin, renderEntity.customShader );
				if ( !tri || !material ) {
					continue;
				}

				mapTri_t	mapTri;
				memset( &mapTri, 0, sizeof( mapTri ) );
				mapTri.material = material;
				// don't let discretes (autosprites, etc) merge together
				if ( mapTri.material->IsDiscrete() ) {
					mapTri.mergeGroup = (void *)surface;
				}
				for ( int j = 0 ; j < tri->numIndexes ; j += 3 ) {
					for ( int k = 0 ; k < 3 ; k++ ) {
						idVec3 v = tri->verts[tri->indexes[j+k]].xyz;

						mapTri.v[k].xyz = v * renderEntity.axis + renderEntity.origin;

						mapTri.v[k].SetNormal( tri->verts[tri->indexes[j+k]].GetNormal() * renderEntity.axis );
						mapTri.v[k].SetST( tri->verts[tri->indexes[j+k]].GetST() );
					}
					AddMapTriToAreas( &mapTri, e );
				}
			}
		}
	}
}

// ETQW uses runtime lighting and its mapProcFile010 surfaces do not contain
// Doom 3 prelight interaction lists, so dmap intentionally stops after area
// assignment and mesh optimization.

