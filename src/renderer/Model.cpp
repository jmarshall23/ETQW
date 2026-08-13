// Copyright (C) 2007 Id Software, Inc.
//
// ETQW static render-model implementation reconstructed in its original
// PDB source unit.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Model.h"
#include "Model_lwo.h"
#include "Model_Stuff.h"
#include "RenderSystem.h"
#include "VertexCache.h"
#include "VulkanBackend.h"
#include "../decllib/declTypeHolder.h"

namespace {

bool ReadDiscard( idFile* file, int byteCount ) {
	if ( file == NULL || byteCount < 0 ) {
		return false;
	}
	byte scratch[ 4096 ];
	int remaining = byteCount;
	while ( remaining > 0 ) {
		const int amount = Min( remaining, static_cast< int >( sizeof( scratch ) ) );
		if ( file->Read( scratch, amount ) != amount ) {
			return false;
		}
		remaining -= amount;
	}
	return true;
}

bool CheckedByteCount( int count, int stride, int& byteCount ) {
	const long long total = static_cast< long long >( count ) * stride;
	if ( count < 0 || stride < 0 || total > 0x7fffffffLL ) {
		return false;
	}
	byteCount = static_cast< int >( total );
	return true;
}

struct md5SkinSurface_t {
	idList< byte >			referencedJoints;
	idList< vertWeight_t >	weights;
	bool					noAnimate;

	md5SkinSurface_t() : noAnimate( false ) {}
};

class idRenderModelStatic : public idRenderModel {
public:
	idRenderModelStatic() :
		loaded( false ),
		defaulted( false ),
		reloadable( false ),
		referencedOutsideLevelLoad( false ),
		levelLoadReferenced( false ),
		timeStamp( 0 ),
		currentLod( 0 ),
		ownsSurfaceGeometry( true ) {
		SetFallbackBounds();
	}

	virtual ~idRenderModelStatic() {
		R_FreeStuffModel( this );
		ClearSurfaces();
	}

	virtual void InitFromFile( const char* fileName ) {
		modelName = fileName != NULL ? fileName : "";
		reloadable = true;
		LoadModel();
	}

	virtual void PartialInitFromFile( const char* fileName ) {
		InitFromFile( fileName );
	}

	virtual void InitEmpty( const char* name ) {
		R_FreeStuffModel( this );
		modelName = name != NULL ? name : "";
		loaded = true;
		defaulted = false;
		reloadable = false;
		ClearSurfaces();
		modelJoints.Clear();
		defaultPose.Clear();
		inverseDefaultPose.Clear();
		surfaceNames.Clear();
		surfaceIds.Clear();
		SetFallbackBounds();
	}

	virtual void AddSurface( modelSurface_t surface ) {
		modelSurfaces.Append( surface );
		if ( surface.geometry != NULL ) {
			modelBounds += surface.geometry->bounds;
		}
	}

	virtual void FinishSurfaces( bool = true, bool = true, bool = false ) {
		if ( renderSystem == NULL || !renderSystem->IsOpenGLRunning() ) {
			return;
		}
		for ( int i = 0; i < modelSurfaces.Num(); ++i ) {
			srfTriangles_t* triangles = modelSurfaces[ i ].geometry;
			if ( triangles == NULL ) {
				continue;
			}
			if ( triangles->ambientCache == NULL && triangles->verts != NULL && triangles->numVerts > 0 ) {
				vertexCache.Alloc( triangles->verts,
					triangles->numVerts * sizeof( triangles->verts[ 0 ] ), &triangles->ambientCache );
			}
			if ( triangles->indexCache == NULL && triangles->indexes != NULL && triangles->numIndexes > 0 ) {
				vertexCache.Alloc( triangles->indexes,
					triangles->numIndexes * sizeof( triangles->indexes[ 0 ] ), &triangles->indexCache, true );
			}
			if ( i < md5SkinSurfaces.Num() && !md5SkinSurfaces[ i ].noAnimate &&
				 triangles->weightCache == NULL &&
				 md5SkinSurfaces[ i ].weights.Num() == triangles->numVerts && triangles->numVerts > 0 ) {
				vertexCache.Alloc( md5SkinSurfaces[ i ].weights.Begin(),
					triangles->numVerts * sizeof( vertWeight_t ), &triangles->weightCache );
			}
		}
	}
	virtual bool Validate() const { return loaded; }

	virtual void PurgeModel() {
		R_FreeStuffModel( this );
		loaded = false;
		ClearSurfaces();
		modelJoints.Clear();
		defaultPose.Clear();
		inverseDefaultPose.Clear();
	}

	virtual void Reset() {}

	virtual void LoadModel() {
		R_FreeStuffModel( this );
		ClearSurfaces();
		modelJoints.Clear();
		defaultPose.Clear();
		inverseDefaultPose.Clear();
		SetFallbackBounds();
		loaded = true;
		defaulted = false;
		timeStamp = 0;

		if ( modelName.IsEmpty() ) {
			defaulted = true;
			return;
		}
		if ( modelName[ 0 ] == '_' ) {
			return;
		}

		idStr extension;
		modelName.ExtractFileExtension( extension );
		if ( !extension.Icmp( "clust" ) ) {
			if ( R_LoadStuffModel( this, modelName ) ) {
				return;
			}
			defaulted = true;
			return;
		} else if ( !extension.Icmp( "modelb" ) ) {
			if ( LoadModelB( modelName ) ) {
				FinishSurfaces();
				return;
			}
		} else if ( !extension.Icmp( "terrain" ) ) {
			if ( LoadTerrainModel( modelName ) ) {
				FinishSurfaces();
				return;
			}
			defaulted = true;
			return;
		} else if ( extension.Icmp( MD5_MESH_EXT ) ) {
			idStr generated = "generated/modelb/";
			generated += modelName;
			generated.StripFileExtension();
			generated.SetFileExtension( "modelb" );
			if ( LoadModelB( generated ) ) {
				FinishSurfaces();
				return;
			}
			if ( !extension.Icmp( "lwo" ) && LoadLWOModel( modelName ) ) {
				FinishSurfaces();
				return;
			}
		} else if ( LoadMD5BinaryReferencePose() ) {
			FinishSurfaces();
			return;
		}

		unsigned sourceTime = 0;
		if ( fileSystem != NULL && fileSystem->ReadFile( modelName, NULL, &sourceTime ) >= 0 ) {
			timeStamp = sourceTime;
			return;
		}

		// FindModel returns a valid default model on failure in the retail
		// renderer.  Callers such as deployment sizing intentionally rely on
		// that contract and dereference the result without a null check.
		defaulted = true;
	}

	virtual bool IsLoaded() const { return loaded; }
	virtual void SetReferencedOutsideLevelLoad( bool referenced ) { referencedOutsideLevelLoad = referenced; }
	virtual bool IsReferencedOutsideLevelLoad() const { return referencedOutsideLevelLoad; }
	virtual void SetLevelLoadReferenced( bool referenced ) { levelLoadReferenced = referenced; }
	virtual bool IsLevelLoadReferenced() const { return levelLoadReferenced; }
	virtual void TouchData() {}
	virtual void FreeVertexCache() {
		for ( int i = 0; i < modelSurfaces.Num(); ++i ) {
			FreeSurfaceVertexCache( modelSurfaces[ i ].geometry );
		}
	}
	virtual void DirtyVertexAmbientCache() {}
	virtual const char* Name() const { return modelName.c_str(); }
	virtual void Print() const { common->Printf( "%s\n", modelName.c_str() ); }
	virtual void List( bool ) const { common->Printf( "%s\n", modelName.c_str() ); }
	virtual void TexUsage() const {}
	virtual void Media( idFile*, sdHashMapGeneric< const idImage*, imageuseinfo >& ) const {}
	virtual int Memory() const { return sizeof( *this ) + modelName.Allocated(); }
	virtual unsigned Timestamp() const { return timeStamp; }
	virtual int NumSurfaces() const { return modelSurfaces.Num(); }
	virtual int NumBaseSurfaces() const { return modelSurfaces.Num(); }
	virtual const modelSurface_t* Surface( int surfaceNum ) const {
		return surfaceNum >= 0 && surfaceNum < modelSurfaces.Num() ? &modelSurfaces[ surfaceNum ] : NULL;
	}
	virtual srfTriangles_t* AllocSurfaceTriangles( int numVerts, int numIndexes ) const {
		if ( numVerts < 0 || numIndexes < 0 ) {
			return NULL;
		}
		srfTriangles_t* triangles = static_cast< srfTriangles_t* >( Mem_ClearedAlloc( sizeof( *triangles ) ) );
		triangles->numVerts = numVerts;
		triangles->numAllocedVerts = numVerts;
		triangles->numIndexes = numIndexes;
		triangles->numAllocedIndices = numIndexes;
		triangles->mode = PM_TRIANGLE;
		triangles->texCoordScale = 1.0f;
		if ( numVerts > 0 ) {
			triangles->verts = static_cast< idDrawVert* >( Mem_ClearedAlloc( numVerts * sizeof( *triangles->verts ) ) );
		}
		if ( numIndexes > 0 ) {
			triangles->indexes = static_cast< glIndex_t* >( Mem_Alloc( numIndexes * sizeof( *triangles->indexes ) ) );
		}
		return triangles;
	}
	virtual void FreeSurfaceTriangles( srfTriangles_t* triangles ) const {
		if ( triangles == NULL ) {
			return;
		}
		FreeSurfaceVertexCache( triangles );
		Mem_Free( triangles->verts );
		Mem_Free( triangles->indexes );
		Mem_Free( triangles->facePlanes );
		Mem_Free( triangles->indexTree );
		Mem_FreeAligned( triangles->joints );
		Mem_Free( triangles );
	}
	virtual bool IsStaticWorldModel() const { return !modelName.Icmpn( "_area", 5 ); }
	virtual bool IsReloadable() const { return reloadable; }
	virtual dynamicModel_t IsDynamicModel() const { return modelJoints.Num() > 0 ? DM_CACHED : DM_STATIC; }
	virtual bool IsDefaultModel() const { return defaulted; }
	virtual idBounds Bounds( const renderEntity_t* = NULL ) const { return modelBounds; }
	virtual float DepthHack() const { return 0.0f; }
	virtual void InstantiateDynamicModel( idRenderEntityLocal*, const viewDef_s*, int = 0 ) {}
	virtual void UpdateDeferredSurface( idRenderEntityLocal*, modelSurface_t* ) {}
	virtual int NumJoints() const { return modelJoints.Num(); }
	virtual const idMD5Joint* GetJoints() const { return modelJoints.Num() > 0 ? &modelJoints[ 0 ] : NULL; }
	virtual jointHandle_t GetJointHandle( const char* name ) const {
		for ( int i = 0; name != NULL && i < modelJoints.Num(); i++ ) {
			if ( !modelJoints[ i ].name.Icmp( name ) ) {
				return static_cast< jointHandle_t >( i );
			}
		}
		return INVALID_JOINT;
	}
	virtual const char* GetJointName( jointHandle_t handle ) const {
		return handle >= 0 && handle < modelJoints.Num() ? modelJoints[ handle ].name.c_str() : "";
	}
	virtual const idJointQuat* GetDefaultPose() const { return defaultPose.Num() > 0 ? &defaultPose[ 0 ] : NULL; }
	virtual int NearestJoint( int, int, int, int ) const { return INVALID_JOINT; }
	virtual int NumGUISurfaces() const { return 0; }
	virtual const guiSurface_t* GetGUISurface( int ) const { return NULL; }
	virtual void ReadFromDemoFile( idDemoFile* ) {}
	virtual void WriteToDemoFile( idDemoFile* ) {}
	virtual int GetCurrentLod() const { return currentLod; }
	virtual void SetCurrentLod( const int lod ) { currentLod = lod; }
	virtual bool UpdateLod( const renderEntity_t*, const viewDef_s*, idRenderModel*, int& newLod ) const {
		newLod = currentLod;
		return false;
	}
	virtual bool NeedsReinstantiating( idRenderEntityLocal*, const viewDef_s*, int = 0 ) const { return false; }
	virtual int FindSurfaceId( const char* surfaceName ) {
		if ( surfaceName == NULL ) {
			return -1;
		}
		for ( int i = 0; i < surfaceNames.Num(); ++i ) {
			if ( !surfaceNames[ i ].Icmp( surfaceName ) ) {
				return surfaceIds[ i ];
			}
		}
		return -1;
	}
	virtual void SetBounds( const idBounds& bounds ) { modelBounds = bounds; }
	virtual void PurgePartialLoadableImages() {}
	virtual void LoadPartialLoadableImages( bool = false ) {}
	virtual bool IsFinishedPartialLoading() const { return true; }
	virtual idList< int >* GetFixedAreas() { return &fixedAreas; }
	virtual void SetFixedAreas( const idList< int >& areas ) { fixedAreas = areas; }
	virtual int NumMeshes( const int = 0 ) const { return modelSurfaces.Num(); }
	virtual idBounds CalcMeshBounds( int, const idJointMat*, const idVec3&, const idMat3&, bool ) { return modelBounds; }

	idRenderModel* CreateDynamicSnapshot( const renderEntity_t* entity ) const {
		if ( entity == NULL || modelJoints.Num() == 0 ) {
			return NULL;
		}
		if ( entity->joints == NULL || entity->numJoints != modelJoints.Num() ||
			 inverseDefaultPose.Num() != modelJoints.Num() || md5SkinSurfaces.Num() != modelSurfaces.Num() ) {
			common->DWarning( "Cannot instantiate animated model '%s': invalid joint data", modelName.c_str() );
			return NULL;
		}

		idList< idJointMat > transformedJoints;
		transformedJoints.SetNum( modelJoints.Num() );
		SIMDProcessor->MultiplyJoints( transformedJoints.Begin(), entity->joints,
			inverseDefaultPose.Begin(), modelJoints.Num() );

		const bool cpuSkinForVulkan = R_UseVulkanBackend();
		idRenderModelStatic* snapshot = new idRenderModelStatic;
		snapshot->InitEmpty( "_MD5_Snapshot_" );
		// The OpenGL renderer consumes the shared reference-pose vertex/index
		// caches plus joints and weights.  Vulkan does not have that pipeline yet,
		// so its compatibility path creates a fully owned, CPU-skinned snapshot.
		snapshot->ownsSurfaceGeometry = cpuSkinForVulkan;
		snapshot->modelBounds = entity->bounds.IsCleared() ? modelBounds : entity->bounds;

		for ( int surfaceIndex = 0; surfaceIndex < modelSurfaces.Num(); ++surfaceIndex ) {
			const modelSurface_t& sourceSurface = modelSurfaces[ surfaceIndex ];
			if ( sourceSurface.geometry == NULL ) {
				continue;
			}

			srfTriangles_t* geometry = NULL;
			if ( cpuSkinForVulkan ) {
				geometry = snapshot->AllocSurfaceTriangles(
					sourceSurface.geometry->numVerts,
					sourceSurface.geometry->numIndexes );
				if ( geometry == NULL ) {
					delete snapshot;
					return NULL;
				}
				const idDrawVert* sourceVerts = sourceSurface.geometry->verts;
				if ( sourceVerts == NULL || sourceSurface.geometry->indexes == NULL ) {
					delete snapshot;
					return NULL;
				}
				memcpy( geometry->verts, sourceVerts,
					geometry->numVerts * sizeof( geometry->verts[ 0 ] ) );
				memcpy( geometry->indexes, sourceSurface.geometry->indexes,
					geometry->numIndexes * sizeof( geometry->indexes[ 0 ] ) );
				geometry->bounds = sourceSurface.geometry->bounds;
				geometry->mode = sourceSurface.geometry->mode;
				geometry->dsFlags = sourceSurface.geometry->dsFlags & ~0x10;
				geometry->texCoordScale = sourceSurface.geometry->texCoordScale;
				geometry->tangentsCalculated = sourceSurface.geometry->tangentsCalculated;
				geometry->facePlanesCalculated = false;
				geometry->hardwareSkinnedSurface = false;
			} else {
				geometry = static_cast< srfTriangles_t* >( Mem_Alloc( sizeof( *geometry ) ) );
				*geometry = *sourceSurface.geometry;
			}
			geometry->joints = NULL;
			geometry->numJoints = 0;
			if ( !entity->bounds.IsCleared() ) {
				geometry->bounds = entity->bounds;
			}

			const md5SkinSurface_t& skin = md5SkinSurfaces[ surfaceIndex ];
			if ( cpuSkinForVulkan && !skin.noAnimate &&
				 skin.referencedJoints.Num() > 0 &&
				 skin.weights.Num() == geometry->numVerts ) {
				geometry->bounds.Clear();
				for ( int vertexIndex = 0; vertexIndex < geometry->numVerts; ++vertexIndex ) {
					const idDrawVert& source = sourceSurface.geometry->verts[ vertexIndex ];
					const vertWeight_t& vertexWeight = skin.weights[ vertexIndex ];
					idVec3 position( 0.0f, 0.0f, 0.0f );
					idVec3 normal( 0.0f, 0.0f, 0.0f );
					idVec3 tangent( 0.0f, 0.0f, 0.0f );
					float totalWeight = 0.0f;
					for ( int weightIndex = 0; weightIndex < MAX_WEIGHTS_PER_VERT; ++weightIndex ) {
						const int byteWeight = vertexWeight.weight[ weightIndex ];
						if ( byteWeight == 0 ) {
							continue;
						}
						// Generated MD5 weights contain the vertex-program register
						// offset for each compact-palette joint.  A joint matrix occupies
						// three vec4 registers, so values are 0, 3, 6, ... rather than
						// direct palette indexes.  Treating the byte as an index skipped
						// most weights and attached the rest to unrelated joints.
						const int jointRegister = vertexWeight.index[ weightIndex ];
						if ( jointRegister % 3 != 0 ) {
							continue;
						}
						const int localJoint = jointRegister / 3;
						if ( localJoint < 0 || localJoint >= skin.referencedJoints.Num() ) {
							continue;
						}
						const int sourceJoint = skin.referencedJoints[ localJoint ];
						if ( sourceJoint < 0 || sourceJoint >= transformedJoints.Num() ) {
							continue;
						}
						const float weight = byteWeight * ( 1.0f / 255.0f );
						const idJointMat& joint = transformedJoints[ sourceJoint ];
						position += ( joint * idVec4( source.xyz.x, source.xyz.y, source.xyz.z, 1.0f ) ) * weight;
						normal += ( joint * source.GetNormal() ) * weight;
						tangent += ( joint * source.GetTangent() ) * weight;
						totalWeight += weight;
					}
					idDrawVert& destination = geometry->verts[ vertexIndex ];
					if ( totalWeight > 0.0f ) {
						destination.xyz = position / totalWeight;
						normal.Normalize();
						tangent.Normalize();
						destination.SetNormal( normal );
						destination.SetTangent( tangent );
					}
					geometry->bounds += destination.xyz;
				}
			} else if ( !cpuSkinForVulkan && !skin.noAnimate && skin.referencedJoints.Num() > 0 && geometry->weightCache != NULL ) {
				geometry->numJoints = skin.referencedJoints.Num();
				geometry->joints = static_cast< idJointMat* >(
					Mem_AllocAligned( geometry->numJoints * sizeof( idJointMat ), ALIGN_16 ) );
				for ( int jointIndex = 0; jointIndex < geometry->numJoints; ++jointIndex ) {
					const int sourceJoint = skin.referencedJoints[ jointIndex ];
					if ( sourceJoint < 0 || sourceJoint >= transformedJoints.Num() ) {
						delete snapshot;
						return NULL;
					}
					geometry->joints[ jointIndex ] = transformedJoints[ sourceJoint ];
				}
				geometry->hardwareSkinnedSurface = true;
				geometry->dsFlags |= 0x10;
			}

			modelSurface_t surface = sourceSurface;
			surface.geometry = geometry;
			snapshot->modelSurfaces.Append( surface );
		}
		return snapshot;
	}

private:
	static void FreeSurfaceVertexCache( srfTriangles_t* triangles ) {
		if ( triangles == NULL ) {
			return;
		}
		if ( triangles->ambientCache != NULL ) {
			if ( renderSystem != NULL && renderSystem->IsOpenGLRunning() ) {
				vertexCache.Free( triangles->ambientCache );
			}
			triangles->ambientCache = NULL;
		}
		if ( triangles->indexCache != NULL ) {
			if ( renderSystem != NULL && renderSystem->IsOpenGLRunning() ) {
				vertexCache.Free( triangles->indexCache );
			}
			triangles->indexCache = NULL;
		}
		if ( triangles->weightCache != NULL ) {
			if ( renderSystem != NULL && renderSystem->IsOpenGLRunning() ) {
				vertexCache.Free( triangles->weightCache );
			}
			triangles->weightCache = NULL;
		}
	}

	void ClearSurfaces() {
		for ( int i = 0; i < modelSurfaces.Num(); i++ ) {
			if ( ownsSurfaceGeometry ) {
				FreeSurfaceTriangles( modelSurfaces[ i ].geometry );
			} else if ( modelSurfaces[ i ].geometry != NULL ) {
				Mem_FreeAligned( modelSurfaces[ i ].geometry->joints );
				Mem_Free( modelSurfaces[ i ].geometry );
			}
			modelSurfaces[ i ].geometry = NULL;
		}
		modelSurfaces.Clear();
		surfaceNames.Clear();
		surfaceIds.Clear();
		surfaceGroupMaterials.Clear();
		surfaceGroupNoAnimate.Clear();
		md5SkinSurfaces.Clear();
	}

	void SetFallbackBounds() {
		modelBounds[ 0 ].Set( -8.0f, -8.0f, -8.0f );
		modelBounds[ 1 ].Set( 8.0f, 8.0f, 8.0f );
	}

	/*
	================
	LoadTerrainModel

	Loads Darklight's editable heightfield model.  This is intentionally separate
	from ETQW sdPrimitiveTerrainFile/.sft terrain, which references an authored
	mesh and must never be displaced with the gameplay hm_heightmap.
	================
	*/
	bool LoadTerrainModel( const char* fileName ) {
		idLexer lexer;
		lexer.SetFlags( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS |
			LEXFL_ALLOWPATHNAMES | LEXFL_NOFATALERRORS );
		if ( !lexer.LoadFile( fileName ) ) {
			return false;
		}

		idToken token;
		if ( !lexer.ReadToken( &token ) || token.Icmp( "terrainModel" ) ||
			!lexer.ReadToken( &token ) || token.GetIntValue() != 1 ||
			!lexer.ExpectTokenString( "{" ) ) {
			return false;
		}

		idStr heightMap;
		idStr weightMap;
		idStr materialName;
		int samples = 0;
		float worldSize = 0.0f;
		while ( lexer.ReadToken( &token ) && token != "}" ) {
			if ( !token.Icmp( "heightMap" ) ) {
				if ( lexer.ReadToken( &token ) ) heightMap = token;
			} else if ( !token.Icmp( "weightMap" ) ) {
				if ( lexer.ReadToken( &token ) ) weightMap = token;
			} else if ( !token.Icmp( "material" ) ) {
				if ( lexer.ReadToken( &token ) ) materialName = token;
			} else if ( !token.Icmp( "samples" ) ) {
				samples = lexer.ParseInt();
			} else if ( !token.Icmp( "worldSize" ) ) {
				worldSize = lexer.ParseFloat();
			} else {
				lexer.ReadToken( &token );
			}
		}
		if ( lexer.HadError() || heightMap.IsEmpty() || materialName.IsEmpty() ||
			samples < 3 || samples > 513 || worldSize <= 0.0f ) {
			return false;
		}

		static const int TERRAIN_HEIGHT_MAGIC = 1213482316; // little-endian "DLHT"
		idFile* heightFile = fileSystem->OpenFileRead( heightMap );
		if ( heightFile == NULL ) {
			common->Warning( "Terrain model '%s' could not read heightfield '%s'",
				fileName, heightMap.c_str() );
			return false;
		}
		int magic = 0;
		int version = 0;
		int fileSamples = 0;
		bool valid = heightFile->ReadInt( magic ) == sizeof( magic ) &&
			heightFile->ReadInt( version ) == sizeof( version ) &&
			heightFile->ReadInt( fileSamples ) == sizeof( fileSamples ) &&
			magic == TERRAIN_HEIGHT_MAGIC && version == 1 && fileSamples == samples;
		idList< float > heights;
		heights.SetNum( samples * samples );
		for ( int i = 0; valid && i < heights.Num(); ++i ) {
			valid = heightFile->ReadFloat( heights[i] ) == sizeof( heights[i] );
		}
		const unsigned int heightTimeStamp =
			static_cast< unsigned int >( heightFile->Timestamp() );
		fileSystem->CloseFile( heightFile );
		if ( !valid ) {
			common->Warning( "Terrain model '%s' has an invalid heightfield '%s'",
				fileName, heightMap.c_str() );
			return false;
		}

		// Four normalized layer weights are stored in vertex colors.  Later file
		// versions append editor/compiler metadata after these bytes.
		idList< byte > weights;
		weights.SetNum( samples * samples * 4 );
		memset( weights.Begin(), 0, weights.Num() );
		for ( int i = 0; i < samples * samples; ++i ) {
			weights[i * 4] = 255;
		}
		if ( !weightMap.IsEmpty() ) {
			static const int TERRAIN_WEIGHT_MAGIC = 1415007300; // little-endian "DLWT"
			idFile* weightFile = fileSystem->OpenFileRead( weightMap );
			if ( weightFile != NULL ) {
				int weightMagic = 0;
				int weightVersion = 0;
				int weightSamples = 0;
				int channels = 0;
				const bool validWeights =
					weightFile->ReadInt( weightMagic ) == sizeof( weightMagic ) &&
					weightFile->ReadInt( weightVersion ) == sizeof( weightVersion ) &&
					weightFile->ReadInt( weightSamples ) == sizeof( weightSamples ) &&
					weightFile->ReadInt( channels ) == sizeof( channels ) &&
					weightMagic == TERRAIN_WEIGHT_MAGIC &&
					weightVersion >= 1 && weightVersion <= 4 &&
					weightSamples == samples && channels == 4 &&
					weightFile->Read( weights.Begin(), weights.Num() ) == weights.Num();
				fileSystem->CloseFile( weightFile );
				if ( !validWeights ) {
					memset( weights.Begin(), 0, weights.Num() );
					for ( int i = 0; i < samples * samples; ++i ) {
						weights[i * 4] = 255;
					}
				}
			}
		}

		const idMaterial* material = declHolder.FindMaterial( materialName );
		const bool lowRangeTexCoords =
			material->TestMaterialFlag( MF_LOWRANGEUVCOMPRESS );
		const float spacing = worldSize / ( samples - 1 );
		const float halfSize = worldSize * 0.5f;
		const int maximumTileCells = 254;
		int surfaceId = 0;
		modelBounds.Clear();

		// ETQW uses 16-bit model indexes.  Split 257/513-sample heightfields into
		// overlapping tiles so every surface stays below 65536 local vertices.
		for ( int tileY = 0; tileY < samples - 1;
			tileY += maximumTileCells ) {
			const int cellsY = Min( maximumTileCells, samples - 1 - tileY );
			for ( int tileX = 0; tileX < samples - 1;
				tileX += maximumTileCells ) {
				const int cellsX = Min( maximumTileCells, samples - 1 - tileX );
				const int rowVertices = cellsX + 1;
				const int vertexCount = rowVertices * ( cellsY + 1 );
				const int indexCount = cellsX * cellsY * 6;
				srfTriangles_t* triangles =
					AllocSurfaceTriangles( vertexCount, indexCount );
				if ( triangles == NULL ) {
					ClearSurfaces();
					return false;
				}
				triangles->bounds.Clear();

				for ( int localY = 0; localY <= cellsY; ++localY ) {
					const int sourceY = tileY + localY;
					const int topY = Max( sourceY - 1, 0 );
					const int bottomY = Min( sourceY + 1, samples - 1 );
					for ( int localX = 0; localX <= cellsX; ++localX ) {
						const int sourceX = tileX + localX;
						const int leftX = Max( sourceX - 1, 0 );
						const int rightX = Min( sourceX + 1, samples - 1 );
						const float height = heights[sourceY * samples + sourceX];
						const float dhdx =
							( heights[sourceY * samples + rightX] -
							heights[sourceY * samples + leftX] ) /
							( ( rightX - leftX ) * spacing );
						const float dhdy =
							( heights[topY * samples + sourceX] -
							heights[bottomY * samples + sourceX] ) /
							( ( bottomY - topY ) * spacing );

						idDrawVert& vertex =
							triangles->verts[localY * rowVertices + localX];
						vertex.Clear();
						vertex.xyz.Set( sourceX * spacing - halfSize,
							halfSize - sourceY * spacing, height );
						vertex.SetST( lowRangeTexCoords,
							idVec2( sourceX / static_cast< float >( samples - 1 ),
							1.0f - sourceY / static_cast< float >( samples - 1 ) ) );
						idVec3 normal( -dhdx, -dhdy, 1.0f );
						normal.Normalize();
						idVec3 tangent( 1.0f, 0.0f, dhdx );
						tangent.Normalize();
						vertex.SetNormal( normal );
						vertex.SetTangent( tangent );
						vertex.SetBiTangentSign( 1.0f );
						memcpy( vertex.color,
							weights.Begin() + ( sourceY * samples + sourceX ) * 4, 4 );
						triangles->bounds.AddPoint( vertex.xyz );
					}
				}

				int outputIndex = 0;
				for ( int localY = 0; localY < cellsY; ++localY ) {
					for ( int localX = 0; localX < cellsX; ++localX ) {
						const int v0 = localY * rowVertices + localX;
						const int v1 = v0 + 1;
						const int v3 = v0 + rowVertices;
						const int v2 = v3 + 1;
						triangles->indexes[outputIndex++] =
							static_cast< glIndex_t >( v0 );
						triangles->indexes[outputIndex++] =
							static_cast< glIndex_t >( v1 );
						triangles->indexes[outputIndex++] =
							static_cast< glIndex_t >( v2 );
						triangles->indexes[outputIndex++] =
							static_cast< glIndex_t >( v0 );
						triangles->indexes[outputIndex++] =
							static_cast< glIndex_t >( v2 );
						triangles->indexes[outputIndex++] =
							static_cast< glIndex_t >( v3 );
					}
				}
				triangles->tangentsCalculated = true;
				triangles->facePlanesCalculated = false;

				modelSurface_t surface;
				surface.id = surfaceId++;
				surface.material = material;
				surface.geometry = triangles;
				AddSurface( surface );
			}
		}

		timeStamp = heightTimeStamp;
		common->Printf( "Loaded editable terrain %s: %d x %d, %d surfaces, bounds (%s)-(%s)\n",
			fileName, samples, samples, surfaceId, modelBounds[0].ToString(),
			modelBounds[1].ToString() );
		return surfaceId > 0;
	}

	bool LoadLWOModel( const char* fileName ) {
		unsigned int failId = 0;
		int failPosition = 0;
		lwObject* object = lwGetObject( fileName, &failId, &failPosition );
		if ( object == NULL || object->layer == NULL || object->surf == NULL ) {
			if ( object != NULL ) {
				lwFreeObject( object );
			}
			common->Warning( "Could not load LWO model '%s' (chunk %08x at %d)",
				fileName, failId, failPosition );
			return false;
		}

		modelBounds.Clear();
		timeStamp = object->timeStamp;
		const int maximumTrianglesPerSurface = 65532 / 3;
		int loadedTriangles = 0;
		int surfaceId = 0;

		// Darklight's native loader intentionally consumes the first LightWave
		// layer. ETQW source models (including terrain) use that same layout.
		lwLayer* layer = object->layer;
		for ( lwSurface* lwoSurface = object->surf; lwoSurface != NULL;
			lwoSurface = lwoSurface->next ) {
			idList< lwPolygon* > polygons;
			for ( int polygonIndex = 0; polygonIndex < layer->polygon.count;
				++polygonIndex ) {
				lwPolygon* polygon = &layer->polygon.pol[ polygonIndex ];
				if ( polygon->surf == lwoSurface && polygon->nverts == 3 ) {
					polygons.Append( polygon );
				}
			}
			if ( polygons.Num() == 0 ) {
				continue;
			}

			const idMaterial* material = declHolder.FindMaterial(
				lwoSurface->name != NULL ? lwoSurface->name : "_default" );
			const bool lowRangeTexCoords = material->TestMaterialFlag(
				MF_LOWRANGEUVCOMPRESS );
			for ( int firstPolygon = 0; firstPolygon < polygons.Num();
				firstPolygon += maximumTrianglesPerSurface ) {
				const int triangleCount = Min( maximumTrianglesPerSurface,
					polygons.Num() - firstPolygon );
				const int vertexCount = triangleCount * 3;
				srfTriangles_t* triangles = AllocSurfaceTriangles( vertexCount,
					vertexCount );
				if ( triangles == NULL ) {
					lwFreeObject( object );
					return false;
				}
				triangles->bounds.Clear();
				idList< idDrawVert > uniqueVertices;
				uniqueVertices.PreAllocate( vertexCount );
				idHashIndex vertexHash( 4096, vertexCount );

				int outputIndex = 0;
				for ( int polygonOffset = 0; polygonOffset < triangleCount;
					++polygonOffset ) {
					lwPolygon* polygon = polygons[ firstPolygon + polygonOffset ];
					for ( int corner = 0; corner < 3; ++corner, ++outputIndex ) {
						const lwPolVert& polygonVertex = polygon->v[ corner ];
						const lwPoint& point = layer->point.pt[ polygonVertex.index ];
						idDrawVert vertex;
						vertex.Clear();
						vertex.xyz.Set( point.pos[0], point.pos[2], point.pos[1] );

						idVec2 textureCoordinate( 0.0f, 0.0f );
						byte color[4] = {
							static_cast< byte >( idMath::ClampInt( 0, 255,
								idMath::Ftoi( lwoSurface->color.rgb[0] * 255.0f ) ) ),
							static_cast< byte >( idMath::ClampInt( 0, 255,
								idMath::Ftoi( lwoSurface->color.rgb[1] * 255.0f ) ) ),
							static_cast< byte >( idMath::ClampInt( 0, 255,
								idMath::Ftoi( lwoSurface->color.rgb[2] * 255.0f ) ) ),
							255
						};
						for ( int mapIndex = 0; mapIndex < point.nvmaps;
							++mapIndex ) {
							const lwVMapPt& map = point.vm[ mapIndex ];
							if ( map.vmap->type == LWID_('T','X','U','V') ) {
								textureCoordinate.Set( map.vmap->val[map.index][0],
									1.0f - map.vmap->val[map.index][1] );
							} else if ( map.vmap->type == LWID_('R','G','B','A') ) {
								for ( int channel = 0; channel < 4; ++channel ) {
									color[channel] = static_cast< byte >(
										idMath::ClampInt( 0, 255, idMath::Ftoi(
										map.vmap->val[map.index][channel] * 255.0f ) ) );
								}
							}
						}
						for ( int mapIndex = 0; mapIndex < polygonVertex.nvmaps;
							++mapIndex ) {
							const lwVMapPt& map = polygonVertex.vm[ mapIndex ];
							if ( map.vmap->type == LWID_('T','X','U','V') ) {
								textureCoordinate.Set( map.vmap->val[map.index][0],
									1.0f - map.vmap->val[map.index][1] );
							} else if ( map.vmap->type == LWID_('R','G','B','A') ) {
								for ( int channel = 0; channel < 4; ++channel ) {
									color[channel] = static_cast< byte >(
										idMath::ClampInt( 0, 255, idMath::Ftoi(
										map.vmap->val[map.index][channel] * 255.0f ) ) );
								}
							}
						}

						idVec3 normal( polygonVertex.norm[0],
							polygonVertex.norm[2], polygonVertex.norm[1] );
						normal.FixDegenerateNormal();
						idVec3 tangent = normal.Cross( idVec3( 0.0f, 0.0f, 1.0f ) );
						if ( tangent.Normalize() < 0.001f ) {
							tangent = normal.Cross( idVec3( 1.0f, 0.0f, 0.0f ) );
							tangent.Normalize();
						}
						vertex.SetST( lowRangeTexCoords, textureCoordinate );
						vertex.SetNormal( normal );
						vertex.SetTangent( tangent );
						vertex.SetBiTangentSign( 1.0f );
						memcpy( vertex.color, color, sizeof( color ) );

						const int hashKey = vertexHash.GenerateKey( vertex.xyz );
						int compactVertex = idHashIndex::NULL_INDEX;
						for ( int candidate = vertexHash.GetFirst( hashKey );
							candidate != idHashIndex::NULL_INDEX;
							candidate = vertexHash.GetNext( candidate ) ) {
							if ( memcmp( &uniqueVertices[candidate], &vertex,
								sizeof( vertex ) ) == 0 ) {
								compactVertex = candidate;
								break;
							}
						}
						if ( compactVertex == idHashIndex::NULL_INDEX ) {
							compactVertex = uniqueVertices.Append( vertex );
							vertexHash.Add( hashKey, compactVertex );
						}
						triangles->indexes[ outputIndex ] =
							static_cast< glIndex_t >( compactVertex );
						triangles->bounds.AddPoint( vertex.xyz );
					}
				}
				memcpy( triangles->verts, uniqueVertices.Begin(),
					uniqueVertices.Num() * sizeof( uniqueVertices[0] ) );
				triangles->numVerts = uniqueVertices.Num();
				triangles->tangentsCalculated = true;
				modelSurface_t surface;
				surface.id = surfaceId++;
				surface.material = material;
				surface.geometry = triangles;
				modelSurfaces.Append( surface );
				modelBounds += triangles->bounds;
				loadedTriangles += triangleCount;
			}
		}
		lwFreeObject( object );
		if ( loadedTriangles == 0 ) {
			modelBounds.Clear();
			return false;
		}
		common->Printf( "Loaded source LWO %s: %d surfaces, %d triangles\n",
			fileName, modelSurfaces.Num(), loadedTriangles );
		return true;
	}

	bool LoadModelB( const char* fileName ) {
		if ( fileSystem == NULL || fileName == NULL ) {
			return false;
		}
		idFile* file = fileSystem->OpenFileRead( fileName, true, NULL, true );
		if ( file == NULL ) {
			return false;
		}

		int version = 0;
		int numSurfaces = 0;
		idBounds fileBounds;
		bool valid = file->ReadInt( version ) == sizeof( version ) && version == 1;
		valid = valid && file->ReadVec3( fileBounds[ 0 ] ) == sizeof( idVec3 );
		valid = valid && file->ReadVec3( fileBounds[ 1 ] ) == sizeof( idVec3 );
		valid = valid && file->ReadInt( numSurfaces ) == sizeof( numSurfaces );
		valid = valid && numSurfaces >= 0 && numSurfaces <= 65536;

		for ( int surfaceIndex = 0; valid && surfaceIndex < numSurfaces; surfaceIndex++ ) {
			idBounds surfaceBounds;
			int numVerts = 0;
			int numIndexes = 0;
			idStr materialName;
			valid = file->ReadVec3( surfaceBounds[ 0 ] ) == sizeof( idVec3 );
			valid = valid && file->ReadVec3( surfaceBounds[ 1 ] ) == sizeof( idVec3 );
			valid = valid && file->ReadInt( numVerts ) == sizeof( numVerts );
			valid = valid && file->ReadInt( numIndexes ) == sizeof( numIndexes );
			valid = valid && numVerts >= 0 && numVerts <= ( 1 << 24 );
			valid = valid && numIndexes >= 0 && numIndexes <= ( 1 << 27 );
			valid = valid && file->ReadString( materialName ) >= 0 && !materialName.IsEmpty();
			if ( !valid ) {
				break;
			}

			srfTriangles_t* triangles = AllocSurfaceTriangles( numVerts, numIndexes );
			if ( triangles == NULL ) {
				valid = false;
				break;
			}
			triangles->bounds = surfaceBounds;
			const idMaterial* material = declHolder.FindMaterial( materialName );
			const bool lowRangeTexCoords = material->TestMaterialFlag( MF_LOWRANGEUVCOMPRESS );
			for ( int vertexIndex = 0; valid && vertexIndex < numVerts; vertexIndex++ ) {
				idVec2 st;
				idVec3 normal;
				idVec4 tangent;
				idDrawVert& vertex = triangles->verts[ vertexIndex ];
				valid = file->ReadVec3( vertex.xyz ) == sizeof( idVec3 );
				valid = valid && file->ReadVec2( st ) == sizeof( idVec2 );
				valid = valid && file->ReadVec3( normal ) == sizeof( idVec3 );
				valid = valid && file->ReadVec4( tangent ) == sizeof( idVec4 );
				for ( int colorIndex = 0; valid && colorIndex < 4; colorIndex++ ) {
					valid = file->ReadUnsignedChar( vertex.color[ colorIndex ] ) == sizeof( byte );
				}
				if ( valid ) {
					vertex.SetST( lowRangeTexCoords, st );
					vertex.SetNormal( normal );
					vertex.SetTangent( tangent.ToVec3() );
					vertex.SetBiTangentSign( tangent.w );
				}
			}

			for ( int index = 0; valid && index < numIndexes; index++ ) {
				unsigned short value = 0;
				valid = file->ReadUnsignedShort( value ) == sizeof( value ) && value < numVerts;
				if ( valid ) {
					triangles->indexes[ index ] = static_cast< glIndex_t >( value );
				}
			}

			if ( !valid ) {
				FreeSurfaceTriangles( triangles );
				break;
			}

			modelSurface_t surface;
			surface.id = 0;
			surface.material = material;
			surface.geometry = triangles;
			modelSurfaces.Append( surface );
		}

		if ( valid ) {
			modelBounds = fileBounds;
			timeStamp = file->Timestamp();
		} else {
			ClearSurfaces();
		}
		fileSystem->CloseFile( file );
		return valid;
	}

	int FindMD5SurfaceGroup( const idStr& materialName, bool noAnimate ) const {
		for ( int i = 0; i < surfaceGroupMaterials.Num(); ++i ) {
			if ( surfaceGroupNoAnimate[ i ] == static_cast< int >( noAnimate ) &&
				 !surfaceGroupMaterials[ i ].Icmp( materialName ) ) {
				return i;
			}
		}
		return -1;
	}

	bool AppendMD5Indexes( int surfaceId, const idList< unsigned short >& indexes ) {
		if ( surfaceId < 0 || surfaceId >= modelSurfaces.Num() ||
			 modelSurfaces[ surfaceId ].geometry == NULL ) {
			return false;
		}

		srfTriangles_t* triangles = modelSurfaces[ surfaceId ].geometry;
		for ( int i = 0; i < indexes.Num(); ++i ) {
			if ( indexes[ i ] >= triangles->numVerts ) {
				return false;
			}
		}

		const long long newCount64 = static_cast< long long >( triangles->numIndexes ) + indexes.Num();
		if ( newCount64 > 0x7fffffffLL ) {
			return false;
		}
		const int oldCount = triangles->numIndexes;
		const int newCount = static_cast< int >( newCount64 );
		glIndex_t* newIndexes = newCount > 0 ?
			static_cast< glIndex_t* >( Mem_Alloc( newCount * sizeof( glIndex_t ) ) ) : NULL;
		for ( int i = 0; i < oldCount; ++i ) {
			newIndexes[ i ] = triangles->indexes[ i ];
		}
		for ( int i = 0; i < indexes.Num(); ++i ) {
			newIndexes[ oldCount + i ] = static_cast< glIndex_t >( indexes[ i ] );
		}
		Mem_Free( triangles->indexes );
		triangles->indexes = newIndexes;
		triangles->numIndexes = newCount;
		triangles->numAllocedIndices = newCount;
		return true;
	}

	bool ReadMD5MeshBinary( idFile* file, bool keepSurface ) {
		idStr meshName;
		idStr materialName;
		int meshIndex = 0;
		int lodZeroIndex = 0;
		int surfaceNum = -1;
		int numMD5Verts = 0;
		bool noAnimate = false;
		bool deferred = false;
		bool singleWeight = false;
		int numVerts = 0;
		int numTris = 0;
		int numIndexes = 0;

		bool valid = file->ReadString( meshName ) > 0 && !meshName.IsEmpty();
		valid = valid && file->ReadInt( meshIndex ) == sizeof( meshIndex );
		valid = valid && file->ReadInt( lodZeroIndex ) == sizeof( lodZeroIndex );
		valid = valid && file->ReadInt( surfaceNum ) == sizeof( surfaceNum );
		valid = valid && file->ReadInt( numMD5Verts ) == sizeof( numMD5Verts );
		valid = valid && file->ReadBool( noAnimate ) == sizeof( byte );
		valid = valid && file->ReadBool( deferred ) == sizeof( byte );
		valid = valid && file->ReadBool( singleWeight ) == sizeof( byte );
		valid = valid && file->ReadString( materialName ) > 0 && !materialName.IsEmpty();
		valid = valid && file->ReadInt( numVerts ) == sizeof( numVerts );
		valid = valid && file->ReadInt( numTris ) == sizeof( numTris );
		valid = valid && file->ReadInt( numIndexes ) == sizeof( numIndexes );
		valid = valid && numMD5Verts >= 0 && numMD5Verts <= ( 1 << 24 );
		valid = valid && numVerts >= 0 && numVerts <= ( 1 << 24 );
		valid = valid && numTris >= 0 && numTris <= ( 1 << 24 );
		valid = valid && numIndexes >= 0 && numIndexes <= ( 1 << 27 );
		if ( !valid ) {
			return false;
		}

		idList< unsigned short > indexes;
		if ( keepSurface ) {
			indexes.SetNum( numIndexes );
			for ( int i = 0; valid && i < numIndexes; ++i ) {
				valid = file->ReadUnsignedShort( indexes[ i ] ) == sizeof( indexes[ i ] );
			}
		} else {
			int byteCount = 0;
			valid = CheckedByteCount( numIndexes, sizeof( unsigned short ), byteCount ) && ReadDiscard( file, byteCount );
		}

		int numSilEdges = 0;
		valid = valid && file->ReadInt( numSilEdges ) == sizeof( numSilEdges );
		valid = valid && numSilEdges >= 0 && numSilEdges <= ( 1 << 25 );
		int byteCount = 0;
		valid = valid && CheckedByteCount( numSilEdges, 4 * sizeof( unsigned short ), byteCount ) && ReadDiscard( file, byteCount );
		if ( valid && !deferred ) {
			valid = CheckedByteCount( numVerts, sizeof( short ), byteCount ) && ReadDiscard( file, byteCount );
		}

		int numSourceVerts = 0;
		int numOutputVerts = 0;
		int numMirroredVerts = 0;
		int deformNumIndexes = 0;
		int deformNumSilEdges = 0;
		int numReferencedJoints = 0;
		valid = valid && file->ReadInt( numSourceVerts ) == sizeof( numSourceVerts );
		valid = valid && file->ReadInt( numOutputVerts ) == sizeof( numOutputVerts );
		valid = valid && file->ReadInt( numMirroredVerts ) == sizeof( numMirroredVerts );
		valid = valid && numSourceVerts >= 0 && numSourceVerts <= ( 1 << 24 );
		valid = valid && numOutputVerts >= 0 && numOutputVerts <= ( 1 << 24 );
		valid = valid && numMirroredVerts >= 0 && numMirroredVerts <= numOutputVerts;
		valid = valid && CheckedByteCount( numMirroredVerts, sizeof( int ), byteCount ) && ReadDiscard( file, byteCount );
		valid = valid && file->ReadInt( deformNumIndexes ) == sizeof( deformNumIndexes );
		valid = valid && file->ReadInt( deformNumSilEdges ) == sizeof( deformNumSilEdges );
		valid = valid && file->ReadInt( numReferencedJoints ) == sizeof( numReferencedJoints );
		valid = valid && deformNumIndexes >= 0 && deformNumIndexes <= ( 1 << 27 );
		valid = valid && deformNumSilEdges >= 0 && deformNumSilEdges <= ( 1 << 25 );
		valid = valid && numReferencedJoints >= 0 && numReferencedJoints <= 255;
		idList< byte > referencedJoints;
		if ( valid && keepSurface && !deferred ) {
			referencedJoints.SetNum( numReferencedJoints );
			for ( int i = 0; valid && i < numReferencedJoints; ++i ) {
				valid = file->ReadUnsignedChar( referencedJoints[ i ] ) == sizeof( byte );
			}
		} else {
			valid = valid && ReadDiscard( file, numReferencedJoints );
		}
		if ( !valid ) {
			return false;
		}

		// A deferred MD5 mesh owns only an index range.  Its vertices are stored
		// once on the first mesh in the matching material/noAnimate group.
		srfTriangles_t* triangles = keepSurface && !deferred ? AllocSurfaceTriangles( numOutputVerts, numIndexes ) : NULL;
		if ( keepSurface && !deferred && triangles == NULL ) {
			return false;
		}
		if ( triangles != NULL ) {
			triangles->bounds.Clear();
			triangles->tangentsCalculated = true;
		}

		const idMaterial* material = keepSurface ? declHolder.FindMaterial( materialName, true ) : NULL;
		const bool lowRangeTexCoords = material != NULL && material->TestMaterialFlag( MF_LOWRANGEUVCOMPRESS );
		if ( keepSurface && !deferred ) {
			for ( int i = 0; valid && i < numOutputVerts; ++i ) {
				idVec2 st;
				idVec3 normal;
				idVec4 tangent;
				idDrawVert& vertex = triangles->verts[ i ];
				valid = file->ReadVec3( vertex.xyz ) == sizeof( idVec3 );
				valid = valid && file->ReadVec2( st ) == sizeof( idVec2 );
				valid = valid && file->ReadVec3( normal ) == sizeof( idVec3 );
				valid = valid && file->ReadVec4( tangent ) == sizeof( idVec4 );
				valid = valid && file->Read( vertex.color, 4 ) == 4;
				if ( valid ) {
					vertex.SetST( lowRangeTexCoords, st );
					vertex.SetNormal( normal );
					vertex.SetTangent( tangent.ToVec3() );
					vertex.SetBiTangentSign( tangent.w );
					triangles->bounds += vertex.xyz;
				}
			}
		} else {
			valid = CheckedByteCount( numOutputVerts, 12 * sizeof( float ) + 4, byteCount ) && ReadDiscard( file, byteCount );
		}

		idList< vertWeight_t > weights;
		if ( valid && keepSurface && !deferred ) {
			weights.SetNum( numOutputVerts );
			if ( weights.Num() > 0 ) {
				memset( weights.Begin(), 0, weights.Num() * sizeof( vertWeight_t ) );
			}
			for ( int vertexIndex = 0; valid && vertexIndex < numOutputVerts; ++vertexIndex ) {
				if ( singleWeight ) {
					valid = file->ReadUnsignedChar( weights[ vertexIndex ].index[ 0 ] ) == sizeof( byte );
					weights[ vertexIndex ].weight[ 0 ] = 255;
				} else {
					for ( int weightIndex = 0; valid && weightIndex < MAX_WEIGHTS_PER_VERT; ++weightIndex ) {
						valid = file->ReadUnsignedChar( weights[ vertexIndex ].index[ weightIndex ] ) == sizeof( byte );
						valid = valid && file->ReadUnsignedChar( weights[ vertexIndex ].weight[ weightIndex ] ) == sizeof( byte );
					}
				}
			}
		} else {
			const int weightStride = singleWeight ? 1 : 8;
			valid = valid && CheckedByteCount( numOutputVerts, weightStride, byteCount ) && ReadDiscard( file, byteCount );
		}
		bool perfectHull = false;
		float surfaceArea = 0.0f;
		valid = valid && file->ReadBool( perfectHull ) == sizeof( byte );
		valid = valid && file->ReadFloat( surfaceArea ) == sizeof( surfaceArea );

		if ( triangles != NULL ) {
			for ( int i = 0; valid && i < numIndexes; ++i ) {
				valid = indexes[ i ] < numOutputVerts;
				if ( valid ) {
					triangles->indexes[ i ] = static_cast< glIndex_t >( indexes[ i ] );
				}
			}
			if ( valid ) {
				const int groupId = modelSurfaces.Num();
				modelSurface_t surface;
				surface.id = groupId;
				surface.material = material;
				surface.geometry = triangles;
				modelSurfaces.Append( surface );
				md5SkinSurface_t& skin = md5SkinSurfaces.Alloc();
				skin.referencedJoints = referencedJoints;
				skin.weights = weights;
				skin.noAnimate = noAnimate;
				surfaceGroupMaterials.Append( materialName );
				surfaceGroupNoAnimate.Append( static_cast< int >( noAnimate ) );
				surfaceNames.Append( meshName );
				surfaceIds.Append( groupId );
			} else {
				FreeSurfaceTriangles( triangles );
			}
		} else if ( valid && keepSurface && deferred ) {
			const int groupId = FindMD5SurfaceGroup( materialName, noAnimate );
			if ( groupId < 0 || !AppendMD5Indexes( groupId, indexes ) ) {
				common->Warning( "MD5 binary '%s': deferred mesh '%s' has no compatible surface group",
					modelName.c_str(), meshName.c_str() );
				valid = false;
			} else {
				surfaceNames.Append( meshName );
				surfaceIds.Append( groupId );
			}
		}

		return valid;
	}

	bool LoadMD5BinaryReferencePose() {
		if ( fileSystem == NULL ) {
			return false;
		}

		idStr generated = "generated/md5binary/";
		generated += modelName;
		generated.StripFileExtension();
		generated.SetFileExtension( "md5b" );
		idFile* sourceFile = fileSystem->OpenFileRead( generated, true, NULL, true );
		if ( sourceFile == NULL ) {
			return false;
		}
		idFile* file = fileSystem->OpenBufferedFile( sourceFile );

		int version = 0;
		int numLods = 0;
		int numJoints = 0;
		bool valid = file->ReadInt( version ) == sizeof( version ) && version == 1;
		valid = valid && file->ReadInt( numLods ) == sizeof( numLods ) && numLods > 0;
		valid = valid && file->ReadInt( numJoints ) == sizeof( numJoints ) && numJoints > 0 && numJoints < 4096;
		if ( !valid ) {
			fileSystem->CloseFile( file );
			return false;
		}

		modelJoints.SetNum( numJoints );
		defaultPose.SetNum( numJoints );
		inverseDefaultPose.SetNum( numJoints );
		idList< int > parents;
		parents.SetNum( numJoints );
		for ( int i = 0; valid && i < numJoints; i++ ) {
			valid = file->ReadString( modelJoints[ i ].name ) > 0;
			valid = valid && file->ReadInt( parents[ i ] ) == sizeof( parents[ i ] );
			valid = valid && file->ReadFloat( defaultPose[ i ].q.x ) == sizeof( float );
			valid = valid && file->ReadFloat( defaultPose[ i ].q.y ) == sizeof( float );
			valid = valid && file->ReadFloat( defaultPose[ i ].q.z ) == sizeof( float );
			valid = valid && file->ReadFloat( defaultPose[ i ].q.w ) == sizeof( float );
			valid = valid && file->ReadVec3( defaultPose[ i ].t ) == sizeof( idVec3 );
			valid = valid && file->ReadFloat( defaultPose[ i ].w ) == sizeof( float );

			for ( int j = 0; valid && j < 12; j++ ) {
				valid = file->ReadFloat( inverseDefaultPose[ i ].ToFloatPtr()[ j ] ) == sizeof( float );
			}
		}

		for ( int lod = 0; valid && lod < numLods; ++lod ) {
			int numMeshes = 0;
			valid = file->ReadInt( numMeshes ) == sizeof( numMeshes );
			valid = valid && numMeshes >= 0 && numMeshes <= 65536;
			for ( int mesh = 0; valid && mesh < numMeshes; ++mesh ) {
				valid = ReadMD5MeshBinary( file, lod == 0 );
			}
		}

		idBounds fileBounds;
		valid = valid && file->ReadVec3( fileBounds[ 0 ] ) == sizeof( idVec3 );
		valid = valid && file->ReadVec3( fileBounds[ 1 ] ) == sizeof( idVec3 );
		if ( valid ) {
			for ( int i = 0; i < numJoints; i++ ) {
				modelJoints[ i ].parent = parents[ i ] >= 0 && parents[ i ] < numJoints ? &modelJoints[ parents[ i ] ] : NULL;
			}
			modelBounds = fileBounds;
			timeStamp = file->Timestamp();
		} else {
			ClearSurfaces();
			modelJoints.Clear();
			defaultPose.Clear();
			inverseDefaultPose.Clear();
		}
		fileSystem->CloseFile( file );
		return valid;
	}

	idStr modelName;
	idBounds modelBounds;
	idList< modelSurface_t > modelSurfaces;
	idStrList surfaceNames;
	idList< int > surfaceIds;
	idStrList surfaceGroupMaterials;
	idList< int > surfaceGroupNoAnimate;
	idList< idMD5Joint > modelJoints;
	idList< idJointQuat > defaultPose;
	idList< idJointMat > inverseDefaultPose;
	idList< md5SkinSurface_t > md5SkinSurfaces;
	idList< int > fixedAreas;
	bool loaded;
	bool defaulted;
	bool reloadable;
	bool referencedOutsideLevelLoad;
	bool levelLoadReferenced;
	unsigned timeStamp;
	int currentLod;
	bool ownsSurfaceGeometry;
};

}

idRenderModel* R_AllocStaticModel() {
	return new idRenderModelStatic;
}

idRenderModel* R_InstantiateDynamicModel( idRenderModel* model, const renderEntity_t* entity ) {
	if ( model == NULL || model->IsDynamicModel() == DM_STATIC ) {
		return model;
	}
	idRenderModelStatic* staticModel = dynamic_cast< idRenderModelStatic* >( model );
	return staticModel != NULL ? staticModel->CreateDynamicSnapshot( entity ) : model;
}
