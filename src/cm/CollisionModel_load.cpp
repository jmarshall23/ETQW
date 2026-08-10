// Copyright (C) 2007 Id Software, Inc.
//
// ETQW collision-model storage, lifetime, and allocation support.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "CollisionModel_local.h"


idCollisionModelManagerLocal	collisionModelManagerLocal;
idCollisionModelManager *		collisionModelManager = &collisionModelManagerLocal;

#if defined( _MSC_VER )
static __declspec( thread ) int cm_threadId = 0;
#else
static thread_local int cm_threadId = 0;
#endif


/*
===============================================================================

	Per-thread collision model cache

===============================================================================
*/

void idModelCache::UpdateForModel( idCollisionModelLocal *model ) {
	if ( model->maxVertices > maxVertices ) {
		Mem_Free( vertexCache );
		Mem_Free( vertexSideCache );
		vertexCache = static_cast< cm_vertexCache_t * >( Mem_ClearedAlloc( sizeof( *vertexCache ) * model->maxVertices ) );
		vertexSideCache = static_cast< cm_vertexSideCache_t * >( Mem_ClearedAlloc( sizeof( *vertexSideCache ) * model->maxVertices ) );
		maxVertices = model->maxVertices;
	}

	if ( model->maxEdges > maxEdges ) {
		Mem_Free( edgeCache );
		Mem_Free( edgeSideCache );
		edgeCache = static_cast< cm_edgeCache_t * >( Mem_ClearedAlloc( sizeof( *edgeCache ) * model->maxEdges ) );
		edgeSideCache = static_cast< cm_edgeSideCache_t * >( Mem_ClearedAlloc( sizeof( *edgeSideCache ) * model->maxEdges ) );
		maxEdges = model->maxEdges;
	}

	if ( model->maxPolygons > maxPolygons ) {
		Mem_Free( polygonCache );
		polygonCache = static_cast< cm_polygonCache_t * >( Mem_ClearedAlloc( sizeof( *polygonCache ) * model->maxPolygons ) );
		maxPolygons = model->maxPolygons;
	}

	if ( model->maxBrushes > maxBrushes ) {
		Mem_Free( brushCache );
		brushCache = static_cast< cm_brushCache_t * >( Mem_ClearedAlloc( sizeof( *brushCache ) * model->maxBrushes ) );
		maxBrushes = model->maxBrushes;
	}
}

void idModelCache::IncCheckCount( void ) {
	if ( checkCount == 0xFFFF ) {
		checkCount = 0;
		memset( polygonCache, 0, sizeof( *polygonCache ) * maxPolygons );
		memset( brushCache, 0, sizeof( *brushCache ) * maxBrushes );
		memset( edgeCache, 0, sizeof( *edgeCache ) * maxEdges );
		memset( vertexCache, 0, sizeof( *vertexCache ) * maxVertices );
	}
	++checkCount;
}

void idModelCache::Shutdown( void ) {
	checkCount = 0;

	Mem_Free( vertexCache );
	vertexCache = NULL;
	Mem_Free( vertexSideCache );
	vertexSideCache = NULL;
	maxVertices = 0;

	Mem_Free( edgeCache );
	edgeCache = NULL;
	Mem_Free( edgeSideCache );
	edgeSideCache = NULL;
	maxEdges = 0;

	Mem_Free( polygonCache );
	polygonCache = NULL;
	maxPolygons = 0;

	Mem_Free( brushCache );
	brushCache = NULL;
	maxBrushes = 0;

	contacts = NULL;
	maxContacts = 0;
	numContacts = 0;
}

void idModelCache::Copy( const idModelCache &base ) {
	vertexCache = static_cast< cm_vertexCache_t * >( Mem_ClearedAlloc( sizeof( *vertexCache ) * base.maxVertices ) );
	vertexSideCache = static_cast< cm_vertexSideCache_t * >( Mem_ClearedAlloc( sizeof( *vertexSideCache ) * base.maxVertices ) );
	maxVertices = base.maxVertices;

	edgeCache = static_cast< cm_edgeCache_t * >( Mem_ClearedAlloc( sizeof( *edgeCache ) * base.maxEdges ) );
	edgeSideCache = static_cast< cm_edgeSideCache_t * >( Mem_ClearedAlloc( sizeof( *edgeSideCache ) * base.maxEdges ) );
	maxEdges = base.maxEdges;

	polygonCache = static_cast< cm_polygonCache_t * >( Mem_ClearedAlloc( sizeof( *polygonCache ) * base.maxPolygons ) );
	maxPolygons = base.maxPolygons;

	brushCache = static_cast< cm_brushCache_t * >( Mem_ClearedAlloc( sizeof( *brushCache ) * base.maxBrushes ) );
	maxBrushes = base.maxBrushes;

	contacts = NULL;
	maxContacts = 0;
	numContacts = 0;
	checkCount = 0;
}


/*
===============================================================================

	Collision model object

===============================================================================
*/

static void CM_ClearModel( idCollisionModelLocal *model ) {
	model->refCount = 0;
	model->bounds.Clear();
	model->contents = 0;
	model->isTraceModel = false;
	model->isConvex = false;
	model->isWorld = false;

	model->maxVertices = 0;
	model->numVertices = 0;
	model->vertices = NULL;
	model->maxEdges = 0;
	model->numEdges = 0;
	model->edges = NULL;
	model->maxPolygonEdges = 0;
	model->numPolygonEdges = 0;
	model->polygonEdges = NULL;
	model->maxPolygons = 0;
	model->numPolygons = 0;
	model->polygons = NULL;
	model->maxBrushPlanes = 0;
	model->numBrushPlanes = 0;
	model->brushPlanes = NULL;
	model->maxBrushes = 0;
	model->numBrushes = 0;
	model->brushes = NULL;
	model->numNodes = 0;
	model->node = NULL;
	model->nodeBlocks = NULL;
	model->polygonRefBlocks = NULL;
	model->brushRefBlocks = NULL;
	model->texCoords = NULL;

	model->numMergedPolys = 0;
	model->numRemovedPolys = 0;
	model->numSharpEdges = 0;
	model->numInternalEdges = 0;
	model->numPolygonRefs = 0;
	model->numBrushRefs = 0;
	model->numPrimitives = 0;
	model->usedMemory = 0;
}

idCollisionModelLocal::idCollisionModelLocal() {
	CM_ClearModel( this );
}

idCollisionModelLocal::~idCollisionModelLocal() {
}

bool idCollisionModelLocal::IsTraceModel( void ) const {
	return isTraceModel;
}

bool idCollisionModelLocal::IsConvex( void ) const {
	return isConvex;
}

bool idCollisionModelLocal::IsWorld( void ) const {
	return isWorld;
}

void idCollisionModelLocal::SetWorld( bool tf ) {
	isWorld = tf;
}


/*
===============================================================================

	Manager lifetime and thread workspaces

===============================================================================
*/

idCollisionModelManagerLocal::idCollisionModelManagerLocal( void ) :
	mapFileTime( 0 ),
	loaded( 0 ),
	checkCount( 0 ),
	maxModels( 0 ),
	numModels( 0 ),
	models( NULL ),
	trmMaterial( NULL ),
	numProcNodes( 0 ),
	procNodes( NULL ),
	getContacts( false ),
	contacts( NULL ),
	maxContacts( 0 ),
	numContacts( 0 ),
	baseTraceWork( NULL ),
	threadCount( 0 ) {
	memset( trmPolygons, 0, sizeof( trmPolygons ) );
	memset( trmBrushes, 0, sizeof( trmBrushes ) );
	memset( traceWork, 0, sizeof( traceWork ) );
}

idCollisionModelManagerLocal::~idCollisionModelManagerLocal( void ) {
}

void idCollisionModelManagerLocal::Clear( void ) {
	mapName.Clear();
	mapFileTime = 0;
	loaded = 0;
	checkCount = 0;
	maxModels = 0;
	numModels = 0;
	models = NULL;
	memset( trmPolygons, 0, sizeof( trmPolygons ) );
	memset( trmBrushes, 0, sizeof( trmBrushes ) );
	trmMaterial = NULL;
	numProcNodes = 0;
	procNodes = NULL;
	getContacts = false;
	contacts = NULL;
	maxContacts = 0;
	numContacts = 0;
}

void idCollisionModelManagerLocal::Init( void ) {
	Clear();

	if ( baseTraceWork == NULL ) {
		baseTraceWork = static_cast< idTraceWork * >( Mem_AllocAligned( sizeof( idTraceWork ), ALIGN_16 ) );
		memset( baseTraceWork, 0, sizeof( *baseTraceWork ) );
	}

	traceWork[ 0 ] = baseTraceWork;
}

void idCollisionModelManagerLocal::Shutdown( void ) {
	if ( models != NULL ) {
		for ( int i = 0; i < numModels; ++i ) {
			if ( models[ i ] != NULL ) {
				FreeModelMemory( models[ i ] );
				delete models[ i ];
			}
		}
		Mem_Free( models );
	}

	if ( procNodes != NULL ) {
		Mem_Free( procNodes );
	}

	for ( int i = 2; i < 32; ++i ) {
		if ( traceWork[ i ] != NULL ) {
			traceWork[ i ]->modelCache.Shutdown();
			Mem_FreeAligned( traceWork[ i ] );
			traceWork[ i ] = NULL;
		}
	}

	if ( baseTraceWork != NULL ) {
		baseTraceWork->modelCache.Shutdown();
		Mem_FreeAligned( baseTraceWork );
		baseTraceWork = NULL;
	}

	memset( traceWork, 0, sizeof( traceWork ) );
	threadCount = 0;
	cm_threadId = 0;
	Clear();
}

void idCollisionModelManagerLocal::AllocThread( void ) {
	if ( cm_threadId != 0 ) {
		return;
	}

	const int id = ++threadCount;
	if ( id >= 32 ) {
		--threadCount;
		common->FatalError( "idCollisionModelManagerLocal::AllocThread: more than 31 collision threads" );
		return;
	}

	cm_threadId = id;
	if ( id == MAIN_THREAD_ID ) {
		traceWork[ id ] = baseTraceWork;
		return;
	}

	idTraceWork *work = static_cast< idTraceWork * >( Mem_AllocAligned( sizeof( idTraceWork ), ALIGN_16 ) );
	memset( work, 0, sizeof( *work ) );
	work->modelCache.Copy( baseTraceWork->modelCache );
	traceWork[ id ] = work;
}

void idCollisionModelManagerLocal::FreeThread( void ) {
	const int id = cm_threadId;
	if ( id == 0 ) {
		return;
	}

	if ( id != MAIN_THREAD_ID ) {
		idTraceWork *work = traceWork[ id ];
		if ( work != NULL ) {
			work->modelCache.Shutdown();
			Mem_FreeAligned( work );
			traceWork[ id ] = NULL;
		}
	}

	--threadCount;
	cm_threadId = 0;
}

int idCollisionModelManagerLocal::GetThreadId( void ) {
	return cm_threadId;
}

int idCollisionModelManagerLocal::GetThreadCount( void ) {
	return threadCount;
}


/*
===============================================================================

	Model storage

===============================================================================
*/

void idCollisionModelManagerLocal::ClearModel( cm_model_t *model ) {
	CM_ClearModel( model );
}

void idCollisionModelManagerLocal::FreeModelMemory( cm_model_t *model ) {
	for ( cm_polygonRefBlock_t *block = model->polygonRefBlocks; block != NULL; ) {
		cm_polygonRefBlock_t *next = block->next;
		Mem_Free( block );
		block = next;
	}

	for ( cm_brushRefBlock_t *block = model->brushRefBlocks; block != NULL; ) {
		cm_brushRefBlock_t *next = block->next;
		Mem_Free( block );
		block = next;
	}

	for ( cm_nodeBlock_t *block = model->nodeBlocks; block != NULL; ) {
		cm_nodeBlock_t *next = block->next;
		Mem_Free( block );
		block = next;
	}

	Mem_FreeAligned( model->polygonEdges );
	Mem_FreeAligned( model->polygons );
	Mem_FreeAligned( model->brushPlanes );
	Mem_FreeAligned( model->brushes );
	Mem_Free( model->edges );
	Mem_Free( model->vertices );
	Mem_Free( model->texCoords );

	ClearModel( model );
}

cm_model_t *idCollisionModelManagerLocal::AllocModel( void ) {
	return new cm_model_t;
}

cm_node_t *idCollisionModelManagerLocal::AllocNode( cm_model_t *model, int blockSize ) {
	if ( model->nodeBlocks == NULL || model->nodeBlocks->nextNode == NULL ) {
		const int bytes = sizeof( cm_nodeBlock_t ) + blockSize * sizeof( cm_node_t );
		cm_nodeBlock_t *block = static_cast< cm_nodeBlock_t * >( Mem_ClearedAlloc( bytes ) );
		block->size = bytes;
		block->nextNode = reinterpret_cast< cm_node_t * >( block + 1 );
		block->next = model->nodeBlocks;
		model->nodeBlocks = block;

		cm_node_t *node = block->nextNode;
		for ( int i = 0; i < blockSize - 1; ++i, ++node ) {
			node->parent = node + 1;
		}
		node->parent = NULL;
	}

	cm_node_t *node = model->nodeBlocks->nextNode;
	model->nodeBlocks->nextNode = node->parent;
	node->parent = NULL;
	return node;
}

cm_polygonRef_t *idCollisionModelManagerLocal::AllocPolygonReference( cm_model_t *model, int blockSize ) {
	if ( model->polygonRefBlocks == NULL || model->polygonRefBlocks->nextRef == NULL ) {
		const int bytes = sizeof( cm_polygonRefBlock_t ) + blockSize * sizeof( cm_polygonRef_t );
		cm_polygonRefBlock_t *block = static_cast< cm_polygonRefBlock_t * >( Mem_Alloc( bytes ) );
		block->size = bytes;
		block->nextRef = reinterpret_cast< cm_polygonRef_t * >( block + 1 );
		block->next = model->polygonRefBlocks;
		model->polygonRefBlocks = block;

		cm_polygonRef_t *ref = block->nextRef;
		for ( int i = 0; i < blockSize - 1; ++i, ++ref ) {
			ref->next = ref + 1;
		}
		ref->next = NULL;
	}

	cm_polygonRef_t *ref = model->polygonRefBlocks->nextRef;
	model->polygonRefBlocks->nextRef = ref->next;
	return ref;
}

cm_brushRef_t *idCollisionModelManagerLocal::AllocBrushReference( cm_model_t *model, int blockSize ) {
	if ( model->brushRefBlocks == NULL || model->brushRefBlocks->nextRef == NULL ) {
		const int bytes = sizeof( cm_brushRefBlock_t ) + blockSize * sizeof( cm_brushRef_t );
		cm_brushRefBlock_t *block = static_cast< cm_brushRefBlock_t * >( Mem_Alloc( bytes ) );
		block->size = bytes;
		block->nextRef = reinterpret_cast< cm_brushRef_t * >( block + 1 );
		block->next = model->brushRefBlocks;
		model->brushRefBlocks = block;

		cm_brushRef_t *ref = block->nextRef;
		for ( int i = 0; i < blockSize - 1; ++i, ++ref ) {
			ref->next = ref + 1;
		}
		ref->next = NULL;
	}

	cm_brushRef_t *ref = model->brushRefBlocks->nextRef;
	model->brushRefBlocks->nextRef = ref->next;
	return ref;
}

cm_polygon_t *idCollisionModelManagerLocal::AllocPolygon( cm_model_t *model, int numEdges ) {
	if ( model->numPolygons + 1 > model->maxPolygons ) {
		cm_polygon_t *oldPolygons = model->polygons;
		model->maxPolygons += 1024;
		model->polygons = static_cast< cm_polygon_t * >( Mem_AllocAligned( sizeof( cm_polygon_t ) * model->maxPolygons, ALIGN_64 ) );
		if ( oldPolygons != NULL ) {
			memcpy( model->polygons, oldPolygons, sizeof( cm_polygon_t ) * model->numPolygons );
			Mem_FreeAligned( oldPolygons );
		}
	}

	const int polygonNum = model->numPolygons++;
	cm_polygon_t *polygon = &model->polygons[ polygonNum ];
	memset( polygon, 0, sizeof( *polygon ) );

	if ( model->numPolygonEdges + numEdges > model->maxPolygonEdges ) {
		signed short *oldEdges = model->polygonEdges;
		do {
			model->maxPolygonEdges += 1024;
		} while ( model->numPolygonEdges + numEdges > model->maxPolygonEdges );

		model->polygonEdges = static_cast< signed short * >( Mem_AllocAligned( sizeof( *model->polygonEdges ) * model->maxPolygonEdges, ALIGN_16 ) );
		if ( oldEdges != NULL ) {
			for ( int i = 0; i < polygonNum; ++i ) {
				model->polygons[ i ].edges = model->polygonEdges + ( model->polygons[ i ].edges - oldEdges );
			}
			memcpy( model->polygonEdges, oldEdges, sizeof( *model->polygonEdges ) * model->numPolygonEdges );
			Mem_FreeAligned( oldEdges );
		}
	}

	polygon->numEdges = static_cast< unsigned short >( numEdges );
	polygon->edges = model->polygonEdges + model->numPolygonEdges;
	model->numPolygonEdges += numEdges;
	return polygon;
}

cm_brush_t *idCollisionModelManagerLocal::AllocBrush( cm_model_t *model, int numPlanes ) {
	if ( model->numBrushes + 1 > model->maxBrushes ) {
		cm_brush_t *oldBrushes = model->brushes;
		const int growth = model->maxBrushes == 0 ? 4 : Min( model->maxBrushes, 256 );
		model->maxBrushes += growth;
		model->brushes = static_cast< cm_brush_t * >( Mem_AllocAligned( sizeof( cm_brush_t ) * model->maxBrushes, ALIGN_64 ) );
		if ( oldBrushes != NULL ) {
			memcpy( model->brushes, oldBrushes, sizeof( cm_brush_t ) * model->numBrushes );
			Mem_FreeAligned( oldBrushes );
		}
	}

	const int brushNum = model->numBrushes++;
	cm_brush_t *brush = &model->brushes[ brushNum ];
	memset( brush, 0, sizeof( *brush ) );

	if ( model->numBrushPlanes + numPlanes > model->maxBrushPlanes ) {
		idPlane *oldPlanes = model->brushPlanes;
		do {
			const int growth = model->maxBrushPlanes == 0 ? 8 : Min( model->maxBrushPlanes, 256 );
			model->maxBrushPlanes += growth;
		} while ( model->numBrushPlanes + numPlanes > model->maxBrushPlanes );

		model->brushPlanes = static_cast< idPlane * >( Mem_AllocAligned( sizeof( idPlane ) * model->maxBrushPlanes, ALIGN_16 ) );
		if ( oldPlanes != NULL ) {
			for ( int i = 0; i < brushNum; ++i ) {
				model->brushes[ i ].planes = model->brushPlanes + ( model->brushes[ i ].planes - oldPlanes );
			}
			memcpy( model->brushPlanes, oldPlanes, sizeof( idPlane ) * model->numBrushPlanes );
			Mem_FreeAligned( oldPlanes );
		}
	}

	brush->numPlanes = numPlanes;
	brush->planes = model->brushPlanes + model->numBrushPlanes;
	model->numBrushPlanes += numPlanes;
	return brush;
}

void idCollisionModelManagerLocal::AddPolygonToNode( cm_model_t *model, cm_node_t *node, cm_polygon_t *polygon ) {
	cm_polygonRef_t *ref = AllocPolygonReference( model,
		model->numPolygonRefs < REFERENCE_BLOCK_SIZE_SMALL ? REFERENCE_BLOCK_SIZE_SMALL : REFERENCE_BLOCK_SIZE_LARGE );
	ref->polygonNum = static_cast< int >( polygon - model->polygons );
	ref->next = node->polygons;
	node->polygons = ref;
	++model->numPolygonRefs;
}

void idCollisionModelManagerLocal::AddBrushToNode( cm_model_t *model, cm_node_t *node, cm_brush_t *brush ) {
	cm_brushRef_t *ref = AllocBrushReference( model,
		model->numBrushRefs < REFERENCE_BLOCK_SIZE_SMALL ? REFERENCE_BLOCK_SIZE_SMALL : REFERENCE_BLOCK_SIZE_LARGE );
	ref->brushNum = static_cast< int >( brush - model->brushes );
	ref->next = node->brushes;
	node->brushes = ref;
	++model->numBrushRefs;
}


/*
===============================================================================

	Edge normals

===============================================================================
*/

static const float CM_SHARP_EDGE_DOT = -0.7f;

static void CM_CalculateEdgeNormals_r( cm_model_t *model, cm_node_t *node, idModelCache &cache ) {
	while ( node != NULL ) {
		for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
			cm_polygonCache_t &visit = cache.polygonCache[ ref->polygonNum ];
			if ( visit.checkcount == cache.checkCount ) {
				continue;
			}
			visit.checkcount = cache.checkCount;

			cm_polygon_t &polygon = model->polygons[ ref->polygonNum ];
			for ( int i = 0; i < polygon.numEdges; ++i ) {
				const int signedEdgeNum = polygon.edges[ i ];
				cm_edge_t &edge = model->edges[ abs( signedEdgeNum ) ];

				if ( edge.normal == vec3_origin ) {
					if ( edge.numUsers == 1 ) {
						const idVec3 dir = model->vertices[ edge.vertexNum[ signedEdgeNum < 0 ] ].p
							- model->vertices[ edge.vertexNum[ signedEdgeNum > 0 ] ].p;
						edge.normal = polygon.plane.Normal().Cross( dir );
						edge.normal.Normalize();
					} else {
						edge.normal = polygon.plane.Normal();
					}
					continue;
				}

				const float dot = edge.normal * polygon.plane.Normal();
				if ( dot < CM_SHARP_EDGE_DOT ) {
					const idVec3 dir = model->vertices[ edge.vertexNum[ signedEdgeNum > 0 ] ].p
						- model->vertices[ edge.vertexNum[ signedEdgeNum < 0 ] ].p;
					edge.normal = edge.normal.Cross( dir ) + polygon.plane.Normal().Cross( -dir );
					edge.normal *= ( 0.5f / ( 0.5f + 0.5f * CM_SHARP_EDGE_DOT ) ) / edge.normal.Length();
					++model->numSharpEdges;
				} else {
					edge.normal = ( 0.5f / ( 0.5f + 0.5f * dot ) ) * ( edge.normal + polygon.plane.Normal() );
				}
			}
		}

		if ( node->planeType == -1 ) {
			break;
		}
		CM_CalculateEdgeNormals_r( model, node->children[ 1 ], cache );
		node = node->children[ 0 ];
	}
}

void idCollisionModelManagerLocal::CalculateEdgeNormals( cm_model_t *model, cm_node_t *node ) {
	baseTraceWork->modelCache.UpdateForModel( model );
	baseTraceWork->modelCache.IncCheckCount();
	CM_CalculateEdgeNormals_r( model, node, baseTraceWork->modelCache );
}


/*
===============================================================================

	Tree filtering and public model storage

===============================================================================
*/

static bool CM_R_InsideAllChildren( const cm_node_t *node, const idBounds &bounds ) {
	if ( node->planeType != -1 ) {
		if ( bounds[ 0 ][ node->planeType ] >= node->planeDist ||
			 bounds[ 1 ][ node->planeType ] <= node->planeDist ) {
			return false;
		}
		return CM_R_InsideAllChildren( node->children[ 0 ], bounds ) &&
			CM_R_InsideAllChildren( node->children[ 1 ], bounds );
	}
	return true;
}

void idCollisionModelManagerLocal::R_FilterPolygonIntoTree( cm_model_t *model, cm_node_t *node,
		cm_polygonRef_t *pref, cm_polygon_t *polygon ) {
	assert( node != NULL );
	const idBounds bounds = polygon->bounds.ToBounds();
	while ( node->planeType != -1 ) {
		if ( CM_R_InsideAllChildren( node, bounds ) ) {
			break;
		}
		if ( bounds[ 0 ][ node->planeType ] >= node->planeDist ) {
			node = node->children[ 0 ];
		} else if ( bounds[ 1 ][ node->planeType ] <= node->planeDist ) {
			node = node->children[ 1 ];
		} else {
			R_FilterPolygonIntoTree( model, node->children[ 1 ], NULL, polygon );
			node = node->children[ 0 ];
		}
	}

	if ( pref != NULL ) {
		pref->polygonNum = static_cast< int >( polygon - model->polygons );
		pref->next = node->polygons;
		node->polygons = pref;
	} else {
		AddPolygonToNode( model, node, polygon );
	}
}

void idCollisionModelManagerLocal::R_FilterBrushIntoTree( cm_model_t *model, cm_node_t *node,
		cm_brushRef_t *bref, cm_brush_t *brush ) {
	assert( node != NULL );
	const idBounds bounds = brush->bounds.ToBounds();
	while ( node->planeType != -1 ) {
		if ( CM_R_InsideAllChildren( node, bounds ) ) {
			break;
		}
		if ( bounds[ 0 ][ node->planeType ] >= node->planeDist ) {
			node = node->children[ 0 ];
		} else if ( bounds[ 1 ][ node->planeType ] <= node->planeDist ) {
			node = node->children[ 1 ];
		} else {
			R_FilterBrushIntoTree( model, node->children[ 1 ], NULL, brush );
			node = node->children[ 0 ];
		}
	}

	if ( bref != NULL ) {
		bref->brushNum = static_cast< int >( brush - model->brushes );
		bref->next = node->brushes;
		node->brushes = bref;
	} else {
		AddBrushToNode( model, node, brush );
	}
}

void idCollisionModelManagerLocal::SetupHash( void ) {
	// The compact reader does not need the construction hash.  Map and render
	// model conversion install their working hashes while those paths run.
}

void idCollisionModelManagerLocal::ShutdownHash( void ) {
}

cmHandle_t idCollisionModelManagerLocal::FindModel( const char *name ) {
	if ( name == NULL ) {
		return -1;
	}
	for ( int i = 0; i < numModels; ++i ) {
		if ( models[ i ] != NULL && models[ i ]->name.Icmp( name ) == 0 ) {
			return i;
		}
	}
	return -1;
}

cmHandle_t idCollisionModelManagerLocal::LoadModel( const char *modelName, const bool precache ) {
	cmHandle_t handle = FindModel( modelName );
	if ( handle >= 0 ) {
		return handle;
	}

	if ( LoadCollisionModelFileBinary( modelName ) || LoadCollisionModelFile( modelName, 0 ) ) {
		handle = FindModel( modelName );
		if ( handle >= 0 ) {
			return handle;
		}
		common->Warning( "idCollisionModelManagerLocal::LoadModel: collision file for '%s' contains a different model", modelName );
	}

	if ( !precache ) {
		common->DPrintf( "idCollisionModelManagerLocal::LoadModel: render-model conversion is not available for '%s'\n", modelName );
	}
	return -1;
}

void idCollisionModelManagerLocal::PrintModelInfo( const cm_model_t *model ) {
	if ( model == NULL ) {
		return;
	}
	common->Printf( "%6i vertices (%i kB)\n", model->numVertices,
		( model->numVertices * sizeof( cm_vertex_t ) ) >> 10 );
	common->Printf( "%6i edges (%i kB)\n", model->numEdges,
		( model->numEdges * sizeof( cm_edge_t ) ) >> 10 );
	common->Printf( "%6i polygons (%i kB)\n", model->numPolygons,
		( model->numPolygons * sizeof( cm_polygon_t ) + model->numPolygonEdges * sizeof( signed short ) ) >> 10 );
	common->Printf( "%6i brushes (%i kB)\n", model->numBrushes,
		( model->numBrushes * sizeof( cm_brush_t ) + model->numBrushPlanes * sizeof( idPlane ) ) >> 10 );
	common->Printf( "%6i nodes (%i kB)\n", model->numNodes,
		( model->numNodes * sizeof( cm_node_t ) ) >> 10 );
	common->Printf( "%6i polygon refs, %6i brush refs\n", model->numPolygonRefs, model->numBrushRefs );
	common->Printf( "%6i internal edges, %6i sharp edges\n", model->numInternalEdges, model->numSharpEdges );
	common->Printf( "%6i kB total memory used\n", model->usedMemory >> 10 );
}

void idCollisionModelManagerLocal::AccumulateModelInfo( cm_model_t *total ) {
	ClearModel( total );
	for ( int i = 0; i < numModels; ++i ) {
		const cm_model_t *model = models[ i ];
		if ( model == NULL ) {
			continue;
		}
		total->numVertices += model->numVertices;
		total->numEdges += model->numEdges;
		total->numPolygons += model->numPolygons;
		total->numPolygonEdges += model->numPolygonEdges;
		total->numBrushes += model->numBrushes;
		total->numBrushPlanes += model->numBrushPlanes;
		total->numNodes += model->numNodes;
		total->numBrushRefs += model->numBrushRefs;
		total->numPolygonRefs += model->numPolygonRefs;
		total->numInternalEdges += model->numInternalEdges;
		total->numSharpEdges += model->numSharpEdges;
		total->numRemovedPolys += model->numRemovedPolys;
		total->numMergedPolys += model->numMergedPolys;
		total->usedMemory += model->usedMemory;
	}
}

void idCollisionModelManagerLocal::ModelInfo( cmHandle_t model ) {
	if ( model == -1 ) {
		cm_model_t total;
		AccumulateModelInfo( &total );
		PrintModelInfo( &total );
		return;
	}
	if ( model < 0 || model >= numModels || models[ model ] == NULL ) {
		common->Printf( "idCollisionModelManagerLocal::ModelInfo: invalid model handle\n" );
		return;
	}
	PrintModelInfo( models[ model ] );
}

void idCollisionModelManagerLocal::ListModels( void ) {
	unsigned int totalMemory = 0;
	for ( int i = 0; i < numModels; ++i ) {
		if ( models[ i ] == NULL ) {
			continue;
		}
		common->Printf( "%4d: %5d kB   %s\n", i, models[ i ]->usedMemory >> 10,
			models[ i ]->name.c_str() );
		totalMemory += models[ i ]->usedMemory;
	}
	common->Printf( "%4d kB in %d models\n", totalMemory >> 10, numModels );
}

cm_model_t *idCollisionModelManagerLocal::CollisionModelForMapEntity( const idMapEntity *mapEnt ) {
	if ( mapEnt == NULL || mapEnt->GetNumPrimitives() == 0 ) {
		return NULL;
	}
	common->Warning( "CollisionModelForMapEntity: map primitive conversion is not reconstructed yet" );
	return NULL;
}


/*
===============================================================================

	Trace-model conversion

===============================================================================
*/

cmHandle_t idCollisionModelManagerLocal::SetupTrmModel( const idTraceModel &trm,
		const idMaterial *material ) {
	if ( trm.type == TRM_INVALID || trm.numPolys <= 0 ||
		 trm.numVerts > MAX_TRACEMODEL_VERTS || trm.numEdges > MAX_TRACEMODEL_EDGES ||
		 trm.numPolys > MAX_TRACEMODEL_POLYS ) {
		return -1;
	}

	idCollisionModelLocal *model = AllocModel();
	model->isTraceModel = true;
	model->isConvex = trm.isConvex;
	model->bounds = trm.bounds;
	model->contents = -1;
	model->name = "_traceModel";
	model->node = AllocNode( model, 1 );
	memset( model->node, 0, sizeof( *model->node ) );
	model->node->planeType = -1;
	model->numNodes = 1;

	model->maxVertices = model->numVertices = trm.numVerts;
	model->vertices = static_cast< cm_vertex_t * >( Mem_ClearedAlloc( sizeof( cm_vertex_t ) * model->maxVertices ) );
	for ( int i = 0; i < trm.numVerts; ++i ) {
		model->vertices[ i ].p = trm.verts[ i ];
	}

	model->maxEdges = model->numEdges = trm.numEdges + 1;
	model->edges = static_cast< cm_edge_t * >( Mem_ClearedAlloc( sizeof( cm_edge_t ) * model->maxEdges ) );
	for ( int i = 1; i <= trm.numEdges; ++i ) {
		model->edges[ i ].vertexNum[ 0 ] = static_cast< unsigned short >( trm.edges[ i ].v[ 0 ] );
		model->edges[ i ].vertexNum[ 1 ] = static_cast< unsigned short >( trm.edges[ i ].v[ 1 ] );
		model->edges[ i ].normal = trm.edges[ i ].normal;
		model->edges[ i ].numUsers = 2;
	}

	if ( material == NULL ) {
		material = trmMaterial;
		if ( material == NULL ) {
			material = declHolder.FindMaterial( "_tracemodel", false );
			trmMaterial = material;
		}
	}

	for ( int i = 0; i < trm.numPolys; ++i ) {
		const traceModelPoly_t &source = trm.polys[ i ];
		cm_polygon_t *polygon = AllocPolygon( model, source.numEdges );
		for ( int j = 0; j < source.numEdges; ++j ) {
			polygon->edges[ j ] = static_cast< signed short >( source.edges[ j ] );
		}
		polygon->plane.SetNormal( source.normal );
		polygon->plane.SetDist( source.dist );
		polygon->bounds.SetBounds( source.bounds );
		polygon->contents = -1;
		polygon->material = material;
		polygon->primitiveNum = i;
		AddPolygonToNode( model, model->node, polygon );
	}

	if ( trm.isConvex ) {
		cm_brush_t *brush = AllocBrush( model, trm.numPolys );
		for ( int i = 0; i < trm.numPolys; ++i ) {
			brush->planes[ i ].SetNormal( trm.polys[ i ].normal );
			brush->planes[ i ].SetDist( trm.polys[ i ].dist );
		}
		brush->bounds.SetBounds( trm.bounds );
		brush->contents = -1;
		brush->material = material;
		AddBrushToNode( model, model->node, brush );
	}

	model->usedMemory = sizeof( *model ) + model->numVertices * sizeof( cm_vertex_t ) +
		model->numEdges * sizeof( cm_edge_t ) + model->numPolygons * sizeof( cm_polygon_t ) +
		model->numPolygonEdges * sizeof( signed short ) + model->numBrushes * sizeof( cm_brush_t ) +
		model->numBrushPlanes * sizeof( idPlane );

	if ( numModels == maxModels ) {
		const int newMax = maxModels == 0 ? 16 : maxModels + 16;
		cm_model_t **newModels = static_cast< cm_model_t ** >( Mem_Alloc( sizeof( *newModels ) * newMax ) );
		if ( models != NULL ) {
			memcpy( newModels, models, sizeof( *newModels ) * numModels );
			Mem_Free( models );
		}
		models = newModels;
		maxModels = newMax;
	}
	models[ numModels ] = model;
	baseTraceWork->modelCache.UpdateForModel( model );
	return numModels++;
}

bool idCollisionModelManagerLocal::TrmFromModel( const cm_model_t *model, idTraceModel &trm ) {
	if ( model == NULL || model->numVertices > MAX_TRACEMODEL_VERTS ||
		 model->numEdges > MAX_TRACEMODEL_EDGES + 1 || model->numPolygons > MAX_TRACEMODEL_POLYS ) {
		return false;
	}

	trm.type = TRM_CUSTOM;
	trm.numVerts = model->numVertices;
	trm.numEdges = Max( 0, model->numEdges - 1 );
	trm.numPolys = model->numPolygons;
	trm.bounds.Clear();

	for ( int i = 0; i < model->numVertices; ++i ) {
		trm.verts[ i ] = model->vertices[ i ].p;
		trm.bounds.AddPoint( trm.verts[ i ] );
	}
	for ( int i = 1; i <= trm.numEdges; ++i ) {
		trm.edges[ i ].v[ 0 ] = model->edges[ i ].vertexNum[ 0 ];
		trm.edges[ i ].v[ 1 ] = model->edges[ i ].vertexNum[ 1 ];
		trm.edges[ i ].normal = model->edges[ i ].normal;
	}
	for ( int i = 0; i < model->numPolygons; ++i ) {
		const cm_polygon_t &source = model->polygons[ i ];
		if ( source.numEdges > MAX_TRACEMODEL_POLYEDGES ) {
			return false;
		}
		traceModelPoly_t &polygon = trm.polys[ i ];
		polygon.bounds = source.bounds.ToBounds();
		polygon.normal = source.plane.Normal();
		polygon.dist = source.plane.Dist();
		polygon.numEdges = source.numEdges;
		for ( int j = 0; j < source.numEdges; ++j ) {
			polygon.edges[ j ] = source.edges[ j ];
		}
	}

	int edgeUsers[ MAX_TRACEMODEL_EDGES + 1 ];
	memset( edgeUsers, 0, sizeof( edgeUsers ) );
	for ( int i = 0; i < trm.numPolys; ++i ) {
		for ( int j = 0; j < trm.polys[ i ].numEdges; ++j ) {
			++edgeUsers[ abs( trm.polys[ i ].edges[ j ] ) ];
		}
	}
	for ( int i = 1; i <= trm.numEdges; ++i ) {
		if ( edgeUsers[ i ] != 2 ) {
			common->Printf( "idCollisionModelManagerLocal::TrmFromModel: model %s has dangling edges\n", model->name.c_str() );
			return false;
		}
	}

	trm.isConvex = true;
	for ( int i = 0; i < trm.numPolys && trm.isConvex; ++i ) {
		for ( int j = 0; j < trm.numVerts; ++j ) {
			if ( trm.polys[ i ].normal * trm.verts[ j ] - trm.polys[ i ].dist > 0.01f ) {
				trm.isConvex = false;
				break;
			}
		}
	}
	trm.offset = trm.bounds.GetCenter();
	trm.GenerateEdgeNormals();
	return true;
}
