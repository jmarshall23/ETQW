#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../../framework/FileSystem.h"
#include "../../../decllib/declAtmosphere.h"
#include "../../../renderer/megatexture/MegaTexture.h"
#include "../../../renderer/megatexture/MegaTextureCodec.h"
#include "../compiler_public.h"
#include "MegaTextureCompiler.h"
#include "MegaTextureShadowBaker.h"
#include "../../radiant/megatexture/RoadBuilder.h"

#include <vector>
#include <stdio.h>

void R_LoadImage( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool makePowerOf2 );

namespace {

static const int TERRAIN_HEIGHT_MAGIC = 1213482316; // little-endian "DLHT"
static const int TERRAIN_HEIGHT_VERSION = 1;
static const int TERRAIN_WEIGHT_MAGIC = 1415007300; // little-endian "DLWT"
static const int TERRAIN_WEIGHT_VERSION = 4;
static const float TERRAIN_TRANSFORM_MIN_SCALE = 0.25f;
static const float TERRAIN_TRANSFORM_MAX_SCALE = 512.0f;

struct megaTextureVertexTransformV2_t {
	unsigned short scaleX;
	unsigned short scaleY;
	unsigned short rotation;
};
static_assert( sizeof( megaTextureVertexTransformV2_t ) == 6, "legacy terrain paint transform disk layout changed" );

struct megaTextureVertexTransformV3_t {
	unsigned short scaleX;
	unsigned short scaleY;
	unsigned short rotation;
	unsigned short pivotU;
	unsigned short pivotV;
};
static_assert( sizeof( megaTextureVertexTransformV3_t ) == 10, "legacy terrain paint pivot disk layout changed" );

static bool IsPowerOfTwo( int value ) {
	return value > 0 && ( value & ( value - 1 ) ) == 0;
}

static bool WriteTextFile( const char *path, const idStr &text, idStr &error ) {
	idFile *file = fileSystem->OpenFileWrite( path, "fs_devpath" );
	if ( !file ) {
		error = va( "could not write %s", path );
		return false;
	}
	const bool okay = file->Write( text.c_str(), text.Length() ) == text.Length();
	fileSystem->CloseFile( file );
	if ( !okay ) error = va( "short write to %s", path );
	return okay;
}

static bool WriteTGA( const char *path, const byte *rgba, int width, int height, idStr &error ) {
	idFile *file = fileSystem->OpenFileWrite( path, "fs_devpath" );
	if ( !file ) {
		error = va( "could not write %s", path );
		return false;
	}
	byte header[18];
	memset( header, 0, sizeof( header ) );
	header[2] = 2;
	header[12] = width & 255;
	header[13] = width >> 8;
	header[14] = height & 255;
	header[15] = height >> 8;
	header[16] = 32;
	header[17] = 8 | 0x20;
	bool okay = file->Write( header, sizeof( header ) ) == sizeof( header );
	std::vector<byte> bgra( width * height * 4 );
	for ( int i = 0; i < width * height; ++i ) {
		bgra[i * 4 + 0] = rgba[i * 4 + 2];
		bgra[i * 4 + 1] = rgba[i * 4 + 1];
		bgra[i * 4 + 2] = rgba[i * 4 + 0];
		bgra[i * 4 + 3] = rgba[i * 4 + 3];
	}
	okay &= file->Write( bgra.data(), (int)bgra.size() ) == (int)bgra.size();
	fileSystem->CloseFile( file );
	if ( !okay ) error = va( "short write to %s", path );
	return okay;
}

static bool ReadTGA( const char *path, byte *rgba, int expectedWidth, int expectedHeight, idStr &error ) {
	void *fileData = NULL;
	const int length = fileSystem->ReadFile( path, &fileData, NULL );
	if ( length < 18 || !fileData ) {
		if ( fileData ) fileSystem->FreeFile( fileData );
		error = va( "could not read %s", path );
		return false;
	}
	const byte *data = (const byte *)fileData;
	const int idLength = data[0];
	const int imageType = data[2];
	const int width = data[12] | ( data[13] << 8 );
	const int height = data[14] | ( data[15] << 8 );
	const int bits = data[16];
	const int pixelBytes = bits / 8;
	const int pixelOffset = 18 + idLength;
	if ( imageType != 2 || width != expectedWidth || height != expectedHeight ||
		 ( bits != 24 && bits != 32 ) || pixelOffset + width * height * pixelBytes > length ) {
		fileSystem->FreeFile( fileData );
		error = va( "%s must be an uncompressed %dx%d, 24/32-bit TGA", path, expectedWidth, expectedHeight );
		return false;
	}
	const bool topOrigin = ( data[17] & 0x20 ) != 0;
	for ( int y = 0; y < height; ++y ) {
		const int sourceY = topOrigin ? y : height - 1 - y;
		for ( int x = 0; x < width; ++x ) {
			const byte *source = data + pixelOffset + ( sourceY * width + x ) * pixelBytes;
			byte *destination = rgba + ( y * width + x ) * 4;
			destination[0] = source[2];
			destination[1] = source[1];
			destination[2] = source[0];
			destination[3] = pixelBytes == 4 ? source[3] : 255;
		}
	}
	fileSystem->FreeFile( fileData );
	return true;
}

static void FillTile( const megaTextureProject_t &project, byte *tile ) {
	for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) {
		memcpy( tile + i * 4, project.fill, 4 );
		tile[i * 4 + 3] = 0x88;
	}
}

static bool MigrateLegacyTileAlpha( byte *tile ) {
	for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) {
		if ( tile[i * 4 + 3] != 255 ) return false;
	}
	for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) tile[i * 4 + 3] = 0x88;
	return true;
}

static bool LoadSourceTile( const megaTextureProject_t &project, int x, int y, byte *tile, idStr &error ) {
	const idStr path = project.TilePath( x, y );
	if ( fileSystem->ReadFile( path, NULL, NULL ) < 0 ) {
		FillTile( project, tile );
		return true;
	}
	const bool okay = ReadTGA( path, tile, MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE, error );
	if ( okay ) MigrateLegacyTileAlpha( tile );
	return okay;
}

static float DecodeNormalComponent( int value ) {
	return ( value - 8 ) / ( value < 8 ? 8.0f : 7.0f );
}

static int EncodeNormalComponent( float value ) {
	value = idMath::ClampFloat( -1.0f, 1.0f, value );
	return idMath::ClampInt( 0, 15, 8 + (int)( value * ( value < 0.0f ? 8.0f : 7.0f ) + ( value < 0.0f ? -0.5f : 0.5f ) ) );
}

static void DownsampleFourTiles( const byte *children[4], byte *parent ) {
	for ( int y = 0; y < MEGA_TEXTURE_TILE_SIZE; ++y ) {
		for ( int x = 0; x < MEGA_TEXTURE_TILE_SIZE; ++x ) {
			const int child = ( y >= 64 ? 2 : 0 ) + ( x >= 64 ? 1 : 0 );
			const int sourceX = ( x & 63 ) * 2;
			const int sourceY = ( y & 63 ) * 2;
			byte *out = parent + ( y * MEGA_TEXTURE_TILE_SIZE + x ) * 4;
			for ( int component = 0; component < 3; ++component ) {
				int sum = 0;
				for ( int oy = 0; oy < 2; ++oy ) for ( int ox = 0; ox < 2; ++ox ) {
					sum += children[child][( ( sourceY + oy ) * MEGA_TEXTURE_TILE_SIZE + sourceX + ox ) * 4 + component];
				}
				out[component] = (byte)( ( sum + 2 ) >> 2 );
			}
			float normalX = 0.0f, normalY = 0.0f, normalZ = 0.0f;
			for ( int oy = 0; oy < 2; ++oy ) for ( int ox = 0; ox < 2; ++ox ) {
				const byte packed = children[child][( ( sourceY + oy ) * MEGA_TEXTURE_TILE_SIZE + sourceX + ox ) * 4 + 3];
				const float nx = DecodeNormalComponent( packed & 15 );
				const float ny = DecodeNormalComponent( ( packed >> 4 ) & 15 );
				normalX += nx; normalY += ny; normalZ += idMath::Sqrt( Max( 0.0f, 1.0f - nx * nx - ny * ny ) );
			}
			const float normalLength = idMath::Sqrt( normalX * normalX + normalY * normalY + normalZ * normalZ );
			if ( normalLength > 0.0001f ) { normalX /= normalLength; normalY /= normalLength; }
			out[3] = (byte)( EncodeNormalComponent( normalX ) | ( EncodeNormalComponent( normalY ) << 4 ) );
		}
	}
}

static int TotalTiles( int resolution ) {
	int total = 0;
	for ( int axis = resolution / MEGA_TEXTURE_TILE_SIZE; axis >= MEGA_TEXTURE_TILES_PER_LEVEL; axis >>= 1 ) total += axis * axis;
	return total;
}

static bool ReplaceFile( const char *temporaryPath, const char *finalPath, idStr &error ) {
	const idStr tempOS = fileSystem->RelativePathToOSPath( temporaryPath, "fs_devpath" );
	const idStr finalOS = fileSystem->RelativePathToOSPath( finalPath, "fs_devpath" );
#ifdef _WIN32
	if ( !MoveFileExA( tempOS, finalOS, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) ) {
		error = va( "could not replace %s (Windows error %u)", finalPath, (unsigned int)GetLastError() );
		return false;
	}
#else
	if ( rename( tempOS, finalOS ) != 0 ) {
		error = va( "could not replace %s", finalPath );
		return false;
	}
#endif
	return true;
}

struct compilerState_t {
	struct sourceLayer_t {
		std::vector<byte> diffuse;
		std::vector<byte> normal;
		int diffuseWidth, diffuseHeight;
		int normalWidth, normalHeight;
		sourceLayer_t() : diffuseWidth( 0 ), diffuseHeight( 0 ), normalWidth( 0 ), normalHeight( 0 ) {}
	};

	megaTextureProject_t project;
	std::vector<byte> weights;
	std::vector<megaTextureVertexTransform_t> transforms;
	std::vector<float> heights;
	std::vector<float> shadowVisibility;
	std::vector<float> staticShadowVisibility;
	int staticShadowResolution;
	sourceLayer_t sourceLayers[megaTextureProject_t::MAX_LAYERS];
	MegaTextureRoadBuilder roads;
	std::vector<sourceLayer_t> roadSources;
	int levels;
	int totalTiles;
	std::vector<int> levelBase;
	std::vector<int> offsets;
	std::vector<int> sizes;
	std::vector<int> maximumSizes;
	std::vector<idStr> rawLevels;
	idBareDctEncoder encoder;
	bool bakeLighting;
	bool terrainShadows;
	idVec3 sunDirection;
	idVec3 sunColor;
	idVec3 ambientColor;
	std::vector<idVec3> ambientDirections;
	std::vector<idVec3> ambientLightColors;
	float directScale;

	compilerState_t() : staticShadowResolution( 0 ), bakeLighting( false ), terrainShadows( true ),
		sunDirection( 0.0f, 0.0f, 1.0f ), sunColor( 1.0f, 1.0f, 1.0f ),
		ambientColor( 1.0f, 1.0f, 1.0f ), directScale( 1.0f ) {}
};

static bool LoadLayerImage( const char *path, std::vector<byte> &pixels, int &width, int &height ) {
	byte *image = NULL;
	width = height = 0;
	R_LoadImage( path, &image, &width, &height, NULL, false );
	if ( !image || width <= 0 || height <= 0 ) {
		if ( image ) Mem_Free( image );
		pixels.clear(); width = height = 0;
		return false;
	}
	pixels.assign( image, image + width * height * 4 );
	Mem_Free( image );
	return true;
}

static idStr LayerNormalPath( const idStr &diffuse ) {
	if ( diffuse.Length() > 6 && !diffuse.Right( 6 ).Icmp( "_d.tga" ) ) return diffuse.Left( diffuse.Length() - 6 ) + "_local.tga";
	return "";
}

static bool PrepareLayerSources( compilerState_t &state, idStr &error ) {
	if ( !MegaTextureLoadTerrainWeights( state.project, state.weights, state.transforms, error ) ) {
		common->Warning( "MegaTexture: %s; using layer 1 for every terrain vertex", error.c_str() );
		state.weights.assign( state.project.terrainSamples * state.project.terrainSamples * megaTextureProject_t::MAX_LAYERS, 0 );
		for ( int i = 0; i < state.project.terrainSamples * state.project.terrainSamples; ++i ) state.weights[i * megaTextureProject_t::MAX_LAYERS] = 255;
		MegaTextureInitializeTerrainTransforms( state.project, state.transforms );
		error.Clear();
	}
	for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) {
		if ( state.project.layers[layer].IsEmpty() ) continue;
		compilerState_t::sourceLayer_t &source = state.sourceLayers[layer];
		if ( !LoadLayerImage( state.project.layers[layer], source.diffuse, source.diffuseWidth, source.diffuseHeight ) ) {
			error = va( "could not load terrain layer %d diffuse image %s", layer + 1, state.project.layers[layer].c_str() );
			return false;
		}
		const idStr normalPath = LayerNormalPath( state.project.layers[layer] );
		if ( !normalPath.IsEmpty() ) LoadLayerImage( normalPath, source.normal, source.normalWidth, source.normalHeight );
	}
	if ( !state.roads.Load( state.project.roadFile, error ) ) return false;
	state.roadSources.resize( state.roads.NumRoads() );
	for ( int roadIndex = 0; roadIndex < state.roads.NumRoads(); ++roadIndex ) {
		const megaTextureRoad_t &road = state.roads.GetRoad( roadIndex );
		if ( !road.enabled || road.points.size() < 2 ) continue;
		if ( road.texture.IsEmpty() ) {
			error = va( "road %d (%s) has no texture", roadIndex + 1, road.name.c_str() );
			return false;
		}
		compilerState_t::sourceLayer_t &source = state.roadSources[roadIndex];
		if ( !LoadLayerImage( road.texture, source.diffuse, source.diffuseWidth, source.diffuseHeight ) ) {
			error = va( "could not load road %d diffuse image %s", roadIndex + 1, road.texture.c_str() );
			return false;
		}
		const idStr normalPath = LayerNormalPath( road.texture );
		if ( !normalPath.IsEmpty() ) LoadLayerImage( normalPath, source.normal, source.normalWidth, source.normalHeight );
	}
	return true;
}

static float SampleTerrainHeight( const compilerState_t &state, float gridX, float gridY ) {
	const int samples = state.project.terrainSamples;
	gridX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), gridX );
	gridY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), gridY );
	const int x0 = (int)floorf( gridX );
	const int y0 = (int)floorf( gridY );
	const int x1 = Min( x0 + 1, samples - 1 );
	const int y1 = Min( y0 + 1, samples - 1 );
	const float fx = gridX - x0;
	const float fy = gridY - y0;
	const float top = state.heights[y0 * samples + x0] * ( 1.0f - fx ) + state.heights[y0 * samples + x1] * fx;
	const float bottom = state.heights[y1 * samples + x0] * ( 1.0f - fx ) + state.heights[y1 * samples + x1] * fx;
	return top * ( 1.0f - fy ) + bottom * fy;
}

static void TerrainSurfaceBasis( const compilerState_t &state, float u, float v,
		idVec3 &tangent, idVec3 &bitangent, idVec3 &normal ) {
	const int samples = state.project.terrainSamples;
	const float gridX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), u * ( samples - 1 ) );
	const float gridY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), v * ( samples - 1 ) );
	const float spacing = state.project.terrainSize / ( samples - 1 );
	const float left = SampleTerrainHeight( state, gridX - 1.0f, gridY );
	const float right = SampleTerrainHeight( state, gridX + 1.0f, gridY );
	const float top = SampleTerrainHeight( state, gridX, gridY - 1.0f );
	const float bottom = SampleTerrainHeight( state, gridX, gridY + 1.0f );
	const float dhdx = ( right - left ) / ( 2.0f * spacing );
	// Height rows advance toward world -Y.
	const float dhdy = -( bottom - top ) / ( 2.0f * spacing );
	normal.Set( -dhdx, -dhdy, 1.0f );
	normal.Normalize();
	tangent.Set( 1.0f, 0.0f, dhdx );
	tangent.Normalize();
	bitangent = normal.Cross( tangent );
	bitangent.Normalize();
}

static void BuildTerrainShadowVisibility( compilerState_t &state ) {
	const int samples = state.project.terrainSamples;
	state.shadowVisibility.assign( samples * samples, 1.0f );
	if ( !state.terrainShadows || state.sunDirection.z <= 0.0f ) {
		if ( state.sunDirection.z <= 0.0f ) {
			std::fill( state.shadowVisibility.begin(), state.shadowVisibility.end(), 0.0f );
		}
		return;
	}
	const float horizontalLength = idMath::Sqrt( state.sunDirection.x * state.sunDirection.x +
		state.sunDirection.y * state.sunDirection.y );
	if ( horizontalLength <= 0.0001f ) {
		return;
	}

	const float spacing = state.project.terrainSize / ( samples - 1 );
	// Cap each horizon test to roughly 128 samples. This cost depends on the
	// authoring heightfield, not on a potentially 32768-square output image.
	const float stepDistance = Max( spacing, state.project.terrainSize / 128.0f );
	const float verticalPerHorizontal = state.sunDirection.z / horizontalLength;
	const float directionX = state.sunDirection.x / horizontalLength;
	const float directionY = state.sunDirection.y / horizontalLength;
	const float heightBias = Max( 0.5f, spacing * 0.02f );
	const int maximumSteps = (int)ceilf( state.project.terrainSize * 1.5f / stepDistance );
	for ( int y = 0; y < samples; ++y ) {
		for ( int x = 0; x < samples; ++x ) {
			const float startHeight = state.heights[y * samples + x];
			for ( int step = 1; step <= maximumSteps; ++step ) {
				const float distance = step * stepDistance;
				const float sampleX = x + directionX * distance / spacing;
				const float sampleY = y - directionY * distance / spacing;
				if ( sampleX < 0.0f || sampleY < 0.0f || sampleX > samples - 1 || sampleY > samples - 1 ) {
					break;
				}
				const float rayHeight = startHeight + verticalPerHorizontal * distance;
				if ( SampleTerrainHeight( state, sampleX, sampleY ) > rayHeight + heightBias ) {
					state.shadowVisibility[y * samples + x] = 0.0f;
					break;
				}
			}
		}
	}
}

static float SampleStaticShadowVisibility( const compilerState_t &state, float u, float v ) {
	if ( state.staticShadowVisibility.empty() || state.staticShadowResolution < 2 ) return 1.0f;
	const int resolution = state.staticShadowResolution;
	const float gridX = idMath::ClampFloat( 0.0f, (float)( resolution - 1 ), u * ( resolution - 1 ) );
	const float gridY = idMath::ClampFloat( 0.0f, (float)( resolution - 1 ), v * ( resolution - 1 ) );
	const int x0 = (int)floorf( gridX ), y0 = (int)floorf( gridY );
	const int x1 = Min( x0 + 1, resolution - 1 ), y1 = Min( y0 + 1, resolution - 1 );
	const float fractionX = gridX - x0, fractionY = gridY - y0;
	const float row0 = state.staticShadowVisibility[y0 * resolution + x0] * ( 1.0f - fractionX ) +
		state.staticShadowVisibility[y0 * resolution + x1] * fractionX;
	const float row1 = state.staticShadowVisibility[y1 * resolution + x0] * ( 1.0f - fractionX ) +
		state.staticShadowVisibility[y1 * resolution + x1] * fractionX;
	return row0 * ( 1.0f - fractionY ) + row1 * fractionY;
}

static bool PrepareLightingBake( compilerState_t &state, const idDict *worldSpawnOverride, idStr &error ) {
	if ( !state.bakeLighting ) {
		return true;
	}
	if ( state.project.mapName.IsEmpty() ) {
		error = "the terrain project has no owning map; save the level before baking";
		return false;
	}
	idMapFile mapFile;
	// The terrain project belongs to the full saved level. Do not let a stale
	// editor .reg file substitute for its worldspawn lighting settings.
	if ( !mapFile.Parse( state.project.mapName, true, false ) || mapFile.GetNumEntities() <= 0 ) {
		error = va( "could not load owning map %s for atmosphere bake", state.project.mapName.c_str() );
		return false;
	}
	const idMapEntity *world = mapFile.GetEntity( 0 );
	const idDict &worldSettings = worldSpawnOverride ? *worldSpawnOverride : world->epairs;
	const char *atmosphereName = worldSettings.GetString( "atmosphere", "" );
	if ( !atmosphereName[0] ) atmosphereName = worldSettings.GetString( "atmospheredecl", "" );
	// Old maps can still bake while they are being migrated to worldspawn.
	if ( !atmosphereName[0] ) {
		for ( int entityIndex = 1; entityIndex < mapFile.GetNumEntities(); ++entityIndex ) {
			const idMapEntity *candidate = mapFile.GetEntity( entityIndex );
			if ( !idStr::Icmp( candidate->epairs.GetString( "classname", "" ), "atmosphere" ) ) {
				atmosphereName = candidate->epairs.GetString( "atmospheredecl", "" );
				break;
			}
		}
	}
	if ( !atmosphereName[0] ) {
		error = "worldspawn has no atmosphere; assign one in Entity properties before MegaTexture Bake";
		return false;
	}
	const sdDeclAtmosphere *atmosphere = declHolder.FindAtmosphere( atmosphereName, false );
	if ( !atmosphere ) {
		error = va( "worldspawn references missing atmosphere %s", atmosphereName );
		return false;
	}
	if ( !MegaTextureLoadTerrainHeightfield( state.project, state.heights, error ) ) {
		return false;
	}
	state.sunDirection = atmosphere->GetSunDirection();
	if ( state.sunDirection.Normalize() == 0.0f ) state.sunDirection.Set( 0.0f, 0.0f, 1.0f );
	state.sunColor = atmosphere->GetSunColor() * Max( 0.0f, worldSettings.GetFloat( "atmosphereSunScale", "1" ) );
	state.ambientColor.Set( 0.025f, 0.025f, 0.025f );
	if ( atmosphere->GetAmbientCubeMap() ) {
		const sdDeclAmbientCubeMap *ambientCube = atmosphere->GetAmbientCubeMap();
		const float ambientScale = Max( 0.0f, worldSettings.GetFloat( "atmosphereAmbientScale", "1" ) ) *
			Max( 0.0f, worldSettings.GetFloat( "megaBakeAmbientScale", "1" ) ) * Max( 0.0f, ambientCube->GetBrightness() );
		state.ambientColor = ambientCube->GetAmbientColor() * ambientScale;
		const idList<sdDeclAmbientCubeMap::ambientLight_t> &ambientLights = ambientCube->GetAmbientLights();
		for ( int lightIndex = 0; lightIndex < ambientLights.Num(); ++lightIndex ) {
			if ( !ambientLights[lightIndex].ambient ) continue;
			idVec3 direction = ambientLights[lightIndex].dir;
			if ( direction.Normalize() == 0.0f ) continue;
			state.ambientDirections.push_back( direction );
			state.ambientLightColors.push_back( ambientLights[lightIndex].color * ambientScale );
		}
	} else {
		state.ambientColor *= Max( 0.0f, worldSettings.GetFloat( "atmosphereAmbientScale", "1" ) ) *
			Max( 0.0f, worldSettings.GetFloat( "megaBakeAmbientScale", "1" ) );
	}
	// MegaTexture illumination is a separate bake. dmap's lightmapDirectScale
	// belongs to the atlas path and must not overexpose the streamed terrain.
	state.directScale = Max( 0.0f, worldSettings.GetFloat( "megaBakeSunScale", "1" ) );
	state.terrainShadows = worldSettings.GetBool( "atmosphereBakeShadows", "1" ) &&
		worldSettings.GetBool( "megaBakeShadows", "1" );
	BuildTerrainShadowVisibility( state );
	int staticShadowEntities = 0;
	int staticShadowTriangles = 0;
	const bool staticModelShadows = state.terrainShadows && worldSettings.GetBool( "megaBakeStaticShadows", "1" );
	if ( staticModelShadows ) {
		// A grid finer than the authoring heightfield preserves recognizable tree
		// and foliage shadows without tracing once per final MegaTexture texel.
		state.staticShadowResolution = idMath::ClampInt( state.project.terrainSamples,
			1025, state.project.resolution / 8 + 1 );
		if ( !MegaTextureBuildStaticModelShadows( mapFile, state.project, state.heights,
			state.sunDirection, state.staticShadowResolution, state.staticShadowVisibility,
			staticShadowEntities, staticShadowTriangles, error ) ) return false;
	}
	common->Printf( "MegaTexture: baking atmosphere '%s', sun %s, ambient base %s + %d directional lights, terrain shadows %s\n",
		atmosphereName, state.sunDirection.ToString(), state.ambientColor.ToString(), (int)state.ambientDirections.size(),
		state.terrainShadows ? "on" : "off" );
	common->Printf( "MegaTexture: %d static model shadow caster%s, %d alpha-aware triangles, %dx%d visibility grid\n",
		staticShadowEntities, staticShadowEntities == 1 ? "" : "s", staticShadowTriangles,
		state.staticShadowResolution, state.staticShadowResolution );
	return true;
}

static void SampleWrappedRGBA( const std::vector<byte> &pixels, int width, int height, float u, float v, float out[4] ) {
	if ( pixels.empty() || width <= 0 || height <= 0 ) { out[0] = out[1] = 128.0f; out[2] = out[3] = 255.0f; return; }
	u -= floorf( u ); v -= floorf( v );
	const float px = u * width - 0.5f, py = v * height - 0.5f;
	const int ix = (int)floorf( px ), iy = (int)floorf( py );
	const float fx = px - ix, fy = py - iy;
	const int x0 = ( ( ix % width ) + width ) % width, x1 = ( x0 + 1 ) % width;
	const int y0 = ( ( iy % height ) + height ) % height, y1 = ( y0 + 1 ) % height;
	for ( int component = 0; component < 4; ++component ) {
		const float top = pixels[( y0 * width + x0 ) * 4 + component] * ( 1.0f - fx ) + pixels[( y0 * width + x1 ) * 4 + component] * fx;
		const float bottom = pixels[( y1 * width + x0 ) * 4 + component] * ( 1.0f - fx ) + pixels[( y1 * width + x1 ) * 4 + component] * fx;
		out[component] = top * ( 1.0f - fy ) + bottom * fy;
	}
}

struct terrainTriangleSample_t {
	int vertex[3];
	float weight[3];
};

static terrainTriangleSample_t TerrainTriangleSample( const compilerState_t &state, float u, float v ) {
	const int samples = state.project.terrainSamples;
	const float px = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), u * ( samples - 1 ) );
	const float py = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), v * ( samples - 1 ) );
	const int x0 = (int)floorf( px ), y0 = (int)floorf( py );
	const int x1 = Min( x0 + 1, samples - 1 ), y1 = Min( y0 + 1, samples - 1 );
	const float fx = px - x0, fy = py - y0;
	terrainTriangleSample_t result;
	if ( fx >= fy ) {
		result.vertex[0] = y0 * samples + x0;
		result.vertex[1] = y0 * samples + x1;
		result.vertex[2] = y1 * samples + x1;
		result.weight[0] = 1.0f - fx;
		result.weight[1] = fx - fy;
		result.weight[2] = fy;
	} else {
		result.vertex[0] = y0 * samples + x0;
		result.vertex[1] = y1 * samples + x1;
		result.vertex[2] = y1 * samples + x0;
		result.weight[0] = 1.0f - fy;
		result.weight[1] = fx;
		result.weight[2] = fy - fx;
	}
	return result;
}

static void SampleVertexWeights( const compilerState_t &state, const terrainTriangleSample_t &sample,
	float out[megaTextureProject_t::MAX_LAYERS] ) {
	float total = 0.0f;
	for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) {
		out[layer] = 0.0f;
		for ( int corner = 0; corner < 3; ++corner ) {
			out[layer] += state.weights[sample.vertex[corner] * megaTextureProject_t::MAX_LAYERS + layer] * sample.weight[corner];
		}
		if ( state.sourceLayers[layer].diffuse.empty() ) out[layer] = 0.0f;
		total += out[layer];
	}
	if ( total <= 0.001f ) { out[0] = 1.0f; for ( int layer = 1; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) out[layer] = 0.0f; }
	else for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) out[layer] /= total;
}

static void SampleLayerTexCoord( const compilerState_t &state, const terrainTriangleSample_t &sample, int layer,
	float &outU, float &outV, float &outRotation ) {
	float scaleX = 0.0f, scaleY = 0.0f, pivotU = 0.0f, pivotV = 0.0f;
	float rotationCosine = 0.0f, rotationSine = 0.0f;
	float phaseUCosine = 0.0f, phaseUSine = 0.0f, phaseVCosine = 0.0f, phaseVSine = 0.0f;
	for ( int corner = 0; corner < 3; ++corner ) {
		// Keep one rigid mapping over the whole terrain triangle. Interpolating
		// rotation at every output pixel bends the UV field and produces severe
		// smearing at 90-degree paint boundaries.
		const float mappingWeight = 1.0f / 3.0f;
		const int vertex = sample.vertex[corner];
		const megaTextureVertexTransform_t &transform = state.transforms[vertex * megaTextureProject_t::MAX_LAYERS + layer];
		float cornerScaleX, cornerScaleY, rotation, cornerPivotU, cornerPivotV, cornerPhaseU, cornerPhaseV;
		MegaTextureDecodeVertexTransform( transform, cornerScaleX, cornerScaleY, rotation,
			cornerPivotU, cornerPivotV, cornerPhaseU, cornerPhaseV );
		scaleX += cornerScaleX * mappingWeight;
		scaleY += cornerScaleY * mappingWeight;
		pivotU += cornerPivotU * mappingWeight;
		pivotV += cornerPivotV * mappingWeight;
		const float radians = rotation * idMath::M_DEG2RAD;
		rotationCosine += idMath::Cos( radians ) * mappingWeight;
		rotationSine += idMath::Sin( radians ) * mappingWeight;
		phaseUCosine += idMath::Cos( cornerPhaseU * idMath::TWO_PI ) * mappingWeight;
		phaseUSine += idMath::Sin( cornerPhaseU * idMath::TWO_PI ) * mappingWeight;
		phaseVCosine += idMath::Cos( cornerPhaseV * idMath::TWO_PI ) * mappingWeight;
		phaseVSine += idMath::Sin( cornerPhaseV * idMath::TWO_PI ) * mappingWeight;
	}
	outRotation = idMath::ATan( rotationSine, rotationCosine ) * idMath::M_RAD2DEG;
	float phaseU = idMath::ATan( phaseUSine, phaseUCosine ) / idMath::TWO_PI;
	float phaseV = idMath::ATan( phaseVSine, phaseVCosine ) / idMath::TWO_PI;
	if ( phaseU < 0.0f ) phaseU += 1.0f;
	if ( phaseV < 0.0f ) phaseV += 1.0f;
	const megaTextureVertexTransform_t interpolated = MegaTextureEncodeVertexTransform( scaleX, scaleY, outRotation, pivotU, pivotV, phaseU, phaseV );
	const int samples = state.project.terrainSamples;
	float sampleU = 0.0f, sampleV = 0.0f;
	for ( int corner = 0; corner < 3; ++corner ) {
		const int vertex = sample.vertex[corner];
		sampleU += ( vertex % samples ) / (float)( samples - 1 ) * sample.weight[corner];
		sampleV += ( vertex / samples ) / (float)( samples - 1 ) * sample.weight[corner];
	}
	MegaTextureTransformTexCoord( interpolated, sampleU, sampleV, outU, outV );
}

static void GenerateLayerPixel( const compilerState_t &state, float u, float v, byte *pixel,
	const std::vector<int> *activeRoads = NULL ) {
	const terrainTriangleSample_t sample = TerrainTriangleSample( state, u, v );
	float weights[megaTextureProject_t::MAX_LAYERS];
	SampleVertexWeights( state, sample, weights );
	float color[3] = { 0.0f, 0.0f, 0.0f };
	float normal[3] = { 0.0f, 0.0f, 0.0f };
	for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) {
		if ( weights[layer] <= 0.0f ) continue;
		const compilerState_t::sourceLayer_t &source = state.sourceLayers[layer];
		float diffuse[4], local[4];
		float layerU, layerV, layerRotation;
		SampleLayerTexCoord( state, sample, layer, layerU, layerV, layerRotation );
		SampleWrappedRGBA( source.diffuse, source.diffuseWidth, source.diffuseHeight, layerU, layerV, diffuse );
		for ( int component = 0; component < 3; ++component ) color[component] += diffuse[component] * weights[layer];
		if ( source.normal.empty() ) { local[0] = local[1] = 128.0f; local[2] = local[3] = 255.0f; }
		else SampleWrappedRGBA( source.normal, source.normalWidth, source.normalHeight, layerU, layerV, local );
		const float sampledX = local[0] / 127.5f - 1.0f;
		const float sampledY = local[1] / 127.5f - 1.0f;
		const float radians = layerRotation * idMath::M_DEG2RAD;
		const float cosine = idMath::Cos( radians ), sine = idMath::Sin( radians );
		// UVs rotate the sampled pattern into terrain space by the inverse angle;
		// rotate its tangent-space XY direction identically before packing.
		const float nx = sampledX * cosine + sampledY * sine;
		const float ny = -sampledX * sine + sampledY * cosine;
		const float nz = idMath::Sqrt( Max( 0.0f, 1.0f - nx * nx - ny * ny ) );
		normal[0] += nx * weights[layer]; normal[1] += ny * weights[layer]; normal[2] += nz * weights[layer];
	}
	// Heightfield rows increase toward local -Y, while terrain texture T
	// increases toward local +Y. Move the blended layer normal into the actual
	// surface S/T basis before road normals are composited.
	normal[1] = -normal[1];
	const idVec2 terrainPoint( u * state.project.terrainSize - state.project.terrainSize * 0.5f,
		state.project.terrainSize * 0.5f - v * state.project.terrainSize );
	const int roadCount = activeRoads ? (int)activeRoads->size() : state.roads.NumRoads();
	for ( int activeRoad = 0; activeRoad < roadCount; ++activeRoad ) {
		const int roadIndex = activeRoads ? ( *activeRoads )[activeRoad] : activeRoad;
		if ( roadIndex >= (int)state.roadSources.size() || state.roadSources[roadIndex].diffuse.empty() ) continue;
		megaTextureRoadSample_t roadSample;
		if ( !state.roads.SampleRoad( roadIndex, terrainPoint, roadSample ) ) continue;
		const compilerState_t::sourceLayer_t &source = state.roadSources[roadIndex];
		float diffuse[4], local[4];
		SampleWrappedRGBA( source.diffuse, source.diffuseWidth, source.diffuseHeight, roadSample.u, roadSample.v, diffuse );
		const float alpha = idMath::ClampFloat( 0.0f, 1.0f, roadSample.alpha * diffuse[3] / 255.0f );
		if ( alpha <= 0.0f ) continue;
		for ( int component = 0; component < 3; ++component ) color[component] = color[component] * ( 1.0f - alpha ) + diffuse[component] * alpha;
		if ( source.normal.empty() ) { local[0] = local[1] = 128.0f; local[2] = local[3] = 255.0f; }
		else SampleWrappedRGBA( source.normal, source.normalWidth, source.normalHeight, roadSample.u, roadSample.v, local );
		const float sampledU = local[0] / 127.5f - 1.0f;
		const float sampledV = local[1] / 127.5f - 1.0f;
		// Road U runs across the spline and V runs forward. Convert those axes
		// into the MegaTexture's terrain U/V normal coordinate system.
		const float roadNormalX = sampledU * -roadSample.tangentY + sampledV * roadSample.tangentX;
		const float roadNormalY = sampledU * roadSample.tangentX + sampledV * roadSample.tangentY;
		const float roadNormalZ = idMath::Sqrt( Max( 0.0f, 1.0f - roadNormalX * roadNormalX - roadNormalY * roadNormalY ) );
		normal[0] = normal[0] * ( 1.0f - alpha ) + roadNormalX * alpha;
		normal[1] = normal[1] * ( 1.0f - alpha ) + roadNormalY * alpha;
		normal[2] = normal[2] * ( 1.0f - alpha ) + roadNormalZ * alpha;
	}
	const float length = idMath::Sqrt( normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2] );
	if ( length > 0.0001f ) {
		normal[0] /= length; normal[1] /= length; normal[2] /= length;
	} else {
		normal[0] = normal[1] = 0.0f; normal[2] = 1.0f;
	}
	if ( state.bakeLighting ) {
		idVec3 tangent, bitangent, surfaceNormal;
		TerrainSurfaceBasis( state, u, v, tangent, bitangent, surfaceNormal );
		idVec3 worldNormal = tangent * normal[0] + bitangent * normal[1] + surfaceNormal * normal[2];
		worldNormal.Normalize();
		float visibility = 1.0f;
		if ( !state.shadowVisibility.empty() ) {
			visibility = 0.0f;
			for ( int corner = 0; corner < 3; ++corner ) {
				visibility += state.shadowVisibility[sample.vertex[corner]] * sample.weight[corner];
			}
		}
		visibility *= SampleStaticShadowVisibility( state, u, v );
		idVec3 ambientLighting = state.ambientColor;
		for ( int lightIndex = 0; lightIndex < (int)state.ambientDirections.size(); ++lightIndex ) {
			ambientLighting += state.ambientLightColors[lightIndex] * Max( 0.0f, worldNormal * state.ambientDirections[lightIndex] );
		}
		const float lambert = Max( 0.0f, worldNormal * state.sunDirection );
		const idVec3 lighting = ambientLighting + state.sunColor * ( state.directScale * lambert * visibility );
		for ( int component = 0; component < 3; ++component ) color[component] *= Max( 0.0f, lighting[component] );
	}
	for ( int component = 0; component < 3; ++component ) pixel[component] =
		(byte)idMath::ClampInt( 0, 255, (int)( color[component] + 0.5f ) );
	pixel[3] = (byte)( EncodeNormalComponent( normal[0] ) | ( EncodeNormalComponent( normal[1] ) << 4 ) );
}

static bool LoadCompiledSourceTile( const compilerState_t &state, int tileX, int tileY, byte *tile, idStr &error ) {
	if ( state.weights.empty() ) return LoadSourceTile( state.project, tileX, tileY, tile, error );
	const float minimumU = tileX * MEGA_TEXTURE_TILE_SIZE / (float)state.project.resolution;
	const float maximumU = ( tileX + 1 ) * MEGA_TEXTURE_TILE_SIZE / (float)state.project.resolution;
	const float minimumTextureV = tileY * MEGA_TEXTURE_TILE_SIZE / (float)state.project.resolution;
	const float maximumTextureV = ( tileY + 1 ) * MEGA_TEXTURE_TILE_SIZE / (float)state.project.resolution;
	const float minimumTerrainV = 1.0f - maximumTextureV;
	const float maximumTerrainV = 1.0f - minimumTextureV;
	const idVec2 tileMinimum( minimumU * state.project.terrainSize - state.project.terrainSize * 0.5f,
		state.project.terrainSize * 0.5f - maximumTerrainV * state.project.terrainSize );
	const idVec2 tileMaximum( maximumU * state.project.terrainSize - state.project.terrainSize * 0.5f,
		state.project.terrainSize * 0.5f - minimumTerrainV * state.project.terrainSize );
	std::vector<int> activeRoads;
	for ( int roadIndex = 0; roadIndex < state.roads.NumRoads(); ++roadIndex ) {
		if ( state.roads.RoadIntersectsBounds( roadIndex, tileMinimum, tileMaximum ) ) activeRoads.push_back( roadIndex );
	}
	for ( int y = 0; y < MEGA_TEXTURE_TILE_SIZE; ++y ) for ( int x = 0; x < MEGA_TEXTURE_TILE_SIZE; ++x ) {
		const float u = ( tileX * MEGA_TEXTURE_TILE_SIZE + x + 0.5f ) / state.project.resolution;
		const float v = 1.0f - ( tileY * MEGA_TEXTURE_TILE_SIZE + y + 0.5f ) / state.project.resolution;
		GenerateLayerPixel( state, u, v, tile + ( y * MEGA_TEXTURE_TILE_SIZE + x ) * 4, &activeRoads );
	}
	return true;
}

static bool WriteLayerPreview( const compilerState_t &state, idStr &error ) {
	const int previewSize = 256;
	std::vector<byte> preview( previewSize * previewSize * 4 );
	for ( int y = 0; y < previewSize; ++y ) for ( int x = 0; x < previewSize; ++x ) {
		byte *pixel = preview.data() + ( y * previewSize + x ) * 4;
		GenerateLayerPixel( state, ( x + 0.5f ) / previewSize, 1.0f - ( y + 0.5f ) / previewSize, pixel );
		pixel[3] = 255;
	}
	return WriteTGA( state.project.PreviewPath(), preview.data(), previewSize, previewSize, error );
}

static bool BuildRawLevels( compilerState_t &state, idStr &error ) {
	state.rawLevels.resize( state.levels );
	const int tileBytes = MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4;
	std::vector<byte> childStorage( tileBytes * 4 );
	std::vector<byte> parent( tileBytes );
	for ( int level = 1; level < state.levels; ++level ) {
		const int axis = ( state.project.resolution / MEGA_TEXTURE_TILE_SIZE ) >> level;
		const idStr rawPath = va( "%s/.%s_level_%d.raw", state.project.sourceDirectory.c_str(), state.project.name.c_str(), level );
		state.rawLevels[level] = rawPath;
		idFile *output = fileSystem->OpenFileWrite( rawPath, "fs_devpath" );
		idFile *previous = level > 1 ? fileSystem->OpenFileRead( state.rawLevels[level - 1] ) : NULL;
		if ( !output || ( level > 1 && !previous ) ) {
			if ( output ) fileSystem->CloseFile( output );
			if ( previous ) fileSystem->CloseFile( previous );
			error = va( "could not create intermediate mip level %d", level );
			return false;
		}
		for ( int y = 0; y < axis; ++y ) {
			for ( int x = 0; x < axis; ++x ) {
				const byte *children[4];
				for ( int child = 0; child < 4; ++child ) {
					byte *destination = childStorage.data() + child * tileBytes;
					const int childX = x * 2 + ( child & 1 );
					const int childY = y * 2 + ( child >> 1 );
					if ( level == 1 ) {
						if ( !LoadCompiledSourceTile( state, childX, childY, destination, error ) ) {
							fileSystem->CloseFile( output );
							return false;
						}
					} else {
						const int childAxis = axis * 2;
						previous->Seek( ( childY * childAxis + childX ) * tileBytes, FS_SEEK_SET );
						if ( previous->Read( destination, tileBytes ) != tileBytes ) {
							error = va( "short read building mip level %d", level );
							fileSystem->CloseFile( previous );
							fileSystem->CloseFile( output );
							return false;
						}
					}
					children[child] = destination;
				}
				DownsampleFourTiles( children, parent.data() );
				if ( output->Write( parent.data(), tileBytes ) != tileBytes ) {
					error = va( "short write building mip level %d", level );
					if ( previous ) fileSystem->CloseFile( previous );
					fileSystem->CloseFile( output );
					return false;
				}
			}
		}
		if ( previous ) fileSystem->CloseFile( previous );
		fileSystem->CloseFile( output );
		common->Printf( "MegaTexture: built source mip %d (%dx%d tiles)\n", level, axis, axis );
	}
	return true;
}

static bool WriteCompressedTile( compilerState_t &state, idFile *output, int level, int x, int y,
								 byte *tile, std::vector<byte> &compressed, idStr &error ) {
	int compressedBytes = 0;
	const int alphaQuality = Max( state.project.quality[2], 90 );
	state.encoder.SetQuality( state.project.quality[0], state.project.quality[1], alphaQuality );
	if ( !state.encoder.CompressImageRGBA( tile, compressed.data(), MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE,
										 (int)compressed.size(), compressedBytes ) ) {
		error = va( "DCT encoder overflow at level %d tile %d,%d", level, x, y );
		return false;
	}
	const int logicalIndex = state.levelBase[level] + y * ( ( state.project.resolution / MEGA_TEXTURE_TILE_SIZE ) >> level ) + x;
	state.offsets[logicalIndex] = output->Tell();
	state.sizes[logicalIndex] = compressedBytes;
	if ( compressedBytes + 3 > state.maximumSizes[level] ) state.maximumSizes[level] = compressedBytes + 3;
	byte quality[3] = { (byte)state.project.quality[0], (byte)state.project.quality[1], (byte)alphaQuality };
	if ( output->Write( quality, 3 ) != 3 || output->Write( compressed.data(), compressedBytes ) != compressedBytes ) {
		error = "short write while writing compressed tiles";
		return false;
	}
	if ( output->Tell() < 0 || output->Tell() >= 0x7fffffff ) {
		error = "compiled MegaTexture exceeds the 2 GB v9 file limit";
		return false;
	}
	return true;
}

static void RemoveRawLevels( const compilerState_t &state ) {
	for ( int level = 1; level < (int)state.rawLevels.size(); ++level ) {
		if ( !state.rawLevels[level].IsEmpty() ) fileSystem->RemoveFile( state.rawLevels[level] );
	}
}

static bool Compile( compilerState_t &state, idStr &error ) {
	if ( !BuildRawLevels( state, error ) ) {
		RemoveRawLevels( state );
		return false;
	}
	const idStr outputPath = state.project.OutputPath();
	const idStr temporaryPath = outputPath + ".tmp";
	idFile *output = fileSystem->OpenFileWrite( temporaryPath, "fs_devpath" );
	if ( !output ) {
		error = va( "could not write %s", temporaryPath.c_str() );
		RemoveRawLevels( state );
		return false;
	}
	const int headerBytes = 16 + state.levels * 8 + state.totalTiles * 8;
	std::vector<byte> emptyHeader( headerBytes, 0 );
	if ( output->Write( emptyHeader.data(), headerBytes ) != headerBytes ) {
		error = "could not reserve MegaTexture header";
		fileSystem->CloseFile( output );
		RemoveRawLevels( state );
		return false;
	}
	std::vector<byte> tile( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4 );
	std::vector<byte> compressed( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 8 );
	bool okay = true;
	// The loader expects coarse levels first, with level-one parents followed by
	// their four level-zero children for seek-friendly streaming.
	for ( int level = state.levels - 1; okay && level >= 2; --level ) {
		const int axis = ( state.project.resolution / MEGA_TEXTURE_TILE_SIZE ) >> level;
		idFile *raw = fileSystem->OpenFileRead( state.rawLevels[level] );
		if ( !raw ) { error = va( "could not read mip level %d", level ); okay = false; break; }
		for ( int y = 0; okay && y < axis; ++y ) for ( int x = 0; okay && x < axis; ++x ) {
			if ( raw->Read( tile.data(), (int)tile.size() ) != (int)tile.size() ) { error = "short mip read"; okay = false; break; }
			okay = WriteCompressedTile( state, output, level, x, y, tile.data(), compressed, error );
		}
		fileSystem->CloseFile( raw );
	}
	if ( okay && state.levels >= 2 ) {
		const int axis = ( state.project.resolution / MEGA_TEXTURE_TILE_SIZE ) >> 1;
		idFile *levelOne = fileSystem->OpenFileRead( state.rawLevels[1] );
		if ( !levelOne ) { error = "could not read mip level 1"; okay = false; }
		for ( int y = 0; okay && y < axis; ++y ) for ( int x = 0; okay && x < axis; ++x ) {
			if ( levelOne->Read( tile.data(), (int)tile.size() ) != (int)tile.size() ) { error = "short mip read"; okay = false; break; }
			okay = WriteCompressedTile( state, output, 1, x, y, tile.data(), compressed, error );
			for ( int child = 0; okay && child < 4; ++child ) {
				const int childX = x * 2 + ( child & 1 );
				const int childY = y * 2 + ( child >> 1 );
				okay = LoadCompiledSourceTile( state, childX, childY, tile.data(), error ) &&
					WriteCompressedTile( state, output, 0, childX, childY, tile.data(), compressed, error );
			}
		}
		if ( levelOne ) fileSystem->CloseFile( levelOne );
	} else if ( okay ) {
		const int axis = state.project.resolution / MEGA_TEXTURE_TILE_SIZE;
		for ( int y = 0; okay && y < axis; ++y ) for ( int x = 0; okay && x < axis; ++x ) {
			okay = LoadCompiledSourceTile( state, x, y, tile.data(), error ) &&
				WriteCompressedTile( state, output, 0, x, y, tile.data(), compressed, error );
		}
	}

	if ( okay ) {
		output->Seek( 0, FS_SEEK_SET );
		output->WriteInt( MEGA_TEXTURE_FILE_MAGIC );
		output->WriteInt( MEGA_TEXTURE_VERSION );
		output->WriteInt( state.project.resolution );
		output->WriteInt( MEGA_COMPRESSION_RGBA );
		for ( int level = 0; level < state.levels; ++level ) output->WriteInt( MEGA_COMPRESSION_RGBA );
		for ( int level = 0; level < state.levels; ++level ) output->WriteInt( state.maximumSizes[level] );
		for ( int tileIndex = 0; tileIndex < state.totalTiles; ++tileIndex ) {
			output->WriteInt( state.offsets[tileIndex] );
			output->WriteInt( state.sizes[tileIndex] );
		}
	}
	fileSystem->CloseFile( output );
	RemoveRawLevels( state );
	if ( !okay ) {
		fileSystem->RemoveFile( temporaryPath );
		return false;
	}
	if ( !ReplaceFile( temporaryPath, outputPath, error ) ) return false;
	common->Printf( "MegaTexture: wrote %s\n", outputPath.c_str() );
	return true;
}

} // namespace

megaTextureVertexTransform_t MegaTextureEncodeVertexTransform( float scaleX, float scaleY, float rotation,
	float pivotU, float pivotV, float phaseU, float phaseV ) {
	megaTextureVertexTransform_t result;
	const float scaleRange = TERRAIN_TRANSFORM_MAX_SCALE - TERRAIN_TRANSFORM_MIN_SCALE;
	result.scaleX = (unsigned short)idMath::ClampInt( 0, 65535,
		(int)( ( idMath::ClampFloat( TERRAIN_TRANSFORM_MIN_SCALE, TERRAIN_TRANSFORM_MAX_SCALE, scaleX ) - TERRAIN_TRANSFORM_MIN_SCALE ) * 65535.0f / scaleRange + 0.5f ) );
	result.scaleY = (unsigned short)idMath::ClampInt( 0, 65535,
		(int)( ( idMath::ClampFloat( TERRAIN_TRANSFORM_MIN_SCALE, TERRAIN_TRANSFORM_MAX_SCALE, scaleY ) - TERRAIN_TRANSFORM_MIN_SCALE ) * 65535.0f / scaleRange + 0.5f ) );
	while ( rotation < -180.0f ) rotation += 360.0f;
	while ( rotation > 180.0f ) rotation -= 360.0f;
	result.rotation = (unsigned short)idMath::ClampInt( 0, 65535, (int)( ( rotation + 180.0f ) * 65535.0f / 360.0f + 0.5f ) );
	result.pivotU = (unsigned short)idMath::ClampInt( 0, 65535, (int)( idMath::ClampFloat( 0.0f, 1.0f, pivotU ) * 65535.0f + 0.5f ) );
	result.pivotV = (unsigned short)idMath::ClampInt( 0, 65535, (int)( idMath::ClampFloat( 0.0f, 1.0f, pivotV ) * 65535.0f + 0.5f ) );
	if ( phaseU < 0.0f ) phaseU = scaleX * pivotU;
	if ( phaseV < 0.0f ) phaseV = scaleY * pivotV;
	phaseU -= floorf( phaseU ); phaseV -= floorf( phaseV );
	result.phaseU = (unsigned short)idMath::ClampInt( 0, 65535, (int)( phaseU * 65535.0f + 0.5f ) );
	result.phaseV = (unsigned short)idMath::ClampInt( 0, 65535, (int)( phaseV * 65535.0f + 0.5f ) );
	return result;
}

void MegaTextureDecodeVertexTransform( const megaTextureVertexTransform_t &transform, float &scaleX, float &scaleY, float &rotation ) {
	float pivotU, pivotV, phaseU, phaseV;
	MegaTextureDecodeVertexTransform( transform, scaleX, scaleY, rotation, pivotU, pivotV, phaseU, phaseV );
}

void MegaTextureDecodeVertexTransform( const megaTextureVertexTransform_t &transform, float &scaleX, float &scaleY,
	float &rotation, float &pivotU, float &pivotV ) {
	float phaseU, phaseV;
	MegaTextureDecodeVertexTransform( transform, scaleX, scaleY, rotation, pivotU, pivotV, phaseU, phaseV );
}

void MegaTextureDecodeVertexTransform( const megaTextureVertexTransform_t &transform, float &scaleX, float &scaleY,
	float &rotation, float &pivotU, float &pivotV, float &phaseU, float &phaseV ) {
	const float scaleRange = TERRAIN_TRANSFORM_MAX_SCALE - TERRAIN_TRANSFORM_MIN_SCALE;
	scaleX = TERRAIN_TRANSFORM_MIN_SCALE + transform.scaleX * scaleRange / 65535.0f;
	scaleY = TERRAIN_TRANSFORM_MIN_SCALE + transform.scaleY * scaleRange / 65535.0f;
	rotation = transform.rotation * 360.0f / 65535.0f - 180.0f;
	pivotU = transform.pivotU / 65535.0f;
	pivotV = transform.pivotV / 65535.0f;
	phaseU = transform.phaseU / 65535.0f;
	phaseV = transform.phaseV / 65535.0f;
}

void MegaTextureTransformTexCoord( const megaTextureVertexTransform_t &transform, float u, float v,
	float &outU, float &outV, bool flipPivotV ) {
	float scaleX, scaleY, rotation, pivotU, pivotV, phaseU, phaseV;
	MegaTextureDecodeVertexTransform( transform, scaleX, scaleY, rotation, pivotU, pivotV, phaseU, phaseV );
	if ( flipPivotV ) { pivotV = 1.0f - pivotV; phaseV = 1.0f - phaseV; }
	const float radians = rotation * idMath::M_DEG2RAD;
	const float cosine = idMath::Cos( radians ), sine = idMath::Sin( radians );
	const float scaledU = ( u - pivotU ) * scaleX;
	const float scaledV = ( v - pivotV ) * scaleY;
	outU = scaledU * cosine - scaledV * sine + phaseU;
	outV = scaledU * sine + scaledV * cosine + phaseV;
}

void MegaTextureInitializeTerrainTransforms( const megaTextureProject_t &project, std::vector<megaTextureVertexTransform_t> &transforms ) {
	const int count = project.terrainSamples * project.terrainSamples * megaTextureProject_t::MAX_LAYERS;
	transforms.resize( count );
	for ( int index = 0; index < count; ++index ) {
		const int layer = index % megaTextureProject_t::MAX_LAYERS;
		transforms[index] = MegaTextureEncodeVertexTransform( project.layerScale[layer], project.layerScaleY[layer], project.layerRotation[layer] );
	}
}

megaTextureProject_t::megaTextureProject_t() : resolution( MEGA_TEXTURE_LEVEL_SIZE ), terrainSamples( 129 ), terrainSize( 8192.0f ), bakeLighting( false ) {
	terrainOrigin[0] = terrainOrigin[1] = terrainOrigin[2] = 0.0f;
	fill[0] = fill[1] = fill[2] = 128; fill[3] = 0x88;
	quality[0] = 75; quality[1] = 75; quality[2] = 90;
	for ( int i = 0; i < MAX_LAYERS; ++i ) {
		layerScale[i] = layerScaleY[i] = 32.0f;
		layerRotation[i] = 0.0f;
	}
}

int megaTextureProject_t::NumLevels() const {
	int result = 1;
	for ( int ratio = resolution / MEGA_TEXTURE_LEVEL_SIZE; ratio > 1; ratio >>= 1 ) ++result;
	return result;
}

idStr megaTextureProject_t::TilePath( int x, int y ) const { return va( "%s/tile_%d_%d.tga", sourceDirectory.c_str(), x, y ); }
idStr megaTextureProject_t::CompiledName() const {
	idStr compiledName;
	if ( !mapName.IsEmpty() ) mapName.ExtractFileBase( compiledName );
	if ( compiledName.IsEmpty() ) compiledName = name;
	for ( int index = 0; index < compiledName.Length(); ++index ) {
		const char character = compiledName[index];
		if ( !( ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' ) ||
			 ( character >= '0' && character <= '9' ) || character == '_' || character == '-' ) ) compiledName[index] = '_';
	}
	return compiledName;
}
idStr megaTextureProject_t::OutputPath() const { return va( "megatextures/%s.mega", CompiledName().c_str() ); }
idStr megaTextureProject_t::PreviewPath() const { return va( "megatextures/%s_preview.tga", CompiledName().c_str() ); }
idStr megaTextureProject_t::MaterialPath() const { return va( "materials/%s_megatexture.mtr", name.c_str() ); }

bool megaTextureProject_t::Validate( idStr &error ) const {
	if ( name.IsEmpty() || name.Find( ".." ) >= 0 || name.Find( '/' ) >= 0 || name.Find( '\\' ) >= 0 ) {
		error = "name must be a simple, unique file base name"; return false;
	}
	if ( !IsPowerOfTwo( resolution ) || resolution < MEGA_TEXTURE_LEVEL_SIZE || resolution > 32768 ) {
		error = "resolution must be a power of two from 2048 through 32768"; return false;
	}
	if ( sourceDirectory.IsEmpty() || material.IsEmpty() ) { error = "sourceDirectory and material are required"; return false; }
	if ( terrainSamples < 33 || terrainSamples > 513 || !IsPowerOfTwo( terrainSamples - 1 ) ) {
		error = "terrainSamples must be one plus a power of two, from 33 through 513"; return false;
	}
	if ( terrainSize <= 0.0f || heightMap.IsEmpty() || weightMap.IsEmpty() || terrainModel.IsEmpty() ) {
		error = "terrainSize, heightMap, weightMap, and terrainModel are required"; return false;
	}
	if ( layers[0].IsEmpty() ) { error = "terrain layer 1 is required"; return false; }
	for ( int i = 0; i < MAX_LAYERS; ++i ) if ( layerScale[i] <= 0.0f || layerScaleY[i] <= 0.0f ) { error = "layer scales must be positive"; return false; }
	for ( int i = 0; i < 3; ++i ) if ( quality[i] < 1 || quality[i] > 100 ) { error = "quality values must be 1 through 100"; return false; }
	return true;
}

bool megaTextureProject_t::Load( const char *path, idStr &error ) {
	idLexer lexer;
	lexer.SetFlags( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS );
	if ( !lexer.LoadFile( path ) ) { error = va( "could not load %s", path ); return false; }
	idToken token;
	if ( !lexer.ReadToken( &token ) || token.Icmp( "megaTextureProject" ) ) { error = "missing megaTextureProject header"; return false; }
	if ( !lexer.ReadToken( &token ) || token.GetIntValue() != 1 ) { error = "unsupported MegaTexture project version"; return false; }
	if ( !lexer.ExpectTokenString( "{" ) ) { error = "missing project body"; return false; }
	bool layerTransformRead[MAX_LAYERS] = { false, false, false, false };
	while ( lexer.ReadToken( &token ) && token != "}" ) {
		if ( !token.Icmp( "name" ) ) { lexer.ReadToken( &token ); name = token; }
		else if ( !token.Icmp( "resolution" ) ) resolution = lexer.ParseInt();
		else if ( !token.Icmp( "sourceDirectory" ) ) { lexer.ReadToken( &token ); sourceDirectory = token; }
		else if ( !token.Icmp( "material" ) ) { lexer.ReadToken( &token ); material = token; }
		else if ( !token.Icmp( "map" ) ) { lexer.ReadToken( &token ); mapName = token; if ( mapName == "-" ) mapName.Clear(); }
		else if ( !token.Icmp( "heightMap" ) ) { lexer.ReadToken( &token ); heightMap = token; }
		else if ( !token.Icmp( "weightMap" ) ) { lexer.ReadToken( &token ); weightMap = token; }
		else if ( !token.Icmp( "roadFile" ) ) { lexer.ReadToken( &token ); roadFile = token; }
		else if ( !token.Icmp( "terrainModel" ) ) { lexer.ReadToken( &token ); terrainModel = token; }
		else if ( !token.Icmpn( "layer", 5 ) && token.Length() == 6 && token[5] >= '0' && token[5] < '0' + MAX_LAYERS ) {
			const int layer = token[5] - '0';
			lexer.ReadToken( &token ); layers[layer] = token; if ( layers[layer] == "-" ) layers[layer].Clear();
			layerScale[layer] = lexer.ParseFloat();
		}
		else if ( !token.Icmpn( "layerTransform", 14 ) && token.Length() == 15 && token[14] >= '0' && token[14] < '0' + MAX_LAYERS ) {
			const int layer = token[14] - '0';
			layerScale[layer] = lexer.ParseFloat();
			layerScaleY[layer] = lexer.ParseFloat();
			layerRotation[layer] = lexer.ParseFloat();
			layerTransformRead[layer] = true;
		}
		else if ( !token.Icmp( "terrainSamples" ) ) terrainSamples = lexer.ParseInt();
		else if ( !token.Icmp( "terrainSize" ) ) terrainSize = lexer.ParseFloat();
		else if ( !token.Icmp( "terrainOrigin" ) ) for ( int i = 0; i < 3; ++i ) terrainOrigin[i] = lexer.ParseFloat();
		else if ( !token.Icmp( "fill" ) ) for ( int i = 0; i < 4; ++i ) fill[i] = (byte)idMath::ClampInt( 0, 255, lexer.ParseInt() );
		else if ( !token.Icmp( "quality" ) ) for ( int i = 0; i < 3; ++i ) quality[i] = lexer.ParseInt();
		else if ( !token.Icmp( "bakeLighting" ) ) bakeLighting = lexer.ParseInt() != 0;
		else if ( !token.Icmp( "bakeLightmap" ) ) bakeLighting = lexer.ParseInt() != 0; // legacy project key
		else lexer.ReadToken( &token );
	}
	if ( heightMap.IsEmpty() && !name.IsEmpty() ) heightMap = va( "megatextures/%s.height", name.c_str() );
	if ( weightMap.IsEmpty() && !name.IsEmpty() ) weightMap = va( "megatextures/%s.weights", name.c_str() );
	if ( roadFile.IsEmpty() && !name.IsEmpty() ) roadFile = va( "megatextures/%s.roads", name.c_str() );
	if ( terrainModel.IsEmpty() && !name.IsEmpty() ) terrainModel = va( "models/terrain/%s.terrain", name.c_str() );
	if ( layers[0].IsEmpty() ) layers[0] = "megatiles/white.tga";
	for ( int layer = 0; layer < MAX_LAYERS; ++layer ) if ( !layerTransformRead[layer] ) layerScaleY[layer] = layerScale[layer];
	projectPath = path;
	return !lexer.HadError() && Validate( error );
}

bool megaTextureProject_t::Save( const char *path, idStr &error ) const {
	if ( !Validate( error ) ) return false;
	idStr text = va( "megaTextureProject 1\n{\n\tname %s\n\tresolution %d\n\tsourceDirectory %s\n\tmaterial %s\n\tmap %s\n\theightMap %s\n\tweightMap %s\n\troadFile %s\n\tterrainModel %s\n",
		name.c_str(), resolution, sourceDirectory.c_str(), material.c_str(), mapName.IsEmpty() ? "-" : mapName.c_str(),
		heightMap.c_str(), weightMap.c_str(), roadFile.c_str(), terrainModel.c_str() );
	for ( int i = 0; i < MAX_LAYERS; ++i ) {
		text += va( "\tlayer%d %s %g\n", i, layers[i].IsEmpty() ? "-" : layers[i].c_str(), layerScale[i] );
		text += va( "\tlayerTransform%d %g %g %g\n", i, layerScale[i], layerScaleY[i], layerRotation[i] );
	}
	text += va( "\tterrainSamples %d\n\tterrainSize %g\n\tterrainOrigin %g %g %g\n\tfill %d %d %d %d\n\tquality %d %d %d\n\tbakeLighting %d\n}\n",
		terrainSamples, terrainSize, terrainOrigin[0], terrainOrigin[1], terrainOrigin[2],
		fill[0], fill[1], fill[2], fill[3], quality[0], quality[1], quality[2], bakeLighting ? 1 : 0 );
	return WriteTextFile( path, text, error );
}

bool megaTextureProject_t::WriteMaterial( idStr &error ) const {
	idStr text = va( "%s\n{\n\tqer_editorImage %s\n", material.c_str(), PreviewPath().c_str() );
	text += va( "\t{\n\t\tmegaTexture %s\n\t}\n}\n", OutputPath().c_str() );
	return WriteTextFile( MaterialPath(), text, error );
}

bool megaTextureProject_t::WritePreview( idStr &error ) const {
	std::vector<byte> preview( 256 * 256 * 4 );
	for ( int i = 0; i < 256 * 256; ++i ) {
		memcpy( preview.data() + i * 4, fill, 4 );
		preview[i * 4 + 3] = 255;
	}
	return WriteTGA( PreviewPath(), preview.data(), 256, 256, error );
}

bool MegaTextureCreateProject( const char *projectName, int projectResolution, const char *mapName,
							   megaTextureProject_t &project, idStr &error, const char *levelProjectPath ) {
	project = megaTextureProject_t();
	project.name = projectName;
	project.resolution = projectResolution;
	project.sourceDirectory = va( "megatextures/%s_source", projectName );
	project.material = va( "megatextures/%s", projectName );
	project.mapName = mapName ? mapName : "";
	project.heightMap = va( "megatextures/%s.height", projectName );
	project.weightMap = va( "megatextures/%s.weights", projectName );
	project.roadFile = va( "megatextures/%s.roads", projectName );
	project.terrainModel = va( "models/terrain/%s.terrain", projectName );
	project.layers[0] = "megatiles/white.tga";
	project.projectPath = levelProjectPath && levelProjectPath[0] ? levelProjectPath : va( "megatextures/%s.megaproject", projectName );
	std::vector<float> heights( project.terrainSamples * project.terrainSamples, 0.0f );
	std::vector<byte> weights( project.terrainSamples * project.terrainSamples * megaTextureProject_t::MAX_LAYERS, 0 );
	for ( int i = 0; i < project.terrainSamples * project.terrainSamples; ++i ) weights[i * megaTextureProject_t::MAX_LAYERS] = 255;
	std::vector<megaTextureVertexTransform_t> transforms;
	MegaTextureInitializeTerrainTransforms( project, transforms );
	MegaTextureRoadBuilder emptyRoads;
	return project.Validate( error ) && project.Save( project.projectPath, error ) &&
		project.WriteMaterial( error ) && project.WritePreview( error ) &&
		MegaTextureWriteTerrainHeightfield( project, heights, error ) &&
		MegaTextureWriteTerrainWeights( project, weights, transforms, error ) && MegaTextureWriteTerrainModel( project, error ) &&
		emptyRoads.Save( project.roadFile, error );
}

bool MegaTextureLoadTileTGA( const megaTextureProject_t &project, int x, int y, byte *rgba, idStr &error ) {
	if ( x < 0 || y < 0 || x >= project.resolution / MEGA_TEXTURE_TILE_SIZE || y >= project.resolution / MEGA_TEXTURE_TILE_SIZE ) {
		error = "tile coordinates are outside the project"; return false;
	}
	return LoadSourceTile( project, x, y, rgba, error );
}

bool MegaTextureWriteTileTGA( const megaTextureProject_t &project, int x, int y, const byte *rgba, idStr &error ) {
	if ( x < 0 || y < 0 || x >= project.resolution / MEGA_TEXTURE_TILE_SIZE || y >= project.resolution / MEGA_TEXTURE_TILE_SIZE ) {
		error = "tile coordinates are outside the project"; return false;
	}
	return WriteTGA( project.TilePath( x, y ), rgba, MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE, error );
}

bool MegaTextureLoadTerrainHeightfield( const megaTextureProject_t &project, std::vector<float> &heights, idStr &error ) {
	idFile *file = fileSystem->OpenFileRead( project.heightMap );
	if ( !file ) { error = va( "could not read %s", project.heightMap.c_str() ); return false; }
	int magic = 0, version = 0, samples = 0;
	bool okay = file->ReadInt( magic ) == 4 && file->ReadInt( version ) == 4 && file->ReadInt( samples ) == 4 &&
		magic == TERRAIN_HEIGHT_MAGIC && version == TERRAIN_HEIGHT_VERSION && samples == project.terrainSamples;
	heights.resize( project.terrainSamples * project.terrainSamples );
	for ( int i = 0; okay && i < (int)heights.size(); ++i ) okay &= file->ReadFloat( heights[i] ) == 4;
	fileSystem->CloseFile( file );
	if ( !okay ) { heights.clear(); error = va( "invalid terrain heightfield %s", project.heightMap.c_str() ); }
	return okay;
}

bool MegaTextureWriteTerrainHeightfield( const megaTextureProject_t &project, const std::vector<float> &heights, idStr &error ) {
	if ( (int)heights.size() != project.terrainSamples * project.terrainSamples ) {
		error = "heightfield sample count does not match the project"; return false;
	}
	idFile *file = fileSystem->OpenFileWrite( project.heightMap, "fs_devpath" );
	if ( !file ) { error = va( "could not write %s", project.heightMap.c_str() ); return false; }
	bool okay = file->WriteInt( TERRAIN_HEIGHT_MAGIC ) == 4 && file->WriteInt( TERRAIN_HEIGHT_VERSION ) == 4 &&
		file->WriteInt( project.terrainSamples ) == 4;
	for ( int i = 0; okay && i < (int)heights.size(); ++i ) okay &= file->WriteFloat( heights[i] ) == 4;
	fileSystem->CloseFile( file );
	if ( !okay ) error = va( "short write to %s", project.heightMap.c_str() );
	return okay;
}

bool MegaTextureLoadTerrainWeights( const megaTextureProject_t &project, std::vector<byte> &weights,
	std::vector<megaTextureVertexTransform_t> &transforms, idStr &error ) {
	idFile *file = fileSystem->OpenFileRead( project.weightMap );
	if ( !file ) { error = va( "could not read %s", project.weightMap.c_str() ); return false; }
	int magic = 0, version = 0, samples = 0, channels = 0;
	bool okay = file->ReadInt( magic ) == 4 && file->ReadInt( version ) == 4 &&
		file->ReadInt( samples ) == 4 && file->ReadInt( channels ) == 4 &&
		magic == TERRAIN_WEIGHT_MAGIC && ( version == 1 || version == 2 || version == 3 || version == TERRAIN_WEIGHT_VERSION ) &&
		samples == project.terrainSamples && channels == megaTextureProject_t::MAX_LAYERS;
	weights.resize( project.terrainSamples * project.terrainSamples * megaTextureProject_t::MAX_LAYERS );
	if ( okay ) okay = file->Read( weights.data(), (int)weights.size() ) == (int)weights.size();
	if ( okay && version == TERRAIN_WEIGHT_VERSION ) {
		transforms.resize( weights.size() );
		const int transformBytes = (int)( transforms.size() * sizeof( megaTextureVertexTransform_t ) );
		okay = file->Read( transforms.data(), transformBytes ) == transformBytes;
	} else if ( okay && version == 2 ) {
		std::vector<megaTextureVertexTransformV2_t> oldTransforms( weights.size() );
		const int transformBytes = (int)( oldTransforms.size() * sizeof( megaTextureVertexTransformV2_t ) );
		okay = file->Read( oldTransforms.data(), transformBytes ) == transformBytes;
		if ( okay ) {
			transforms.resize( oldTransforms.size() );
			for ( int index = 0; index < (int)oldTransforms.size(); ++index ) {
				const float scaleRange = TERRAIN_TRANSFORM_MAX_SCALE - TERRAIN_TRANSFORM_MIN_SCALE;
				const float scaleX = TERRAIN_TRANSFORM_MIN_SCALE + oldTransforms[index].scaleX * scaleRange / 65535.0f;
				const float scaleY = TERRAIN_TRANSFORM_MIN_SCALE + oldTransforms[index].scaleY * scaleRange / 65535.0f;
				const float rotation = oldTransforms[index].rotation * 360.0f / 65535.0f - 180.0f;
				transforms[index] = MegaTextureEncodeVertexTransform( scaleX, scaleY, rotation );
			}
		}
	} else if ( okay && version == 3 ) {
		std::vector<megaTextureVertexTransformV3_t> oldTransforms( weights.size() );
		const int transformBytes = (int)( oldTransforms.size() * sizeof( megaTextureVertexTransformV3_t ) );
		okay = file->Read( oldTransforms.data(), transformBytes ) == transformBytes;
		if ( okay ) {
			transforms.resize( oldTransforms.size() );
			for ( int index = 0; index < (int)oldTransforms.size(); ++index ) {
				const float scaleRange = TERRAIN_TRANSFORM_MAX_SCALE - TERRAIN_TRANSFORM_MIN_SCALE;
				const float scaleX = TERRAIN_TRANSFORM_MIN_SCALE + oldTransforms[index].scaleX * scaleRange / 65535.0f;
				const float scaleY = TERRAIN_TRANSFORM_MIN_SCALE + oldTransforms[index].scaleY * scaleRange / 65535.0f;
				const float rotation = oldTransforms[index].rotation * 360.0f / 65535.0f - 180.0f;
				const float pivotU = oldTransforms[index].pivotU / 65535.0f;
				const float pivotV = oldTransforms[index].pivotV / 65535.0f;
				transforms[index] = MegaTextureEncodeVertexTransform( scaleX, scaleY, rotation, pivotU, pivotV );
			}
		}
	} else if ( okay ) {
		// Version one stored one transform in the project for the entire layer.
		// Seed every vertex with it so old levels retain their exact appearance,
		// then the next save upgrades the file to locally paintable mappings.
		MegaTextureInitializeTerrainTransforms( project, transforms );
	}
	fileSystem->CloseFile( file );
	if ( !okay ) { weights.clear(); transforms.clear(); error = va( "invalid terrain layer weights %s", project.weightMap.c_str() ); }
	return okay;
}

bool MegaTextureWriteTerrainWeights( const megaTextureProject_t &project, const std::vector<byte> &weights,
	const std::vector<megaTextureVertexTransform_t> &transforms, idStr &error ) {
	const int expected = project.terrainSamples * project.terrainSamples * megaTextureProject_t::MAX_LAYERS;
	if ( (int)weights.size() != expected ) { error = "terrain weight count does not match the project"; return false; }
	if ( (int)transforms.size() != expected ) { error = "terrain paint transform count does not match the project"; return false; }
	idFile *file = fileSystem->OpenFileWrite( project.weightMap, "fs_devpath" );
	if ( !file ) { error = va( "could not write %s", project.weightMap.c_str() ); return false; }
	bool okay = file->WriteInt( TERRAIN_WEIGHT_MAGIC ) == 4 && file->WriteInt( TERRAIN_WEIGHT_VERSION ) == 4 &&
		file->WriteInt( project.terrainSamples ) == 4 && file->WriteInt( megaTextureProject_t::MAX_LAYERS ) == 4 &&
		file->Write( weights.data(), (int)weights.size() ) == (int)weights.size();
	const int transformBytes = (int)( transforms.size() * sizeof( megaTextureVertexTransform_t ) );
	if ( okay ) okay = file->Write( transforms.data(), transformBytes ) == transformBytes;
	fileSystem->CloseFile( file );
	if ( !okay ) error = va( "short write to %s", project.weightMap.c_str() );
	return okay;
}

bool MegaTextureWriteTerrainModel( const megaTextureProject_t &project, idStr &error ) {
	idStr text = va( "terrainModel 1\n{\n\theightMap %s\n\tweightMap %s\n\tmaterial %s\n\tsamples %d\n\tworldSize %g\n}\n",
		project.heightMap.c_str(), project.weightMap.c_str(), project.material.c_str(), project.terrainSamples, project.terrainSize );
	return WriteTextFile( project.terrainModel, text, error );
}

bool MegaTextureCompileProject( const char *projectPath, int buildResolution, bool bakeLighting, idStr &error ) {
	return MegaTextureCompileProject( projectPath, buildResolution, bakeLighting, NULL, error );
}

bool MegaTextureCompileProject( const char *projectPath, int buildResolution, bool bakeLighting,
		const idDict *worldSpawnOverride, idStr &error ) {
	compilerState_t state;
	if ( !state.project.Load( projectPath, error ) ) return false;
	state.project.resolution = buildResolution;
	state.project.bakeLighting = bakeLighting;
	state.bakeLighting = bakeLighting;
	if ( !state.project.Validate( error ) ) return false;
	if ( !PrepareLightingBake( state, worldSpawnOverride, error ) ) return false;
	if ( !PrepareLayerSources( state, error ) ) return false;
	state.levels = state.project.NumLevels();
	state.totalTiles = TotalTiles( state.project.resolution );
	state.levelBase.resize( state.levels );
	int base = 0;
	for ( int level = 0; level < state.levels; ++level ) {
		state.levelBase[level] = base;
		const int axis = ( state.project.resolution / MEGA_TEXTURE_TILE_SIZE ) >> level;
		base += axis * axis;
	}
	state.offsets.assign( state.totalTiles, 0 );
	state.sizes.assign( state.totalTiles, 0 );
	state.maximumSizes.assign( state.levels, 0 );
	if ( !Compile( state, error ) ) return false;
	if ( !WriteLayerPreview( state, error ) ) return false;
	if ( !state.project.WriteMaterial( error ) ) return false;
	if ( !MegaTextureVerifyFile( state.project.OutputPath(), error ) ) return false;
	// Projects created by older editor builds used <map>_<hash> for compiled
	// outputs. Retire those generated files only after mapname.mega validates.
	const idStr legacyOutput = va( "megatextures/%s.mega", state.project.name.c_str() );
	const idStr legacyPreview = va( "megatextures/%s_preview.tga", state.project.name.c_str() );
	if ( idStr::Icmp( legacyOutput, state.project.OutputPath() ) ) {
		if ( fileSystem->ReadFile( legacyOutput, NULL, NULL ) >= 0 ) fileSystem->RemoveFile( legacyOutput );
		if ( fileSystem->ReadFile( legacyPreview, NULL, NULL ) >= 0 ) fileSystem->RemoveFile( legacyPreview );
		common->Printf( "MegaTexture: retired legacy compiled output %s\n", legacyOutput.c_str() );
	}
	return true;
}

bool MegaTextureVerifyFile( const char *megaPath, idStr &error ) {
	idFile *file = fileSystem->OpenFileRead( megaPath );
	if ( !file ) { error = va( "could not open %s", megaPath ); return false; }
	int magic, version, resolution, compression;
	bool okay = file->ReadInt( magic ) == 4 && file->ReadInt( version ) == 4 && file->ReadInt( resolution ) == 4 && file->ReadInt( compression ) == 4;
	if ( !okay || magic != MEGA_TEXTURE_FILE_MAGIC || version != MEGA_TEXTURE_VERSION || !IsPowerOfTwo( resolution ) ||
		 ( compression != MEGA_COMPRESSION_RGB && compression != MEGA_COMPRESSION_RGBA ) ) {
		fileSystem->CloseFile( file ); error = "invalid MegaTexture header"; return false;
	}
	int levels = 1;
	for ( int ratio = resolution / MEGA_TEXTURE_LEVEL_SIZE; ratio > 1; ratio >>= 1 ) ++levels;
	std::vector<int> formats( levels );
	for ( int level = 0; level < levels; ++level ) {
		okay &= file->ReadInt( formats[level] ) == 4 &&
			( formats[level] == MEGA_COMPRESSION_RGB || formats[level] == MEGA_COMPRESSION_RGBA );
	}
	for ( int level = 0, value; level < levels; ++level ) okay &= file->ReadInt( value ) == 4 && value > 3;
	const int total = TotalTiles( resolution );
	std::vector<int> offsets( total ), sizes( total );
	for ( int i = 0; i < total; ++i ) {
		okay &= file->ReadInt( offsets[i] ) == 4 && file->ReadInt( sizes[i] ) == 4;
		okay &= offsets[i] >= 0 && sizes[i] > 0 && (long long)offsets[i] + sizes[i] + 3 <= file->Length();
	}
	std::vector<byte> compressedData;
	std::vector<byte> decoded( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4 );
	idBareDctDecoder decoder;
	const int samples[3] = { 0, total / 2, total - 1 };
	for ( int sample = 0; okay && sample < 3; ++sample ) {
		const int index = samples[sample];
		compressedData.resize( sizes[index] + 3 );
		file->Seek( offsets[index], FS_SEEK_SET );
		okay &= file->Read( compressedData.data(), (int)compressedData.size() ) == (int)compressedData.size();
		if ( okay ) {
			decoder.SetQuality( compressedData[0], compressedData[1], compressedData[2] );
			okay &= ( compression == MEGA_COMPRESSION_RGBA ?
				decoder.DecompressImageRGBA( compressedData.data() + 3, decoded.data(), MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE, sizes[index] ) :
				decoder.DecompressImageRGB( compressedData.data() + 3, decoded.data(), MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE, sizes[index] ) );
		}
	}
	fileSystem->CloseFile( file );
	if ( !okay ) { error = va( "validation failed for %s", megaPath ); return false; }
	common->Printf( "MegaTexture: verified %s (%d tiles)\n", megaPath, total );
	return true;
}

void MegaTextureCompile_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) { common->Printf( "usage: megaCompile [-size 2048..32768] [-bake] <project.megaproject>\n" ); return; }
	bool bake = false;
	int buildResolution = 0;
	const char *path = "";
	for ( int i = 1; i < args.Argc(); ++i ) {
		if ( !idStr::Icmp( args.Argv( i ), "-bake" ) ) bake = true;
		else if ( !idStr::Icmp( args.Argv( i ), "-dmap" ) ) {
			common->Warning( "megaCompile: -dmap is deprecated; performing the separate MegaTexture atmosphere bake" );
			bake = true;
		}
		else if ( !idStr::Icmp( args.Argv( i ), "-size" ) && i + 1 < args.Argc() ) buildResolution = atoi( args.Argv( ++i ) );
		else path = args.Argv( i );
	}
	idStr error;
	megaTextureProject_t compileProject;
	if ( !path[0] || !compileProject.Load( path, error ) ) { common->Warning( "megaCompile: %s", error.c_str() ); return; }
	if ( buildResolution == 0 ) buildResolution = compileProject.resolution;
	if ( !MegaTextureCompileProject( path, buildResolution, bake, error ) ) { common->Warning( "megaCompile: %s", error.c_str() ); return; }
}

void MegaTextureCreate_f( const idCmdArgs &args ) {
	if ( args.Argc() < 2 ) { common->Printf( "usage: megaCreate <name> [terrainSamples] [worldSize]\n" ); return; }
	const int terrainSamples = args.Argc() > 2 ? atoi( args.Argv( 2 ) ) : 129;
	const float worldSize = args.Argc() > 3 ? atof( args.Argv( 3 ) ) : 8192.0f;
	idStr error;
	megaTextureProject_t project;
	if ( !MegaTextureCreateProject( args.Argv( 1 ), MEGA_TEXTURE_LEVEL_SIZE, "", project, error ) ) {
		common->Warning( "megaCreate: %s", error.c_str() ); return;
	}
	project.terrainSamples = terrainSamples;
	project.terrainSize = worldSize;
	std::vector<float> heights( terrainSamples * terrainSamples, 0.0f );
	std::vector<byte> weights( terrainSamples * terrainSamples * megaTextureProject_t::MAX_LAYERS, 0 );
	for ( int i = 0; i < terrainSamples * terrainSamples; ++i ) weights[i * megaTextureProject_t::MAX_LAYERS] = 255;
	std::vector<megaTextureVertexTransform_t> transforms;
	MegaTextureInitializeTerrainTransforms( project, transforms );
	if ( !project.Validate( error ) || !project.Save( project.projectPath, error ) ||
		 !MegaTextureWriteTerrainHeightfield( project, heights, error ) ||
		 !MegaTextureWriteTerrainWeights( project, weights, transforms, error ) || !MegaTextureWriteTerrainModel( project, error ) ) {
		common->Warning( "megaCreate: %s", error.c_str() ); return;
	}
	common->Printf( "Created terrain project %s\n", project.projectPath.c_str() );
}

void MegaTextureVerify_f( const idCmdArgs &args ) {
	if ( args.Argc() != 2 ) { common->Printf( "usage: megaVerify <megatextures/file.mega>\n" ); return; }
	idStr error;
	if ( !MegaTextureVerifyFile( args.Argv( 1 ), error ) ) common->Warning( "megaVerify: %s", error.c_str() );
}
