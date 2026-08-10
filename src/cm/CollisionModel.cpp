// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW SDK interface and the address-matched
// Hex-Rays routines in etqw.exe.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "CollisionModel_local.h"
#include "../libs/qglLib/qgl.h"
#include "../decllib/DeclSurfaceTypeMap.h"
#include "../renderer/SurfaceTypeMap.h"

const idVec3 &idCollisionModelLocal::GetVertex( int vertexNum ) const {
	if ( vertexNum < 0 || vertexNum >= numVertices ) {
		common->Error( "idCollisionModelLocal::GetModelVertex: invalid vertex number" );
	}
	return vertices[ vertexNum ].p;
}

int idCollisionModelLocal::GetNumBrushPlanes() const {
	return numBrushPlanes;
}

const idPlane &idCollisionModelLocal::GetBrushPlane( int planeNum ) const {
	return brushPlanes[ planeNum ];
}

const idMaterial *idCollisionModelLocal::GetPolygonMaterial( int polygonNum ) const {
	return polygons[ polygonNum ].material;
}

const idPlane &idCollisionModelLocal::GetPolygonPlane( int polygonNum ) const {
	return polygons[ polygonNum ].plane;
}

int idCollisionModelLocal::GetNumPolygons() const {
	return numPolygons;
}

void idCollisionModelLocal::NodeBounds_r( const cm_node_t *node, idBounds &outBounds,
		int surfaceMask, bool inclusive ) const {
	for ( const cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
		const cm_polygon_t &polygon = polygons[ ref->polygonNum ];
		if ( ( ( polygon.material->GetSurfaceFlags() & surfaceMask ) != 0 ) == inclusive ) {
			outBounds.AddBounds( polygon.bounds.ToBounds() );
		}
	}

	if ( node->planeType != -1 ) {
		NodeBounds_r( node->children[ 0 ], outBounds, surfaceMask, inclusive );
		NodeBounds_r( node->children[ 1 ], outBounds, surfaceMask, inclusive );
	}
}

void idCollisionModelLocal::GetEdge( int edgeNum, idVec3 &start, idVec3 &end ) const {
	const int absoluteEdgeNum = abs( edgeNum );
	if ( absoluteEdgeNum >= numEdges ) {
		common->Error( "idCollisionModelLocal::GetModelEdge: invalid edge number" );
	}

	start = vertices[ edges[ absoluteEdgeNum ].vertexNum[ 0 ] ].p;
	end = vertices[ edges[ absoluteEdgeNum ].vertexNum[ 1 ] ].p;
}

void idCollisionModelLocal::DrawNode_r( const cm_node_t *node, int surfaceMask, bool inclusive ) const {
	for ( const cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
		const cm_polygon_t &polygon = polygons[ ref->polygonNum ];
		if ( ( ( polygon.material->GetSurfaceFlags() & surfaceMask ) != 0 ) != inclusive ) {
			continue;
		}

		qglBegin( GL_POLYGON );
		qglNormal3fv( polygon.plane.ToFloatPtr() );
		for ( int i = polygon.numEdges - 1; i >= 0; --i ) {
			const int edgeNum = polygon.edges[ i ];
			const int vertexNum = edges[ abs( edgeNum ) ].vertexNum[ INTSIGNBITSET( edgeNum ) ];
			qglVertex3fv( vertices[ vertexNum ].p.ToFloatPtr() );
		}
		qglEnd();
	}

	if ( node->planeType != -1 ) {
		DrawNode_r( node->children[ 0 ], surfaceMask, inclusive );
		DrawNode_r( node->children[ 1 ], surfaceMask, inclusive );
	}
}

void idCollisionModelLocal::GetBounds( idBounds &outBounds, int surfaceMask, bool inclusive ) const {
	outBounds.Clear();
	NodeBounds_r( node, outBounds, surfaceMask, inclusive );
}

void idCollisionModelLocal::Draw( int surfaceMask, bool inclusive ) const {
	DrawNode_r( node, surfaceMask, inclusive );
}

void idCollisionModelLocal::GetPolygon( int polygonNum, idFixedWinding &winding ) const {
	const cm_polygon_t &polygon = polygons[ polygonNum ];
	winding.Clear();

	for ( int i = 0; i < polygon.numEdges; ++i ) {
		const int edgeNum = polygon.edges[ i ];
		const int vertexNum = edges[ abs( edgeNum ) ].vertexNum[ INTSIGNBITSET( edgeNum ) ];
		winding += vertices[ vertexNum ].p;
	}
}

void idCollisionModelManagerLocal::GetFullModelName( idStr &out, const char *mapName,
		const char *modelName ) const {
	if ( modelName == NULL ) {
		out.Clear();
		return;
	}

	if ( idStr::IcmpnPath( modelName, "models/", 7 ) == 0 ) {
		out = modelName;
		return;
	}

	idStr mapBase = mapName != NULL ? mapName : "";
	mapBase.StripFileExtension();
	if ( mapBase.IsEmpty() || idStr::IcmpnPath( modelName, mapBase.c_str(), mapBase.Length() ) == 0 ) {
		out = modelName;
		return;
	}

	out = mapBase;
	out.AppendPath( modelName );
}

void idCollisionModelManagerLocal::LoadMap( const char *fileName, bool forceReload ) {
	idStr worldName;
	GetFullModelName( worldName, fileName, WORLD_MODEL_NAME );
	if ( !forceReload && FindModel( worldName.c_str() ) >= 0 ) {
		return;
	}

	SetupHash();
	if ( !LoadCollisionModelFileBinary( fileName ) ) {
		LoadCollisionModelFile( fileName, 0 );
	}
	ShutdownHash();
}

idCollisionModel *idCollisionModelManagerLocal::LoadModel( const char *mapName, const char *modelName ) {
	idStr fullName;
	GetFullModelName( fullName, mapName, modelName );
	const cmHandle_t handle = LoadModel( fullName.c_str(), true );
	if ( handle < 0 || handle >= numModels || models[ handle ] == NULL ) {
		return NULL;
	}
	++models[ handle ]->refCount;
	return models[ handle ];
}

void idCollisionModelManagerLocal::FreeModel( idCollisionModel *model ) {
	if ( model == NULL ) {
		return;
	}
	idCollisionModelLocal *localModel = static_cast< idCollisionModelLocal * >( model );
	if ( localModel->refCount > 0 ) {
		--localModel->refCount;
	}
}

void idCollisionModelManagerLocal::PurgeModels( void ) {
	for ( int i = numModels - 1; i >= 0; --i ) {
		idCollisionModelLocal *model = models[ i ];
		if ( model == NULL || model->refCount != 0 || model->isWorld ) {
			continue;
		}

		FreeModelMemory( model );
		delete model;
		for ( int j = i + 1; j < numModels; ++j ) {
			models[ j - 1 ] = models[ j ];
		}
		--numModels;
	}
}

idCollisionModel *idCollisionModelManagerLocal::ModelFromTrm( const char *mapName,
		const char *modelName, const idTraceModel &trm, bool includeBrushes ) {
	const cmHandle_t handle = SetupTrmModel( trm, NULL );
	if ( handle < 0 || handle >= numModels || models[ handle ] == NULL ) {
		return NULL;
	}
	idCollisionModelLocal *model = models[ handle ];
	GetFullModelName( model->name, mapName, modelName );
	model->isTraceModel = true;
	++model->refCount;
	return model;
}

bool idCollisionModelManagerLocal::TrmFromModel( const char *mapName, const char *modelName,
		idTraceModel &trm ) {
	idCollisionModel *model = LoadModel( mapName, modelName );
	if ( model == NULL ) {
		return false;
	}
	const bool result = TrmFromModel( static_cast< idCollisionModelLocal * >( model ), trm );
	FreeModel( model );
	return result;
}

int idCollisionModelManagerLocal::CompoundTrmFromModel( const char *mapName, const char *modelName,
		idTraceModel *trms, int maxTrms ) {
	if ( trms == NULL || maxTrms <= 0 ) {
		return 0;
	}
	return TrmFromModel( mapName, modelName, trms[ 0 ] ) ? 1 : 0;
}

void idCollisionModelManagerLocal::CreateCollisionFromWorld( const collisionWorldFile_t &world ) {
	common->Warning( "CreateCollisionFromWorld is awaiting the ETQW world-conversion pass for '%s'", world.name.c_str() );
}

void idCollisionModelManagerLocal::DumpCollisionModelStats( void ) {
	ListModels();
}

const sdDeclSurfaceType *idCollisionModelManagerLocal::GetSurfaceType( contactInfo_t *contact,
		cm_polygon_t *polygon, idVec3 *color ) {
	if ( contact == NULL || contact->material == NULL ) {
		if ( color != NULL ) {
			color->Zero();
		}
		return NULL;
	}

	if ( polygon != NULL ) {
		const sdDeclSurfaceTypeMap* declMap = contact->material->GetSurfaceTypeMapDecl();
		const sdSurfaceTypeMap* imageMap = contact->material->GetSurfaceTypeMap();
		if ( declMap != NULL || imageMap != NULL ) {
			const idVec3& normal = polygon->plane.Normal();
			int dominantAxis = 0;
			if ( idMath::Fabs( normal.y ) > idMath::Fabs( normal.x ) ) {
				dominantAxis = 1;
			}
			if ( idMath::Fabs( normal.z ) > idMath::Fabs( normal[ dominantAxis ] ) ) {
				dominantAxis = 2;
			}

			const idVec2 point(
				contact->point[ ( dominantAxis + 1 ) % 3 ],
				contact->point[ ( dominantAxis + 2 ) % 3 ]
			);
			idVec2 tc;
			tc.x = polygon->texAxis.axis[ 0 ][ 0 ] * point.x +
				polygon->texAxis.axis[ 1 ][ 0 ] * point.y +
				polygon->texAxis.offset[ 0 ] * ( 1.0f / 65536.0f );
			tc.y = polygon->texAxis.axis[ 0 ][ 1 ] * point.x +
				polygon->texAxis.axis[ 1 ][ 1 ] * point.y +
				polygon->texAxis.offset[ 1 ] * ( 1.0f / 65536.0f );
			if ( tc.x < 0.0f || tc.x > 1.0f ) {
				tc.x -= idMath::Floor( tc.x );
			}
			if ( tc.y < 0.0f || tc.y > 1.0f ) {
				tc.y -= idMath::Floor( tc.y );
			}

			if ( declMap != NULL ) {
				const sdDeclSurfaceType* mappedType = declMap->GetSurfaceType( tc, color );
				if ( mappedType != NULL ) {
					return mappedType;
				}
			} else {
				return imageMap->GetSurfaceType( tc, color );
			}
		}
	}

	if ( color != NULL ) {
		*color = contact->material->GetSurfaceColor();
	}
	return contact->material->GetSurfaceType();
}
