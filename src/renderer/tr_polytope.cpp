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
#include "tr_local.h"

#define MAX_POLYTOPE_PLANES		6

/*
=====================
R_PolytopeSurface

Generate vertexes and indexes for a polytope, and optionally returns the polygon windings.
The positive sides of the planes will be visible.
=====================
*/
srfTriangles_t *R_PolytopeSurface( int numPlanes, const idPlane *planes, idWinding **windings ) {
	int i, j;
	srfTriangles_t *tri;
	idFixedWinding planeWindings[MAX_POLYTOPE_PLANES];
	int numVerts, numIndexes;

	if ( numPlanes > MAX_POLYTOPE_PLANES ) {
		common->Error( "R_PolytopeSurface: more than %d planes", MAX_POLYTOPE_PLANES );
	}

	numVerts = 0;
	numIndexes = 0;
	for ( i = 0; i < numPlanes; i++ ) {
		const idPlane &plane = planes[i];
		idFixedWinding &w = planeWindings[i];

		w.BaseForPlane( plane );
		for ( j = 0; j < numPlanes; j++ ) {
			const idPlane &plane2 = planes[j];
			if ( j == i ) {
				continue;
			}
			if ( !w.ClipInPlace( -plane2, ON_EPSILON ) ) {
				break;
			}
		}
		if ( w.GetNumPoints() <= 2 ) {
			continue;
		}
		numVerts += w.GetNumPoints();
		numIndexes += ( w.GetNumPoints() - 2 ) * 3;
	}

	// allocate the surface
	tri = R_AllocStaticTriSurf();
	R_AllocStaticTriSurfVerts( tri, numVerts );
	R_AllocStaticTriSurfIndexes( tri, numIndexes );

	// copy the data from the windings
	for ( i = 0; i < numPlanes; i++ ) {
		idFixedWinding &w = planeWindings[i];
		if ( !w.GetNumPoints() ) {
			continue;
		}
		for ( j = 0 ; j < w.GetNumPoints() ; j++ ) {
			tri->verts[tri->numVerts + j ].Clear();
			tri->verts[tri->numVerts + j ].xyz = w[j].ToVec3();
		}

		for ( j = 1 ; j < w.GetNumPoints() - 1 ; j++ ) {
			tri->indexes[ tri->numIndexes + 0 ] = tri->numVerts;
			tri->indexes[ tri->numIndexes + 1 ] = tri->numVerts + j;
			tri->indexes[ tri->numIndexes + 2 ] = tri->numVerts + j + 1;
			tri->numIndexes += 3;
		}
		tri->numVerts += w.GetNumPoints();

		// optionally save the winding
		if ( windings ) {
			windings[i] = new idWinding( w.GetNumPoints() );
			*windings[i] = w;
		}
	}

	R_BoundTriSurf( tri );

	return tri;
}
#endif

#include "Model.h"

#define ETQW_MAX_POLYTOPE_PLANES 6

/*
=====================
R_PolytopeSurface

Active ETQW translation of the retail tr_polytope.cpp owner.  The temporary
surface is owned by the front-end view and released with
R_FreePolytopeSurface after the back-end has consumed it.
=====================
*/
srfTriangles_t* R_PolytopeSurface( int numPlanes, const idPlane* planes, idWinding** windings ) {
	if ( numPlanes < 0 || numPlanes > ETQW_MAX_POLYTOPE_PLANES || planes == NULL ) {
		common->Error( "R_PolytopeSurface: more than %d planes", ETQW_MAX_POLYTOPE_PLANES );
	}

	idFixedWinding planeWindings[ ETQW_MAX_POLYTOPE_PLANES ];
	int numVerts = 0;
	int numIndexes = 0;
	for ( int planeIndex = 0; planeIndex < numPlanes; ++planeIndex ) {
		idFixedWinding& winding = planeWindings[ planeIndex ];
		winding.BaseForPlane( planes[ planeIndex ], MAX_WORLD_COORD );
		for ( int clipIndex = 0; clipIndex < numPlanes; ++clipIndex ) {
			if ( clipIndex == planeIndex ) continue;
			if ( !winding.ClipInPlace( -planes[ clipIndex ], ON_EPSILON ) ) break;
		}
		if ( winding.GetNumPoints() <= 2 ) continue;
		numVerts += winding.GetNumPoints();
		numIndexes += ( winding.GetNumPoints() - 2 ) * 3;
	}

	srfTriangles_t* triangles = new srfTriangles_t;
	memset( triangles, 0, sizeof( *triangles ) );
	triangles->numAllocedVerts = numVerts;
	triangles->numAllocedIndices = numIndexes;
	triangles->verts = numVerts > 0 ? new idDrawVert[ numVerts ] : NULL;
	triangles->indexes = numIndexes > 0 ? new glIndex_t[ numIndexes ] : NULL;
	triangles->bounds.Clear();

	for ( int planeIndex = 0; planeIndex < numPlanes; ++planeIndex ) {
		idFixedWinding& winding = planeWindings[ planeIndex ];
		if ( winding.GetNumPoints() <= 2 ) continue;
		const int firstVertex = triangles->numVerts;
		for ( int pointIndex = 0; pointIndex < winding.GetNumPoints(); ++pointIndex ) {
			idDrawVert& vertex = triangles->verts[ triangles->numVerts++ ];
			vertex.Clear();
			vertex.xyz = winding[ pointIndex ].ToVec3();
			triangles->bounds.AddPoint( vertex.xyz );
		}
		for ( int pointIndex = 1; pointIndex < winding.GetNumPoints() - 1; ++pointIndex ) {
			triangles->indexes[ triangles->numIndexes++ ] = static_cast< glIndex_t >( firstVertex );
			triangles->indexes[ triangles->numIndexes++ ] = static_cast< glIndex_t >( firstVertex + pointIndex );
			triangles->indexes[ triangles->numIndexes++ ] = static_cast< glIndex_t >( firstVertex + pointIndex + 1 );
		}
		if ( windings != NULL ) {
			windings[ planeIndex ] = new idWinding( winding.GetNumPoints() );
			*windings[ planeIndex ] = winding;
		}
	}
	return triangles;
}

void R_FreePolytopeSurface( srfTriangles_t* triangles ) {
	if ( triangles == NULL ) return;
	delete[] triangles->verts;
	delete[] triangles->indexes;
	delete triangles;
}
