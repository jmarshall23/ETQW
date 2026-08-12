#ifndef __MEGATEXTURE_ROAD_BUILDER_H__
#define __MEGATEXTURE_ROAD_BUILDER_H__

#include <vector>

struct megaTextureRoad_t {
	megaTextureRoad_t();

	idStr name;
	idStr texture;
	float width;
	float feather;
	float repeatLength;
	bool enabled;
	std::vector<idVec2> points;
};

struct megaTextureRoadPolylinePoint_t {
	idVec2 position;
	idVec2 tangent;
	float distance;
};

struct megaTextureRoadSample_t {
	megaTextureRoadSample_t();

	float alpha;
	float u;
	float v;
	float tangentX;
	float tangentY;
	float distance;
};

// Editable, level-owned road splines. Control points use terrain-local XY
// coordinates, so changing the final MegaTexture resolution never moves roads.
class MegaTextureRoadBuilder {
public:
	MegaTextureRoadBuilder();

	void Clear();
	int NumRoads() const;
	const megaTextureRoad_t &GetRoad( int index ) const;
	megaTextureRoad_t &EditRoad( int index );
	int AddRoad( const char *texture, const char *name = NULL );
	void DeleteRoad( int index );
	void MoveRoad( int index, int direction );
	void ReverseRoad( int index );
	void AddPoint( int roadIndex, const idVec2 &point );
	void SetPoint( int roadIndex, int pointIndex, const idVec2 &point );
	void DeletePoint( int roadIndex, int pointIndex );
	void InvalidateRoad( int roadIndex );

	bool Load( const char *path, idStr &error );
	bool Save( const char *path, idStr &error ) const;

	const std::vector<megaTextureRoadPolylinePoint_t> &Polyline( int roadIndex ) const;
	bool SampleRoad( int roadIndex, const idVec2 &point, megaTextureRoadSample_t &sample ) const;
	bool RoadIntersectsBounds( int roadIndex, const idVec2 &minimum, const idVec2 &maximum ) const;
	int FindClosestPoint( const idVec2 &point, float maximumDistance, int &pointIndex ) const;
	int FindClosestRoad( const idVec2 &point, float extraDistance ) const;

private:
	struct roadCache_t {
		roadCache_t() : valid( false ), minimum( 0.0f, 0.0f ), maximum( 0.0f, 0.0f ) {}
		bool valid;
		idVec2 minimum;
		idVec2 maximum;
		std::vector<megaTextureRoadPolylinePoint_t> polyline;
	};

	void BuildCache( int roadIndex ) const;

	std::vector<megaTextureRoad_t> roads;
	mutable std::vector<roadCache_t> caches;
};

#endif
