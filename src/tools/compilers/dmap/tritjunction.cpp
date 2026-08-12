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

/*

  T junction fixing never creates more xyz points, but
  new vertexes will be created when different surfaces
  cause a fix

  The vertex cleaning accomplishes two goals: removing extranious low order
  bits to avoid numbers like 1.000001233, and grouping nearby vertexes
  together.  Straight truncation accomplishes the first foal, but two vertexes
  only a tiny epsilon apart could still be spread to different snap points.
  To avoid this, we allow the merge test to group points together that
  snapped to neighboring integer coordinates.

  Snaping verts can drag some triangles backwards or collapse them to points,
  which will cause them to be removed.
  

  When snapping to ints, a point can move a maximum of sqrt(3)/2 distance
  Two points that were an epsilon apart can then become sqrt(3) apart

  A case that causes recursive overflow with point to triangle fixing:

               A
	C            D
	           B

  Triangle ABC tests against point D and splits into triangles ADC and DBC
  Triangle DBC then tests against point A again and splits into ABC and ADB
  infinite recursive loop


  For a given source triangle
	init the no-check list to hold the three triangle hashVerts

  recursiveFixTriAgainstHash

  recursiveFixTriAgainstHashVert_r
	if hashVert is on the no-check list
		exit
	if the hashVert should split the triangle
		add to the no-check list
		recursiveFixTriAgainstHash(a)
		recursiveFixTriAgainstHash(b)

*/

#define	SNAP_FRACTIONS	32
//#define	SNAP_FRACTIONS	8
//#define	SNAP_FRACTIONS	1

#define	VERTEX_EPSILON	( 1.0 / SNAP_FRACTIONS )

#define	COLINEAR_EPSILON	( 1.8 * VERTEX_EPSILON )

#define	HASH_BINS	64

typedef struct hashVert_s {
	struct hashVert_s	*next;
	idVec3				v;
	int					iv[3];
	unsigned int		visitStamp;
} hashVert_t;

static idBounds	hashBounds;
static idVec3	hashScale;
static hashVert_t	*hashVerts[HASH_BINS][HASH_BINS][HASH_BINS];
static idList<int>	hashUsedBuckets;
static int		numHashVerts, numTotalVerts;
static int		hashIntMins[3], hashIntScale[3];
static unsigned int hashVisitStamp;
static unsigned long long numTJunctionCandidates;

static int HashBlockForInt( int axis, int value ) {
	int block = ( value - hashIntMins[axis] ) / hashIntScale[axis];
	return idMath::ClampInt( 0, HASH_BINS - 1, block );
}

/*
===============
GetHashVert

Also modifies the original vert to the snapped value
===============
*/
struct hashVert_s	*GetHashVert( idVec3 &v ) {
	int		iv[3];
	int		block[3];
	int		i;
	hashVert_t	*hv;

	numTotalVerts++;

	// snap the vert to integral values
	for ( i = 0 ; i < 3 ; i++ ) {
		iv[i] = floor( ( v[i] + 0.5/SNAP_FRACTIONS ) * SNAP_FRACTIONS );
		block[i] = HashBlockForInt( i, iv[i] );
	}

	// see if a vertex near enough already exists
	// Search the few adjacent buckets reached by the epsilon, so subdivision of
	// the hash cannot separate two vertices that should snap together.
	int blockMins[3];
	int blockMaxs[3];
	for ( i = 0; i < 3; i++ ) {
		blockMins[i] = HashBlockForInt( i, iv[i] - 1 );
		blockMaxs[i] = HashBlockForInt( i, iv[i] + 1 );
	}
	for ( int blockX = blockMins[0]; blockX <= blockMaxs[0]; blockX++ ) {
		for ( int blockY = blockMins[1]; blockY <= blockMaxs[1]; blockY++ ) {
			for ( int blockZ = blockMins[2]; blockZ <= blockMaxs[2]; blockZ++ ) {
				for ( hv = hashVerts[blockX][blockY][blockZ] ; hv ; hv = hv->next ) {
					for ( i = 0 ; i < 3 ; i++ ) {
						int	d;
						d = hv->iv[i] - iv[i];
						if ( d < -1 || d > 1 ) {
							break;
						}
					}
					if ( i == 3 ) {
						VectorCopy( hv->v, v );
						return hv;
					}
				}
			}
		}
	}

	// create a new one 
	hv = (hashVert_t *)Mem_Alloc( sizeof( *hv ) );

	if ( hashVerts[block[0]][block[1]][block[2]] == NULL ) {
		hashUsedBuckets.Append( ( block[0] * HASH_BINS + block[1] ) * HASH_BINS + block[2] );
	}
	hv->next = hashVerts[block[0]][block[1]][block[2]];
	hashVerts[block[0]][block[1]][block[2]] = hv;

	hv->iv[0] = iv[0];
	hv->iv[1] = iv[1];
	hv->iv[2] = iv[2];
	hv->visitStamp = 0;

	hv->v[0] = (float)iv[0] / SNAP_FRACTIONS;
	hv->v[1] = (float)iv[1] / SNAP_FRACTIONS;
	hv->v[2] = (float)iv[2] / SNAP_FRACTIONS;

	VectorCopy( hv->v, v );

	numHashVerts++;

	return hv;
}


/*
==================
HashBlocksForEdge

Returns an inclusive bounding box of hash
bins that can contain a vertex on the edge
==================
*/
static void HashBlocksForEdge( const idVec3 &start, const idVec3 &end, int blocks[2][3] ) {
	idBounds	bounds;
	int			i;

	bounds.Clear();
	bounds.AddPoint( start );
	bounds.AddPoint( end );

	// FixTriangleAgainstHashVert rejects anything farther than this from the
	// edge.  Keeping the hash query to the same narrow band avoids testing all
	// vertices inside a large triangle's three-dimensional bounding box.
	const float slop = COLINEAR_EPSILON + VERTEX_EPSILON;
	for ( i = 0 ; i < 3 ; i++ ) {
		blocks[0][i] = ( bounds[0][i] - slop - hashBounds[0][i] ) / hashScale[i];
		if ( blocks[0][i] < 0 ) {
			blocks[0][i] = 0;
		} else if ( blocks[0][i] >= HASH_BINS ) {
			blocks[0][i] = HASH_BINS - 1;
		}

		blocks[1][i] = ( bounds[1][i] + slop - hashBounds[0][i] ) / hashScale[i];
		if ( blocks[1][i] < 0 ) {
			blocks[1][i] = 0;
		} else if ( blocks[1][i] >= HASH_BINS ) {
			blocks[1][i] = HASH_BINS - 1;
		}
	}
}


/*
=================
HashTriangles

Removes triangles that are degenerated or flipped backwards
=================
*/
void HashTriangles( optimizeGroup_t *groupList ) {
	mapTri_t	*a;
	int			vert;
	int			i;
	optimizeGroup_t	*group;

	// clear the hash tables
	memset( hashVerts, 0, sizeof( hashVerts ) );
	hashUsedBuckets.Clear();

	numHashVerts = 0;
	numTotalVerts = 0;
	numTJunctionCandidates = 0;

	// bound all the triangles to determine the bucket size
	hashBounds.Clear();
	for ( group = groupList ; group ; group = group->nextGroup ) {
		for ( a = group->triList ; a ; a = a->next ) {
			hashBounds.AddPoint( a->v[0].xyz );
			hashBounds.AddPoint( a->v[1].xyz );
			hashBounds.AddPoint( a->v[2].xyz );
		}
	}

	// spread the bounds so it will never have a zero size
	for ( i = 0 ; i < 3 ; i++ ) {
		hashBounds[0][i] = floor( hashBounds[0][i] - 1 );
		hashBounds[1][i] = ceil( hashBounds[1][i] + 1 );
		hashIntMins[i] = hashBounds[0][i] * SNAP_FRACTIONS;

		hashScale[i] = ( hashBounds[1][i] - hashBounds[0][i] ) / HASH_BINS;
		hashIntScale[i] = hashScale[i] * SNAP_FRACTIONS;
		if ( hashIntScale[i] < 1 ) {
			hashIntScale[i] = 1;
		}
	}

	// add all the points to the hash buckets
	for ( group = groupList ; group ; group = group->nextGroup ) {
		// don't create tjunctions against discrete surfaces (blood decals, etc)
		if ( group->material != NULL && group->material->IsDiscrete() ) {
			continue;
		}
		for ( a = group->triList ; a ; a = a->next ) {
			for ( vert = 0 ; vert < 3 ; vert++ ) {
				a->hashVert[vert] = GetHashVert( a->v[vert].xyz );
			}
		}
	}
}

/*
=================
FreeTJunctionHash

The optimizer may add some more crossing verts
after t junction processing
=================
*/
void FreeTJunctionHash( void ) {
	hashVert_t	*hv, *next;

	for ( int bucketIndex = 0; bucketIndex < hashUsedBuckets.Num(); bucketIndex++ ) {
		int flat = hashUsedBuckets[bucketIndex];
		const int k = flat % HASH_BINS;
		flat /= HASH_BINS;
		const int j = flat % HASH_BINS;
		const int i = flat / HASH_BINS;
		for ( hv = hashVerts[i][j][k] ; hv ; hv = next ) {
			next = hv->next;
			Mem_Free( hv );
		}
		hashVerts[i][j][k] = NULL;
	}
	hashUsedBuckets.Clear();
}


/*
==================
FixTriangleAgainstHashVert

Returns a list of two new mapTri if the hashVert is
on an edge of the given mapTri, otherwise returns NULL.
==================
*/
static mapTri_t *FixTriangleAgainstHashVert( const mapTri_t *a, const hashVert_t *hv ) {
	int			i;
	const idDrawVert	*v1, *v2, *v3;
	idDrawVert	split;
	idVec3		dir;
	float		len;
	float		frac;
	mapTri_t	*new1, *new2;
	idVec3		temp;
	float		d, off;
	const idVec3 *v;
	idPlane		plane1, plane2;

	v = &hv->v;

	// if the triangle already has this hashVert as a vert,
	// it can't be split by it
	if ( a->hashVert[0] == hv || a->hashVert[1] == hv || a->hashVert[2] == hv ) {
		return NULL;
	}

	// we probably should find the edge that the vertex is closest to.
	// it is possible to be < 1 unit away from multiple
	// edges, but we only want to split by one of them
	for ( i = 0 ; i < 3 ; i++ ) {
		v1 = &a->v[i];
		v2 = &a->v[(i+1)%3];
		v3 = &a->v[(i+2)%3];
		VectorSubtract( v2->xyz, v1->xyz, dir );
		len = dir.Normalize();

		// if it is close to one of the edge vertexes, skip it
		VectorSubtract( *v, v1->xyz, temp );
		d = DotProduct( temp, dir );
		if ( d <= 0 || d >= len ) {
			continue;
		}

		// make sure it is on the line
		VectorMA( v1->xyz, d, dir, temp );
		VectorSubtract( temp, *v, temp );
		off = temp.Length();
		if ( off <= -COLINEAR_EPSILON || off >= COLINEAR_EPSILON ) {
			continue;
		}

		// take the x/y/z from the splitter,
		// but interpolate everything else from the original tri
		VectorCopy( *v, split.xyz );
		frac = d / len;
		const idVec2 st1 = v1->GetST();
		const idVec2 st2 = v2->GetST();
		split.SetST( st1 + frac * ( st2 - st1 ) );
		idVec3 normal = v1->GetNormal() + frac * ( v2->GetNormal() - v1->GetNormal() );
		normal.Normalize();
		split.SetNormal( normal );

		// split the tri
		new1 = CopyMapTri( a );
		new1->v[(i+1)%3] = split;
		new1->hashVert[(i+1)%3] = hv;
		new1->next = NULL;

		new2 = CopyMapTri( a );
		new2->v[i] = split;
		new2->hashVert[i] = hv;
		new2->next = new1;

		plane1.FromPoints( new1->hashVert[0]->v, new1->hashVert[1]->v, new1->hashVert[2]->v );
		plane2.FromPoints( new2->hashVert[0]->v, new2->hashVert[1]->v, new2->hashVert[2]->v );

		d = DotProduct( plane1, plane2 );

		// if the two split triangle's normals don't face the same way,
		// it should not be split
		if ( d <= 0 ) {
			FreeTriList( new2 );
			continue;
		}

		return new2;
	}


	return NULL;
}

static mapTri_t *FixTriangleListAgainstHashVert( mapTri_t *fixed, const hashVert_t *hv ) {
	mapTri_t *test = fixed;
	fixed = NULL;
	while ( test ) {
		mapTri_t *next = test->next;
		mapTri_t *split = FixTriangleAgainstHashVert( test, hv );
		if ( split ) {
			// cut into two triangles
			split->next->next = fixed;
			fixed = split;
			FreeTri( test );
		} else {
			test->next = fixed;
			fixed = test;
		}
		test = next;
	}
	return fixed;
}



/*
==================
FixTriangleAgainstHash

Potentially splits a triangle into a list of triangles based on tjunctions
==================
*/
static mapTri_t	*FixTriangleAgainstHash( const mapTri_t *tri ) {
	mapTri_t		*fixed;
	int				blocks[2][3];
	int				i, j, k;
	hashVert_t		*hv;

	// if this triangle is degenerate after point snapping,
	// do nothing (this shouldn't happen, because they should
	// be removed as they are hashed)
	if ( tri->hashVert[0] == tri->hashVert[1]
		|| tri->hashVert[0] == tri->hashVert[2]
		|| tri->hashVert[1] == tri->hashVert[2] ) {
		return NULL;
	}

	fixed = CopyMapTri( tri );
	fixed->next = NULL;

	// A T-junction can only occur on one of the original boundary edges.
	// Gather each nearby hash vertex once, then retain the existing splitter.
	hashVisitStamp++;
	if ( hashVisitStamp == 0 ) {
		// The wrap is practically unreachable, but zero is reserved for newly
		// allocated vertices.
		for ( i = 0; i < HASH_BINS; i++ ) {
			for ( j = 0; j < HASH_BINS; j++ ) {
				for ( k = 0; k < HASH_BINS; k++ ) {
					for ( hv = hashVerts[i][j][k]; hv; hv = hv->next ) {
						hv->visitStamp = 0;
					}
				}
			}
		}
		hashVisitStamp = 1;
	}
	for ( int edge = 0; edge < 3; edge++ ) {
		HashBlocksForEdge( tri->v[edge].xyz, tri->v[( edge + 1 ) % 3].xyz, blocks );
		for ( i = blocks[0][0] ; i <= blocks[1][0] ; i++ ) {
			for ( j = blocks[0][1] ; j <= blocks[1][1] ; j++ ) {
				for ( k = blocks[0][2] ; k <= blocks[1][2] ; k++ ) {
					for ( hv = hashVerts[i][j][k] ; hv ; hv = hv->next ) {
						if ( hv->visitStamp != hashVisitStamp ) {
							hv->visitStamp = hashVisitStamp;
							numTJunctionCandidates++;
							fixed = FixTriangleListAgainstHashVert( fixed, hv );
						}
					}
				}
			}
		}
	}

	return fixed;
}


/*
==================
CountGroupListTris
==================
*/
int CountGroupListTris( const optimizeGroup_t *groupList ) {
	int		c;

	c = 0;
	for ( ; groupList ; groupList = groupList->nextGroup ) {
		c += CountTriList( groupList->triList );
	}

	return c;
}

/*
==================
FixAreaGroupsTjunctions
==================
*/
void	FixAreaGroupsTjunctions( optimizeGroup_t *groupList ) {
	const mapTri_t	*tri;
	mapTri_t		*newList;
	mapTri_t		*fixed;
	int				startCount, endCount;
	optimizeGroup_t	*group;

	if ( dmapGlobals.noTJunc ) {
		return;
	}

	if ( !groupList ) {
		return;
	}

	startCount = CountGroupListTris( groupList );

	if ( dmapGlobals.verbose ) {
		common->Printf( "----- FixAreaGroupsTjunctions -----\n" );
		common->Printf( "%6i triangles in\n", startCount );
	}

	HashTriangles( groupList );

	for ( group = groupList ; group ; group = group->nextGroup ) {
		// don't touch discrete surfaces
		if ( group->material != NULL && group->material->IsDiscrete() ) {
			continue;
		}

		newList = NULL;
		for ( tri = group->triList ; tri ; tri = tri->next ) {
			fixed = FixTriangleAgainstHash( tri );
			newList = MergeTriLists( newList, fixed );
		}
		FreeTriList( group->triList );
		group->triList = newList;
	}

	endCount = CountGroupListTris( groupList );
	if ( dmapGlobals.verbose ) {
		common->Printf( "%6i triangles out\n", endCount );
		common->Printf( "%6i unique vertices, %llu edge candidates tested\n",
			numHashVerts, numTJunctionCandidates );
	}
}


/*
==================
FixEntityTjunctions
==================
*/
void	FixEntityTjunctions( uEntity_t *e ) {
	int		i;

	for ( i = 0 ; i < e->numAreas ; i++ ) {
		FixAreaGroupsTjunctions( e->areas[i].groups );
		FreeTJunctionHash();
	}
}

/*
==================
FixGlobalTjunctions
==================
*/
void	FixGlobalTjunctions( uEntity_t *e ) {
	mapTri_t	*a;
	int			vert;
	int			i;
	optimizeGroup_t	*group;
	int			areaNum;

	common->Printf( "----- FixGlobalTjunctions -----\n" );

	// clear the hash tables
	memset( hashVerts, 0, sizeof( hashVerts ) );
	hashUsedBuckets.Clear();

	numHashVerts = 0;
	numTotalVerts = 0;
	numTJunctionCandidates = 0;

	// bound all the triangles to determine the bucket size
	hashBounds.Clear();
	for ( areaNum = 0 ; areaNum < e->numAreas ; areaNum++ ) {
		for ( group = e->areas[areaNum].groups ; group ; group = group->nextGroup ) {
			for ( a = group->triList ; a ; a = a->next ) {
				hashBounds.AddPoint( a->v[0].xyz );
				hashBounds.AddPoint( a->v[1].xyz );
				hashBounds.AddPoint( a->v[2].xyz );
			}
		}
	}

	// spread the bounds so it will never have a zero size
	for ( i = 0 ; i < 3 ; i++ ) {
		hashBounds[0][i] = floor( hashBounds[0][i] - 1 );
		hashBounds[1][i] = ceil( hashBounds[1][i] + 1 );
		hashIntMins[i] = hashBounds[0][i] * SNAP_FRACTIONS;

		hashScale[i] = ( hashBounds[1][i] - hashBounds[0][i] ) / HASH_BINS;
		hashIntScale[i] = hashScale[i] * SNAP_FRACTIONS;
		if ( hashIntScale[i] < 1 ) {
			hashIntScale[i] = 1;
		}
	}

	// add all the points to the hash buckets
	for ( areaNum = 0 ; areaNum < e->numAreas ; areaNum++ ) {
		for ( group = e->areas[areaNum].groups ; group ; group = group->nextGroup ) {
			// don't touch discrete surfaces
			if ( group->material != NULL && group->material->IsDiscrete() ) {
				continue;
			}

			for ( a = group->triList ; a ; a = a->next ) {
				for ( vert = 0 ; vert < 3 ; vert++ ) {
					a->hashVert[vert] = GetHashVert( a->v[vert].xyz );
				}
			}
		}
	}

	// add all the func_static model vertexes to the hash buckets
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
			if ( !modelName ) {
				continue;
			}
			if ( !strstr( modelName, ".lwo" ) && !strstr( modelName, ".ase" ) && !strstr( modelName, ".ma" ) ) {
				continue;
			}

			idRenderModel	*model = renderModelManager->FindModel( modelName );

//			common->Printf( "adding T junction verts for %s.\n", entity->mapEntity->epairs.GetString( "name" ) );

			idMat3	axis;
			// get the rotation matrix in either full form, or single angle form
			if ( !entity->mapEntity->epairs.GetMatrix( "rotation", "1 0 0 0 1 0 0 0 1", axis ) ) {
				float angle = entity->mapEntity->epairs.GetFloat( "angle" );
				if ( angle != 0.0f ) {
					axis = idAngles( 0.0f, angle, 0.0f ).ToMat3();
				} else {
					axis.Identity();
				}
			}		

			idVec3	origin = entity->mapEntity->epairs.GetVector( "origin" );

			for ( i = 0 ; i < model->NumSurfaces() ; i++ ) {
				const modelSurface_t *surface = model->Surface( i );
				const srfTriangles_t *tri = surface->geometry;

				mapTri_t	mapTri;
				memset( &mapTri, 0, sizeof( mapTri ) );
				mapTri.material = surface->material;
				// don't let discretes (autosprites, etc) merge together
				if ( mapTri.material->IsDiscrete() ) {
					mapTri.mergeGroup = (void *)surface;
				}
				for ( int j = 0 ; j < tri->numVerts ; j += 3 ) {
					idVec3 v = tri->verts[j].xyz * axis + origin;
					GetHashVert( v );
				}
			}
		}
	}



	// now fix each area
	for ( areaNum = 0 ; areaNum < e->numAreas ; areaNum++ ) {
		for ( group = e->areas[areaNum].groups ; group ; group = group->nextGroup ) {
			// don't touch discrete surfaces
			if ( group->material != NULL && group->material->IsDiscrete() ) {
				continue;
			}

			mapTri_t *newList = NULL;
			for ( mapTri_t *tri = group->triList ; tri ; tri = tri->next ) {
				mapTri_t *fixed = FixTriangleAgainstHash( tri );
				newList = MergeTriLists( newList, fixed );
			}
			FreeTriList( group->triList );
			group->triList = newList;
		}
	}


	// done
	common->Printf( "%i unique vertices, %llu edge candidates tested\n",
		numHashVerts, numTJunctionCandidates );
	FreeTJunctionHash();
}
