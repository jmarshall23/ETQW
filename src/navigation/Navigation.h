// Copyright (C) 2026 - QuakeWars2 contributors.
//
// Public engine/game navigation boundary.  Recast and Detour types must never
// cross this interface: the engine owns all navmesh memory and query objects.

#ifndef __NAVIGATION_H__
#define __NAVIGATION_H__

#define NAV_FILE_EXTENSION                 "nav"
#define NAV_MAX_PROFILE_NAME               32
#define NAV_MAX_PATH_POINTS                32

typedef unsigned int navPolyRef_t;

enum navTravelFlags_t {
	NAV_TFL_INVALID          = BIT( 0 ),
	NAV_TFL_INVALID_GDF      = BIT( 1 ),
	NAV_TFL_INVALID_STROGG   = BIT( 2 ),
	NAV_TFL_AIR              = BIT( 3 ),
	NAV_TFL_WATER            = BIT( 4 ),
	NAV_TFL_WALK             = BIT( 5 ),
	NAV_TFL_WALKOFFLEDGE     = BIT( 6 ),
	NAV_TFL_WALKOFFBARRIER   = BIT( 7 ),
	NAV_TFL_BARRIERJUMP      = BIT( 8 ),
	NAV_TFL_JUMP             = BIT( 9 ),
	NAV_TFL_LADDER           = BIT( 10 ),
	NAV_TFL_SWIM             = BIT( 11 ),
	NAV_TFL_WATERJUMP        = BIT( 12 ),
	NAV_TFL_TELEPORT         = BIT( 13 ),
	NAV_TFL_ELEVATOR         = BIT( 14 )
};

struct navProfileSettings_t {
	char		name[ NAV_MAX_PROFILE_NAME ];
	idBounds	boundingBox;
	float		maxStepHeight;
	float		maxBarrierHeight;
	float		maxFallHeight;
	float		minFloorCos;
	float		groundSpeed;
	float		obstacleQueryRadius;
	float		cellSize;
	float		cellHeight;
	int		tileSize;
};

struct navPathResult_t {
	navPathResult_t() : reachedGoal( false ), numPoints( 0 ), travelTime( 0 ) {}
	bool			reachedGoal;
	int			numPoints;
	idVec3		points[ NAV_MAX_PATH_POINTS ];
	navPolyRef_t	polys[ NAV_MAX_PATH_POINTS ];
	unsigned short	travelFlags[ NAV_MAX_PATH_POINTS ];
	int			travelTime;
};

struct navTraceResult_t {
	navTraceResult_t() : fraction( 0.0f ), endPos( vec3_origin ), lastPoly( 0 ) {}
	float			fraction;
	idVec3		endPos;
	navPolyRef_t	lastPoly;
};

struct navBoundarySegment_t {
	idVec3		start;
	idVec3		end;
	int			flags;
};

class idNavigationQuery {
public:
	virtual					~idNavigationQuery() {}
	virtual bool			IsValid() const = 0;
	virtual const char*		GetMapName() const = 0;
	virtual unsigned int		GetGeometryCRC() const = 0;
	virtual const navProfileSettings_t& GetSettings() const = 0;
	virtual int				GetPolyCount() const = 0;
	virtual navPolyRef_t		GetPolyByIndex( int index ) const = 0;
	virtual navPolyRef_t		FindNearestPoly( const idVec3& point, const idBounds& searchBounds, int excludeTravelFlags, idVec3* nearestPoint = NULL ) const = 0;
	virtual bool			GetPolyCenter( navPolyRef_t poly, idVec3& center ) const = 0;
	virtual bool			ClampPointToPoly( navPolyRef_t poly, const idVec3& point, idVec3& clamped ) const = 0;
	virtual bool			Raycast( navPolyRef_t startPoly, const idVec3& start, const idVec3& end, int excludeTravelFlags, navTraceResult_t& result ) const = 0;
	virtual bool			FindPath( navPolyRef_t startPoly, const idVec3& start, navPolyRef_t goalPoly, const idVec3& goal, int excludeTravelFlags, navPathResult_t& result ) const = 0;
	virtual int				GetBoundarySegments( navPolyRef_t aroundPoly, const idBounds& bounds, int excludeTravelFlags, navBoundarySegment_t* segments, int maxSegments ) const = 0;
	virtual bool			ChangePolyTravelFlags( navPolyRef_t poly, int travelFlags, bool set ) = 0;
	virtual bool			ChangeTravelFlags( const idBounds& bounds, int travelFlags, bool set ) = 0;
	virtual bool			ChangeNamedLinkTravelFlags( const char* name, int travelFlags, bool set ) = 0;
	virtual void			Stats() const = 0;
};

class idNavigationSystem {
public:
	virtual					~idNavigationSystem() {}
	virtual idNavigationQuery*	LoadQuery( const char* mapName, unsigned int geometryCRC, const char* profileName ) = 0;
	virtual void			FreeQuery( idNavigationQuery* query ) = 0;
	virtual bool			ValidateFile( const char* mapName, unsigned int geometryCRC, idStr& error ) const = 0;
};

extern idNavigationSystem* navigationSystem;

#endif // __NAVIGATION_H__
