#include "RadiantPch.h"
#pragma hdrstop

#include "../../sys/win32/win_local.h"

static bool afxInitialized = false;

// ETQW's reconstructed renderer keeps the original deferred-trisurf owner
// disabled. Radiant only needs short-lived CPU surfaces for brush export and
// selection overlays, so provide the matching editor-side ownership here.
srfTriangles_t* R_AllocStaticTriSurf() {
	srfTriangles_t* triangles = new srfTriangles_t;
	memset( triangles, 0, sizeof( *triangles ) );
	return triangles;
}

void R_AllocStaticTriSurfVerts( srfTriangles_t* triangles, int numVerts ) {
	triangles->numAllocedVerts = numVerts;
	triangles->verts = numVerts > 0 ? new idDrawVert[ numVerts ] : NULL;
}

void R_AllocStaticTriSurfIndexes( srfTriangles_t* triangles, int numIndexes ) {
	triangles->numAllocedIndices = numIndexes;
	triangles->indexes = numIndexes > 0 ? new glIndex_t[ numIndexes ] : NULL;
}

srfTriangles_t* R_CopyStaticTriSurf( const srfTriangles_t* source ) {
	if ( source == NULL ) {
		return NULL;
	}
	srfTriangles_t* copy = R_AllocStaticTriSurf();
	copy->bounds = source->bounds;
	copy->numVerts = source->numVerts;
	copy->numIndexes = source->numIndexes;
	copy->mode = source->mode;
	R_AllocStaticTriSurfVerts( copy, copy->numVerts );
	R_AllocStaticTriSurfIndexes( copy, copy->numIndexes );
	if ( copy->numVerts > 0 ) {
		memcpy( copy->verts, source->verts, copy->numVerts * sizeof( copy->verts[ 0 ] ) );
	}
	if ( copy->numIndexes > 0 ) {
		memcpy( copy->indexes, source->indexes, copy->numIndexes * sizeof( copy->indexes[ 0 ] ) );
	}
	return copy;
}

void R_FreeStaticTriSurf( srfTriangles_t* triangles ) {
	if ( triangles == NULL ) {
		return;
	}
	delete[] triangles->verts;
	delete[] triangles->indexes;
	delete triangles;
}

void R_ToggleSmpFrame() {
	// Radiant's CPU-only temporary surfaces are released synchronously above.
}

void InitAfx( void ) {
	if ( afxInitialized ) {
		return;
	}
	AfxWinInit( win32.hInstance, NULL, GetCommandLineA(), SW_SHOW );
	AfxInitRichEdit();
	afxInitialized = true;
}
