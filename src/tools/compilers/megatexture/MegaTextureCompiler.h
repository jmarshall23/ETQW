#ifndef __MEGATEXTURE_COMPILER_H__
#define __MEGATEXTURE_COMPILER_H__

#include <vector>

// Compact paint mapping for one texture layer at one terrain vertex.
struct megaTextureVertexTransform_t {
	unsigned short scaleX;
	unsigned short scaleY;
	unsigned short rotation;
	unsigned short pivotU;
	unsigned short pivotV;
	unsigned short phaseU;
	unsigned short phaseV;
};
static_assert( sizeof( megaTextureVertexTransform_t ) == 14, "terrain paint transform disk layout changed" );

struct megaTextureProject_t {
	static const int MAX_LAYERS = 4;

	megaTextureProject_t();

	bool Load( const char *path, idStr &error );
	bool Save( const char *path, idStr &error ) const;
	bool WriteMaterial( idStr &error ) const;
	bool WritePreview( idStr &error ) const;
	bool Validate( idStr &error ) const;
	idStr TilePath( int x, int y ) const;
	idStr CompiledName() const;
	idStr OutputPath() const;
	idStr PreviewPath() const;
	idStr MaterialPath() const;
	int NumLevels() const;

	idStr projectPath;
	idStr name;
	idStr sourceDirectory;
	idStr material;
	idStr mapName;
	idStr heightMap;
	idStr weightMap;
	idStr roadFile;
	idStr terrainModel;
	idStr layers[MAX_LAYERS];
	// Last-used paint-tool presets. Actual rendered mappings live per vertex/layer
	// in the versioned weight file.
	float layerScale[MAX_LAYERS];
	float layerScaleY[MAX_LAYERS];
	float layerRotation[MAX_LAYERS];
	int resolution;
	int terrainSamples;
	float terrainSize;
	float terrainOrigin[3];
	byte fill[4];
	int quality[3];
	// Last compiler mode. MegaTexture lighting is baked into streamed RGB and
	// never consumes dmap's lightmap atlas.
	bool bakeLighting;
};

megaTextureVertexTransform_t MegaTextureEncodeVertexTransform( float scaleX, float scaleY, float rotation,
	float pivotU = 0.5f, float pivotV = 0.5f, float phaseU = -1.0f, float phaseV = -1.0f );
void MegaTextureDecodeVertexTransform( const megaTextureVertexTransform_t &transform, float &scaleX, float &scaleY, float &rotation );
void MegaTextureDecodeVertexTransform( const megaTextureVertexTransform_t &transform, float &scaleX, float &scaleY,
	float &rotation, float &pivotU, float &pivotV );
void MegaTextureDecodeVertexTransform( const megaTextureVertexTransform_t &transform, float &scaleX, float &scaleY,
	float &rotation, float &pivotU, float &pivotV, float &phaseU, float &phaseV );
void MegaTextureTransformTexCoord( const megaTextureVertexTransform_t &transform, float u, float v,
	float &outU, float &outV, bool flipPivotV = false );
void MegaTextureInitializeTerrainTransforms( const megaTextureProject_t &project, std::vector<megaTextureVertexTransform_t> &transforms );

bool MegaTextureCreateProject( const char *name, int resolution, const char *mapName,
							   megaTextureProject_t &project, idStr &error, const char *projectPath = NULL );
bool MegaTextureCompileProject( const char *projectPath, int buildResolution, bool bakeLighting, idStr &error );
bool MegaTextureCompileProject( const char *projectPath, int buildResolution, bool bakeLighting,
	const idDict *worldSpawnOverride, idStr &error );
bool MegaTextureVerifyFile( const char *megaPath, idStr &error );
bool MegaTextureLoadTileTGA( const megaTextureProject_t &project, int x, int y, byte *rgba, idStr &error );
bool MegaTextureWriteTileTGA( const megaTextureProject_t &project, int x, int y, const byte *rgba, idStr &error );
bool MegaTextureLoadTerrainHeightfield( const megaTextureProject_t &project, std::vector<float> &heights, idStr &error );
bool MegaTextureWriteTerrainHeightfield( const megaTextureProject_t &project, const std::vector<float> &heights, idStr &error );
bool MegaTextureLoadTerrainWeights( const megaTextureProject_t &project, std::vector<byte> &weights,
	std::vector<megaTextureVertexTransform_t> &transforms, idStr &error );
bool MegaTextureWriteTerrainWeights( const megaTextureProject_t &project, const std::vector<byte> &weights,
	const std::vector<megaTextureVertexTransform_t> &transforms, idStr &error );
bool MegaTextureWriteTerrainModel( const megaTextureProject_t &project, idStr &error );

#endif
