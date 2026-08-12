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

//=================================================================================


#if 0

should we try and snap values very close to 0.5, 0.25, 0.125, etc?

  do we write out normals, or just a "smooth shade" flag?
resolved: normals.  otherwise adjacent facet shaded surfaces get their
		  vertexes merged, and they would have to be split apart before drawing

  do we save out "wings" for shadow silhouette info?


#endif

static	idFile	*procFile;
static	idStrList procMaterials;

static srfTriangles_t *DmapAllocTriSurf( int maxVerts, int maxIndexes ) {
	srfTriangles_t *tri = static_cast<srfTriangles_t *>( Mem_ClearedAlloc( sizeof( *tri ) ) );
	tri->verts = static_cast<idDrawVert *>( Mem_ClearedAllocAligned( maxVerts * sizeof( tri->verts[0] ), ALIGN_16 ) );
	tri->indexes = static_cast<glIndex_t *>( Mem_ClearedAlloc( maxIndexes * sizeof( tri->indexes[0] ) ) );
	tri->numAllocedVerts = maxVerts;
	tri->numAllocedIndices = maxIndexes;
	return tri;
}

static void DmapFreeTriSurf( srfTriangles_t *tri ) {
	if ( tri == NULL ) {
		return;
	}
	Mem_FreeAligned( tri->verts );
	Mem_Free( tri->indexes );
	Mem_Free( tri );
}

static void DmapBoundTriSurf( srfTriangles_t *tri ) {
	tri->bounds.Clear();
	for ( int i = 0; i < tri->numVerts; ++i ) {
		tri->bounds.AddPoint( tri->verts[i].xyz );
	}
}

static int BeginBinarySection( const char *name ) {
	procFile->WriteString( name );
	const int lengthPosition = procFile->Tell();
	procFile->WriteInt( 0 );
	return lengthPosition;
}

static void EndBinarySection( int lengthPosition ) {
	const int endPosition = procFile->Tell();
	procFile->Seek( lengthPosition, FS_SEEK_SET );
	procFile->WriteInt( endPosition - lengthPosition - sizeof( int ) );
	procFile->Seek( endPosition, FS_SEEK_SET );
}

static int ProcMaterialIndex( const idMaterial *material ) {
	const char *name = material != NULL ? material->GetName() : "_default";
	for ( int i = 0; i < procMaterials.Num(); ++i ) {
		if ( !procMaterials[ i ].Icmp( name ) ) {
			return i;
		}
	}
	return procMaterials.Append( name );
}

static void BuildProcMaterialList() {
	procMaterials.Clear();
	for ( int entityNum = 0; entityNum < dmapGlobals.num_entities; ++entityNum ) {
		const uEntity_t &entity = dmapGlobals.uEntities[ entityNum ];
		for ( int areaNum = 0; areaNum < entity.numAreas; ++areaNum ) {
			for ( optimizeGroup_t *group = entity.areas[ areaNum ].groups; group != NULL; group = group->nextGroup ) {
				if ( group->triList != NULL ) {
					ProcMaterialIndex( group->material );
				}
			}
		}
	}
}

static void WriteProcMaterials() {
	procFile->WriteString( "materials" );
	procFile->WriteInt( procMaterials.Num() );
	for ( int i = 0; i < procMaterials.Num(); ++i ) {
		procFile->WriteString( procMaterials[ i ] );
	}
}

#define	AREANUM_DIFFERENT	-2
/*
=============
PruneNodes_r

Any nodes that have all children with the same
area can be combined into a single leaf node

Returns the area number of all children, or
AREANUM_DIFFERENT if not the same.
=============
*/
int	PruneNodes_r( node_t *node ) {
	int		a1, a2;

	if ( node->planenum == PLANENUM_LEAF ) {
		return node->area;
	}

	a1 = PruneNodes_r( node->children[0] );
	a2 = PruneNodes_r( node->children[1] );

	if ( a1 != a2 || a1 == AREANUM_DIFFERENT ) {
		return AREANUM_DIFFERENT;
	}

	// free all the nodes below this point
	FreeTreePortals_r( node->children[0] );
	FreeTreePortals_r( node->children[1] );
	FreeTree_r( node->children[0] );
	FreeTree_r( node->children[1] );

	// change this node to a leaf
	node->planenum = PLANENUM_LEAF;
	node->area = a1;

	return a1;
}

static void WriteFloat( idFile *f, float v )
{
	if ( idMath::Fabs(v - idMath::Rint(v)) < 0.001 ) {
		f->WriteFloatString( "%i ", (int)idMath::Rint(v) );
	}
	else {
		f->WriteFloatString( "%f ", v );
	}
}

void Write1DMatrix( idFile *f, int x, float *m ) {
	int		i;

	f->WriteFloatString( "( " );

	for ( i = 0; i < x; i++ ) {
		WriteFloat( f, m[i] );
	}

	f->WriteFloatString( ") " );
}

static int CountUniqueShaders( optimizeGroup_t *groups ) {
	optimizeGroup_t		*a, *b;
	int					count;

	count = 0;

	for ( a = groups ; a ; a = a->nextGroup ) {
		if ( !a->triList ) {	// ignore groups with no tris
			continue;
		}
		for ( b = groups ; b != a ; b = b->nextGroup ) {
			if ( !b->triList ) {
				continue;
			}
			if ( a->material != b->material ) {
				continue;
			}
			if ( a->mergeGroup != b->mergeGroup ) {
				continue;
			}
			if ( a->planeNum != b->planeNum ) {
				continue;
			}
			break;
		}
		if ( a == b ) {
			count++;
		}
	}

	return count;
}


/*
==============
MatchVert
==============
*/
#define	XYZ_EPSILON	0.01
#define	ST_EPSILON	0.001
#define	COSINE_EPSILON	0.999

static bool MatchVert( const idDrawVert *a, const idDrawVert *b ) {
	if ( idMath::Fabs( a->xyz[0] - b->xyz[0] ) > XYZ_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( a->xyz[1] - b->xyz[1] ) > XYZ_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( a->xyz[2] - b->xyz[2] ) > XYZ_EPSILON ) {
		return false;
	}
	const idVec2 aST = a->GetST();
	const idVec2 bST = b->GetST();
	if ( idMath::Fabs( aST[0] - bST[0] ) > ST_EPSILON ) {
		return false;
	}
	if ( idMath::Fabs( aST[1] - bST[1] ) > ST_EPSILON ) {
		return false;
	}

	// if the normal is 0 (smoothed normals), consider it a match
	const idVec3 aNormal = a->GetNormal();
	const idVec3 bNormal = b->GetNormal();
	if ( aNormal[0] == 0 && aNormal[1] == 0 && aNormal[2] == 0
		&& bNormal[0] == 0 && bNormal[1] == 0 && bNormal[2] == 0 ) {
		return true;
	}

	// otherwise do a dot-product cosine check
	if ( DotProduct( aNormal, bNormal ) < COSINE_EPSILON ) {
		return false;
	}

	return true;
}

/*
====================
ShareMapTriVerts

Converts independent triangles to shared vertex triangles
====================
*/
srfTriangles_t	*ShareMapTriVerts( const mapTri_t *tris ) {
	const mapTri_t	*step;
	int			count;
	int			i, j;
	int			numVerts;
	int			numIndexes;
	srfTriangles_t	*uTri;

	// unique the vertexes
	count = CountTriList( tris );

	uTri = DmapAllocTriSurf( count * 3, count * 3 );

	numVerts = 0;
	numIndexes = 0;

	for ( step = tris ; step ; step = step->next ) {
		for ( i = 0 ; i < 3 ; i++ ) {
			const idDrawVert	*dv;

			dv = &step->v[i];

			// search for a match
			for ( j = 0 ; j < numVerts ; j++ ) {
				if ( MatchVert( &uTri->verts[j], dv ) ) {
					break;
				}
			}
			if ( j == numVerts ) {
				numVerts++;
				uTri->verts[j].xyz = dv->xyz;
				uTri->verts[j].SetNormal( dv->GetNormal() );
				uTri->verts[j].SetST( dv->GetST() );
				uTri->verts[j].SetColor( dv->GetColor() );
			}

			uTri->indexes[numIndexes++] = j;
		}
	}

	uTri->numVerts = numVerts;
	uTri->numIndexes = numIndexes;

	return uTri;
}

/*
==================
CleanupUTriangles
==================
*/
static void CleanupUTriangles( srfTriangles_t *tri ) {
	int outputIndex = 0;
	for ( int inputIndex = 0; inputIndex + 2 < tri->numIndexes; inputIndex += 3 ) {
		const glIndex_t a = tri->indexes[inputIndex + 0];
		const glIndex_t b = tri->indexes[inputIndex + 1];
		const glIndex_t c = tri->indexes[inputIndex + 2];
		if ( a >= tri->numVerts || b >= tri->numVerts || c >= tri->numVerts ) {
			common->Error( "dmap: surface index out of range" );
		}
		if ( a == b || b == c || c == a ) {
			continue;
		}
		const idVec3 edge1 = tri->verts[b].xyz - tri->verts[a].xyz;
		const idVec3 edge2 = tri->verts[c].xyz - tri->verts[a].xyz;
		if ( edge1.Cross( edge2 ).LengthSqr() <= 1e-12f ) {
			continue;
		}
		tri->indexes[outputIndex++] = a;
		tri->indexes[outputIndex++] = b;
		tri->indexes[outputIndex++] = c;
	}
	tri->numIndexes = outputIndex;
}

static void DeriveSurfaceTangents( srfTriangles_t *tri ) {
	idList< idVec3 > tangentSums;
	idList< idVec3 > bitangentSums;
	tangentSums.SetNum( tri->numVerts );
	bitangentSums.SetNum( tri->numVerts );
	for ( int i = 0; i < tri->numVerts; ++i ) {
		tangentSums[ i ].Zero();
		bitangentSums[ i ].Zero();
	}

	for ( int i = 0; i + 2 < tri->numIndexes; i += 3 ) {
		const int indexes[ 3 ] = { tri->indexes[ i ], tri->indexes[ i + 1 ], tri->indexes[ i + 2 ] };
		const idDrawVert &a = tri->verts[ indexes[ 0 ] ];
		const idDrawVert &b = tri->verts[ indexes[ 1 ] ];
		const idDrawVert &c = tri->verts[ indexes[ 2 ] ];
		const idVec3 edge1 = b.xyz - a.xyz;
		const idVec3 edge2 = c.xyz - a.xyz;
		const idVec2 aST = a.GetST();
		const idVec2 bST = b.GetST();
		const idVec2 cST = c.GetST();
		const float du1 = bST.x - aST.x;
		const float dv1 = bST.y - aST.y;
		const float du2 = cST.x - aST.x;
		const float dv2 = cST.y - aST.y;
		const float determinant = du1 * dv2 - du2 * dv1;
		if ( idMath::Fabs( determinant ) < 1e-20f ) {
			continue;
		}
		const float inverse = 1.0f / determinant;
		const idVec3 tangent = ( edge1 * dv2 - edge2 * dv1 ) * inverse;
		const idVec3 bitangent = ( edge2 * du1 - edge1 * du2 ) * inverse;
		for ( int corner = 0; corner < 3; ++corner ) {
			tangentSums[ indexes[ corner ] ] += tangent;
			bitangentSums[ indexes[ corner ] ] += bitangent;
		}
	}

	for ( int i = 0; i < tri->numVerts; ++i ) {
		idVec3 normal = tri->verts[ i ].GetNormal();
		idVec3 tangent = tangentSums[ i ] - normal * ( normal * tangentSums[ i ] );
		if ( tangent.Normalize() == 0.0f ) {
			idVec3 fallback;
			normal.NormalVectors( tangent, fallback );
		}
		tri->verts[ i ].SetTangent( tangent );
		tri->verts[ i ].SetBiTangent( bitangentSums[ i ] );
	}
	tri->tangentsCalculated = true;
}

/*
====================
WriteUTriangles

Writes ETQW mapProcFile010 binary verts and indexes.
====================
*/
static void WriteUTriangles( const srfTriangles_t *uTris ) {
	for ( int i = 0; i < uTris->numVerts; ++i ) {
		const idDrawVert &dv = uTris->verts[ i ];
		const idVec2 st = dv.GetST();
		const idVec3 normal = dv.GetNormal();
		const idVec3 tangent = dv.GetTangent();
		float values[ 12 ] = {
			dv.xyz[ 0 ], dv.xyz[ 1 ], dv.xyz[ 2 ],
			st[ 0 ], st[ 1 ],
			normal[ 0 ], normal[ 1 ], normal[ 2 ],
			tangent[ 0 ], tangent[ 1 ], tangent[ 2 ], dv.GetBiTangentSign()
		};
		procFile->WriteInt( 12 );
		procFile->WriteFloatArray( values, 12 );
		// The current dmap path does not synthesize ETQW vertex paint colors.
		procFile->WriteInt( 0 );
	}

	for ( int i = 0; i < uTris->numIndexes; ++i ) {
		procFile->WriteInt( uTris->indexes[ i ] );
	}
}


/*
=======================
GroupsAreSurfaceCompatible

Planes, texcoords, and groupLights can differ,
but the material and mergegroup must match
=======================
*/
static bool GroupsAreSurfaceCompatible( const optimizeGroup_t *a, const optimizeGroup_t *b ) {
	if ( a->material != b->material ) {
		return false;
	}
	if ( a->mergeGroup != b->mergeGroup ) {
		return false;
	}
	if ( a->planeNum != b->planeNum ) {
		return false;
	}
	return true;
}

/*
====================
WriteOutputSurfaces
====================
*/
static void WriteOutputSurfaces( int entityNum, int areaNum ) {
	mapTri_t	*ambient, *copy;
	int			surfaceNum;
	int			numSurfaces;
	idMapEntity	*entity;
	uArea_t		*area;
	optimizeGroup_t	*group, *groupStep;
	srfTriangles_t	*uTri;


	area = &dmapGlobals.uEntities[entityNum].areas[areaNum];
	entity = dmapGlobals.uEntities[entityNum].mapEntity;

	numSurfaces = CountUniqueShaders( area->groups );


	idStr modelName;
	if ( entityNum == 0 ) {
		modelName = va( "_area%i", areaNum );
	} else {
		const char *name;

		entity->epairs.GetString( "name", "", &name );
		if ( !name[0] ) {
			common->Error( "Entity %i has surfaces, but no name key", entityNum );
		}
		modelName = name;
	}

	const int modelLengthPosition = BeginBinarySection( "model" );
	procFile->WriteString( modelName );
	procFile->WriteInt( numSurfaces );
	// mapProcFile010 records the areas statically occupied by each model.
	procFile->WriteInt( entityNum == 0 ? 1 : 0 );
	if ( entityNum == 0 ) {
		procFile->WriteInt( areaNum );
	}

	surfaceNum = 0;
	for ( group = area->groups ; group ; group = group->nextGroup ) {
		if ( group->surfaceEmited ) {
			continue;
		}

		// combine all groups compatible with this one
		// usually several optimizeGroup_t can be combined into a single
		// surface, even though they couldn't be merged together to save
		// vertexes because they had different planes, texture coordinates, or lights.
		// Different mergeGroups will stay in separate surfaces.
		ambient = NULL;

		for ( groupStep = group ; groupStep ; groupStep = groupStep->nextGroup ) {
			if ( groupStep->surfaceEmited ) {
				continue;
			}
			if ( !GroupsAreSurfaceCompatible( group, groupStep ) ) {
				continue;
			}

			// copy it out to the ambient list
			copy = CopyTriList( groupStep->triList );
			ambient = MergeTriLists( ambient, copy );
			groupStep->surfaceEmited = true;
		}

		if ( !ambient ) {
			continue;
		}

		if ( surfaceNum >= numSurfaces ) {
			common->Error( "WriteOutputSurfaces: surfaceNum >= numSurfaces" );
		}

		surfaceNum++;
		procFile->WriteInt( ProcMaterialIndex( ambient->material ) );
		procFile->WriteBool( false );

		uTri = ShareMapTriVerts( ambient );
		FreeTriList( ambient );

		CleanupUTriangles( uTri );
		DmapBoundTriSurf( uTri );
		DeriveSurfaceTangents( uTri );
		procFile->WriteVec3( uTri->bounds[ 0 ] );
		procFile->WriteVec3( uTri->bounds[ 1 ] );
		procFile->WriteInt( uTri->numVerts );
		procFile->WriteInt( uTri->numIndexes );
		procFile->WriteInt( 0 );
		WriteUTriangles( uTri );
		DmapFreeTriSurf( uTri );
	}

	EndBinarySection( modelLengthPosition );
}

/*
===============
WriteNode_r

===============
*/
static void WriteNode_r( node_t *node ) {
	int		child[2];
	int		i;
	idPlane	*plane;

	if ( node->planenum == PLANENUM_LEAF ) {
		// we shouldn't get here unless the entire world
		// was a single leaf
		const float plane[ 4 ] = { 0, 0, 0, 0 };
		procFile->Write1DFloatArray( 4, plane );
		procFile->WriteInt( -1 );
		procFile->WriteInt( -1 );
		return;
	}

	for ( i = 0 ; i < 2 ; i++ ) {
		if ( node->children[i]->planenum == PLANENUM_LEAF ) {
			child[i] = -1 - node->children[i]->area;
		} else {
			child[i] = node->children[i]->nodeNumber;
		}
	}

	plane = &dmapGlobals.mapPlanes[node->planenum];

	procFile->Write1DFloatArray( 4, plane->ToFloatPtr() );
	procFile->WriteInt( child[ 0 ] );
	procFile->WriteInt( child[ 1 ] );

	if ( child[0] > 0 ) {
		WriteNode_r( node->children[0] );
	}
	if ( child[1] > 0 ) {
		WriteNode_r( node->children[1] );
	}
}

static int NumberNodes_r( node_t *node, int nextNumber ) {
	if ( node->planenum == PLANENUM_LEAF ) {
		return nextNumber;
	}
	node->nodeNumber = nextNumber;
	nextNumber++;
	nextNumber = NumberNodes_r( node->children[0], nextNumber );
	nextNumber = NumberNodes_r( node->children[1], nextNumber );

	return nextNumber;
}

/*
====================
WriteOutputNodes
====================
*/
static void WriteOutputNodes( node_t *node ) {
	int		numNodes;

	// prune unneeded nodes and count
	PruneNodes_r( node );
	numNodes = NumberNodes_r( node, 0 );

	const int lengthPosition = BeginBinarySection( "nodes" );
	procFile->WriteInt( numNodes );
	WriteNode_r( node );
	EndBinarySection( lengthPosition );
}

/*
====================
WriteOutputPortals
====================
*/
static void WriteOutputPortals( uEntity_t *e ) {
	int			i, j;
	interAreaPortal_t	*iap;
	idWinding			*w;

	const int lengthPosition = BeginBinarySection( "interAreaPortals" );
	procFile->WriteInt( e->numAreas );
	procFile->WriteInt( numInterAreaPortals );
	const float portalColor[ 4 ] = { 1, 1, 1, 1 };
	for ( i = 0 ; i < numInterAreaPortals ; i++ ) {
		iap = &interAreaPortals[i];
		w = iap->side->winding;
		procFile->WriteInt( w->GetNumPoints() );
		procFile->WriteInt( iap->area0 );
		procFile->WriteInt( iap->area1 );
		procFile->WriteInt( 0 );
		procFile->WriteInt( 0 );
		procFile->Write1DFloatArray( 4, portalColor );
		for ( j = 0 ; j < w->GetNumPoints() ; j++ ) {
			procFile->Write1DFloatArray( 3, (*w)[j].ToFloatPtr() );
		}
	}
	EndBinarySection( lengthPosition );
}


/*
====================
WriteOutputEntity
====================
*/
static void WriteOutputEntity( int entityNum ) {
	int		i;
	uEntity_t *e;

	e = &dmapGlobals.uEntities[entityNum];

	if ( entityNum != 0 ) {
		// entities may have enclosed, empty areas that we don't need to write out
		if ( e->numAreas > 1 ) {
			e->numAreas = 1;
		}
	}

	for ( i = 0 ; i < e->numAreas ; i++ ) {
		WriteOutputSurfaces( entityNum, i );
	}

	// we will completely skip the portals and nodes if it is a single area
	if ( entityNum == 0 && e->numAreas > 1 ) {
		// output the area portals
		WriteOutputPortals( e );

		// output the nodes
		WriteOutputNodes( e->tree->headnode );
	}
}


/*
====================
WriteOutputFile
====================
*/
void WriteOutputFile( void ) {
	int				i;
	uEntity_t		*entity;
	idStr			qpath;

	// write the file
	common->Printf( "----- WriteOutputFile -----\n" );
	qpath = dmapGlobals.mapFileBase;
	qpath.SetFileExtension( "procb" );

	common->Printf( "writing %s\n", qpath.c_str() );
	// _D3XP used fs_cdpath
	procFile = fileSystem->OpenFileWrite( qpath, "fs_devpath" );
	if ( !procFile ) {
		common->Error( "Error opening %s", qpath.c_str() );
	}

	procFile->WriteString( PROC_FILE_ID );
	BuildProcMaterialList();
	WriteProcMaterials();

	// write the entity models and information, writing entities first
	for ( i=dmapGlobals.num_entities - 1 ; i >= 0 ; i-- ) {
		entity = &dmapGlobals.uEntities[i];
	
		if ( !entity->primitives ) {
			continue;
		}

		WriteOutputEntity( i );
	}

	fileSystem->CloseFile( procFile );
	procFile = NULL;
	procMaterials.Clear();
}
