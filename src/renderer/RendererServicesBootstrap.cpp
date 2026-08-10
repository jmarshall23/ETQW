// Copyright (C) 2007 Id Software, Inc.
//
// Public renderer services reconstructed from the ETQW SDK/PDB boundary.
// Model and GPU image creation remain backend-neutral, while the image file
// helpers are functional so screenshots and filesystem image probes work.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Image.h"
#include "Model.h"
#include "RenderSystem.h"
#include "ModelManager.h"
#include "../decllib/declTypeHolder.h"

extern glconfig_t glConfig;

namespace {

const char* imageFilterValues[] = {
	"GL_LINEAR_MIPMAP_NEAREST",
	"GL_LINEAR_MIPMAP_LINEAR",
	"GL_NEAREST",
	"GL_LINEAR",
	"GL_NEAREST_MIPMAP_NEAREST",
	"GL_NEAREST_MIPMAP_LINEAR",
	NULL
};

}

idCVar idImageManager::image_filter( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_RENDERER | CVAR_ARCHIVE,
	"changes texture filtering on mipmapped images", imageFilterValues );
idCVar idImageManager::image_anisotropy( "image_anisotropy", "1", CVAR_RENDERER | CVAR_ARCHIVE,
	"set the maximum texture anisotropy if available" );
idCVar idImageManager::image_lodbias( "image_lodbias", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"change lod bias on mipmapped images", -1.0f, 1.0f );
idCVar idImageManager::image_roundDown( "image_roundDown", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"round bad sizes down to nearest power of two" );
idCVar idImageManager::image_colorMipLevels( "image_colorMipLevels", "0", CVAR_RENDERER | CVAR_BOOL,
	"development aid to see texture mip usage" );
idCVar idImageManager::image_useCompression( "image_useCompression", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"0 = force everything to high quality" );
idCVar idImageManager::image_useAllFormats( "image_useAllFormats", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"allow alpha/intensity/luminance/luminance+alpha" );
idCVar idImageManager::image_useNormalCompression( "image_useNormalCompression", "2", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"2 = use rxgb compression for normal maps, 1 = use 256 color compression for normal maps if available" );
idCVar idImageManager::image_writeNormalTGA( "image_writeNormalTGA", "0", CVAR_RENDERER | CVAR_BOOL,
	"write .tgas of the final normal maps for debugging" );
idCVar idImageManager::image_writeNormalTGAPalletized( "image_writeNormalTGAPalletized", "0", CVAR_RENDERER | CVAR_BOOL,
	"write .tgas of the final palletized normal maps for debugging" );
idCVar idImageManager::image_writeTGA( "image_writeTGA", "0", CVAR_RENDERER | CVAR_BOOL,
	"write .tgas of the non normal maps for debugging" );
idCVar idImageManager::image_useOffLineCompression( "image_useOfflineCompression", "0", CVAR_RENDERER | CVAR_BOOL,
	"write a batch file for offline compression of DDS files" );
idCVar idImageManager::image_skipUpload( "image_skipUpload", "0", CVAR_RENDERER | CVAR_BOOL,
	"used during the build process, will skip uploads" );
idCVar idImageManager::image_useBackgroundLoads( "image_useBackgroundLoads", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"1 = enable background loading of images" );
idCVar idImageManager::image_showBackgroundLoads( "image_showBackgroundLoads", "0", CVAR_RENDERER | CVAR_BOOL,
	"1 = print number of outstanding background loads" );
idCVar idImageManager::image_ignoreHighQuality( "image_ignoreHighQuality", "0", CVAR_RENDERER | CVAR_ARCHIVE,
	"ignore high quality setting on materials" );
idCVar idImageManager::image_detailPower( "image_detailPower", "0.7", CVAR_RENDERER | CVAR_ARCHIVE,
	"Controls how fast the detail textures fade out (0 = normal mipmaps, 1 is falloff after the first level)", 0.0f, 1.0f );
idCVar idImageManager::image_picMipEnable( "image_picMipEnable", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Enable picmip" );
idCVar idImageManager::image_picMip( "image_picMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );
idCVar idImageManager::image_editorPicMip( "image_editorPicMip", "1", CVAR_RENDERER | CVAR_INTEGER,
	"", -4.0f, 1.0f );
idCVar idImageManager::image_bumpPicMip( "image_bumpPicMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );
idCVar idImageManager::image_diffusePicMip( "image_diffusePicMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );
idCVar idImageManager::image_specularPicMip( "image_specularPicMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );

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

class idRenderModelBootstrap : public idRenderModel {
public:
	idRenderModelBootstrap() :
		loaded( false ),
		defaulted( false ),
		reloadable( false ),
		referencedOutsideLevelLoad( false ),
		levelLoadReferenced( false ),
		timeStamp( 0 ),
		currentLod( 0 ) {
		SetFallbackBounds();
	}

	virtual ~idRenderModelBootstrap() {
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
		modelName = name != NULL ? name : "";
		loaded = true;
		defaulted = false;
		reloadable = false;
		ClearSurfaces();
		modelJoints.Clear();
		defaultPose.Clear();
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

	virtual void FinishSurfaces( bool = true, bool = true, bool = false ) {}
	virtual bool Validate() const { return loaded; }

	virtual void PurgeModel() {
		loaded = false;
		ClearSurfaces();
		modelJoints.Clear();
		defaultPose.Clear();
	}

	virtual void Reset() {}

	virtual void LoadModel() {
		ClearSurfaces();
		modelJoints.Clear();
		defaultPose.Clear();
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
		if ( !extension.Icmp( "modelb" ) ) {
			if ( LoadModelB( modelName ) ) {
				return;
			}
		} else if ( extension.Icmp( MD5_MESH_EXT ) ) {
			idStr generated = "generated/modelb/";
			generated += modelName;
			generated.StripFileExtension();
			generated.SetFileExtension( "modelb" );
			if ( LoadModelB( generated ) ) {
				return;
			}
		} else if ( LoadMD5BinaryReferencePose() ) {
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
	virtual void FreeVertexCache() {}
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
		Mem_Free( triangles->verts );
		Mem_Free( triangles->indexes );
		Mem_Free( triangles->facePlanes );
		Mem_Free( triangles->indexTree );
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

private:
	void ClearSurfaces() {
		for ( int i = 0; i < modelSurfaces.Num(); i++ ) {
			FreeSurfaceTriangles( modelSurfaces[ i ].geometry );
			modelSurfaces[ i ].geometry = NULL;
		}
		modelSurfaces.Clear();
		surfaceNames.Clear();
		surfaceIds.Clear();
		surfaceGroupMaterials.Clear();
		surfaceGroupNoAnimate.Clear();
	}

	void SetFallbackBounds() {
		modelBounds[ 0 ].Set( -8.0f, -8.0f, -8.0f );
		modelBounds[ 1 ].Set( 8.0f, 8.0f, 8.0f );
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
		valid = valid && ReadDiscard( file, numReferencedJoints );
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

		const int weightStride = singleWeight ? 1 : 8;
		valid = valid && CheckedByteCount( numOutputVerts, weightStride, byteCount ) && ReadDiscard( file, byteCount );
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

			// The inverse default-pose matrix follows every joint.  The bootstrap
			// model does not skin geometry yet, but consuming it keeps the binary
			// cursor and the reconstructed skeleton format exact.
			for ( int j = 0; valid && j < 12; j++ ) {
				float unused;
				valid = file->ReadFloat( unused ) == sizeof( unused );
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
	idList< int > fixedAreas;
	bool loaded;
	bool defaulted;
	bool reloadable;
	bool referencedOutsideLevelLoad;
	bool levelLoadReferenced;
	unsigned timeStamp;
	int currentLod;
};

class idRenderModelManagerBootstrap : public idRenderModelManager {
public:
	idRenderModelManagerBootstrap() : defaultModel( NULL ), insideLevelLoad( false ) {}

	virtual void Init() {
		if ( defaultModel != NULL ) {
			return;
		}
		idRenderModelBootstrap* model = new idRenderModelBootstrap;
		model->InitEmpty( "_DEFAULT" );
		defaultModel = model;
		models.Append( model );
	}

	virtual void Shutdown() {
		models.DeleteContents( true );
		defaultModel = NULL;
		insideLevelLoad = false;
	}

	virtual void BeginLevelLoad() {
		insideLevelLoad = true;
		for ( int i = 0; i < models.Num(); i++ ) {
			models[ i ]->SetLevelLoadReferenced( false );
		}
	}

	virtual void EndLevelLoad() { insideLevelLoad = false; }

	virtual idRenderModel* AllocModel() {
		idRenderModelBootstrap* model = new idRenderModelBootstrap;
		model->InitEmpty( "_allocated" );
		return model;
	}

	virtual void FreeModel( idRenderModel* model ) {
		if ( model == NULL || model == defaultModel ) {
			return;
		}
		RemoveModel( model );
		delete model;
	}

	virtual idRenderModel* FindModel( const char* name ) { return FindOrCreateModel( name, true ); }

	virtual idRenderModel* CheckModel( const char* name ) {
		idRenderModel* model = FindOrCreateModel( name, false );
		return model != NULL && !model->IsDefaultModel() ? model : NULL;
	}

	virtual idRenderModel* GetModel( const char* name ) { return FindExistingModel( name ); }
	virtual idRenderModel* DefaultModel() { return defaultModel; }

	virtual void AddModel( idRenderModel* model ) {
		if ( model != NULL && models.FindIndex( model ) < 0 ) {
			models.Append( model );
		}
	}

	virtual void RemoveModel( idRenderModel* model ) {
		const int index = models.FindIndex( model );
		if ( index >= 0 ) {
			models.RemoveIndex( index );
		}
	}

	virtual void ReloadModels( bool forceAll ) {
		for ( int i = 0; i < models.Num(); i++ ) {
			if ( forceAll || models[ i ]->IsReloadable() ) {
				models[ i ]->LoadModel();
			}
		}
	}

	virtual void WritePrecacheCommands( idFile* file ) {
		if ( file == NULL ) {
			return;
		}
		for ( int i = 0; i < models.Num(); i++ ) {
			if ( models[ i ]->IsReloadable() ) {
				file->Printf( "touchModel %s\n", models[ i ]->Name() );
			}
		}
	}

	virtual void FreeModelVertexCaches() {
		for ( int i = 0; i < models.Num(); i++ ) {
			models[ i ]->FreeVertexCache();
		}
	}
	virtual bool WriteSurfaceModel( const char*, idList< idSurface* >&, idStrList& ) { return false; }
	virtual bool WriteTriangleModelB( const char*, idRenderModel* ) { return false; }
	virtual bool WriteTriangleModel( const char*, idRenderModel* ) { return false; }

private:
	idRenderModel* FindExistingModel( const char* name ) const {
		if ( name == NULL || name[ 0 ] == '\0' ) {
			return NULL;
		}
		for ( int i = 0; i < models.Num(); i++ ) {
			if ( !idStr::Icmp( models[ i ]->Name(), name ) ) {
				return models[ i ];
			}
		}
		return NULL;
	}

	idRenderModel* FindOrCreateModel( const char* name, bool keepDefault ) {
		idRenderModel* existing = FindExistingModel( name );
		if ( existing != NULL ) {
			existing->SetLevelLoadReferenced( true );
			if ( !insideLevelLoad ) {
				existing->SetReferencedOutsideLevelLoad( true );
			}
			return existing;
		}
		if ( name == NULL || name[ 0 ] == '\0' ) {
			return NULL;
		}

		idRenderModelBootstrap* model = new idRenderModelBootstrap;
		model->InitFromFile( name );
		model->SetLevelLoadReferenced( true );
		model->SetReferencedOutsideLevelLoad( !insideLevelLoad );
		if ( model->IsDefaultModel() && !keepDefault ) {
			delete model;
			return NULL;
		}
		models.Append( model );
		return model;
	}

	idList< idRenderModel* > models;
	idRenderModel* defaultModel;
	bool insideLevelLoad;
};

idRenderModelManagerBootstrap modelManagerBootstrap;
idImageManager imageManagerLocal;

idStr NormalizeImageName( const char* name ) {
	idStr normalized = name != NULL ? name : "";
	normalized.BackSlashesToSlashes();
	return normalized;
}

bool ParseImageProgramText( idParser& src, idStr& imageProgram ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return false;
	}

	imageProgram = token;
	if ( !src.PeekTokenString( "(" ) ) {
		return true;
	}

	int depth = 0;
	while ( src.ReadToken( &token ) ) {
		imageProgram += token;
		if ( token == "(" ) {
			++depth;
		} else if ( token == ")" ) {
			if ( --depth == 0 ) {
				return true;
			}
		}
	}
	return false;
}

}

idImage* idImageManager::ImageFromFile( const char* name, imageParams_t params ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return defaultImage;
	}

	const idStr normalized = NormalizeImageName( name );
	if ( !normalized.Icmp( "default" ) || !normalized.Icmp( "_default" ) ) {
		return defaultImage;
	}

	const int hash = normalized.FileNameHash( FILE_HASH_SIZE );
	for ( idImage* image = imageHashTable[ hash ]; image != NULL; image = image->hashNext ) {
		if ( idStr::Icmp( image->imgName, normalized ) != 0 ) {
			continue;
		}
		if ( image->filter != params.tf || image->repeat != params.trp || image->cubeFiles != params.cubeMap ) {
			continue;
		}
		image->levelLoadReferenced = true;
		if ( !insideLevelLoad ) {
			image->referencedOutsideLevelLoad = true;
			if ( !image->IsLoaded() && image->partialImage == NULL ) {
				image->ActuallyLoadImage( true );
			}
		}
		return image;
	}

	idImage* image = AllocImage( normalized );
	image->filter = params.tf;
	image->repeat = params.trp;
	image->depth = params.td;
	image->cubeFiles = params.cubeMap;
	image->mipmapState = params.mipState;
	image->allowDownSize = params.allowPicmip;
	image->picMipOfs = params.picmipofs;
	image->picMipMin = params.picMipMin;
	image->anisotropy = params.anisotropy;
	image->minLod = params.minLod;
	image->maxLod = params.maxLod;
	image->type = params.cubeMap == CF_2D ? TT_2D : TT_CUBIC;
	image->levelLoadReferenced = true;
	if ( !insideLevelLoad ) {
		image->referencedOutsideLevelLoad = true;
		image->ActuallyLoadImage( true );
	}
	return image;
}

idImage* idImageManager::ImageFromFunction( const char* name, const idImageGeneratorFunctorBase& generatorFunction ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		common->Error( "idImageManager::ImageFromFunction: NULL name" );
	}
	const idStr normalized = NormalizeImageName( name );
	const int hash = normalized.FileNameHash( FILE_HASH_SIZE );
	for ( idImage* image = imageHashTable[ hash ]; image != NULL; image = image->hashNext ) {
		if ( idStr::Icmp( image->imgName, normalized ) == 0 ) {
			if ( image->generatorFunction == NULL ) {
				image->generatorFunction = &generatorFunction;
			} else if ( image->generatorFunction != &generatorFunction ) {
				common->Warning( "reused image %s with mixed generators", normalized.c_str() );
			}
			return image;
		}
	}

	idImage* image = AllocImage( normalized );
	image->generatorFunction = &generatorFunction;
	image->referencedOutsideLevelLoad = true;
	image->levelLoadReferenced = true;
	image->ActuallyLoadImage( true );
	return image;
}

idImage* idImageManager::ImageFromParameters(
	const char* name,
	int width,
	int height,
	int internalFormat,
	textureType_t type,
	textureFilter_t filter,
	textureRepeat_t repeat
) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		common->Error( "idImageManager::ImageFromParameters: NULL name" );
	}
	const idStr normalized = NormalizeImageName( name );
	const int hash = normalized.FileNameHash( FILE_HASH_SIZE );
	for ( idImage* image = imageHashTable[ hash ]; image != NULL; image = image->hashNext ) {
		if ( idStr::Icmp( image->imgName, normalized ) != 0 ) {
			continue;
		}
		if ( image->uploadWidth != width || image->uploadHeight != height ||
			 image->internalFormat != internalFormat || image->type != type ||
			 image->filter != filter || image->repeat != repeat ) {
			image->FromParameters( width, height, internalFormat, type, filter, repeat );
		}
		return image;
	}
	idImage* image = AllocImage( normalized );
	image->referencedOutsideLevelLoad = true;
	image->levelLoadReferenced = true;
	image->FromParameters( width, height, internalFormat, type, filter, repeat );
	return image;
}

idMegaTexture* idImageManager::MegaTextureFromFile( const char* ) { return NULL; }
idImage* idImageManager::ParseImage( idParser& src, const imageParams_t& defaultParms ) {
	imageParams_t parms = defaultParms;
	idToken token;
	while ( src.ReadTokenOnLine( &token ) ) {
		if ( token.Icmp( "bumpMap" ) == 0 ) {
			parms.td = TD_BUMP;
		} else if ( token.Icmp( "diffuseMap" ) == 0 ) {
			parms.td = TD_DIFFUSE;
		} else if ( token.Icmp( "specularMap" ) == 0 ) {
			parms.td = TD_SPECULAR;
		} else if ( token.Icmp( "cubeMap" ) == 0 ) {
			parms.cubeMap = CF_NATIVE;
		} else if ( token.Icmp( "cameraCubeMap" ) == 0 ) {
			parms.cubeMap = CF_CAMERA;
		} else if ( token.Icmp( "halfSphereMap" ) == 0 ) {
			parms.cubeMap = CF_HALFSPHERE;
		} else if ( token.Icmp( "nearest" ) == 0 ) {
			parms.tf = TF_NEAREST;
		} else if ( token.Icmp( "linear" ) == 0 ) {
			parms.tf = TF_LINEAR;
		} else if ( token.Icmp( "linearNearest" ) == 0 ) {
			parms.tf = TF_LINEARNEAREST;
		} else if ( token.Icmp( "waternormal" ) == 0 ) {
			parms.mipState.colorType = mipmapState_t::MT_WATER;
		} else if ( token.Icmp( "colormipmaps" ) == 0 ) {
			parms.mipState.colorType = mipmapState_t::MT_COLORLEVELS;
			if ( !src.Parse2DMatrix( 2, 4, parms.mipState.color ) ) {
				return NULL;
			}
		} else if ( token.Icmp( "mirror" ) == 0 ) {
			parms.trp = TR_MIRROR;
		} else if ( token.Icmp( "mirror_x" ) == 0 ) {
			parms.trp = TR_MIRROR_X;
		} else if ( token.Icmp( "mirror_y" ) == 0 ) {
			parms.trp = TR_MIRROR_Y;
		} else if ( token.Icmp( "clamp" ) == 0 ) {
			parms.trp = TR_CLAMP;
		} else if ( token.Icmp( "clamp_x" ) == 0 ) {
			parms.trp = TR_CLAMP_X;
		} else if ( token.Icmp( "clamp_y" ) == 0 ) {
			parms.trp = TR_CLAMP_Y;
		} else if ( token.Icmp( "noclamp" ) == 0 ) {
			parms.trp = TR_REPEAT;
		} else if ( token.Icmp( "zeroclamp" ) == 0 ) {
			parms.trp = TR_CLAMP_TO_ZERO;
		} else if ( token.Icmp( "alphazeroclamp" ) == 0 ) {
			parms.trp = TR_CLAMP_TO_ZERO_ALPHA;
		} else if ( token.Icmp( "forceHighQuality" ) == 0 ) {
			parms.td = TD_HIGH_QUALITY;
		} else if ( token.Icmp( "uncompressed" ) == 0 || token.Icmp( "highquality" ) == 0 ) {
			if ( !image_ignoreHighQuality.GetBool() ) {
				parms.td = TD_HIGH_QUALITY;
			}
		} else if ( token.Icmp( "nopicmip" ) == 0 ) {
			parms.allowPicmip = false;
		} else if ( token.Icmp( "picmip" ) == 0 ) {
			parms.allowPicmip = true;
			parms.picmipofs = src.ParseInt();
		} else if ( token.Icmp( "picmipmin" ) == 0 ) {
			parms.picMipMin = src.ParseInt();
		} else if ( token.Icmp( "anisotropy" ) == 0 ) {
			parms.anisotropy = src.ParseFloat();
		} else if ( token.Icmp( "minLod" ) == 0 ) {
			parms.minLod = src.ParseFloat();
		} else if ( token.Icmp( "maxLod" ) == 0 ) {
			parms.maxLod = src.ParseFloat();
		} else if ( token.Icmp( "partialLoad" ) == 0 ) {
			parms.partialLoad = true;
		} else {
			src.UnreadToken( token );
			break;
		}
	}

	idStr imageProgram;
	if ( !ParseImageProgramText( src, imageProgram ) ) {
		return NULL;
	}
	return globalImages != NULL ? globalImages->ImageFromFile( imageProgram, parms ) : NULL;
}

void idImageManager::BindNull() {
	if ( glConfig.isInitialized ) {
		glBindTexture( GL_TEXTURE_2D, 0 );
		if ( glConfig.cubeMapAvailable ) {
			glBindTexture( GL_TEXTURE_CUBE_MAP_ARB, 0 );
		}
		if ( glConfig.rectangleTextureAvailable ) {
			glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
		}
	}
}

void idImageManager::LoadImage(
	const char* fileName,
	byte** pic,
	int* width,
	int* height,
	unsigned* timestamp,
	bool
) {
	LoadTGA( fileName, pic, width, height, timestamp, true );
	if ( pic != NULL && *pic == NULL && fileName != NULL ) {
		idStr tgaName = fileName;
		tgaName.DefaultFileExtension( "tga" );
		if ( idStr::Icmp( tgaName, fileName ) != 0 ) {
			LoadTGA( tgaName, pic, width, height, timestamp, true );
		}
	}
}

void idImageManager::FreeImageBuffer( byte*& buffer ) {
	Mem_Free( buffer );
	buffer = NULL;
}

void idImageManager::WriteTGA( const char* fileName, const byte* data, int width, int height, int depth, bool swapBGR, bool flipVertical ) {
	byte* buffer = NULL;
	const int length = WriteTGABuffer( buffer, data, width, height, depth, swapBGR, flipVertical );
	if ( length > 0 && fileSystem != NULL ) {
		fileSystem->WriteFile( fileName, buffer, length );
	}
	Mem_Free( buffer );
}

int idImageManager::WriteTGABuffer( byte*& outBuffer, const byte* data, int width, int height, int depth, bool swapBGR, bool flipVertical ) {
	outBuffer = NULL;
	if ( data == NULL || width <= 0 || height <= 0 || ( depth != 3 && depth != 4 ) ) {
		return 0;
	}

	const int length = 18 + width * height * depth;
	byte* output = static_cast< byte* >( Mem_Alloc( length ) );
	memset( output, 0, 18 );
	output[ 2 ] = 2;
	output[ 12 ] = width & 0xff;
	output[ 13 ] = ( width >> 8 ) & 0xff;
	output[ 14 ] = height & 0xff;
	output[ 15 ] = ( height >> 8 ) & 0xff;
	output[ 16 ] = depth * 8;
	output[ 17 ] = static_cast< byte >( 0x20 | ( depth == 4 ? 8 : 0 ) );

	byte* destination = output + 18;
	for ( int y = 0; y < height; y++ ) {
		const int sourceY = flipVertical ? height - 1 - y : y;
		const byte* source = data + sourceY * width * depth;
		for ( int x = 0; x < width; x++, source += depth, destination += depth ) {
			if ( swapBGR ) {
				destination[ 0 ] = source[ 2 ];
				destination[ 1 ] = source[ 1 ];
				destination[ 2 ] = source[ 0 ];
			} else {
				memcpy( destination, source, 3 );
			}
			if ( depth == 4 ) {
				destination[ 3 ] = source[ 3 ];
			}
		}
	}
	outBuffer = output;
	return length;
}

void idImageManager::WriteBMP( const char*, const byte*, int, int, int ) {}
int idImageManager::WriteBMPBuffer( byte*& outBuffer, const byte*, int, int, int ) {
	outBuffer = NULL;
	return 0;
}
void idImageManager::WritePalTGA( const char*, const byte*, const byte*, int, int, bool ) {}

idImage* idImageManager::AllocImage( const char* name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		common->Error( "idImageManager::AllocImage: NULL name" );
	}
	if ( idStr::Length( name ) >= MAX_IMAGE_NAME ) {
		common->Error( "idImageManager::AllocImage: \"%s\" is too long", name );
	}

	idImage* image = new idImage;
	image->imgName = name;
	image->imageHash = image->imgName.FileNameHash( FILE_HASH_SIZE );
	image->hashNext = imageHashTable[ image->imageHash ];
	imageHashTable[ image->imageHash ] = image;
	images.Append( image );
	return image;
}

idRenderModelManager* renderModelManager = &modelManagerBootstrap;
idImageManager* globalImages = &imageManagerLocal;

void LoadTGA( const char* name, byte** pic, int* width, int* height, unsigned* timestamp, bool markPaksReferenced ) {
	if ( pic != NULL ) {
		*pic = NULL;
	}
	if ( width != NULL ) {
		*width = 0;
	}
	if ( height != NULL ) {
		*height = 0;
	}
	if ( timestamp != NULL ) {
		*timestamp = 0;
	}
	if ( name == NULL || fileSystem == NULL ) {
		return;
	}

	void* fileBuffer = NULL;
	const int fileLength = fileSystem->ReadFile( name, &fileBuffer, timestamp, markPaksReferenced );
	if ( fileLength < 18 || fileBuffer == NULL ) {
		return;
	}

	const byte* source = static_cast< const byte* >( fileBuffer );
	const int idLength = source[ 0 ];
	const int colorMapType = source[ 1 ];
	const int imageType = source[ 2 ];
	const int imageWidth = source[ 12 ] | ( source[ 13 ] << 8 );
	const int imageHeight = source[ 14 ] | ( source[ 15 ] << 8 );
	const int bitsPerPixel = source[ 16 ];
	const int bytesPerPixel = bitsPerPixel / 8;
	const bool topOrigin = ( source[ 17 ] & 0x20 ) != 0;
	const int dataOffset = 18 + idLength;
	const int dataLength = imageWidth * imageHeight * bytesPerPixel;

	const bool validType = imageType == 2 || imageType == 3;
	const bool validDepth = ( imageType == 2 && ( bitsPerPixel == 24 || bitsPerPixel == 32 ) ) ||
		( imageType == 3 && bitsPerPixel == 8 );
	if ( colorMapType != 0 || !validType || !validDepth || imageWidth <= 0 || imageHeight <= 0 ||
		 dataOffset < 18 || dataLength < 0 || dataOffset + dataLength > fileLength ) {
		fileSystem->FreeFile( fileBuffer );
		return;
	}

	if ( width != NULL ) {
		*width = imageWidth;
	}
	if ( height != NULL ) {
		*height = imageHeight;
	}

	if ( pic != NULL ) {
		byte* output = static_cast< byte* >( Mem_Alloc( imageWidth * imageHeight * 4 ) );
		const byte* pixels = source + dataOffset;
		for ( int y = 0; y < imageHeight; y++ ) {
			const int sourceY = topOrigin ? y : imageHeight - 1 - y;
			const byte* sourceRow = pixels + sourceY * imageWidth * bytesPerPixel;
			byte* destination = output + y * imageWidth * 4;
			for ( int x = 0; x < imageWidth; x++, sourceRow += bytesPerPixel, destination += 4 ) {
				if ( imageType == 3 ) {
					destination[ 0 ] = sourceRow[ 0 ];
					destination[ 1 ] = sourceRow[ 0 ];
					destination[ 2 ] = sourceRow[ 0 ];
					destination[ 3 ] = 255;
				} else {
					destination[ 0 ] = sourceRow[ 2 ];
					destination[ 1 ] = sourceRow[ 1 ];
					destination[ 2 ] = sourceRow[ 0 ];
					destination[ 3 ] = bytesPerPixel == 4 ? sourceRow[ 3 ] : 255;
				}
			}
		}
		*pic = output;
	}

	fileSystem->FreeFile( fileBuffer );
}
