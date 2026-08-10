// Copyright (C) 2007 Id Software, Inc.
//
// ETQW text collision-model serialization.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "CollisionModel_local.h"

#define CM_FILE_EXT			"cm"
#define CM_FILEID			"CM"
#define CM_FILEVERSION		"2.70"


static void CM_R_GetNodeBounds( cm_model_t *model, idBounds &bounds, cm_node_t *node ) {
	while ( node != NULL ) {
		for ( cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
			bounds.AddBounds( model->brushes[ ref->brushNum ].bounds.ToBounds() );
		}
		for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
			bounds.AddBounds( model->polygons[ ref->polygonNum ].bounds.ToBounds() );
		}

		if ( node->planeType == -1 ) {
			break;
		}
		CM_R_GetNodeBounds( model, bounds, node->children[ 0 ] );
		node = node->children[ 1 ];
	}
}

static void CM_GetNodeBounds( cm_model_t *model, idBounds &bounds, cm_node_t *node ) {
	bounds.Clear();
	CM_R_GetNodeBounds( model, bounds, node );
	if ( bounds.IsCleared() ) {
		bounds.Zero();
	}
}

static int CM_GetNodeContents( cm_model_t *model, cm_node_t *node ) {
	int contents = 0;
	while ( node != NULL ) {
		for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
			contents |= model->polygons[ ref->polygonNum ].contents;
		}
		for ( cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
			contents |= model->brushes[ ref->brushNum ].contents;
		}

		if ( node->planeType == -1 ) {
			break;
		}
		contents |= CM_GetNodeContents( model, node->children[ 1 ] );
		node = node->children[ 0 ];
	}
	return contents;
}

static int CM_GetModelMemory( cm_model_t *model ) {
	int memory = 2 * ( model->maxPolygonEdges
		+ 10 * ( model->maxEdges + 11 )
		+ 8 * ( model->maxBrushPlanes + 2 * ( model->maxBrushes + 2 * model->maxPolygons ) )
		+ 6 * model->maxVertices );

	for ( cm_nodeBlock_t *block = model->nodeBlocks; block != NULL; block = block->next ) {
		memory += block->size;
	}
	for ( cm_polygonRefBlock_t *block = model->polygonRefBlocks; block != NULL; block = block->next ) {
		memory += block->size;
	}
	for ( cm_brushRefBlock_t *block = model->brushRefBlocks; block != NULL; block = block->next ) {
		memory += block->size;
	}
	return memory;
}


/*
===============================================================================

	Text writer

===============================================================================
*/

void idCollisionModelManagerLocal::WriteNodes( idFile *fp, cm_node_t *node ) {
	while ( node != NULL ) {
		fp->WriteFloatString( "\t( %d %f )\n", node->planeType, node->planeDist );
		if ( node->planeType == -1 ) {
			break;
		}
		WriteNodes( fp, node->children[ 0 ] );
		node = node->children[ 1 ];
	}
}

int idCollisionModelManagerLocal::CountPolygonMemory( cm_model_t *model, cm_node_t *node ) {
	int memory = 0;
	while ( node != NULL ) {
		for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
			cm_polygonCache_t &visit = baseTraceWork->modelCache.polygonCache[ ref->polygonNum ];
			if ( visit.checkcount == baseTraceWork->modelCache.checkCount ) {
				continue;
			}
			visit.checkcount = baseTraceWork->modelCache.checkCount;
			memory += sizeof( cm_polygon_t ) + sizeof( signed short ) * model->polygons[ ref->polygonNum ].numEdges;
		}
		if ( node->planeType == -1 ) {
			break;
		}
		memory += CountPolygonMemory( model, node->children[ 0 ] );
		node = node->children[ 1 ];
	}
	return memory;
}

void idCollisionModelManagerLocal::WritePolygons( idFile *fp, cm_model_t *model, cm_node_t *node ) {
	while ( node != NULL ) {
		for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
			cm_polygonCache_t &visit = baseTraceWork->modelCache.polygonCache[ ref->polygonNum ];
			if ( visit.checkcount == baseTraceWork->modelCache.checkCount ) {
				continue;
			}
			visit.checkcount = baseTraceWork->modelCache.checkCount;

			const cm_polygon_t &polygon = model->polygons[ ref->polygonNum ];
			fp->WriteFloatString( "\t%d (", polygon.numEdges );
			for ( int i = 0; i < polygon.numEdges; ++i ) {
				fp->WriteFloatString( " %d", polygon.edges[ i ] );
			}
			fp->WriteFloatString( " ) ( %f %f %f ) %f",
				polygon.plane[ 0 ], polygon.plane[ 1 ], polygon.plane[ 2 ], polygon.plane.Dist() );
			fp->WriteFloatString( " ( %d %d %d )",
				polygon.bounds[ 0 ][ 0 ], polygon.bounds[ 0 ][ 1 ], polygon.bounds[ 0 ][ 2 ] );
			fp->WriteFloatString( " ( %d %d %d )",
				polygon.bounds[ 1 ][ 0 ], polygon.bounds[ 1 ][ 1 ], polygon.bounds[ 1 ][ 2 ] );
			fp->WriteFloatString( " \"%s\"", polygon.material != NULL ? polygon.material->GetName() : "_default" );

			cm_contents_override_t overrideValue;
			if ( ref->polygonNum < model->contentsOverrides.Num() ) {
				overrideValue = model->contentsOverrides[ ref->polygonNum ];
			}
			fp->WriteFloatString( " %i %i", overrideValue.contentsAdd, overrideValue.contentsRemove );
			fp->WriteFloatString( " %f %f %f %f",
				polygon.texAxis.axis[ 0 ][ 0 ], polygon.texAxis.axis[ 0 ][ 1 ],
				polygon.texAxis.axis[ 1 ][ 0 ], polygon.texAxis.axis[ 1 ][ 1 ] );
			fp->WriteFloatString( " %d %d\n", polygon.texAxis.offset[ 0 ], polygon.texAxis.offset[ 1 ] );
		}

		if ( node->planeType == -1 ) {
			break;
		}
		WritePolygons( fp, model, node->children[ 0 ] );
		node = node->children[ 1 ];
	}
}

int idCollisionModelManagerLocal::CountBrushMemory( cm_model_t *model, cm_node_t *node ) {
	int memory = 0;
	while ( node != NULL ) {
		for ( cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
			cm_brushCache_t &visit = baseTraceWork->modelCache.brushCache[ ref->brushNum ];
			if ( visit.checkcount == baseTraceWork->modelCache.checkCount ) {
				continue;
			}
			visit.checkcount = baseTraceWork->modelCache.checkCount;
			memory += sizeof( cm_brush_t ) + sizeof( idPlane ) * model->brushes[ ref->brushNum ].numPlanes;
		}
		if ( node->planeType == -1 ) {
			break;
		}
		memory += CountBrushMemory( model, node->children[ 0 ] );
		node = node->children[ 1 ];
	}
	return memory;
}

void idCollisionModelManagerLocal::WriteBrushes( idFile *fp, cm_model_t *model, cm_node_t *node ) {
	while ( node != NULL ) {
		for ( cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
			cm_brush_t &brush = model->brushes[ ref->brushNum ];
			if ( !( brush.contents & 0xFFFFFFFC ) ) {
				continue;
			}

			cm_brushCache_t &visit = baseTraceWork->modelCache.brushCache[ ref->brushNum ];
			if ( visit.checkcount == baseTraceWork->modelCache.checkCount ) {
				continue;
			}
			visit.checkcount = baseTraceWork->modelCache.checkCount;

			fp->WriteFloatString( "\t%d {\n", brush.numPlanes );
			for ( int i = 0; i < brush.numPlanes; ++i ) {
				fp->WriteFloatString( "\t\t( %f %f %f ) %f\n",
					brush.planes[ i ][ 0 ], brush.planes[ i ][ 1 ], brush.planes[ i ][ 2 ], brush.planes[ i ].Dist() );
			}
			fp->WriteFloatString( "\t} ( %d %d %d )",
				brush.bounds[ 0 ][ 0 ], brush.bounds[ 0 ][ 1 ], brush.bounds[ 0 ][ 2 ] );
			fp->WriteFloatString( " ( %d %d %d ) \"%s\" %d\n",
				brush.bounds[ 1 ][ 0 ], brush.bounds[ 1 ][ 1 ], brush.bounds[ 1 ][ 2 ],
				StringFromContents( brush.contents ), 0 );
		}

		if ( node->planeType == -1 ) {
			break;
		}
		WriteBrushes( fp, model, node->children[ 0 ] );
		node = node->children[ 1 ];
	}
}

void idCollisionModelManagerLocal::WriteCollisionModel( idFile *fp, cm_model_t *model ) {
	fp->WriteFloatString( "collisionModel \"%s\" %d {\n", model->name.c_str(), model->numPrimitives );

	fp->WriteFloatString( "\tvertices { /* numVertices = */ %d\n", model->numVertices );
	for ( int i = 0; i < model->numVertices; ++i ) {
		fp->WriteFloatString( "\t/* %d */ ( %f %f %f )\n", i,
			model->vertices[ i ].p[ 0 ], model->vertices[ i ].p[ 1 ], model->vertices[ i ].p[ 2 ] );
	}
	fp->WriteFloatString( "\t}\n" );

	fp->WriteFloatString( "\tedges { /* numEdges = */ %d\n", model->numEdges );
	for ( int i = 0; i < model->numEdges; ++i ) {
		fp->WriteFloatString( "\t/* %d */ ( %d %d ) %d %d\n", i,
			model->edges[ i ].vertexNum[ 0 ], model->edges[ i ].vertexNum[ 1 ],
			model->edges[ i ].internal, model->edges[ i ].numUsers );
	}
	fp->WriteFloatString( "\t}\n" );

	fp->WriteFloatString( "\tnodes {\n" );
	WriteNodes( fp, model->node );
	fp->WriteFloatString( "\t}\n" );

	baseTraceWork->modelCache.UpdateForModel( model );
	baseTraceWork->modelCache.IncCheckCount();
	fp->WriteFloatString( "\tpolygons /* numPolygons = */ %d /* numPolygonEdges = */ %d {\n",
		model->numPolygons, model->numPolygonEdges );
	WritePolygons( fp, model, model->node );
	fp->WriteFloatString( "\t}\n" );

	baseTraceWork->modelCache.IncCheckCount();
	fp->WriteFloatString( "\tbrushes /* numBrushes = */ %d /* numBrushPlanes = */ %d {\n",
		model->numBrushes, model->numBrushPlanes );
	WriteBrushes( fp, model, model->node );
	fp->WriteFloatString( "\t}\n" );
	fp->WriteFloatString( "}\n" );
}

void idCollisionModelManagerLocal::WriteCollisionModelsToFile( const char *filename, int firstModel,
		int lastModel, unsigned int mapFileCRC ) {
	idStr name = filename;
	name.SetFileExtension( CM_FILE_EXT );

	common->Printf( "writing %s\n", name.c_str() );
	idFile *fp = fileSystem->OpenFileWrite( name, "fs_devpath" );
	if ( fp == NULL ) {
		common->Warning( "idCollisionModelManagerLocal::WriteCollisionModelsToFile: Error opening file %s", name.c_str() );
		return;
	}

	fp->WriteFloatString( "%s \"%s\"\n", CM_FILEID, CM_FILEVERSION );
	fp->WriteFloatString( "%u\n", mapFileCRC );
	for ( int i = firstModel; i < lastModel && i < numModels; ++i ) {
		WriteCollisionModel( fp, models[ i ] );
	}
	fileSystem->CloseFile( fp );
}

bool idCollisionModelManagerLocal::WriteCollisionModelForMapEntity( const idMapEntity *mapEnt,
		const char *filename, const bool testTraceModel ) {
	SetupHash();
	cm_model_t *model = CollisionModelForMapEntity( mapEnt );
	ShutdownHash();
	if ( model == NULL ) {
		return false;
	}
	model->name = filename;

	idStr name = filename;
	name.SetFileExtension( CM_FILE_EXT );
	idFile *fp = fileSystem->OpenFileWrite( name, "fs_devpath" );
	if ( fp == NULL ) {
		FreeModelMemory( model );
		delete model;
		return false;
	}

	fp->WriteFloatString( "%s \"%s\"\n0\n", CM_FILEID, CM_FILEVERSION );
	WriteCollisionModel( fp, model );
	fileSystem->CloseFile( fp );

	if ( testTraceModel ) {
		idTraceModel trm;
		TrmFromModel( model, trm );
	}

	FreeModelMemory( model );
	delete model;
	return true;
}


/*
===============================================================================

	Text reader

===============================================================================
*/

void idCollisionModelManagerLocal::ParseVertices( idLexer *src, cm_model_t *model ) {
	src->ExpectTokenString( "{" );
	model->numVertices = src->ParseInt();
	model->maxVertices = model->numVertices;
	model->vertices = static_cast< cm_vertex_t * >( Mem_Alloc( sizeof( cm_vertex_t ) * model->maxVertices ) );
	baseTraceWork->modelCache.UpdateForModel( model );

	for ( int i = 0; i < model->numVertices; ++i ) {
		src->Parse1DMatrix( 3, model->vertices[ i ].p.ToFloatPtr() );
	}
	src->ExpectTokenString( "}" );
}

void idCollisionModelManagerLocal::ParseEdges( idLexer *src, cm_model_t *model ) {
	src->ExpectTokenString( "{" );
	model->numEdges = src->ParseInt();
	model->maxEdges = model->numEdges;
	model->edges = static_cast< cm_edge_t * >( Mem_Alloc( sizeof( cm_edge_t ) * model->maxEdges ) );
	baseTraceWork->modelCache.UpdateForModel( model );

	for ( int i = 0; i < model->numEdges; ++i ) {
		src->ExpectTokenString( "(" );
		model->edges[ i ].vertexNum[ 0 ] = static_cast< unsigned short >( src->ParseInt() );
		model->edges[ i ].vertexNum[ 1 ] = static_cast< unsigned short >( src->ParseInt() );
		src->ExpectTokenString( ")" );
		model->edges[ i ].internal = static_cast< unsigned short >( src->ParseInt() );
		model->edges[ i ].numUsers = static_cast< unsigned short >( src->ParseInt() );
		model->edges[ i ].normal.Zero();
		model->numInternalEdges += model->edges[ i ].internal;
	}
	src->ExpectTokenString( "}" );
}

cm_node_t *idCollisionModelManagerLocal::ParseNodes( idLexer *src, cm_model_t *model, cm_node_t *parent ) {
	++model->numNodes;
	cm_node_t *node = AllocNode( model,
		model->numNodes < NODE_BLOCK_SIZE_SMALL ? NODE_BLOCK_SIZE_SMALL : NODE_BLOCK_SIZE_LARGE );
	node->brushes = NULL;
	node->polygons = NULL;
	node->parent = parent;
	src->ExpectTokenString( "(" );
	node->planeType = src->ParseInt();
	node->planeDist = src->ParseFloat();
	src->ExpectTokenString( ")" );
	if ( node->planeType != -1 ) {
		node->children[ 0 ] = ParseNodes( src, model, node );
		node->children[ 1 ] = ParseNodes( src, model, node );
	}
	return node;
}

void idCollisionModelManagerLocal::ParsePolygons( idLexer *src, cm_model_t *model ) {
	model->maxPolygons = src->ParseInt();
	model->numPolygons = 0;
	model->polygons = model->maxPolygons > 0
		? static_cast< cm_polygon_t * >( Mem_AllocAligned( sizeof( cm_polygon_t ) * model->maxPolygons, ALIGN_64 ) )
		: NULL;
	model->maxPolygonEdges = src->ParseInt();
	model->numPolygonEdges = 0;
	model->polygonEdges = model->maxPolygonEdges > 0
		? static_cast< signed short * >( Mem_AllocAligned( sizeof( signed short ) * model->maxPolygonEdges, ALIGN_16 ) )
		: NULL;
	baseTraceWork->modelCache.UpdateForModel( model );
	model->contentsOverrides.Clear();

	idToken token;
	src->ExpectTokenString( "{" );
	while ( !src->CheckTokenString( "}" ) ) {
		const int numEdges = src->ParseInt();
		cm_polygon_t *polygon = AllocPolygon( model, numEdges );
		src->ExpectTokenString( "(" );
		for ( int i = 0; i < polygon->numEdges; ++i ) {
			polygon->edges[ i ] = static_cast< signed short >( src->ParseInt() );
		}
		src->ExpectTokenString( ")" );

		idVec3 normal;
		src->Parse1DMatrix( 3, normal.ToFloatPtr() );
		polygon->plane.SetNormal( normal );
		polygon->plane.SetDist( src->ParseFloat() );

		idBounds bounds;
		src->Parse1DMatrix( 3, bounds[ 0 ].ToFloatPtr() );
		src->Parse1DMatrix( 3, bounds[ 1 ].ToFloatPtr() );
		polygon->bounds.SetBounds( bounds );

		src->ExpectTokenType( TT_STRING, 0, &token );
		polygon->material = declHolder.FindMaterial( token );
		polygon->contents = polygon->material != NULL ? polygon->material->GetContentFlags() : 0;

		cm_contents_override_t overrideValue;
		overrideValue.contentsAdd = src->ParseInt();
		overrideValue.contentsRemove = src->ParseInt();
		polygon->contents = ( polygon->contents | overrideValue.contentsAdd ) & ~overrideValue.contentsRemove;
		model->contentsOverrides.Append( overrideValue );

		polygon->texAxis.axis[ 0 ][ 0 ] = src->ParseFloat();
		polygon->texAxis.axis[ 0 ][ 1 ] = src->ParseFloat();
		polygon->texAxis.axis[ 1 ][ 0 ] = src->ParseFloat();
		polygon->texAxis.axis[ 1 ][ 1 ] = src->ParseFloat();
		polygon->texAxis.offset[ 0 ] = static_cast< unsigned short >( src->ParseInt() );
		polygon->texAxis.offset[ 1 ] = static_cast< unsigned short >( src->ParseInt() );
		polygon->primitiveNum = 0;

		R_FilterPolygonIntoTree( model, model->node, NULL, polygon );
	}
}

void idCollisionModelManagerLocal::ParseBrushes( idLexer *src, cm_model_t *model ) {
	model->maxBrushes = src->ParseInt();
	model->numBrushes = 0;
	model->brushes = model->maxBrushes > 0
		? static_cast< cm_brush_t * >( Mem_AllocAligned( sizeof( cm_brush_t ) * model->maxBrushes, ALIGN_64 ) )
		: NULL;
	model->maxBrushPlanes = src->ParseInt();
	model->numBrushPlanes = 0;
	model->brushPlanes = model->maxBrushPlanes > 0
		? static_cast< idPlane * >( Mem_AllocAligned( sizeof( idPlane ) * model->maxBrushPlanes, ALIGN_16 ) )
		: NULL;
	baseTraceWork->modelCache.UpdateForModel( model );

	idToken token;
	src->ExpectTokenString( "{" );
	while ( !src->CheckTokenString( "}" ) ) {
		const int numPlanes = src->ParseInt();
		cm_brush_t *brush = AllocBrush( model, numPlanes );
		src->ExpectTokenString( "{" );
		for ( int i = 0; i < brush->numPlanes; ++i ) {
			idVec3 normal;
			src->Parse1DMatrix( 3, normal.ToFloatPtr() );
			brush->planes[ i ].SetNormal( normal );
			brush->planes[ i ].SetDist( src->ParseFloat() );
		}
		src->ExpectTokenString( "}" );

		idBounds bounds;
		src->Parse1DMatrix( 3, bounds[ 0 ].ToFloatPtr() );
		src->Parse1DMatrix( 3, bounds[ 1 ].ToFloatPtr() );
		brush->bounds.SetBounds( bounds );

		src->ExpectTokenType( TT_STRING, 0, &token );
		brush->contents = ContentsFromString( token );
		brush->primitiveNum = 0;
		brush->material = NULL;
		src->ParseInt();
		R_FilterBrushIntoTree( model, model->node, NULL, brush );
	}
}

bool idCollisionModelManagerLocal::ParseCollisionModel( idLexer *src ) {
	idToken token;
	src->ExpectTokenType( TT_STRING, 0, &token );

	cm_model_t *model = NULL;
	for ( int i = 0; i < numModels; ++i ) {
		if ( !models[ i ]->name.Icmp( token ) ) {
			model = models[ i ];
			FreeModelMemory( model );
			break;
		}
	}

	if ( model == NULL ) {
		model = AllocModel();
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
		models[ numModels++ ] = model;
	}

	model->name = token;
	model->refCount = 0;
	model->numPrimitives = src->ParseInt();
	src->ExpectTokenString( "{" );
	while ( !src->CheckTokenString( "}" ) ) {
		src->ReadToken( &token );
		if ( token == "vertices" ) {
			ParseVertices( src, model );
		} else if ( token == "edges" ) {
			ParseEdges( src, model );
		} else if ( token == "nodes" ) {
			src->ExpectTokenString( "{" );
			model->node = ParseNodes( src, model, NULL );
			src->ExpectTokenString( "}" );
		} else if ( token == "polygons" ) {
			ParsePolygons( src, model );
		} else if ( token == "brushes" ) {
			ParseBrushes( src, model );
		} else {
			src->Error( "ParseCollisionModel: bad token \"%s\"", token.c_str() );
		}
	}

	CalculateEdgeNormals( model, model->node );
	CM_GetNodeBounds( model, model->bounds, model->node );
	model->contents = CM_GetNodeContents( model, model->node );
	model->usedMemory = CM_GetModelMemory( model );
	return true;
}

bool idCollisionModelManagerLocal::LoadCollisionModelFile( const char *name, unsigned int mapFileCRC ) {
	idStr fileName = va( "generated/cm/%s", name );
	fileName.SetFileExtension( CM_FILE_EXT );
	idLexer *src = new idLexer( fileName, LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE );
	if ( !src->IsLoaded() ) {
		delete src;
		fileName = name;
		fileName.SetFileExtension( CM_FILE_EXT );
		src = new idLexer( fileName, LEXFL_NOSTRINGCONCAT | LEXFL_NODOLLARPRECOMPILE );
	}
	if ( !src->IsLoaded() ) {
		delete src;
		return false;
	}

	idToken token;
	if ( !src->ExpectTokenString( CM_FILEID ) ) {
		common->Warning( "%s is not a CM file", fileName.c_str() );
		delete src;
		return false;
	}
	if ( !src->ReadToken( &token ) || token != CM_FILEVERSION ) {
		common->Warning( "%s has version %s instead of %s", fileName.c_str(), token.c_str(), CM_FILEVERSION );
		delete src;
		return false;
	}
	if ( !src->ExpectTokenType( TT_NUMBER, TT_INTEGER, &token ) ) {
		common->Warning( "%s has no map file CRC", fileName.c_str() );
		delete src;
		return false;
	}
	const unsigned int crc = token.GetUnsignedLongValue();
	if ( mapFileCRC != 0 && crc != 0 && crc != mapFileCRC ) {
		common->Printf( "%s is out of date\n", fileName.c_str() );
		delete src;
		return false;
	}

	while ( src->ReadToken( &token ) ) {
		if ( token != "collisionModel" ) {
			src->Error( "idCollisionModelManagerLocal::LoadCollisionModelFile: bad token \"%s\"", token.c_str() );
		}
		if ( !ParseCollisionModel( src ) ) {
			delete src;
			return false;
		}
	}

	delete src;
	return true;
}

