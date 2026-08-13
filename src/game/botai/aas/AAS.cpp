// Copyright (C) 2007 Id Software, Inc.
// Copyright (C) 2026 - QuakeWars2 contributors.

#include "../../precompiled.h"
#pragma hdrstop

#include "AAS_local.h"

idAASSettings::idAASSettings() {
	type = AAS_PLAYER;
	boundingBox.Zero();
	primitiveModeBrush = primitiveModePatch = primitiveModeModel = primitiveModeTerrain = AAS_PRIMITIVE_MODE_NEVER;
	gravity.Set( 0.0f, 0.0f, -1066.0f );
	gravityDir.Set( 0.0f, 0.0f, -1.0f );
	invGravityDir.Set( 0.0f, 0.0f, 1.0f );
	gravityValue = 1066.0f;
	maxStepHeight = maxBarrierHeight = maxWaterJumpHeight = maxFallHeight = 0.0f;
	minFloorCos = 0.7f;
	minHighCeiling = 80.0f;
	groundSpeed = waterSpeed = ladderSpeed = 1.0f;
	wallCornerEdgeRadius = ledgeCornerEdgeRadius = 0.0f;
	obstaclePVSRadius = 1024.0f;
	tt_barrierJump = tt_waterJump = tt_startWalkOffLedge = tt_startLadderClimb = 100;
}

idAAS* idAAS::Alloc() {
	return new idAASLocal();
}

idAAS::~idAAS() {
}

idAASLocal::idAASLocal() {
	query = NULL;
	memset( &routeReachability, 0, sizeof( routeReachability ) );
}

idAASLocal::~idAASLocal() {
	Shutdown();
}

void idAASLocal::Shutdown() {
	if ( query != NULL && navigationSystem != NULL ) navigationSystem->FreeQuery( query );
	query = NULL;
	boundaryEdges.Clear();
	obstaclePVS.Clear();
}

bool idAASLocal::Init( const char* mapName, unsigned int mapFileCRC ) {
	Shutdown();
	if ( navigationSystem == NULL ) return false;
	idStr sourceMap = mapName;
	idStr profile;
	sourceMap.ExtractFileExtension( profile );
	sourceMap.StripFileExtension();
	query = navigationSystem->LoadQuery( sourceMap, mapFileCRC, profile );
	if ( query == NULL ) return false;
	CopySettings();
	return true;
}

void idAASLocal::CopySettings() {
	const navProfileSettings_t& source = query->GetSettings();
	settings.type = idStr::Icmp( source.name, "nav_vehicle" ) == 0 ? idAASSettings::AAS_VEHICLE : idAASSettings::AAS_PLAYER;
	settings.fileExtensionAAS = source.name;
	settings.boundingBox = source.boundingBox;
	settings.maxStepHeight = source.maxStepHeight;
	settings.maxBarrierHeight = source.maxBarrierHeight;
	settings.maxFallHeight = source.maxFallHeight;
	settings.minFloorCos = source.minFloorCos;
	settings.groundSpeed = source.groundSpeed;
	settings.waterSpeed = source.groundSpeed;
	settings.ladderSpeed = source.groundSpeed;
	settings.obstaclePVSRadius = source.obstacleQueryRadius;
}

void idAASLocal::Stats() const {
	if ( query != NULL ) query->Stats();
}

void idAASLocal::Test( const idVec3& origin ) {
	const int area = PointReachableAreaNum( origin, settings.boundingBox, AAS_AREA_REACHABLE_WALK, 0 );
	if ( area != 0 ) DrawArea( area );
}

const idAASSettings* idAASLocal::GetSettings() const {
	return query != NULL ? &settings : NULL;
}

int idAASLocal::PointAreaNum( const idVec3& origin ) const {
	return PointReachableAreaNum( origin, idBounds( idVec3( -2, -2, -4 ), idVec3( 2, 2, 4 ) ), AAS_AREA_REACHABLE_WALK, 0 );
}

int idAASLocal::PointReachableAreaNum( const idVec3& origin, const idBounds& bounds, const int, int excludeTravelFlags ) const {
	return query != NULL ? static_cast< int >( query->FindNearestPoly( origin, bounds, excludeTravelFlags ) ) : 0;
}

int idAASLocal::BoundsReachableAreaNum( const idBounds& bounds, const int, int excludeTravelFlags ) const {
	if ( query == NULL ) return 0;
	const idVec3 center = bounds.GetCenter();
	return static_cast< int >( query->FindNearestPoly( center, idBounds( bounds[ 0 ] - center, bounds[ 1 ] - center ), excludeTravelFlags ) );
}

void idAASLocal::PushPointIntoArea( int areaNum, idVec3& origin ) const {
	if ( query != NULL ) query->ClampPointToPoly( static_cast< navPolyRef_t >( areaNum ), origin, origin );
}

idVec3 idAASLocal::AreaCenter( int areaNum ) const {
	idVec3 center = vec3_origin;
	if ( query != NULL ) query->GetPolyCenter( static_cast< navPolyRef_t >( areaNum ), center );
	return center;
}

bool idAASLocal::Trace( aasTrace_t& trace, const idVec3& start, const idVec3& end ) const {
	trace.fraction = 0.0f; trace.endpos = start; trace.lastAreaNum = 0; trace.blockingAreaNum = 0; trace.numAreas = 0;
	if ( query == NULL ) return false;
	int startArea = PointReachableAreaNum( start, settings.boundingBox, AAS_AREA_REACHABLE_WALK, trace.travelFlags );
	if ( startArea == 0 ) return false;
	navTraceResult_t result;
	if ( !query->Raycast( startArea, start, end, trace.travelFlags, result ) ) return false;
	trace.fraction = result.fraction; trace.endpos = result.endPos; trace.lastAreaNum = static_cast< int >( result.lastPoly );
	if ( trace.maxAreas > 0 && trace.areas != NULL ) { trace.areas[ 0 ] = startArea; trace.numAreas = 1; }
	return result.fraction < 1.0f;
}

bool idAASLocal::TraceHeight( aasTraceHeight_t& trace, const idVec3& start, const idVec3& end ) const {
	trace.numPoints = 0;
	if ( query == NULL || trace.points == NULL || trace.maxPoints <= 0 ) return false;
	trace.points[ trace.numPoints++ ] = start;
	if ( trace.numPoints < trace.maxPoints ) {
		idVec3 clamped = end;
		const int area = PointReachableAreaNum( end, settings.boundingBox, AAS_AREA_REACHABLE_WALK, 0 );
		if ( area != 0 ) query->ClampPointToPoly( area, end, clamped );
		trace.points[ trace.numPoints++ ] = clamped;
	}
	return true;
}

bool idAASLocal::TraceFloor( aasTraceFloor_t& trace, const idVec3& start, int startAreaNum, const idVec3& end, int travelFlags ) const {
	trace.fraction = 0.0f; trace.endpos = start; trace.lastAreaNum = startAreaNum; trace.lastEdgeNum = 0;
	if ( query == NULL ) return false;
	const int excludedFlags = ExcludedForAllowedFlags( travelFlags );
	if ( startAreaNum == 0 ) startAreaNum = PointReachableAreaNum( start, settings.boundingBox, AAS_AREA_REACHABLE_WALK, excludedFlags );
	navTraceResult_t result;
	if ( startAreaNum == 0 || !query->Raycast( startAreaNum, start, end, excludedFlags, result ) ) return false;
	trace.fraction = result.fraction; trace.endpos = result.endPos; trace.lastAreaNum = static_cast< int >( result.lastPoly );
	return result.fraction >= 1.0f;
}

const byte* idAASLocal::GetObstaclePVS( int ) const {
	if ( query == NULL ) return NULL;
	navPolyRef_t maxPoly = 0;
	for ( int index = 0; index < query->GetPolyCount(); ++index ) maxPoly = Max( maxPoly, query->GetPolyByIndex( index ) );
	const int bytes = static_cast< int >( ( maxPoly + 8 ) >> 3 );
	obstaclePVS.SetNum( Max( bytes, 1 ), false );
	memset( obstaclePVS.Begin(), 0xff, obstaclePVS.Num() );
	return obstaclePVS.Begin();
}

int idAASLocal::CacheBoundaryEdges( int areaNum, const idBounds& bounds, int travelFlags ) const {
	boundaryEdges.SetNum( 4096, false );
	const int count = query != NULL ? query->GetBoundarySegments( areaNum, bounds, travelFlags, boundaryEdges.Begin(), boundaryEdges.Num() ) : 0;
	boundaryEdges.SetNum( count, false );
	return count;
}

int idAASLocal::GetObstaclePVSWallEdges( int areaNum, int* edges, int maxEdges ) const {
	if ( query == NULL || edges == NULL || maxEdges <= 0 ) return 0;
	idVec3 center;
	if ( !query->GetPolyCenter( areaNum, center ) ) return 0;
	idBounds bounds( center );
	bounds.ExpandSelf( settings.obstaclePVSRadius );
	const int count = Min( CacheBoundaryEdges( areaNum, bounds, 0 ), maxEdges );
	for ( int index = 0; index < count; ++index ) edges[ index ] = index + 1;
	return count;
}

int idAASLocal::GetWallEdges( int areaNum, const idBounds& bounds, int travelFlags, float, int* edges, int maxEdges ) const {
	if ( edges == NULL || maxEdges <= 0 ) return 0;
	const int count = Min( CacheBoundaryEdges( areaNum, bounds, ExcludedForAllowedFlags( travelFlags ) ), maxEdges );
	for ( int index = 0; index < count; ++index ) edges[ index ] = index + 1;
	return count;
}

void idAASLocal::GetEdgeVertexNumbers( int edgeNum, int verts[ 2 ] ) const {
	verts[ 0 ] = edgeNum * 2; verts[ 1 ] = edgeNum * 2 + 1;
}

void idAASLocal::GetEdge( int edgeNum, idVec3& start, idVec3& end ) const {
	const int index = abs( edgeNum ) - 1;
	if ( index < 0 || index >= boundaryEdges.Num() ) { start.Zero(); end.Zero(); return; }
	if ( edgeNum < 0 ) { start = boundaryEdges[ index ].end; end = boundaryEdges[ index ].start; }
	else { start = boundaryEdges[ index ].start; end = boundaryEdges[ index ].end; }
}

int idAASLocal::GetEdgeFlags( int ) const { return AAS_EDGE_WALL; }

int idAASLocal::GetAreaFlags( int areaNum ) const {
	return query != NULL && areaNum != 0 ? AAS_AREA_REACHABLE_WALK : 0;
}

void idAASLocal::SetAreaTravelFlags( int areaNum, int travelFlags ) {
	if ( query == NULL || areaNum == 0 ) return;
	query->ChangePolyTravelFlags( static_cast< navPolyRef_t >( areaNum ), travelFlags, true );
}

bool idAASLocal::ChangeAreaTravelFlags( const idBounds& bounds, const int, int travelFlags, bool set ) {
	return query != NULL && query->ChangeTravelFlags( bounds, travelFlags, set );
}

bool idAASLocal::ChangeReachabilityTravelFlags( const char* name, int travelFlags, bool set ) {
	return query != NULL && query->ChangeNamedLinkTravelFlags( name, travelFlags, set );
}

int idAASLocal::TravelTimeToGoalArea( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, int travelFlags ) const {
	if ( query == NULL ) return 0;
	navPathResult_t path;
	const idVec3 goal = AreaCenter( goalAreaNum );
	return query->FindPath( startAreaNum, startOrigin, goalAreaNum, goal, ExcludedForAllowedFlags( travelFlags ), path ) ? path.travelTime : 0;
}

bool idAASLocal::RouteToGoalArea( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, int travelFlags, int& travelTime, const aasReachability_t** reach ) const {
	travelTime = 0;
	if ( reach != NULL ) *reach = NULL;
	if ( query == NULL ) return false;
	navPathResult_t path;
	const idVec3 goal = AreaCenter( goalAreaNum );
	if ( !query->FindPath( startAreaNum, startOrigin, goalAreaNum, goal, ExcludedForAllowedFlags( travelFlags ), path ) || path.numPoints < 1 ) return false;
	travelTime = path.travelTime;
	memset( &routeReachability, 0, sizeof( routeReachability ) );
	const int next = path.numPoints > 1 ? 1 : 0;
	routeReachability.travelFlags = path.travelFlags[ next ] != 0 ? path.travelFlags[ next ] : NAV_TFL_WALK;
	routeReachability.travelTime = static_cast< unsigned short >( Min( travelTime, 65535 ) );
	routeReachability.fromAreaNum = static_cast< unsigned short >( startAreaNum );
	routeReachability.toAreaNum = static_cast< unsigned short >( path.polys[ next ] );
	routeReachability.SetStart( startOrigin, vec3_origin );
	routeReachability.SetEnd( path.points[ next ], vec3_origin );
	if ( reach != NULL ) *reach = &routeReachability;
	return true;
}

bool idAASLocal::FindNearestGoal( idAASGoal& goal, int startAreaNum, const idVec3& startOrigin, int travelFlags, idAASCallback& callback ) const {
	if ( query == NULL ) return false;
	int bestTime = INT_MAX;
	bool found = false;
	for ( int index = 0; index < query->GetPolyCount(); ++index ) {
		const int area = static_cast< int >( query->GetPolyByIndex( index ) );
		if ( area == 0 || !callback.AreaIsGoal( this, area ) ) continue;
		const idVec3 center = AreaCenter( area );
		if ( !callback.PathValid( this, startOrigin, center ) ) continue;
		const int time = TravelTimeToGoalArea( startAreaNum, startOrigin, area, travelFlags ) + callback.AdditionalTravelTimeForPath( this, startOrigin, center );
		if ( time <= 0 || time >= bestTime ) continue;
		bestTime = time; goal.areaNum = area; goal.origin = center; found = true;
	}
	return found;
}

int idAASLocal::PathTypeForFlags( int flags ) {
	if ( flags & NAV_TFL_TELEPORT ) return PATHTYPE_TELEPORT;
	if ( flags & NAV_TFL_ELEVATOR ) return PATHTYPE_ELEVATOR;
	if ( flags & NAV_TFL_LADDER ) return PATHTYPE_LADDER;
	if ( flags & NAV_TFL_JUMP ) return PATHTYPE_JUMP;
	if ( flags & NAV_TFL_BARRIERJUMP ) return PATHTYPE_BARRIERJUMP;
	if ( flags & NAV_TFL_WALKOFFBARRIER ) return PATHTYPE_WALKOFFBARRIER;
	if ( flags & NAV_TFL_WALKOFFLEDGE ) return PATHTYPE_WALKOFFLEDGE;
	return PATHTYPE_WALK;
}

int idAASLocal::ExcludedForAllowedFlags( int allowedFlags ) {
	return ( ~allowedFlags ) & 0x7fff;
}

bool idAASLocal::WalkPathToGoal( idAASPath& path, int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int ) const {
	path.type = MAX_PATHTYPE; path.moveGoal = startOrigin; path.moveAreaNum = startAreaNum; path.reachability = NULL; path.viewGoal = goalOrigin; path.travelTime = 0;
	if ( query == NULL ) return false;
	navPathResult_t result;
	if ( !query->FindPath( startAreaNum, startOrigin, goalAreaNum, goalOrigin, ExcludedForAllowedFlags( travelFlags ), result ) || result.numPoints == 0 ) return false;
	const int next = result.numPoints > 1 ? 1 : 0;
	path.type = PathTypeForFlags( result.travelFlags[ next ] );
	path.moveGoal = result.points[ next ];
	path.moveAreaNum = static_cast< int >( result.polys[ next ] );
	path.viewGoal = result.numPoints > 2 ? result.points[ 2 ] : goalOrigin;
	path.travelTime = result.travelTime;
	if ( path.type != PATHTYPE_WALK ) {
		memset( &routeReachability, 0, sizeof( routeReachability ) );
		routeReachability.travelFlags = result.travelFlags[ next ];
		routeReachability.SetStart( startOrigin, vec3_origin );
		routeReachability.SetEnd( result.points[ next ], vec3_origin );
		path.reachability = &routeReachability;
	}
	return true;
}

bool idAASLocal::ExtendHopPathToGoal( idAASPath& path, int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int walkTravelFlags, const idAASHopPathParms& ) const {
	return WalkPathToGoal( path, startAreaNum, startOrigin, goalAreaNum, goalOrigin, travelFlags, walkTravelFlags );
}

void idAASLocal::ShowWalkPath( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int ) const {
	if ( query == NULL ) return;
	navPathResult_t path;
	if ( !query->FindPath( startAreaNum, startOrigin, goalAreaNum, goalOrigin, ExcludedForAllowedFlags( travelFlags ), path ) ) return;
	for ( int index = 1; index < path.numPoints; ++index ) gameRenderWorld->DebugArrow( colorGreen, path.points[ index - 1 ], path.points[ index ], 2.0f );
}

void idAASLocal::ShowHopPath( int startAreaNum, const idVec3& startOrigin, int goalAreaNum, const idVec3& goalOrigin, int travelFlags, int walkTravelFlags, const idAASHopPathParms& ) const {
	ShowWalkPath( startAreaNum, startOrigin, goalAreaNum, goalOrigin, travelFlags, walkTravelFlags );
}

void idAASLocal::DrawArea( int areaNum ) const {
	if ( query == NULL || areaNum == 0 ) return;
	idVec3 center;
	if ( !query->GetPolyCenter( areaNum, center ) ) return;
	idBounds bounds( center ); bounds.ExpandSelf( settings.boundingBox.GetRadius() );
	CacheBoundaryEdges( areaNum, bounds, 0 );
	for ( int index = 0; index < boundaryEdges.Num(); ++index ) gameRenderWorld->DebugLine( colorGreen, boundaryEdges[ index ].start, boundaryEdges[ index ].end );
}
