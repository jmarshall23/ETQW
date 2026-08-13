// Copyright (C) 2026 - QuakeWars2 contributors.
//
// Recast/Detour compiler for ETQW .world source maps.

#include "../../../framework/precompiled.h"
#pragma hdrstop

#include "NavBuild.h"
#include "../../../cm/CollisionModel.h"
#include "../../../navigation/NavigationFile.h"
#include "../../../renderer/Material.h"
#include "../../../renderer/Model.h"
#include "../../../renderer/ModelManager.h"
#include "../../../recast/Recast/Include/Recast.h"
#include "../../../recast/Detour/Include/DetourAlloc.h"
#include "../../../recast/Detour/Include/DetourNavMesh.h"
#include "../../../recast/Detour/Include/DetourNavMeshBuilder.h"

#include <vector>

namespace {

static const float NAV_WORLD_RADIUS = 131072.0f;
static int navBuildRecastWarningCount = 0;
static const int NAV_BUILD_RECAST_WARNING_LIMIT = 8;

struct navSourceTriangle_t {
	int		firstVertex;
	int		contents;
};

struct navSourceLink_t {
	idStr		name;
	idVec3		start;
	idVec3		end;
	int			travelFlags;
	unsigned int userId;
};

struct navSourceGeometry_t {
	std::vector< float > vertices;
	std::vector< navSourceTriangle_t > triangles;
	std::vector< navSourceLink_t > links;
	idBounds bounds;

	navSourceGeometry_t() { bounds.Clear(); }
};

struct navTileData_t {
	unsigned char*	data;
	int				size;
	navTileData_t() : data( NULL ), size( 0 ) {}
};

struct navCompiledProfile_t {
	navFileProfileHeader_t header;
	std::vector< navTileData_t > tiles;
	std::vector< navFileLink_t > links;
	navCompiledProfile_t() { memset( &header, 0, sizeof( header ) ); }
	~navCompiledProfile_t() {
		for ( size_t index = 0; index < tiles.size(); ++index ) {
			if ( tiles[ index ].data != NULL ) dtFree( tiles[ index ].data );
		}
	}
};

class navBuildContext_t : public rcContext {
public:
	navBuildContext_t() : rcContext( true ) {}
protected:
	virtual void doLog( const rcLogCategory category, const char* message, const int length ) {
		idStr text( message, 0, length );
		if ( category == RC_LOG_ERROR ) common->Warning( "navBuild: %s", text.c_str() );
		else if ( category == RC_LOG_WARNING ) {
			if ( navBuildRecastWarningCount < NAV_BUILD_RECAST_WARNING_LIMIT ) common->Printf( "navBuild warning: %s\n", text.c_str() );
			++navBuildRecastWarningCount;
		}
	}
};

static void IdToRecast( const idVec3& point, float out[ 3 ] ) {
	out[ 0 ] = point.x;
	out[ 1 ] = point.z;
	out[ 2 ] = point.y;
}

static unsigned int HashLinkName( const char* name ) {
	unsigned int hash = 2166136261u;
	for ( const unsigned char* cursor = reinterpret_cast< const unsigned char* >( name ); *cursor != 0; ++cursor ) {
		hash ^= *cursor;
		hash *= 16777619u;
	}
	return hash != 0 ? hash : 1;
}

static int TravelFlagsForType( const char* type ) {
	if ( idStr::Icmp( type, "jump" ) == 0 ) return NAV_TFL_JUMP;
	if ( idStr::Icmp( type, "barrierjump" ) == 0 ) return NAV_TFL_BARRIERJUMP;
	if ( idStr::Icmp( type, "walkoffledge" ) == 0 ) return NAV_TFL_WALKOFFLEDGE;
	if ( idStr::Icmp( type, "walkoffbarrier" ) == 0 ) return NAV_TFL_WALKOFFBARRIER;
	if ( idStr::Icmp( type, "ladder" ) == 0 ) return NAV_TFL_LADDER;
	if ( idStr::Icmp( type, "teleport" ) == 0 ) return NAV_TFL_TELEPORT;
	if ( idStr::Icmp( type, "elevator" ) == 0 ) return NAV_TFL_ELEVATOR;
	return NAV_TFL_WALK;
}

static void AddTriangle( navSourceGeometry_t& geometry, const idVec3& a, const idVec3& b, const idVec3& c, int contents ) {
	// idTech is Z-up while Recast is Y-up.  Swapping Y/Z reverses handedness,
	// so reverse B/C to preserve the original outward-facing normal.
	const idVec3 points[ 3 ] = { a, c, b };
	navSourceTriangle_t triangle;
	triangle.firstVertex = static_cast< int >( geometry.vertices.size() / 3 );
	triangle.contents = contents;
	geometry.triangles.push_back( triangle );
	for ( int index = 0; index < 3; ++index ) {
		float recastPoint[ 3 ];
		IdToRecast( points[ index ], recastPoint );
		geometry.vertices.push_back( recastPoint[ 0 ] );
		geometry.vertices.push_back( recastPoint[ 1 ] );
		geometry.vertices.push_back( recastPoint[ 2 ] );
		geometry.bounds.AddPoint( points[ index ] );
	}
}

static int MaterialContents( const char* materialName ) {
	const idMaterial* material = declManager->FindMaterial( materialName, false );
	return material != NULL ? material->GetContentFlags() : 0;
}

static bool ContentsContributeGeometry( int contents ) {
	const int navigationSolids = CONTENTS_SOLID | CONTENTS_AAS_SOLID_PLAYER | CONTENTS_AAS_SOLID_VEHICLE;
	return ( contents & navigationSolids ) != 0 && ( contents & ( CONTENTS_AAS_OBSTACLE | CONTENTS_AAS_CLUSTER_PORTAL | CONTENTS_TRIGGER ) ) == 0;
}

static void AddBrush( navSourceGeometry_t& geometry, const idMapBrush& brush, const idVec3& origin, const idMat3& axis ) {
	int brushContents = 0;
	for ( int sideIndex = 0; sideIndex < brush.GetNumSides(); ++sideIndex ) brushContents |= MaterialContents( brush.GetSide( sideIndex )->GetMaterial() );
	// Binary obstacle volumes must leave polygons underneath them so the game
	// can toggle those polygons at runtime.  Cluster portals and triggers are
	// authoring metadata rather than rasterized geometry.
	if ( !ContentsContributeGeometry( brushContents ) ) return;
	for ( int sideIndex = 0; sideIndex < brush.GetNumSides(); ++sideIndex ) {
		const idMapBrushSide* side = brush.GetSide( sideIndex );
		idWinding winding( side->GetPlane(), NAV_WORLD_RADIUS );
		for ( int clipIndex = 0; clipIndex < brush.GetNumSides() && winding.GetNumPoints() >= 3; ++clipIndex ) {
			if ( clipIndex == sideIndex ) continue;
			idPlane clipPlane = -brush.GetSide( clipIndex )->GetPlane();
			winding.ClipInPlace( clipPlane, 0.1f, true );
		}
		if ( winding.GetNumPoints() < 3 ) continue;
		const int contents = MaterialContents( side->GetMaterial() );
		for ( int pointIndex = 1; pointIndex + 1 < winding.GetNumPoints(); ++pointIndex ) {
			AddTriangle( geometry,
				winding[ 0 ].ToVec3() * axis + origin,
				winding[ pointIndex ].ToVec3() * axis + origin,
				winding[ pointIndex + 1 ].ToVec3() * axis + origin, contents );
		}
	}
}

static void AddPatch( navSourceGeometry_t& geometry, const idMapPatch& source, const idVec3& origin, const idMat3& axis ) {
	const int contents = MaterialContents( source.GetMaterial() );
	if ( !ContentsContributeGeometry( contents ) ) return;
	idSurface_Patch patch = source;
	if ( source.GetExplicitlySubdivided() ) patch.SubdivideExplicit( Max( source.GetHorzSubdivisions(), 1 ), Max( source.GetVertSubdivisions(), 1 ), false );
	else patch.Subdivide( DEFAULT_CURVE_MAX_ERROR, DEFAULT_CURVE_MAX_ERROR, DEFAULT_CURVE_MAX_LENGTH, false );
	for ( int index = 0; index + 2 < patch.GetNumIndexes(); index += 3 ) {
		AddTriangle( geometry,
			patch.GetIndexedVertex( index ).xyz * axis + origin,
			patch.GetIndexedVertex( index + 1 ).xyz * axis + origin,
			patch.GetIndexedVertex( index + 2 ).xyz * axis + origin, contents );
	}
}

static bool IsStaticModelEntity( const idMapEntity& entity ) {
	const char* className = entity.epairs.GetString( "classname" );
	return idStr::Icmp( className, "model_static" ) == 0 || idStr::Icmp( className, "func_static" ) == 0;
}

static void AddStaticModel( navSourceGeometry_t& geometry, const idMapEntity& entity ) {
	const char* modelName = entity.epairs.GetString( "model" );
	if ( modelName[ 0 ] == '\0' ) return;
	// Generated .entities files use the entity name as the model key for
	// inline brush/patch models.  Those are collision-model names, not render
	// model paths.  Source-world primitives have already been collected above.
	if ( entity.GetNumPrimitives() > 0 && idStr::Icmp( modelName, entity.epairs.GetString( "name" ) ) == 0 ) return;
	idRenderModel* model = renderModelManager->CheckModel( modelName );
	if ( model == NULL || model->IsDefaultModel() ) {
		common->Warning( "navBuild: could not load static model %s", modelName );
		return;
	}
	idVec3 origin;
	idMat3 axis;
	entity.epairs.GetVector( "origin", "0 0 0", origin );
	if ( !entity.epairs.GetMatrix( "rotation", NULL, axis ) ) axis.Identity();
	for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex ) {
		const modelSurface_t* surface = model->Surface( surfaceIndex );
		if ( surface == NULL || surface->geometry == NULL ) continue;
		const srfTriangles_t* mesh = surface->geometry;
		const int contents = surface->material != NULL ? surface->material->GetContentFlags() : 0;
		if ( !ContentsContributeGeometry( contents ) ) continue;
		for ( int index = 0; index + 2 < mesh->numIndexes; index += 3 ) {
			const idVec3 a = mesh->verts[ mesh->indexes[ index ] ].xyz * axis + origin;
			const idVec3 b = mesh->verts[ mesh->indexes[ index + 1 ] ].xyz * axis + origin;
			const idVec3 c = mesh->verts[ mesh->indexes[ index + 2 ] ].xyz * axis + origin;
			AddTriangle( geometry, a, b, c, contents );
		}
	}
}

static bool EntityHasStaticCollision( const idMapEntity& entity ) {
	if ( !IsStaticModelEntity( entity ) ) return false;
	if ( entity.epairs.GetBool( "noclipmodel" ) ) return false;
	if ( !entity.epairs.GetBool( "solid", "1" ) ) return false;
	return true;
}

static int AddCollisionModel( navSourceGeometry_t& geometry, idCollisionModel* model, const idVec3& origin, const idMat3& axis ) {
	if ( model == NULL ) return 0;
	int addedTriangles = 0;
	for ( int polygonIndex = 0; polygonIndex < model->GetNumPolygons(); ++polygonIndex ) {
		const idMaterial* material = model->GetPolygonMaterial( polygonIndex );
		const int contents = material != NULL ? material->GetContentFlags() : 0;
		if ( !ContentsContributeGeometry( contents ) ) continue;
		idFixedWinding winding;
		model->GetPolygon( polygonIndex, winding );
		if ( winding.GetNumPoints() < 3 ) continue;
		const idVec3 first = winding[ 0 ].ToVec3() * axis + origin;
		for ( int pointIndex = 1; pointIndex + 1 < winding.GetNumPoints(); ++pointIndex ) {
			const idVec3 second = winding[ pointIndex ].ToVec3() * axis + origin;
			const idVec3 third = winding[ pointIndex + 1 ].ToVec3() * axis + origin;
			AddTriangle( geometry, first, second, third, contents );
			++addedTriangles;
		}
	}
	return addedTriangles;
}

static bool CollectCompiledCollisionGeometry( const char* mapName, const idMapFile& mapFile, navSourceGeometry_t& geometry ) {
	collisionModelManager->LoadMap( mapName, false );
	int worldModels = 0;
	int worldTriangles = 0;
	idCollisionModel* model = collisionModelManager->LoadModel( mapName, WORLD_MODEL_NAME );
	if ( model != NULL ) {
		worldTriangles += AddCollisionModel( geometry, model, vec3_origin, mat3_identity );
		collisionModelManager->FreeModel( model );
		++worldModels;
	}
	// ETQW retail maps commonly split the world into worldMap0, worldMap1,
	// ... without providing an unnumbered worldMap model.
	for ( int modelIndex = 0; ; ++modelIndex ) {
		model = collisionModelManager->LoadModel( mapName, va( "%s%d", WORLD_MODEL_NAME, modelIndex ) );
		if ( model == NULL ) break;
		worldTriangles += AddCollisionModel( geometry, model, vec3_origin, mat3_identity );
		collisionModelManager->FreeModel( model );
		++worldModels;
	}

	int entityModels = 0;
	int entityTriangles = 0;
	for ( int entityIndex = 0; entityIndex < mapFile.GetNumEntities(); ++entityIndex ) {
		const idMapEntity* entity = mapFile.GetEntity( entityIndex );
		if ( !EntityHasStaticCollision( *entity ) ) continue;
		const char* modelName = entity->epairs.GetString( "cm_model" );
		if ( modelName[ 0 ] == '\0' ) modelName = entity->epairs.GetString( "model" );
		if ( modelName[ 0 ] == '\0' ) continue;
		idCollisionModel* model = collisionModelManager->LoadModel( mapName, modelName );
		if ( model == NULL ) continue;
		idVec3 origin;
		idMat3 axis;
		entity->epairs.GetVector( "origin", "0 0 0", origin );
		if ( !entity->epairs.GetMatrix( "rotation", NULL, axis ) ) axis.Identity();
		const int before = entityTriangles;
		entityTriangles += AddCollisionModel( geometry, model, origin, axis );
		if ( entityTriangles != before ) ++entityModels;
		collisionModelManager->FreeModel( model );
	}
	common->Printf( "navBuild: collision geometry: %d world models/%d triangles, %d static models/%d triangles\n",
		worldModels, worldTriangles, entityModels, entityTriangles );
	return worldTriangles + entityTriangles > 0;
}

static void CollectLinks( const idMapFile& mapFile, navSourceGeometry_t& geometry ) {
	for ( int entityIndex = 0; entityIndex < mapFile.GetNumEntities(); ++entityIndex ) {
		const idMapEntity* entity = mapFile.GetEntity( entityIndex );
		if ( idStr::Icmp( entity->epairs.GetString( "classname" ), "bot_reachability" ) != 0 ) continue;
		const char* targetName = entity->epairs.GetString( "target" );
		const idMapEntity* target = mapFile.FindEntity( targetName );
		if ( target == NULL ) {
			common->Warning( "navBuild: reachability %s has no target %s", entity->epairs.GetString( "name" ), targetName );
			continue;
		}
		navSourceLink_t link;
		link.name = entity->epairs.GetString( "name" );
		entity->epairs.GetVector( "origin", "0 0 0", link.start );
		target->epairs.GetVector( "origin", "0 0 0", link.end );
		link.travelFlags = TravelFlagsForType( entity->epairs.GetString( "traveltype", "walk" ) );
		link.userId = HashLinkName( link.name );
		geometry.links.push_back( link );
	}
}

static bool CollectGeometry( const char* mapName, const idMapFile& mapFile, bool compiledMap, navSourceGeometry_t& geometry ) {
	if ( compiledMap ) {
		CollectCompiledCollisionGeometry( mapName, mapFile, geometry );
		CollectLinks( mapFile, geometry );
		if ( geometry.triangles.empty() || geometry.bounds.IsCleared() ) return false;
		common->Printf( "navBuild: collected %d triangles and %d authored links\n", static_cast< int >( geometry.triangles.size() ), static_cast< int >( geometry.links.size() ) );
		return true;
	}
	for ( int entityIndex = 0; entityIndex < mapFile.GetNumEntities(); ++entityIndex ) {
		const idMapEntity* entity = mapFile.GetEntity( entityIndex );
		idVec3 origin;
		idMat3 axis;
		entity->epairs.GetVector( "origin", "0 0 0", origin );
		if ( !entity->epairs.GetMatrix( "rotation", NULL, axis ) ) axis.Identity();
		for ( int primitiveIndex = 0; primitiveIndex < entity->GetNumPrimitives(); ++primitiveIndex ) {
			const idMapPrimitive* primitive = entity->GetPrimitive( primitiveIndex );
			if ( primitive->GetType() == idMapPrimitive::TYPE_BRUSH ) AddBrush( geometry, *static_cast< const idMapBrush* >( primitive ), origin, axis );
			else if ( primitive->GetType() == idMapPrimitive::TYPE_PATCH ) AddPatch( geometry, *static_cast< const idMapPatch* >( primitive ), origin, axis );
		}
		if ( IsStaticModelEntity( *entity ) ) AddStaticModel( geometry, *entity );
	}
	CollectLinks( mapFile, geometry );
	if ( geometry.triangles.empty() || geometry.bounds.IsCleared() ) return false;
	common->Printf( "navBuild: collected %d triangles and %d authored links\n", static_cast< int >( geometry.triangles.size() ), static_cast< int >( geometry.links.size() ) );
	return true;
}

static bool ParseNavigationMap( const char* mapName, idMapFile& mapFile, bool& compiledMap ) {
	idStr worldPath = mapName;
	worldPath.StripFileExtension();
	worldPath.SetFileExtension( ".world" );
	compiledMap = !fileSystem->FileExists( worldPath );
	// A source build must bypass a stale generated .entities sidecar.  A retail
	// map without .world source deliberately falls back to .entities plus .cmb.
	return mapFile.Parse( mapName, true, false, true, !compiledMap );
}

static navProfileSettings_t PlayerProfile() {
	navProfileSettings_t profile;
	memset( &profile, 0, sizeof( profile ) );
	idStr::Copynz( profile.name, "nav_player", sizeof( profile.name ) );
	profile.boundingBox = idBounds( idVec3( -16, -16, 0 ), idVec3( 16, 16, 79 ) );
	profile.maxStepHeight = 16.0f;
	profile.maxBarrierHeight = 64.0f;
	profile.maxFallHeight = 900.0f;
	profile.minFloorCos = 0.7f;
	profile.groundSpeed = 256.0f;
	profile.obstacleQueryRadius = 1024.0f;
	profile.cellSize = 8.0f;
	profile.cellHeight = 4.0f;
	profile.tileSize = 128;
	return profile;
}

static navProfileSettings_t VehicleProfile() {
	navProfileSettings_t profile;
	memset( &profile, 0, sizeof( profile ) );
	idStr::Copynz( profile.name, "nav_vehicle", sizeof( profile.name ) );
	profile.boundingBox = idBounds( idVec3( -82, -82, 0 ), idVec3( 82, 82, 128 ) );
	profile.maxStepHeight = 16.0f;
	profile.maxBarrierHeight = 0.0f;
	profile.maxFallHeight = 32.0f;
	profile.minFloorCos = 0.8f;
	profile.groundSpeed = 512.0f;
	profile.obstacleQueryRadius = 1024.0f;
	profile.cellSize = 16.0f;
	profile.cellHeight = 4.0f;
	profile.tileSize = 128;
	return profile;
}

static int NextPowerOfTwo( int value ) {
	--value;
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	return value + 1;
}

static int IntegerLog2( int value ) {
	int result = 0;
	while ( value > 1 ) { value >>= 1; ++result; }
	return result;
}

static bool LinkStartsInTile( const navSourceLink_t& link, const float bmin[ 3 ], const float bmax[ 3 ] ) {
	float start[ 3 ];
	IdToRecast( link.start, start );
	return start[ 0 ] >= bmin[ 0 ] && start[ 0 ] < bmax[ 0 ] && start[ 2 ] >= bmin[ 2 ] && start[ 2 ] < bmax[ 2 ];
}

static bool BuildTile( const navSourceGeometry_t& geometry, const navProfileSettings_t& profile, const rcConfig& baseConfig, int tileX, int tileY, navTileData_t& output ) {
	navBuildContext_t context;
	rcConfig config = baseConfig;
	const float tileWorldSize = config.tileSize * config.cs;
	config.bmin[ 0 ] = baseConfig.bmin[ 0 ] + tileX * tileWorldSize;
	config.bmin[ 2 ] = baseConfig.bmin[ 2 ] + tileY * tileWorldSize;
	config.bmax[ 0 ] = baseConfig.bmin[ 0 ] + ( tileX + 1 ) * tileWorldSize;
	config.bmax[ 2 ] = baseConfig.bmin[ 2 ] + ( tileY + 1 ) * tileWorldSize;
	const float coreMin[ 3 ] = { config.bmin[ 0 ], config.bmin[ 1 ], config.bmin[ 2 ] };
	const float coreMax[ 3 ] = { config.bmax[ 0 ], config.bmax[ 1 ], config.bmax[ 2 ] };
	config.bmin[ 0 ] -= config.borderSize * config.cs;
	config.bmin[ 2 ] -= config.borderSize * config.cs;
	config.bmax[ 0 ] += config.borderSize * config.cs;
	config.bmax[ 2 ] += config.borderSize * config.cs;
	config.width = config.tileSize + config.borderSize * 2;
	config.height = config.tileSize + config.borderSize * 2;

	rcHeightfield* heightfield = NULL;
	rcCompactHeightfield* compact = NULL;
	rcContourSet* contours = NULL;
	rcPolyMesh* polyMesh = NULL;
	rcPolyMeshDetail* detailMesh = NULL;
	bool success = false;

	do {
		heightfield = rcAllocHeightfield();
		if ( heightfield == NULL || !rcCreateHeightfield( &context, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch ) ) break;

		const int triangleCount = static_cast< int >( geometry.triangles.size() );
		std::vector< int > indices( triangleCount * 3 );
		std::vector< unsigned char > areas( triangleCount );
		for ( int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex ) {
			const int first = geometry.triangles[ triangleIndex ].firstVertex;
			indices[ triangleIndex * 3 ] = first;
			indices[ triangleIndex * 3 + 1 ] = first + 1;
			indices[ triangleIndex * 3 + 2 ] = first + 2;
		}
		rcMarkWalkableTriangles( &context, config.walkableSlopeAngle, &geometry.vertices[ 0 ], static_cast< int >( geometry.vertices.size() / 3 ), &indices[ 0 ], triangleCount, &areas[ 0 ] );
		const int solidMask = idStr::Icmp( profile.name, "nav_vehicle" ) == 0 ? CONTENTS_AAS_SOLID_VEHICLE : CONTENTS_AAS_SOLID_PLAYER;
		for ( int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex ) {
			if ( ( geometry.triangles[ triangleIndex ].contents & solidMask ) != 0 ) areas[ triangleIndex ] = RC_NULL_AREA;
		}
		if ( !rcRasterizeTriangles( &context, &geometry.vertices[ 0 ], static_cast< int >( geometry.vertices.size() / 3 ), &indices[ 0 ], &areas[ 0 ], triangleCount, *heightfield, config.walkableClimb ) ) break;

		rcFilterLowHangingWalkableObstacles( &context, config.walkableClimb, *heightfield );
		rcFilterLedgeSpans( &context, config.walkableHeight, config.walkableClimb, *heightfield );
		rcFilterWalkableLowHeightSpans( &context, config.walkableHeight, *heightfield );
		compact = rcAllocCompactHeightfield();
		if ( compact == NULL || !rcBuildCompactHeightfield( &context, config.walkableHeight, config.walkableClimb, *heightfield, *compact ) ) break;
		rcFreeHeightField( heightfield ); heightfield = NULL;
		if ( !rcErodeWalkableArea( &context, config.walkableRadius, *compact ) ) break;
		if ( !rcBuildRegionsMonotone( &context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea ) ) break;
		contours = rcAllocContourSet();
		if ( contours == NULL || !rcBuildContours( &context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours ) ) break;
		if ( contours->nconts == 0 ) { success = true; break; }
		polyMesh = rcAllocPolyMesh();
		if ( polyMesh == NULL || !rcBuildPolyMesh( &context, *contours, config.maxVertsPerPoly, *polyMesh ) ) break;
		detailMesh = rcAllocPolyMeshDetail();
		if ( detailMesh == NULL || !rcBuildPolyMeshDetail( &context, *polyMesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detailMesh ) ) break;
		for ( int polyIndex = 0; polyIndex < polyMesh->npolys; ++polyIndex ) polyMesh->flags[ polyIndex ] = NAV_TFL_WALK;

		std::vector< float > linkVertices;
		std::vector< float > linkRadii;
		std::vector< unsigned short > linkFlags;
		std::vector< unsigned char > linkAreas;
		std::vector< unsigned char > linkDirections;
		std::vector< unsigned int > linkUserIds;
		for ( size_t linkIndex = 0; linkIndex < geometry.links.size(); ++linkIndex ) {
			const navSourceLink_t& link = geometry.links[ linkIndex ];
			if ( !LinkStartsInTile( link, coreMin, coreMax ) ) continue;
			float start[ 3 ], end[ 3 ];
			IdToRecast( link.start, start ); IdToRecast( link.end, end );
			for ( int axis = 0; axis < 3; ++axis ) linkVertices.push_back( start[ axis ] );
			for ( int axis = 0; axis < 3; ++axis ) linkVertices.push_back( end[ axis ] );
			linkRadii.push_back( Max( profile.boundingBox[ 1 ].x, profile.cellSize * 2.0f ) );
			linkFlags.push_back( static_cast< unsigned short >( link.travelFlags ) );
			linkAreas.push_back( RC_WALKABLE_AREA );
			linkDirections.push_back( 0 );
			linkUserIds.push_back( link.userId );
		}

		dtNavMeshCreateParams params;
		memset( &params, 0, sizeof( params ) );
		params.verts = polyMesh->verts; params.vertCount = polyMesh->nverts;
		params.polys = polyMesh->polys; params.polyAreas = polyMesh->areas; params.polyFlags = polyMesh->flags;
		params.polyCount = polyMesh->npolys; params.nvp = polyMesh->nvp;
		params.detailMeshes = detailMesh->meshes; params.detailVerts = detailMesh->verts; params.detailVertsCount = detailMesh->nverts;
		params.detailTris = detailMesh->tris; params.detailTriCount = detailMesh->ntris;
		if ( !linkUserIds.empty() ) {
			params.offMeshConVerts = &linkVertices[ 0 ]; params.offMeshConRad = &linkRadii[ 0 ]; params.offMeshConFlags = &linkFlags[ 0 ];
			params.offMeshConAreas = &linkAreas[ 0 ]; params.offMeshConDir = &linkDirections[ 0 ]; params.offMeshConUserID = &linkUserIds[ 0 ];
			params.offMeshConCount = static_cast< int >( linkUserIds.size() );
		}
		params.walkableHeight = profile.boundingBox[ 1 ].z - profile.boundingBox[ 0 ].z;
		params.walkableRadius = Max( profile.boundingBox[ 1 ].x, profile.boundingBox[ 1 ].y );
		params.walkableClimb = profile.maxStepHeight;
		params.tileX = tileX; params.tileY = tileY; params.tileLayer = 0;
		rcVcopy( params.bmin, polyMesh->bmin ); rcVcopy( params.bmax, polyMesh->bmax );
		params.cs = config.cs; params.ch = config.ch; params.buildBvTree = true;
		if ( !dtCreateNavMeshData( &params, &output.data, &output.size ) ) break;
		success = true;
	} while ( false );

	if ( detailMesh != NULL ) rcFreePolyMeshDetail( detailMesh );
	if ( polyMesh != NULL ) rcFreePolyMesh( polyMesh );
	if ( contours != NULL ) rcFreeContourSet( contours );
	if ( compact != NULL ) rcFreeCompactHeightfield( compact );
	if ( heightfield != NULL ) rcFreeHeightField( heightfield );
	return success;
}

static bool BuildProfile( const navSourceGeometry_t& geometry, const navProfileSettings_t& profile, navCompiledProfile_t& output ) {
	rcConfig config;
	memset( &config, 0, sizeof( config ) );
	config.cs = profile.cellSize; config.ch = profile.cellHeight; config.tileSize = profile.tileSize;
	config.walkableSlopeAngle = idMath::ACos( profile.minFloorCos ) * idMath::M_RAD2DEG;
	config.walkableHeight = Max( 3, static_cast< int >( idMath::Ceil( ( profile.boundingBox[ 1 ].z - profile.boundingBox[ 0 ].z ) / config.ch ) ) );
	config.walkableClimb = idMath::Floor( profile.maxStepHeight / config.ch );
	config.walkableRadius = idMath::Ceil( Max( profile.boundingBox[ 1 ].x, profile.boundingBox[ 1 ].y ) / config.cs );
	config.borderSize = config.walkableRadius + 3;
	config.maxEdgeLen = 12;
	config.maxSimplificationError = 1.3f;
	config.minRegionArea = 64;
	config.mergeRegionArea = 400;
	config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
	config.detailSampleDist = config.cs * 6.0f;
	config.detailSampleMaxError = config.ch;
	IdToRecast( geometry.bounds[ 0 ], config.bmin );
	IdToRecast( geometry.bounds[ 1 ], config.bmax );
	config.bmin[ 1 ] -= profile.maxFallHeight + profile.cellHeight * 2.0f;
	config.bmax[ 1 ] += profile.boundingBox[ 1 ].z + profile.cellHeight * 2.0f;

	int gridWidth = 0, gridHeight = 0;
	rcCalcGridSize( config.bmin, config.bmax, config.cs, &gridWidth, &gridHeight );
	const int tileWidth = ( gridWidth + config.tileSize - 1 ) / config.tileSize;
	const int tileHeight = ( gridHeight + config.tileSize - 1 ) / config.tileSize;
	const int tileBits = Min( IntegerLog2( NextPowerOfTwo( tileWidth * tileHeight ) ), 14 );
	const int polyBits = 22 - tileBits;
	if ( tileWidth * tileHeight > ( 1 << tileBits ) || polyBits < 8 ) {
		common->Warning( "navBuild: profile %s requires too many tiles", profile.name );
		return false;
	}

	output.header.settings = profile;
	rcVcopy( output.header.orig, config.bmin );
	output.header.tileWidth = config.tileSize * config.cs;
	output.header.tileHeight = config.tileSize * config.cs;
	output.header.maxTiles = 1 << tileBits;
	output.header.maxPolys = 1 << polyBits;

	common->Printf( "navBuild: %s grid %dx%d, tiles %dx%d\n", profile.name, gridWidth, gridHeight, tileWidth, tileHeight );
	for ( int tileY = 0; tileY < tileHeight; ++tileY ) {
		for ( int tileX = 0; tileX < tileWidth; ++tileX ) {
			navTileData_t tile;
			if ( !BuildTile( geometry, profile, config, tileX, tileY, tile ) ) {
				common->Warning( "navBuild: failed profile %s tile %d,%d", profile.name, tileX, tileY );
				return false;
			}
			if ( tile.data != NULL && tile.size > 0 ) {
				output.tiles.push_back( tile );
				tile.data = NULL;
			}
		}
		common->Printf( "navBuild: %s row %d/%d (%d populated tiles)\n", profile.name, tileY + 1, tileHeight, static_cast< int >( output.tiles.size() ) );
	}
	output.header.tileCount = static_cast< int >( output.tiles.size() );
	for ( size_t linkIndex = 0; linkIndex < geometry.links.size(); ++linkIndex ) {
		navFileLink_t record;
		memset( &record, 0, sizeof( record ) );
		idStr::Copynz( record.name, geometry.links[ linkIndex ].name, sizeof( record.name ) );
		record.userId = geometry.links[ linkIndex ].userId;
		output.links.push_back( record );
	}
	output.header.linkCount = static_cast< int >( output.links.size() );
	return !output.tiles.empty();
}

static bool WriteNavFile( const char* mapName, unsigned int geometryCRC, const navCompiledProfile_t* profiles[], int profileCount ) {
	idStr outputPath = mapName;
	outputPath.StripFileExtension();
	outputPath.SetFileExtension( NAV_FILE_EXTENSION );
	idFile* file = fileSystem->OpenFileWrite( outputPath, "fs_devpath" );
	if ( file == NULL ) return false;
	navFileHeader_t header;
	header.magic = NAV_FILE_MAGIC; header.version = NAV_FILE_VERSION; header.geometryCRC = geometryCRC; header.profileCount = profileCount;
	file->Write( &header, sizeof( header ) );
	for ( int profileIndex = 0; profileIndex < profileCount; ++profileIndex ) {
		const navCompiledProfile_t& profile = *profiles[ profileIndex ];
		file->Write( &profile.header, sizeof( profile.header ) );
		for ( size_t tileIndex = 0; tileIndex < profile.tiles.size(); ++tileIndex ) {
			navFileTileHeader_t tileHeader;
			tileHeader.dataSize = profile.tiles[ tileIndex ].size;
			file->Write( &tileHeader, sizeof( tileHeader ) );
			file->Write( profile.tiles[ tileIndex ].data, profile.tiles[ tileIndex ].size );
		}
		for ( size_t linkIndex = 0; linkIndex < profile.links.size(); ++linkIndex ) file->Write( &profile.links[ linkIndex ], sizeof( navFileLink_t ) );
	}
	fileSystem->CloseFile( file );
	common->Printf( "navBuild: wrote %s\n", outputPath.c_str() );
	return true;
}

} // namespace

void NavBuild_f( const idCmdArgs& args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: navBuild <maps/mapname>\n" );
		return;
	}
	idStr mapName = args.Argv( 1 );
	mapName.StripFileExtension();
	navBuildRecastWarningCount = 0;
	idMapFile mapFile;
	bool compiledMap = false;
	if ( !ParseNavigationMap( mapName, mapFile, compiledMap ) ) {
		common->Warning( "navBuild: could not parse source or compiled map %s", mapName.c_str() );
		return;
	}
	navSourceGeometry_t geometry;
	if ( !CollectGeometry( mapName, mapFile, compiledMap, geometry ) ) {
		common->Warning( "navBuild: %s contains no navigation geometry", mapName.c_str() );
		return;
	}
	navCompiledProfile_t player, vehicle;
	if ( !BuildProfile( geometry, PlayerProfile(), player ) || !BuildProfile( geometry, VehicleProfile(), vehicle ) ) return;
	const navCompiledProfile_t* profiles[] = { &player, &vehicle };
	if ( !WriteNavFile( mapName, mapFile.GetGeometryCRC(), profiles, 2 ) ) common->Warning( "navBuild: could not write output" );
	if ( navBuildRecastWarningCount > NAV_BUILD_RECAST_WARNING_LIMIT ) {
		common->Printf( "navBuild: suppressed %d additional non-fatal Recast warnings\n", navBuildRecastWarningCount - NAV_BUILD_RECAST_WARNING_LIMIT );
	}
}

void NavVerify_f( const idCmdArgs& args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: navVerify <maps/mapname>\n" );
		return;
	}
	idStr error;
	if ( navigationSystem->ValidateFile( args.Argv( 1 ), 0, error ) ) common->Printf( "navVerify: %s is valid\n", args.Argv( 1 ) );
	else common->Warning( "navVerify: %s", error.c_str() );
}

void NavTest_f( const idCmdArgs& args ) {
	if ( args.Argc() != 2 ) {
		common->Printf( "usage: navTest <maps/mapname>\n" );
		return;
	}
	idStr mapName = args.Argv( 1 );
	mapName.StripFileExtension();
	idMapFile mapFile;
	bool compiledMap = false;
	if ( !ParseNavigationMap( mapName, mapFile, compiledMap ) ) {
		common->Warning( "navTest: could not parse source or compiled map %s", mapName.c_str() );
		return;
	}
	const char* profiles[] = { "nav_player", "nav_vehicle" };
	for ( int profileIndex = 0; profileIndex < 2; ++profileIndex ) {
		idNavigationQuery* query = navigationSystem->LoadQuery( mapName, mapFile.GetGeometryCRC(), profiles[ profileIndex ] );
		if ( query == NULL || !query->IsValid() || query->GetPolyCount() == 0 ) {
			common->Warning( "navTest: profile %s did not load", profiles[ profileIndex ] );
			if ( query != NULL ) navigationSystem->FreeQuery( query );
			return;
		}
		const navPolyRef_t firstPoly = query->GetPolyByIndex( 0 );
		idVec3 center, nearest;
		navTraceResult_t trace;
		if ( firstPoly == 0 || !query->GetPolyCenter( firstPoly, center ) || query->FindNearestPoly( center, query->GetSettings().boundingBox, 0, &nearest ) == 0 || !query->Raycast( firstPoly, center, center, 0, trace ) ) {
			common->Warning( "navTest: profile %s failed its first query", profiles[ profileIndex ] );
			navigationSystem->FreeQuery( query );
			return;
		}
		idBounds mutationBounds( center );
		mutationBounds.ExpandSelf( Max( query->GetSettings().cellSize * 2.0f, 16.0f ) );
		if ( !query->ChangeTravelFlags( mutationBounds, NAV_TFL_INVALID_GDF, true ) || !query->ChangeTravelFlags( mutationBounds, NAV_TFL_INVALID_GDF, false ) ) {
			common->Warning( "navTest: profile %s failed its runtime mutation test", profiles[ profileIndex ] );
			navigationSystem->FreeQuery( query );
			return;
		}
		query->Stats();
		navigationSystem->FreeQuery( query );
	}
	common->Printf( "navTest: loaded and queried both profiles for %s (CRC %08x)\n", mapName.c_str(), mapFile.GetGeometryCRC() );
}
