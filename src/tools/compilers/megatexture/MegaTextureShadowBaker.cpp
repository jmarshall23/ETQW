#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../../renderer/Image.h"
#include "../../../renderer/Material.h"
#include "../../../renderer/Model.h"
#include "../../../renderer/renderbindings.h"
#include "MegaTextureCompiler.h"
#include "MegaTextureShadowBaker.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

const idMaterial *R_RemapShaderBySkin( const idMaterial *shader,
	const idDeclSkin *customSkin, const idMaterial *customShader );

namespace {

struct shadowAlphaStage_t {
	std::vector<byte> rgba;
	int width;
	int height;
	float threshold;
	float alphaScale;
	float matrix[2][3];
	textureRepeat_t repeat;
};

struct shadowAlphaMask_t {
	const idMaterial *material;
	std::vector<shadowAlphaStage_t> stages;
	bool opaqueFallback;
	shadowAlphaMask_t() : material( NULL ), opaqueFallback( false ) {}
};

struct shadowTriangle_t {
	idVec3 origin;
	idVec3 edge1;
	idVec3 edge2;
	idVec2 textureCoords[3];
	idBounds bounds;
	idVec3 center;
	int alphaMask;
};

struct shadowNode_t {
	idBounds bounds;
	int first;
	int count;
	int children[2];
	shadowNode_t() : first( 0 ), count( 0 ) { children[0] = children[1] = -1; }
};

class staticShadowScene_t {
public:
	bool AddMapModels( const idMapFile &mapFile, int &entityCount, idStr &error );
	void Build();
	bool Occluded( const idVec3 &start, const idVec3 &direction, float maximumDistance ) const;
	int NumTriangles() const { return (int)triangles.size(); }

private:
	int FindOrCreateAlphaMask( const idMaterial *material );
	bool AlphaBlocks( const shadowTriangle_t &triangle, float u, float v ) const;
	int BuildNode( int first, int count );
	static bool RayBounds( const idBounds &bounds, const idVec3 &start,
		const idVec3 &direction, float maximumDistance );
	static bool RayTriangle( const shadowTriangle_t &triangle, const idVec3 &start,
		const idVec3 &direction, float maximumDistance, float &u, float &v );

	std::vector<shadowTriangle_t> triangles;
	std::vector<shadowAlphaMask_t> alphaMasks;
	std::vector<shadowNode_t> nodes;
};

static int WrapCoordinate( int coordinate, int size ) {
	coordinate %= size;
	return coordinate < 0 ? coordinate + size : coordinate;
}

static float SampleAlphaTexel( const shadowAlphaStage_t &stage, int x, int y ) {
	if ( stage.repeat == TR_REPEAT ) {
		x = WrapCoordinate( x, stage.width );
		y = WrapCoordinate( y, stage.height );
	} else if ( x < 0 || x >= stage.width || y < 0 || y >= stage.height ) {
		if ( stage.repeat == TR_CLAMP_TO_ZERO_ALPHA || stage.repeat == TR_CLAMP_TO_BORDER ) return 0.0f;
		x = idMath::ClampInt( 0, stage.width - 1, x );
		y = idMath::ClampInt( 0, stage.height - 1, y );
	}
	return stage.rgba[( y * stage.width + x ) * 4 + 3] * ( 1.0f / 255.0f );
}

static float SampleAlpha( const shadowAlphaStage_t &stage, const idVec2 &textureCoord ) {
	const float s = textureCoord[0] * stage.matrix[0][0] +
		textureCoord[1] * stage.matrix[0][1] + stage.matrix[0][2];
	const float t = textureCoord[0] * stage.matrix[1][0] +
		textureCoord[1] * stage.matrix[1][1] + stage.matrix[1][2];
	const float imageX = s * stage.width - 0.5f;
	const float imageY = t * stage.height - 0.5f;
	const int x0 = (int)floorf( imageX );
	const int y0 = (int)floorf( imageY );
	const float fractionX = imageX - x0;
	const float fractionY = imageY - y0;
	const float row0 = SampleAlphaTexel( stage, x0, y0 ) * ( 1.0f - fractionX ) +
		SampleAlphaTexel( stage, x0 + 1, y0 ) * fractionX;
	const float row1 = SampleAlphaTexel( stage, x0, y0 + 1 ) * ( 1.0f - fractionX ) +
		SampleAlphaTexel( stage, x0 + 1, y0 + 1 ) * fractionX;
	return row0 * ( 1.0f - fractionY ) + row1 * fractionY;
}

int staticShadowScene_t::FindOrCreateAlphaMask( const idMaterial *material ) {
	if ( !material || material->Coverage() != MC_PERFORATED ) return -1;
	for ( int index = 0; index < (int)alphaMasks.size(); ++index ) {
		if ( alphaMasks[index].material == material ) return index;
	}

	shadowAlphaMask_t mask;
	mask.material = material;
	std::vector<float> evaluatedRegisters;
	float shaderParms[MAX_ENTITY_SHADER_PARMS];
	memset( shaderParms, 0, sizeof( shaderParms ) );
	shaderParms[SHADERPARM_RED] = shaderParms[SHADERPARM_GREEN] =
		shaderParms[SHADERPARM_BLUE] = shaderParms[SHADERPARM_ALPHA] = 1.0f;
	const float *registers = material->ConstantRegisters( shaderParms, NULL );
	if ( !registers ) {
		evaluatedRegisters.resize( material->GetNumRegisters() );
		material->EvaluateRegisters( evaluatedRegisters.data(), shaderParms, NULL, NULL );
		registers = evaluatedRegisters.data();
	}

	bool activeAlphaStage = false;
	for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
		const materialStage_t *stage = material->GetStage( stageIndex );
		if ( !stage || !stage->hasAlphaTest || registers[stage->conditionRegister] == 0.0f ) continue;
		activeAlphaStage = true;
		const float alphaScale = stage->colorVector != NULL ?
			idMath::ClampFloat( 0.0f, 1.0f, registers[stage->colorVector->registers[3]] ) : 1.0f;
		if ( alphaScale <= 0.0f ) continue;
		const stageTexture_t *diffuseTexture = NULL;
		for ( int textureIndex = 0; textureIndex < stage->numTextures; ++textureIndex ) {
			const sdDeclRenderBinding *binding = stage->textures[textureIndex].renderBinding;
			if ( binding == rbinds->diffuseMap || binding == rbinds->map ) {
				diffuseTexture = &stage->textures[textureIndex];
				break;
			}
		}
		if ( diffuseTexture == NULL || diffuseTexture->image == NULL ||
			diffuseTexture->image->generatorFunction || diffuseTexture->image->cubeFiles != CF_2D ) {
			mask.opaqueFallback = true;
			break;
		}

		byte *pixels = NULL;
		int width = 0, height = 0;
		R_LoadImageProgram( diffuseTexture->image->imgName, &pixels, &width, &height, NULL );
		if ( !pixels || width <= 0 || height <= 0 ) {
			if ( pixels ) Mem_Free( pixels );
			mask.opaqueFallback = true;
			break;
		}
		shadowAlphaStage_t alphaStage;
		alphaStage.rgba.assign( pixels, pixels + width * height * 4 );
		Mem_Free( pixels );
		alphaStage.width = width;
		alphaStage.height = height;
		alphaStage.threshold = registers[stage->alphaTestRegister];
		alphaStage.alphaScale = alphaScale;
		alphaStage.repeat = diffuseTexture->image->repeat;
		alphaStage.matrix[0][0] = alphaStage.matrix[1][1] = 1.0f;
		alphaStage.matrix[0][1] = alphaStage.matrix[0][2] =
			alphaStage.matrix[1][0] = alphaStage.matrix[1][2] = 0.0f;
		if ( stage->diffuseTextureMatrix != NULL ) {
			for ( int row = 0; row < 2; ++row ) for ( int column = 0; column < 3; ++column ) {
				alphaStage.matrix[row][column] = registers[stage->diffuseTextureMatrix->matrix[row][column]];
			}
		}
		mask.stages.push_back( alphaStage );
	}
	if ( !activeAlphaStage ) mask.opaqueFallback = true;
	alphaMasks.push_back( mask );
	return (int)alphaMasks.size() - 1;
}

bool staticShadowScene_t::AddMapModels( const idMapFile &mapFile, int &entityCount, idStr &error ) {
	entityCount = 0;
	if ( !gameEdit ) {
		error = "the game edit interface is unavailable while collecting MegaTexture shadow casters";
		return false;
	}
	for ( int entityIndex = 1; entityIndex < mapFile.GetNumEntities(); ++entityIndex ) {
		const idMapEntity *mapEntity = mapFile.GetEntity( entityIndex );
		const idDict &args = mapEntity->epairs;
		if ( idStr::Icmp( args.GetString( "classname", "" ), "func_static" ) ||
			mapEntity->GetNumPrimitives() != 0 || args.GetBool( "megaTextureTerrain", "0" ) ||
			args.GetBool( "noshadows", "0" ) || args.GetBool( "hide", "0" ) ||
			args.GetBool( "start_off", "0" ) ) continue;
		const idKeyValue *shadowOverride = args.FindKey( "megaBakeShadows" );
		if ( shadowOverride && !args.GetBool( "megaBakeShadows", "1" ) ) continue;
		const char *modelName = args.GetString( "model", "" );
		if ( !modelName[0] || !idStr::Icmp( modelName, args.GetString( "name", "" ) ) ) continue;

		renderEntity_t renderEntity;
		gameEdit->ParseSpawnArgsToRenderEntity( args, renderEntity );
		idRenderModel *model = renderEntity.hModel;
		if ( !model || model->IsDefaultModel() || model->IsDynamicModel() != DM_STATIC ) continue;
		const int firstTriangle = (int)triangles.size();
		for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex ) {
			const modelSurface_t *surface = model->Surface( surfaceIndex );
			if ( !surface || !surface->geometry ) continue;
			const idMaterial *material = R_RemapShaderBySkin( surface->material,
				renderEntity.customSkin, renderEntity.customShader );
			// MegaTexture shadows are an offline terrain bake.  A model may opt out
			// per entity, but bakeLightmap and a material's runtime noShadows flag do
			// not stop it casting into the terrain.  This is essential for foliage.
			if ( !material || !material->IsDrawn() || material->Coverage() == MC_TRANSLUCENT ) continue;
			const int alphaMask = FindOrCreateAlphaMask( material );
			const srfTriangles_t *geometry = surface->geometry;
			for ( int index = 0; index + 2 < geometry->numIndexes; index += 3 ) {
				const int vertex[3] = { geometry->indexes[index], geometry->indexes[index + 1], geometry->indexes[index + 2] };
				const idVec3 point[3] = {
					renderEntity.origin + geometry->verts[vertex[0]].xyz * renderEntity.axis,
					renderEntity.origin + geometry->verts[vertex[1]].xyz * renderEntity.axis,
					renderEntity.origin + geometry->verts[vertex[2]].xyz * renderEntity.axis
				};
				shadowTriangle_t triangle;
				triangle.origin = point[0];
				triangle.edge1 = point[1] - point[0];
				triangle.edge2 = point[2] - point[0];
				if ( triangle.edge1.Cross( triangle.edge2 ).LengthSqr() <= 0.000001f ) continue;
				triangle.bounds.Clear();
				for ( int corner = 0; corner < 3; ++corner ) {
					triangle.bounds.AddPoint( point[corner] );
					triangle.textureCoords[corner] = geometry->verts[vertex[corner]].GetST();
				}
				triangle.bounds.ExpandSelf( 0.05f );
				triangle.center = ( point[0] + point[1] + point[2] ) * ( 1.0f / 3.0f );
				triangle.alphaMask = alphaMask;
				triangles.push_back( triangle );
			}
		}
		if ( (int)triangles.size() > firstTriangle ) ++entityCount;
	}
	return true;
}

int staticShadowScene_t::BuildNode( int first, int count ) {
	shadowNode_t node;
	idBounds centerBounds;
	node.bounds.Clear();
	centerBounds.Clear();
	for ( int index = first; index < first + count; ++index ) {
		node.bounds.AddBounds( triangles[index].bounds );
		centerBounds.AddPoint( triangles[index].center );
	}
	const int nodeIndex = (int)nodes.size();
	nodes.push_back( node );
	if ( count <= 8 ) {
		node.first = first;
		node.count = count;
		nodes[nodeIndex] = node;
		return nodeIndex;
	}
	const idVec3 centerSize = centerBounds[1] - centerBounds[0];
	int axis = centerSize.y > centerSize.x ? 1 : 0;
	if ( centerSize.z > centerSize[axis] ) axis = 2;
	std::sort( triangles.begin() + first, triangles.begin() + first + count,
		[axis]( const shadowTriangle_t &left, const shadowTriangle_t &right ) {
			return left.center[axis] < right.center[axis];
		} );
	const int leftCount = count / 2;
	node.children[0] = BuildNode( first, leftCount );
	node.children[1] = BuildNode( first + leftCount, count - leftCount );
	nodes[nodeIndex] = node;
	return nodeIndex;
}

void staticShadowScene_t::Build() {
	nodes.clear();
	if ( !triangles.empty() ) BuildNode( 0, (int)triangles.size() );
}

bool staticShadowScene_t::RayBounds( const idBounds &bounds, const idVec3 &start,
		const idVec3 &direction, float maximumDistance ) {
	float nearDistance = 0.0f;
	float farDistance = maximumDistance;
	for ( int axis = 0; axis < 3; ++axis ) {
		if ( idMath::Fabs( direction[axis] ) < 0.000001f ) {
			if ( start[axis] < bounds[0][axis] || start[axis] > bounds[1][axis] ) return false;
			continue;
		}
		float distance0 = ( bounds[0][axis] - start[axis] ) / direction[axis];
		float distance1 = ( bounds[1][axis] - start[axis] ) / direction[axis];
		if ( distance0 > distance1 ) idSwap( distance0, distance1 );
		nearDistance = Max( nearDistance, distance0 );
		farDistance = Min( farDistance, distance1 );
		if ( nearDistance > farDistance ) return false;
	}
	return farDistance >= 0.0f;
}

bool staticShadowScene_t::RayTriangle( const shadowTriangle_t &triangle, const idVec3 &start,
		const idVec3 &direction, float maximumDistance, float &u, float &v ) {
	const idVec3 p = direction.Cross( triangle.edge2 );
	const float determinant = triangle.edge1 * p;
	if ( idMath::Fabs( determinant ) < 0.0000001f ) return false;
	const float inverseDeterminant = 1.0f / determinant;
	const idVec3 t = start - triangle.origin;
	u = ( t * p ) * inverseDeterminant;
	if ( u < 0.0f || u > 1.0f ) return false;
	const idVec3 q = t.Cross( triangle.edge1 );
	v = ( direction * q ) * inverseDeterminant;
	if ( v < 0.0f || u + v > 1.0f ) return false;
	const float distance = ( triangle.edge2 * q ) * inverseDeterminant;
	return distance > 0.01f && distance < maximumDistance;
}

bool staticShadowScene_t::AlphaBlocks( const shadowTriangle_t &triangle, float u, float v ) const {
	if ( triangle.alphaMask < 0 ) return true;
	const shadowAlphaMask_t &mask = alphaMasks[triangle.alphaMask];
	if ( mask.opaqueFallback ) return true;
	const float w = 1.0f - u - v;
	const idVec2 textureCoord = triangle.textureCoords[0] * w +
		triangle.textureCoords[1] * u + triangle.textureCoords[2] * v;
	for ( int stageIndex = 0; stageIndex < (int)mask.stages.size(); ++stageIndex ) {
		const shadowAlphaStage_t &stage = mask.stages[stageIndex];
		if ( SampleAlpha( stage, textureCoord ) * stage.alphaScale > stage.threshold ) return true;
	}
	return false;
}

bool staticShadowScene_t::Occluded( const idVec3 &start, const idVec3 &direction, float maximumDistance ) const {
	if ( nodes.empty() || !RayBounds( nodes[0].bounds, start, direction, maximumDistance ) ) return false;
	int stack[256];
	int stackCount = 0;
	stack[stackCount++] = 0;
	while ( stackCount > 0 ) {
		const shadowNode_t &node = nodes[stack[--stackCount]];
		if ( !RayBounds( node.bounds, start, direction, maximumDistance ) ) continue;
		if ( node.count > 0 ) {
			for ( int index = node.first; index < node.first + node.count; ++index ) {
				float u, v;
				if ( RayTriangle( triangles[index], start, direction, maximumDistance, u, v ) &&
					AlphaBlocks( triangles[index], u, v ) ) return true;
			}
		} else {
			if ( stackCount + 2 >= (int)( sizeof( stack ) / sizeof( stack[0] ) ) ) return true;
			stack[stackCount++] = node.children[0];
			stack[stackCount++] = node.children[1];
		}
	}
	return false;
}

static float SampleHeight( const megaTextureProject_t &project, const std::vector<float> &heights,
		float u, float v ) {
	const int samples = project.terrainSamples;
	const float gridX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), u * ( samples - 1 ) );
	const float gridY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), v * ( samples - 1 ) );
	const int x0 = (int)floorf( gridX ), y0 = (int)floorf( gridY );
	const int x1 = Min( x0 + 1, samples - 1 ), y1 = Min( y0 + 1, samples - 1 );
	const float fractionX = gridX - x0, fractionY = gridY - y0;
	const float row0 = heights[y0 * samples + x0] * ( 1.0f - fractionX ) + heights[y0 * samples + x1] * fractionX;
	const float row1 = heights[y1 * samples + x0] * ( 1.0f - fractionX ) + heights[y1 * samples + x1] * fractionX;
	return row0 * ( 1.0f - fractionY ) + row1 * fractionY;
}

} // namespace

bool MegaTextureBuildStaticModelShadows( const idMapFile &mapFile,
		const megaTextureProject_t &project, const std::vector<float> &heights,
		const idVec3 &sunDirection, int shadowResolution,
		std::vector<float> &visibility, int &entityCount, int &triangleCount,
		idStr &error ) {
	visibility.clear();
	entityCount = triangleCount = 0;
	if ( shadowResolution < 2 || (int)heights.size() != project.terrainSamples * project.terrainSamples ) {
		error = "invalid terrain data for static-model MegaTexture shadows";
		return false;
	}
	staticShadowScene_t scene;
	if ( !scene.AddMapModels( mapFile, entityCount, error ) ) return false;
	triangleCount = scene.NumTriangles();
	visibility.assign( shadowResolution * shadowResolution, 1.0f );
	if ( triangleCount == 0 ) return true;
	scene.Build();
	const float spacing = project.terrainSize / ( shadowResolution - 1 );
	const float bias = Max( 0.5f, spacing * 0.02f );
	const float maximumDistance = Max( 65536.0f, project.terrainSize * 4.0f );
	std::atomic<int> nextRow( 0 );
	const unsigned int hardwareThreads = std::thread::hardware_concurrency();
	const int workerCount = idMath::ClampInt( 1, 16, hardwareThreads > 0 ? (int)hardwareThreads : 1 );
	std::vector<std::thread> workers;
	workers.reserve( workerCount );
	for ( int workerIndex = 0; workerIndex < workerCount; ++workerIndex ) {
		workers.push_back( std::thread( [&]() {
			for ( ;; ) {
				const int y = nextRow.fetch_add( 1 );
				if ( y >= shadowResolution ) break;
				const float v = y / (float)( shadowResolution - 1 );
				for ( int x = 0; x < shadowResolution; ++x ) {
					const float u = x / (float)( shadowResolution - 1 );
					idVec3 point(
						project.terrainOrigin[0] + u * project.terrainSize - project.terrainSize * 0.5f,
						project.terrainOrigin[1] + project.terrainSize * 0.5f - v * project.terrainSize,
						project.terrainOrigin[2] + SampleHeight( project, heights, u, v ) );
					point += sunDirection * bias;
					if ( scene.Occluded( point, sunDirection, maximumDistance ) ) {
						visibility[y * shadowResolution + x] = 0.0f;
					}
				}
			}
		} ) );
	}
	for ( int workerIndex = 0; workerIndex < workerCount; ++workerIndex ) workers[workerIndex].join();
	return true;
}
