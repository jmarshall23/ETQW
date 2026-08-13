// Copyright (C) 2026 - QuakeWars2 contributors.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "NavigationFile.h"
#include "../recast/Detour/Include/DetourAlloc.h"
#include "../recast/Detour/Include/DetourCommon.h"
#include "../recast/Detour/Include/DetourNavMesh.h"
#include "../recast/Detour/Include/DetourNavMeshQuery.h"

#include <vector>

namespace {

static void NavToDetour( const idVec3& in, float out[ 3 ] ) {
	out[ 0 ] = in.x;
	out[ 1 ] = in.z;
	out[ 2 ] = in.y;
}

static idVec3 DetourToNav( const float in[ 3 ] ) {
	return idVec3( in[ 0 ], in[ 2 ], in[ 1 ] );
}

static idStr NavFileName( const char* mapName ) {
	idStr path = mapName;
	path.StripFileExtension();
	path.SetFileExtension( NAV_FILE_EXTENSION );
	return path;
}

static bool ReadExact( idFile* file, void* data, int size ) {
	return file != NULL && size >= 0 && file->Read( data, size ) == size;
}

static dtQueryFilter MakeFilter( int excludeTravelFlags ) {
	dtQueryFilter filter;
	filter.setIncludeFlags( 0xffff );
	filter.setExcludeFlags( static_cast< unsigned short >( excludeTravelFlags ) );
	return filter;
}

struct namedLinkRef_t {
	idStr		name;
	dtPolyRef	ref;
};

class idNavigationQueryLocal : public idNavigationQuery {
public:
	idNavigationQueryLocal();
	virtual ~idNavigationQueryLocal();

	bool Load( const char* mapName, unsigned int expectedCRC, const char* profileName, idStr& error );
	virtual bool IsValid() const { return navMesh != NULL && navQuery != NULL; }
	virtual const char* GetMapName() const { return loadedMap.c_str(); }
	virtual unsigned int GetGeometryCRC() const { return geometryCRC; }
	virtual const navProfileSettings_t& GetSettings() const { return settings; }
	virtual int GetPolyCount() const { return static_cast< int >( polygons.size() ); }
	virtual navPolyRef_t GetPolyByIndex( int index ) const;
	virtual navPolyRef_t FindNearestPoly( const idVec3& point, const idBounds& searchBounds, int excludeTravelFlags, idVec3* nearestPoint ) const;
	virtual bool GetPolyCenter( navPolyRef_t poly, idVec3& center ) const;
	virtual bool ClampPointToPoly( navPolyRef_t poly, const idVec3& point, idVec3& clamped ) const;
	virtual bool Raycast( navPolyRef_t startPoly, const idVec3& start, const idVec3& end, int excludeTravelFlags, navTraceResult_t& result ) const;
	virtual bool FindPath( navPolyRef_t startPoly, const idVec3& start, navPolyRef_t goalPoly, const idVec3& goal, int excludeTravelFlags, navPathResult_t& result ) const;
	virtual int GetBoundarySegments( navPolyRef_t aroundPoly, const idBounds& bounds, int excludeTravelFlags, navBoundarySegment_t* segments, int maxSegments ) const;
	virtual bool ChangePolyTravelFlags( navPolyRef_t poly, int travelFlags, bool set );
	virtual bool ChangeTravelFlags( const idBounds& bounds, int travelFlags, bool set );
	virtual bool ChangeNamedLinkTravelFlags( const char* name, int travelFlags, bool set );
	virtual void Stats() const;

private:
	void BuildPolyIndex();
	void ResolveNamedLinks( const std::vector< navFileLink_t >& links );

	idStr					loadedMap;
	unsigned int			geometryCRC;
	navProfileSettings_t	settings;
	dtNavMesh*				navMesh;
	dtNavMeshQuery*			navQuery;
	std::vector< dtPolyRef > polygons;
	std::vector< namedLinkRef_t > namedLinks;
};

idNavigationQueryLocal::idNavigationQueryLocal() {
	geometryCRC = 0;
	memset( &settings, 0, sizeof( settings ) );
	navMesh = NULL;
	navQuery = NULL;
}

idNavigationQueryLocal::~idNavigationQueryLocal() {
	if ( navQuery != NULL ) {
		dtFreeNavMeshQuery( navQuery );
	}
	if ( navMesh != NULL ) {
		dtFreeNavMesh( navMesh );
	}
}

bool idNavigationQueryLocal::Load( const char* mapName, unsigned int expectedCRC, const char* profileName, idStr& error ) {
	const idStr path = NavFileName( mapName );
	idFile* file = fileSystem->OpenFileRead( path );
	if ( file == NULL ) {
		error = va( "could not open %s", path.c_str() );
		return false;
	}

	navFileHeader_t header;
	if ( !ReadExact( file, &header, sizeof( header ) ) || header.magic != NAV_FILE_MAGIC || header.version != NAV_FILE_VERSION ) {
		error = va( "%s is not a supported QuakeWars2 navmesh", path.c_str() );
		fileSystem->CloseFile( file );
		return false;
	}
	if ( expectedCRC != 0 && header.geometryCRC != expectedCRC ) {
		error = va( "%s geometry CRC is %08x, expected %08x", path.c_str(), header.geometryCRC, expectedCRC );
		fileSystem->CloseFile( file );
		return false;
	}

	bool foundProfile = false;
	std::vector< navFileLink_t > linkRecords;
	for ( unsigned int profileIndex = 0; profileIndex < header.profileCount; ++profileIndex ) {
		navFileProfileHeader_t profile;
		if ( !ReadExact( file, &profile, sizeof( profile ) ) || profile.tileCount < 0 || profile.linkCount < 0 ) {
			error = va( "%s has a truncated profile table", path.c_str() );
			break;
		}
		const bool select = !foundProfile && idStr::Icmp( profile.settings.name, profileName ) == 0;
		if ( select ) {
			dtNavMeshParams params;
			memset( &params, 0, sizeof( params ) );
			dtVcopy( params.orig, profile.orig );
			params.tileWidth = profile.tileWidth;
			params.tileHeight = profile.tileHeight;
			params.maxTiles = profile.maxTiles;
			params.maxPolys = profile.maxPolys;
			navMesh = dtAllocNavMesh();
			if ( navMesh == NULL || dtStatusFailed( navMesh->init( &params ) ) ) {
				error = va( "could not allocate Detour mesh for profile %s", profileName );
				break;
			}
			settings = profile.settings;
			foundProfile = true;
		}

		for ( int tileIndex = 0; tileIndex < profile.tileCount; ++tileIndex ) {
			navFileTileHeader_t tileHeader;
			if ( !ReadExact( file, &tileHeader, sizeof( tileHeader ) ) || tileHeader.dataSize == 0 ) {
				error = va( "%s has a truncated tile table", path.c_str() );
				foundProfile = false;
				break;
			}
			if ( select ) {
				unsigned char* tileData = static_cast< unsigned char* >( dtAlloc( tileHeader.dataSize, DT_ALLOC_PERM ) );
				if ( tileData == NULL || !ReadExact( file, tileData, tileHeader.dataSize ) ) {
					if ( tileData != NULL ) dtFree( tileData );
					error = va( "%s has invalid tile data", path.c_str() );
					foundProfile = false;
					break;
				}
				if ( dtStatusFailed( navMesh->addTile( tileData, tileHeader.dataSize, DT_TILE_FREE_DATA, 0, NULL ) ) ) {
					dtFree( tileData );
					error = va( "%s contains a tile Detour rejected", path.c_str() );
					foundProfile = false;
					break;
				}
			} else {
				file->Seek( tileHeader.dataSize, FS_SEEK_CUR );
			}
		}
		if ( !error.IsEmpty() ) break;

		for ( int linkIndex = 0; linkIndex < profile.linkCount; ++linkIndex ) {
			navFileLink_t link;
			if ( !ReadExact( file, &link, sizeof( link ) ) ) {
				error = va( "%s has a truncated link table", path.c_str() );
				foundProfile = false;
				break;
			}
			if ( select ) linkRecords.push_back( link );
		}
		if ( !error.IsEmpty() ) break;
	}
	fileSystem->CloseFile( file );

	if ( !foundProfile || navMesh == NULL ) {
		if ( error.IsEmpty() ) error = va( "%s does not contain profile %s", path.c_str(), profileName );
		return false;
	}

	navQuery = dtAllocNavMeshQuery();
	if ( navQuery == NULL || dtStatusFailed( navQuery->init( navMesh, 8192 ) ) ) {
		error = va( "could not initialize Detour query for profile %s", profileName );
		return false;
	}
	loadedMap = mapName;
	loadedMap.StripFileExtension();
	geometryCRC = header.geometryCRC;
	BuildPolyIndex();
	ResolveNamedLinks( linkRecords );
	return true;
}

void idNavigationQueryLocal::BuildPolyIndex() {
	polygons.clear();
	const dtNavMesh* readOnlyMesh = navMesh;
	for ( int tileIndex = 0; tileIndex < readOnlyMesh->getMaxTiles(); ++tileIndex ) {
		const dtMeshTile* tile = readOnlyMesh->getTile( tileIndex );
		if ( tile == NULL || tile->header == NULL ) continue;
		const dtPolyRef base = navMesh->getPolyRefBase( tile );
		for ( int polyIndex = 0; polyIndex < tile->header->polyCount; ++polyIndex ) {
			polygons.push_back( base | static_cast< dtPolyRef >( polyIndex ) );
		}
	}
}

void idNavigationQueryLocal::ResolveNamedLinks( const std::vector< navFileLink_t >& links ) {
	namedLinks.clear();
	for ( size_t polyIndex = 0; polyIndex < polygons.size(); ++polyIndex ) {
		const dtOffMeshConnection* connection = navMesh->getOffMeshConnectionByRef( polygons[ polyIndex ] );
		if ( connection == NULL ) continue;
		for ( size_t linkIndex = 0; linkIndex < links.size(); ++linkIndex ) {
			if ( connection->userId != links[ linkIndex ].userId ) continue;
			namedLinkRef_t entry;
			entry.name = links[ linkIndex ].name;
			entry.ref = polygons[ polyIndex ];
			namedLinks.push_back( entry );
			break;
		}
	}
}

navPolyRef_t idNavigationQueryLocal::GetPolyByIndex( int index ) const {
	return index >= 0 && index < static_cast< int >( polygons.size() ) ? polygons[ index ] : 0;
}

navPolyRef_t idNavigationQueryLocal::FindNearestPoly( const idVec3& point, const idBounds& searchBounds, int excludeTravelFlags, idVec3* nearestPoint ) const {
	if ( navQuery == NULL ) return 0;
	float center[ 3 ], extents[ 3 ], nearest[ 3 ];
	NavToDetour( point, center );
	const idVec3 boundsExtent = ( searchBounds[ 1 ] - searchBounds[ 0 ] ) * 0.5f;
	extents[ 0 ] = Max( idMath::Fabs( boundsExtent.x ), settings.cellSize * 2.0f );
	extents[ 1 ] = Max( idMath::Fabs( boundsExtent.z ), settings.cellHeight * 4.0f );
	extents[ 2 ] = Max( idMath::Fabs( boundsExtent.y ), settings.cellSize * 2.0f );
	dtPolyRef ref = 0;
	dtQueryFilter filter = MakeFilter( excludeTravelFlags );
	if ( dtStatusFailed( navQuery->findNearestPoly( center, extents, &filter, &ref, nearest ) ) || ref == 0 ) return 0;
	if ( nearestPoint != NULL ) *nearestPoint = DetourToNav( nearest );
	return ref;
}

bool idNavigationQueryLocal::GetPolyCenter( navPolyRef_t ref, idVec3& center ) const {
	const dtMeshTile* tile = NULL;
	const dtPoly* poly = NULL;
	if ( ref == 0 || dtStatusFailed( navMesh->getTileAndPolyByRef( ref, &tile, &poly ) ) ) return false;
	float result[ 3 ] = { 0.0f, 0.0f, 0.0f };
	for ( unsigned int vertexIndex = 0; vertexIndex < poly->vertCount; ++vertexIndex ) {
		dtVadd( result, result, &tile->verts[ poly->verts[ vertexIndex ] * 3 ] );
	}
	if ( poly->vertCount == 0 ) return false;
	dtVscale( result, result, 1.0f / poly->vertCount );
	center = DetourToNav( result );
	return true;
}

bool idNavigationQueryLocal::ClampPointToPoly( navPolyRef_t poly, const idVec3& point, idVec3& clamped ) const {
	float input[ 3 ], result[ 3 ];
	NavToDetour( point, input );
	if ( poly == 0 || dtStatusFailed( navQuery->closestPointOnPoly( poly, input, result, NULL ) ) ) return false;
	clamped = DetourToNav( result );
	return true;
}

bool idNavigationQueryLocal::Raycast( navPolyRef_t startPoly, const idVec3& start, const idVec3& end, int excludeTravelFlags, navTraceResult_t& result ) const {
	result = navTraceResult_t();
	if ( startPoly == 0 ) return false;
	float startPoint[ 3 ], endPoint[ 3 ], normal[ 3 ];
	NavToDetour( start, startPoint );
	NavToDetour( end, endPoint );
	dtPolyRef visited[ 256 ];
	int visitedCount = 0;
	float fraction = 0.0f;
	dtQueryFilter filter = MakeFilter( excludeTravelFlags );
	if ( dtStatusFailed( navQuery->raycast( startPoly, startPoint, endPoint, &filter, &fraction, normal, visited, &visitedCount, 256 ) ) ) return false;
	if ( fraction > 1.0f ) fraction = 1.0f;
	result.fraction = fraction;
	result.endPos = start + ( end - start ) * fraction;
	result.lastPoly = visitedCount > 0 ? visited[ visitedCount - 1 ] : startPoly;
	return true;
}

bool idNavigationQueryLocal::FindPath( navPolyRef_t startPoly, const idVec3& start, navPolyRef_t goalPoly, const idVec3& goal, int excludeTravelFlags, navPathResult_t& result ) const {
	result = navPathResult_t();
	if ( startPoly == 0 || goalPoly == 0 ) return false;
	float startPoint[ 3 ], goalPoint[ 3 ];
	NavToDetour( start, startPoint );
	NavToDetour( goal, goalPoint );
	dtPolyRef corridor[ 512 ];
	int corridorCount = 0;
	dtQueryFilter filter = MakeFilter( excludeTravelFlags );
	if ( dtStatusFailed( navQuery->findPath( startPoly, goalPoly, startPoint, goalPoint, &filter, corridor, &corridorCount, 512 ) ) || corridorCount == 0 ) return false;

	float straightPoints[ NAV_MAX_PATH_POINTS * 3 ];
	unsigned char straightFlags[ NAV_MAX_PATH_POINTS ];
	dtPolyRef straightPolys[ NAV_MAX_PATH_POINTS ];
	int straightCount = 0;
	if ( dtStatusFailed( navQuery->findStraightPath( startPoint, goalPoint, corridor, corridorCount, straightPoints, straightFlags, straightPolys, &straightCount, NAV_MAX_PATH_POINTS ) ) || straightCount == 0 ) return false;

	float distance = 0.0f;
	for ( int index = 0; index < straightCount; ++index ) {
		result.points[ index ] = DetourToNav( &straightPoints[ index * 3 ] );
		result.polys[ index ] = straightPolys[ index ];
		result.travelFlags[ index ] = 0;
		if ( straightPolys[ index ] != 0 ) navMesh->getPolyFlags( straightPolys[ index ], &result.travelFlags[ index ] );
		if ( index > 0 ) distance += ( result.points[ index ] - result.points[ index - 1 ] ).Length();
	}
	result.numPoints = straightCount;
	result.reachedGoal = corridor[ corridorCount - 1 ] == goalPoly && ( straightFlags[ straightCount - 1 ] & DT_STRAIGHTPATH_END ) != 0;
	result.travelTime = idMath::Ftoi( distance * 100.0f / Max( settings.groundSpeed, 1.0f ) );
	return true;
}

int idNavigationQueryLocal::GetBoundarySegments( navPolyRef_t aroundPoly, const idBounds& bounds, int excludeTravelFlags, navBoundarySegment_t* segments, int maxSegments ) const {
	if ( navQuery == NULL || segments == NULL || maxSegments <= 0 ) return 0;
	dtQueryFilter filter = MakeFilter( excludeTravelFlags );
	dtPolyRef refs[ 1024 ];
	int refCount = 0;
	const idVec3 boundsCenter = bounds.GetCenter();
	const idVec3 boundsExtent = ( bounds[ 1 ] - bounds[ 0 ] ) * 0.5f;
	float center[ 3 ], extents[ 3 ];
	NavToDetour( boundsCenter, center );
	NavToDetour( boundsExtent, extents );
	extents[ 0 ] = idMath::Fabs( extents[ 0 ] );
	extents[ 1 ] = idMath::Fabs( extents[ 1 ] );
	extents[ 2 ] = idMath::Fabs( extents[ 2 ] );
	if ( dtStatusFailed( navQuery->queryPolygons( center, extents, &filter, refs, &refCount, 1024 ) ) || refCount == 0 ) {
		if ( aroundPoly == 0 ) return 0;
		refs[ 0 ] = aroundPoly;
		refCount = 1;
	}

	int resultCount = 0;
	for ( int refIndex = 0; refIndex < refCount && resultCount < maxSegments; ++refIndex ) {
		float wallVerts[ DT_VERTS_PER_POLYGON * 6 ];
		dtPolyRef wallRefs[ DT_VERTS_PER_POLYGON ];
		int wallCount = 0;
		if ( dtStatusFailed( navQuery->getPolyWallSegments( refs[ refIndex ], &filter, wallVerts, wallRefs, &wallCount, DT_VERTS_PER_POLYGON ) ) ) continue;
		for ( int wallIndex = 0; wallIndex < wallCount && resultCount < maxSegments; ++wallIndex ) {
			if ( wallRefs[ wallIndex ] != 0 ) continue;
			segments[ resultCount ].start = DetourToNav( &wallVerts[ wallIndex * 6 ] );
			segments[ resultCount ].end = DetourToNav( &wallVerts[ wallIndex * 6 + 3 ] );
			segments[ resultCount ].flags = 1;
			++resultCount;
		}
	}
	return resultCount;
}

bool idNavigationQueryLocal::ChangeTravelFlags( const idBounds& bounds, int travelFlags, bool set ) {
	if ( navQuery == NULL ) return false;
	dtQueryFilter all;
	all.setIncludeFlags( 0xffff );
	all.setExcludeFlags( 0 );
	dtPolyRef refs[ 2048 ];
	int refCount = 0;
	const idVec3 centerId = bounds.GetCenter();
	const idVec3 extentId = ( bounds[ 1 ] - bounds[ 0 ] ) * 0.5f;
	float center[ 3 ], extents[ 3 ];
	NavToDetour( centerId, center );
	NavToDetour( extentId, extents );
	for ( int axis = 0; axis < 3; ++axis ) extents[ axis ] = idMath::Fabs( extents[ axis ] );
	if ( dtStatusFailed( navQuery->queryPolygons( center, extents, &all, refs, &refCount, 2048 ) ) ) return false;
	for ( int index = 0; index < refCount; ++index ) {
		unsigned short flags = 0;
		if ( dtStatusFailed( navMesh->getPolyFlags( refs[ index ], &flags ) ) ) continue;
		flags = set ? static_cast< unsigned short >( flags | travelFlags ) : static_cast< unsigned short >( flags & ~travelFlags );
		navMesh->setPolyFlags( refs[ index ], flags );
	}
	return refCount > 0;
}

bool idNavigationQueryLocal::ChangePolyTravelFlags( navPolyRef_t poly, int travelFlags, bool set ) {
	if ( navMesh == NULL || poly == 0 ) return false;
	unsigned short flags = 0;
	if ( dtStatusFailed( navMesh->getPolyFlags( poly, &flags ) ) ) return false;
	flags = set ? static_cast< unsigned short >( flags | travelFlags ) : static_cast< unsigned short >( flags & ~travelFlags );
	return dtStatusSucceed( navMesh->setPolyFlags( poly, flags ) );
}

bool idNavigationQueryLocal::ChangeNamedLinkTravelFlags( const char* name, int travelFlags, bool set ) {
	bool changed = false;
	for ( size_t index = 0; index < namedLinks.size(); ++index ) {
		if ( idStr::Icmp( namedLinks[ index ].name, name ) != 0 ) continue;
		unsigned short flags = 0;
		if ( dtStatusFailed( navMesh->getPolyFlags( namedLinks[ index ].ref, &flags ) ) ) continue;
		flags = set ? static_cast< unsigned short >( flags | travelFlags ) : static_cast< unsigned short >( flags & ~travelFlags );
		navMesh->setPolyFlags( namedLinks[ index ].ref, flags );
		changed = true;
	}
	return changed;
}

void idNavigationQueryLocal::Stats() const {
	common->Printf( "[%s:%s] %d Detour polygons, %d named links, CRC %08x\n", loadedMap.c_str(), settings.name, static_cast< int >( polygons.size() ), static_cast< int >( namedLinks.size() ), geometryCRC );
}

class idNavigationSystemLocal : public idNavigationSystem {
public:
	virtual idNavigationQuery* LoadQuery( const char* mapName, unsigned int geometryCRC, const char* profileName ) {
		idNavigationQueryLocal* query = new idNavigationQueryLocal();
		idStr error;
		if ( !query->Load( mapName, geometryCRC, profileName, error ) ) {
			common->Warning( "navigation: %s", error.c_str() );
			delete query;
			return NULL;
		}
		return query;
	}

	virtual void FreeQuery( idNavigationQuery* query ) {
		delete query;
	}

	virtual bool ValidateFile( const char* mapName, unsigned int geometryCRC, idStr& error ) const {
		const idStr path = NavFileName( mapName );
		idFile* file = fileSystem->OpenFileRead( path );
		if ( file == NULL ) {
			error = va( "could not open %s", path.c_str() );
			return false;
		}
		navFileHeader_t header;
		const bool valid = ReadExact( file, &header, sizeof( header ) ) && header.magic == NAV_FILE_MAGIC && header.version == NAV_FILE_VERSION && header.profileCount > 0 && ( geometryCRC == 0 || header.geometryCRC == geometryCRC );
		fileSystem->CloseFile( file );
		if ( !valid ) error = va( "%s has an invalid header or geometry CRC", path.c_str() );
		return valid;
	}
};

idNavigationSystemLocal navigationSystemLocal;

} // namespace

idNavigationSystem* navigationSystem = &navigationSystemLocal;
