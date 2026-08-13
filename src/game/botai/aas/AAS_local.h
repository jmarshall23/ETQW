// Copyright (C) 2007 Id Software, Inc.
// Copyright (C) 2026 - QuakeWars2 contributors.

#ifndef __AAS_LOCAL_H__
#define __AAS_LOCAL_H__

#include "AAS.h"

// Source-compatible bot navigation facade.  All spatial work is performed by
// the engine-side idNavigationQuery; none of the former AAS2 routing state is
// retained in the game DLL.
class idAASLocal : public idAAS {
public:
	idAASLocal();
	virtual ~idAASLocal();

	virtual bool Init( const char* mapName, unsigned int mapFileCRC );
	virtual void Stats() const;
	virtual void Test( const idVec3& origin );
	virtual const idAASSettings* GetSettings() const;
	virtual int PointAreaNum( const idVec3& origin ) const;
	virtual int PointReachableAreaNum( const idVec3& origin, const idBounds& bounds, const int areaFlags, int excludeTravelFlags ) const;
	virtual int BoundsReachableAreaNum( const idBounds& bounds, const int areaFlags, int excludeTravelFlags ) const;
	virtual void PushPointIntoArea( int areaNum, idVec3& origin ) const;
	virtual idVec3 AreaCenter( int areaNum ) const;
	virtual bool Trace( aasTrace_t& trace, const idVec3& start, const idVec3& end ) const;
	virtual bool TraceHeight( aasTraceHeight_t& trace, const idVec3& start, const idVec3& end ) const;
	virtual bool TraceFloor( aasTraceFloor_t& trace, const idVec3& start, int startAreaNum, const idVec3& end, int travelFlags ) const;
	virtual const byte* GetObstaclePVS( int areaNum ) const;
	virtual int GetObstaclePVSWallEdges( int areaNum, int* edges, int maxEdges ) const;
	virtual int GetWallEdges( int areaNum, const idBounds& bounds, int travelFlags, float height, int* edges, int maxEdges ) const;
	virtual void GetEdgeVertexNumbers( int edgeNum, int verts[ 2 ] ) const;
	virtual void GetEdge( int edgeNum, idVec3& start, idVec3& end ) const;
	virtual int GetEdgeFlags( int edgeNum ) const;
	virtual int GetAreaFlags( int areaNum ) const;
	virtual void SetAreaTravelFlags( int areaNum, int travelFlags );
	virtual bool ChangeAreaTravelFlags( const idBounds& bounds, const int areaFlags, int travelFlags, bool set );
	virtual bool ChangeReachabilityTravelFlags( const char* name, int travelFlags, bool set );
	virtual int TravelTimeToGoalArea( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, int travelFlags ) const;
	virtual bool RouteToGoalArea( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, int travelFlags, int& travelTime, const aasReachability_t** reach ) const;
	virtual bool FindNearestGoal( idAASGoal& goal, int startAreaNum, const idVec3& startOrigin, int travelFlags, idAASCallback& callback ) const;
	virtual bool WalkPathToGoal( idAASPath& path, int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int walkTravelFlags ) const;
	virtual bool ExtendHopPathToGoal( idAASPath& path, int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int walkTravelFlags, const idAASHopPathParms& parms ) const;
	virtual void ShowWalkPath( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int walkTravelFlags ) const;
	virtual void ShowHopPath( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int walkTravelFlags, const idAASHopPathParms& parms ) const;
	virtual void DrawArea( int areaNum ) const;

private:
	void Shutdown();
	void CopySettings();
	int CacheBoundaryEdges( int areaNum, const idBounds& bounds, int travelFlags ) const;
	static int PathTypeForFlags( int flags );
	static int ExcludedForAllowedFlags( int allowedFlags );

	idNavigationQuery*		query;
	idAASSettings			settings;
	mutable aasReachability_t routeReachability;
	mutable idList< navBoundarySegment_t > boundaryEdges;
	mutable idList< byte >	obstaclePVS;
};

#endif // __AAS_LOCAL_H__
