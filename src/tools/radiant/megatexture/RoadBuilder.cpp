#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../../framework/FileSystem.h"
#include "RoadBuilder.h"

#include <algorithm>

namespace {

static idVec2 CatmullRom( const idVec2 &p0, const idVec2 &p1, const idVec2 &p2, const idVec2 &p3, float t ) {
	const float t2 = t * t;
	const float t3 = t2 * t;
	idVec2 result;
	result.x = 0.5f * ( 2.0f * p1.x + ( -p0.x + p2.x ) * t +
		( 2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x ) * t2 +
		( -p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x ) * t3 );
	result.y = 0.5f * ( 2.0f * p1.y + ( -p0.y + p2.y ) * t +
		( 2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y ) * t2 +
		( -p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y ) * t3 );
	return result;
}

static float DistanceSquared( const idVec2 &a, const idVec2 &b ) {
	const float x = a.x - b.x;
	const float y = a.y - b.y;
	return x * x + y * y;
}

static bool WriteRoadText( const char *path, const idStr &text, idStr &error ) {
	idFile *file = fileSystem->OpenFileWrite( path, "fs_devpath" );
	if ( !file ) {
		error = va( "could not write road spline file %s", path );
		return false;
	}
	const bool okay = file->Write( text.c_str(), text.Length() ) == text.Length();
	fileSystem->CloseFile( file );
	if ( !okay ) error = va( "short write to road spline file %s", path );
	return okay;
}

} // namespace

megaTextureRoad_t::megaTextureRoad_t() : width( 512.0f ), feather( 64.0f ), repeatLength( 512.0f ), enabled( true ) {
}

megaTextureRoadSample_t::megaTextureRoadSample_t() :
	alpha( 0.0f ), u( 0.0f ), v( 0.0f ), tangentX( 1.0f ), tangentY( 0.0f ), distance( 0.0f ) {
}

MegaTextureRoadBuilder::MegaTextureRoadBuilder() {
}

void MegaTextureRoadBuilder::Clear() {
	roads.clear();
	caches.clear();
}

int MegaTextureRoadBuilder::NumRoads() const {
	return (int)roads.size();
}

const megaTextureRoad_t &MegaTextureRoadBuilder::GetRoad( int index ) const {
	return roads[index];
}

megaTextureRoad_t &MegaTextureRoadBuilder::EditRoad( int index ) {
	return roads[index];
}

int MegaTextureRoadBuilder::AddRoad( const char *texture, const char *name ) {
	megaTextureRoad_t road;
	road.name = name && name[0] ? name : va( "Road %d", (int)roads.size() + 1 );
	road.texture = texture ? texture : "";
	roads.push_back( road );
	caches.push_back( roadCache_t() );
	return (int)roads.size() - 1;
}

void MegaTextureRoadBuilder::DeleteRoad( int index ) {
	if ( index < 0 || index >= NumRoads() ) return;
	roads.erase( roads.begin() + index );
	caches.erase( caches.begin() + index );
}

void MegaTextureRoadBuilder::MoveRoad( int index, int direction ) {
	const int destination = index + direction;
	if ( index < 0 || index >= NumRoads() || destination < 0 || destination >= NumRoads() ) return;
	std::swap( roads[index], roads[destination] );
	std::swap( caches[index], caches[destination] );
}

void MegaTextureRoadBuilder::ReverseRoad( int index ) {
	if ( index < 0 || index >= NumRoads() ) return;
	std::reverse( roads[index].points.begin(), roads[index].points.end() );
	InvalidateRoad( index );
}

void MegaTextureRoadBuilder::AddPoint( int roadIndex, const idVec2 &point ) {
	if ( roadIndex < 0 || roadIndex >= NumRoads() ) return;
	roads[roadIndex].points.push_back( point );
	InvalidateRoad( roadIndex );
}

void MegaTextureRoadBuilder::SetPoint( int roadIndex, int pointIndex, const idVec2 &point ) {
	if ( roadIndex < 0 || roadIndex >= NumRoads() || pointIndex < 0 || pointIndex >= (int)roads[roadIndex].points.size() ) return;
	roads[roadIndex].points[pointIndex] = point;
	InvalidateRoad( roadIndex );
}

void MegaTextureRoadBuilder::DeletePoint( int roadIndex, int pointIndex ) {
	if ( roadIndex < 0 || roadIndex >= NumRoads() || pointIndex < 0 || pointIndex >= (int)roads[roadIndex].points.size() ) return;
	roads[roadIndex].points.erase( roads[roadIndex].points.begin() + pointIndex );
	InvalidateRoad( roadIndex );
}

void MegaTextureRoadBuilder::InvalidateRoad( int roadIndex ) {
	if ( roadIndex >= 0 && roadIndex < (int)caches.size() ) caches[roadIndex].valid = false;
}

bool MegaTextureRoadBuilder::Load( const char *path, idStr &error ) {
	Clear();
	if ( !path || !path[0] || fileSystem->ReadFile( path, NULL, NULL ) < 0 ) return true;
	idLexer lexer;
	lexer.SetFlags( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS );
	if ( !lexer.LoadFile( path ) ) { error = va( "could not load road spline file %s", path ); return false; }
	idToken token;
	if ( !lexer.ReadToken( &token ) || token.Icmp( "megaTextureRoads" ) ) { error = "missing megaTextureRoads header"; return false; }
	if ( !lexer.ReadToken( &token ) || token.GetIntValue() != 1 ) { error = "unsupported MegaTexture road version"; return false; }
	if ( !lexer.ExpectTokenString( "{" ) ) { error = "missing road spline body"; return false; }
	while ( lexer.ReadToken( &token ) && token != "}" ) {
		if ( token.Icmp( "road" ) ) { lexer.ReadToken( &token ); continue; }
		if ( !lexer.ExpectTokenString( "{" ) ) { error = "missing road body"; return false; }
		megaTextureRoad_t road;
		while ( lexer.ReadToken( &token ) && token != "}" ) {
			if ( !token.Icmp( "name" ) ) { lexer.ReadToken( &token ); road.name = token; }
			else if ( !token.Icmp( "texture" ) ) { lexer.ReadToken( &token ); road.texture = token; if ( road.texture == "-" ) road.texture.Clear(); }
			else if ( !token.Icmp( "width" ) ) road.width = lexer.ParseFloat();
			else if ( !token.Icmp( "feather" ) ) road.feather = lexer.ParseFloat();
			else if ( !token.Icmp( "repeatLength" ) ) road.repeatLength = lexer.ParseFloat();
			else if ( !token.Icmp( "enabled" ) ) road.enabled = lexer.ParseInt() != 0;
			else if ( !token.Icmp( "point" ) ) {
				// Parse in explicit source order. C++ does not guarantee function
				// argument evaluation order, and MSVC evaluated the two ParseFloat
				// calls right-to-left, silently loading every saved point as ( y, x ).
				const float x = lexer.ParseFloat();
				const float y = lexer.ParseFloat();
				road.points.push_back( idVec2( x, y ) );
			}
			else lexer.ReadToken( &token );
		}
		road.width = Max( road.width, 1.0f );
		road.feather = idMath::ClampFloat( 0.0f, road.width * 0.5f, road.feather );
		road.repeatLength = Max( road.repeatLength, 1.0f );
		if ( road.name.IsEmpty() ) road.name = va( "Road %d", (int)roads.size() + 1 );
		roads.push_back( road );
		caches.push_back( roadCache_t() );
	}
	if ( lexer.HadError() ) { Clear(); error = va( "invalid road spline file %s", path ); return false; }
	return true;
}

bool MegaTextureRoadBuilder::Save( const char *path, idStr &error ) const {
	if ( !path || !path[0] ) { error = "road spline path is empty"; return false; }
	idStr text = "megaTextureRoads 1\n{\n";
	for ( int index = 0; index < NumRoads(); ++index ) {
		const megaTextureRoad_t &road = roads[index];
		text += "\troad\n\t{\n";
		text += va( "\t\tname \"%s\"\n\t\ttexture %s\n\t\twidth %g\n\t\tfeather %g\n\t\trepeatLength %g\n\t\tenabled %d\n",
			road.name.c_str(), road.texture.IsEmpty() ? "-" : road.texture.c_str(), Max( road.width, 1.0f ),
			idMath::ClampFloat( 0.0f, Max( road.width, 1.0f ) * 0.5f, road.feather ), Max( road.repeatLength, 1.0f ), road.enabled ? 1 : 0 );
		for ( int point = 0; point < (int)road.points.size(); ++point ) {
			text += va( "\t\tpoint %.9g %.9g\n", road.points[point].x, road.points[point].y );
		}
		text += "\t}\n";
	}
	text += "}\n";
	return WriteRoadText( path, text, error );
}

void MegaTextureRoadBuilder::BuildCache( int roadIndex ) const {
	if ( roadIndex < 0 || roadIndex >= NumRoads() ) return;
	roadCache_t &cache = caches[roadIndex];
	if ( cache.valid ) return;
	cache.polyline.clear();
	const megaTextureRoad_t &road = roads[roadIndex];
	if ( road.points.empty() ) { cache.valid = true; return; }
	if ( road.points.size() == 1 ) {
		megaTextureRoadPolylinePoint_t point;
		point.position = road.points[0]; point.tangent.Set( 1.0f, 0.0f ); point.distance = 0.0f;
		cache.polyline.push_back( point ); cache.minimum = cache.maximum = point.position; cache.valid = true; return;
	}
	const int subdivisions = 12;
	for ( int segment = 0; segment + 1 < (int)road.points.size(); ++segment ) {
		const idVec2 &p0 = road.points[Max( 0, segment - 1 )];
		const idVec2 &p1 = road.points[segment];
		const idVec2 &p2 = road.points[segment + 1];
		const idVec2 &p3 = road.points[Min( (int)road.points.size() - 1, segment + 2 )];
		const int firstStep = segment == 0 ? 0 : 1;
		for ( int step = firstStep; step <= subdivisions; ++step ) {
			megaTextureRoadPolylinePoint_t point;
			point.position = CatmullRom( p0, p1, p2, p3, step / (float)subdivisions );
			point.tangent.Set( 1.0f, 0.0f ); point.distance = 0.0f;
			cache.polyline.push_back( point );
		}
	}
	float distance = 0.0f;
	for ( int index = 0; index < (int)cache.polyline.size(); ++index ) {
		if ( index > 0 ) distance += idMath::Sqrt( DistanceSquared( cache.polyline[index - 1].position, cache.polyline[index].position ) );
		cache.polyline[index].distance = distance;
		const idVec2 &before = cache.polyline[Max( 0, index - 1 )].position;
		const idVec2 &after = cache.polyline[Min( (int)cache.polyline.size() - 1, index + 1 )].position;
		idVec2 tangent( after.x - before.x, after.y - before.y );
		const float length = tangent.Length();
		if ( length > 0.0001f ) tangent /= length;
		else tangent.Set( 1.0f, 0.0f );
		cache.polyline[index].tangent = tangent;
		if ( index == 0 ) cache.minimum = cache.maximum = cache.polyline[index].position;
		else {
			cache.minimum.x = Min( cache.minimum.x, cache.polyline[index].position.x );
			cache.minimum.y = Min( cache.minimum.y, cache.polyline[index].position.y );
			cache.maximum.x = Max( cache.maximum.x, cache.polyline[index].position.x );
			cache.maximum.y = Max( cache.maximum.y, cache.polyline[index].position.y );
		}
	}
	cache.valid = true;
}

const std::vector<megaTextureRoadPolylinePoint_t> &MegaTextureRoadBuilder::Polyline( int roadIndex ) const {
	static const std::vector<megaTextureRoadPolylinePoint_t> empty;
	if ( roadIndex < 0 || roadIndex >= NumRoads() ) return empty;
	BuildCache( roadIndex );
	return caches[roadIndex].polyline;
}

bool MegaTextureRoadBuilder::SampleRoad( int roadIndex, const idVec2 &point, megaTextureRoadSample_t &sample ) const {
	sample = megaTextureRoadSample_t();
	if ( roadIndex < 0 || roadIndex >= NumRoads() ) return false;
	const megaTextureRoad_t &road = roads[roadIndex];
	if ( !road.enabled || road.texture.IsEmpty() || road.points.size() < 2 ) return false;
	const std::vector<megaTextureRoadPolylinePoint_t> &line = Polyline( roadIndex );
	const roadCache_t &cache = caches[roadIndex];
	const float halfWidth = Max( road.width * 0.5f, 0.5f );
	if ( point.x < cache.minimum.x - halfWidth || point.x > cache.maximum.x + halfWidth ||
		 point.y < cache.minimum.y - halfWidth || point.y > cache.maximum.y + halfWidth ) return false;
	float bestDistanceSquared = idMath::INFINITY;
	float bestSide = 0.0f, bestAlong = 0.0f, bestTangentX = 1.0f, bestTangentY = 0.0f;
	for ( int index = 0; index + 1 < (int)line.size(); ++index ) {
		const idVec2 &a = line[index].position;
		const idVec2 &b = line[index + 1].position;
		const float dx = b.x - a.x, dy = b.y - a.y;
		const float lengthSquared = dx * dx + dy * dy;
		if ( lengthSquared <= 0.0001f ) continue;
		const float fraction = idMath::ClampFloat( 0.0f, 1.0f,
			( ( point.x - a.x ) * dx + ( point.y - a.y ) * dy ) / lengthSquared );
		const idVec2 nearest( a.x + dx * fraction, a.y + dy * fraction );
		const float candidateDistanceSquared = DistanceSquared( point, nearest );
		if ( candidateDistanceSquared >= bestDistanceSquared ) continue;
		const float length = idMath::Sqrt( lengthSquared );
		bestDistanceSquared = candidateDistanceSquared;
		bestTangentX = dx / length; bestTangentY = dy / length;
		bestSide = bestTangentX * ( point.y - nearest.y ) - bestTangentY * ( point.x - nearest.x );
		bestAlong = line[index].distance + length * fraction;
	}
	const float absoluteSide = idMath::Fabs( bestSide );
	if ( bestDistanceSquared == idMath::INFINITY || absoluteSide >= halfWidth ) return false;
	const float feather = idMath::ClampFloat( 0.0f, halfWidth, road.feather );
	sample.alpha = feather <= 0.001f ? 1.0f : idMath::ClampFloat( 0.0f, 1.0f, ( halfWidth - absoluteSide ) / feather );
	sample.u = 0.5f + bestSide / Max( road.width, 1.0f );
	sample.v = bestAlong / Max( road.repeatLength, 1.0f );
	sample.tangentX = bestTangentX; sample.tangentY = bestTangentY;
	sample.distance = bestAlong;
	return sample.alpha > 0.0f;
}

bool MegaTextureRoadBuilder::RoadIntersectsBounds( int roadIndex, const idVec2 &minimum, const idVec2 &maximum ) const {
	if ( roadIndex < 0 || roadIndex >= NumRoads() ) return false;
	const megaTextureRoad_t &road = roads[roadIndex];
	if ( !road.enabled || road.texture.IsEmpty() || road.points.size() < 2 ) return false;
	BuildCache( roadIndex );
	const roadCache_t &cache = caches[roadIndex];
	const float expansion = Max( road.width * 0.5f, 0.5f );
	if ( cache.maximum.x + expansion < minimum.x || cache.minimum.x - expansion > maximum.x ||
		 cache.maximum.y + expansion < minimum.y || cache.minimum.y - expansion > maximum.y ) return false;
	for ( int point = 0; point + 1 < (int)cache.polyline.size(); ++point ) {
		const idVec2 &a = cache.polyline[point].position;
		const idVec2 &b = cache.polyline[point + 1].position;
		if ( Max( a.x, b.x ) + expansion >= minimum.x && Min( a.x, b.x ) - expansion <= maximum.x &&
			 Max( a.y, b.y ) + expansion >= minimum.y && Min( a.y, b.y ) - expansion <= maximum.y ) return true;
	}
	return false;
}

int MegaTextureRoadBuilder::FindClosestPoint( const idVec2 &point, float maximumDistance, int &pointIndex ) const {
	int bestRoad = -1; pointIndex = -1;
	float best = maximumDistance * maximumDistance;
	for ( int roadIndex = NumRoads() - 1; roadIndex >= 0; --roadIndex ) {
		for ( int index = 0; index < (int)roads[roadIndex].points.size(); ++index ) {
			const float distance = DistanceSquared( point, roads[roadIndex].points[index] );
			if ( distance <= best ) { best = distance; bestRoad = roadIndex; pointIndex = index; }
		}
	}
	return bestRoad;
}

int MegaTextureRoadBuilder::FindClosestRoad( const idVec2 &point, float extraDistance ) const {
	for ( int roadIndex = NumRoads() - 1; roadIndex >= 0; --roadIndex ) {
		megaTextureRoadSample_t sample;
		if ( SampleRoad( roadIndex, point, sample ) ) return roadIndex;
		const megaTextureRoad_t &road = roads[roadIndex];
		const std::vector<megaTextureRoadPolylinePoint_t> &line = Polyline( roadIndex );
		const float maximum = road.width * 0.5f + extraDistance;
		for ( int index = 0; index + 1 < (int)line.size(); ++index ) {
			const idVec2 &a = line[index].position, &b = line[index + 1].position;
			const float dx = b.x - a.x, dy = b.y - a.y, lengthSquared = dx * dx + dy * dy;
			if ( lengthSquared <= 0.0001f ) continue;
			const float fraction = idMath::ClampFloat( 0.0f, 1.0f, ( ( point.x - a.x ) * dx + ( point.y - a.y ) * dy ) / lengthSquared );
			if ( DistanceSquared( point, idVec2( a.x + dx * fraction, a.y + dy * fraction ) ) <= maximum * maximum ) return roadIndex;
		}
	}
	return -1;
}
