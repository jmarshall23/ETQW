#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../compilers/compiler_public.h"
#include "../compilers/megatexture/MegaTextureCompiler.h"
#include "../../renderer/megatexture/MegaTexture.h"
#include "Radiant.h"
#include "QE3.H"
#include "CamWnd.h"
#include "MegaTextureEditorImGui.h"
#include "RadiantImGuiVulkan.h"
#include "megatexture/RoadBuilder.h"

#include "imgui.h"
#include <vector>

extern void Brush_Resize( brush_t *brush, idVec3 minimum, idVec3 maximum );

namespace {

enum terrainBrushMode_t {
	TERRAIN_BRUSH_RAISE_LOWER = 0,
	TERRAIN_BRUSH_SMOOTH,
	TERRAIN_BRUSH_FLATTEN,
	TERRAIN_BRUSH_NOISE
};

enum terrainBrushShape_t {
	TERRAIN_SHAPE_CIRCLE = 0,
	TERRAIN_SHAPE_SQUARE,
	TERRAIN_SHAPE_RECTANGLE
};

enum megaTextureEditMode_t {
	MEGA_EDIT_SCULPT = 0,
	MEGA_EDIT_PAINT,
	MEGA_EDIT_ROADS
};

static const byte MEGA_FLAT_NORMAL_ALPHA = 0x88;

struct paintSnapshot_t {
	std::vector<byte> weights;
	std::vector<megaTextureVertexTransform_t> transforms;
};

struct MegaTextureEditorState {
	MegaTextureEditorState() : open( false ), loaded( false ), dirty( false ), projectDirty( false ), textureDirty( false ),
		texture( 0 ), heightTexture( 0 ), megatileTexture( 0 ), stencilTexture( 0 ),
		tileX( 0 ), tileY( 0 ), brushRadius( 8 ), painting( false ),
		buildResolutionIndex( 0 ), newTerrainSamplesIndex( 2 ), newTerrainSize( 8192.0f ), buildStatusError( false ),
		pendingCompile( false ), pendingCompileBake( false ), pendingCompileFrame( -1 ), terrainDirty( false ),
		heightTextureDirty( false ), terrainPainting( false ), terrainBrushMode( TERRAIN_BRUSH_RAISE_LOWER ),
		terrainBrushShape( TERRAIN_SHAPE_CIRCLE ), terrainBrushRadius( 8 ), terrainBrushStrength( 256.0f ),
		terrainFlattenHeight( 0.0f ), terrainBrushFeather( 0.35f ), terrainBrushAspect( 2.0f ),
		megatileTextureDirty( false ), selectedMegatile( -1 ), selectedLayer( 0 ), lockLayerScale( true ),
		paintMegatile( true ), paintOpacity( 0.35f ), paintScale( 1.0f ), paintMappingFeather( 0.15f ),
		stencilTextureDirty( false ), selectedStencil( -1 ), editMode( MEGA_EDIT_SCULPT ), cameraStroke( false ),
		cameraBrushValid( false ), cameraBrushSampleX( 0.0f ), cameraBrushSampleY( 0.0f ),
		paintStrokePivotX( 0.0f ), paintStrokePivotY( 0.0f ), paintStrokePhaseU( 0.0f ), paintStrokePhaseV( 0.0f ),
		roadsDirty( false ), selectedRoad( -1 ), selectedRoadPoint( -1 ), roadDrawing( false ), roadDragging( false ),
		requestNewProjectPopup( false ),
		heightImportMinimum( 0.0f ), heightImportMaximum( 2048.0f ) {
		projectPath[0] = megatileFilter[0] = roadTextureFilter[0] = stencilFilter[0] = heightImagePath[0] = '\0';
		newTerrainOrigin[0] = newTerrainOrigin[1] = newTerrainOrigin[2] = 0.0f;
		brushColor[0] = brushColor[1] = brushColor[2] = 0.5f; brushColor[3] = 1.0f;
		idStr::Copynz( status, "Save the level, then open the MegaTexture tab.", sizeof( status ) );
		buildStatus[0] = '\0';
		tile.resize( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4, 128 );
	}
	bool open, loaded, dirty, projectDirty, textureDirty;
	GLuint texture, heightTexture, megatileTexture, stencilTexture;
	megaTextureProject_t project;
	char projectPath[512];
	int tileX, tileY, brushRadius;
	float brushColor[4];
	bool painting;
	int buildResolutionIndex;
	int newTerrainSamplesIndex;
	float newTerrainSize;
	float newTerrainOrigin[3];
	char status[1024];
	char buildStatus[1024];
	bool buildStatusError;
	bool pendingCompile;
	bool pendingCompileBake;
	int pendingCompileFrame;
	std::vector<byte> tile;
	std::vector<byte> weights;
	std::vector<megaTextureVertexTransform_t> transforms;
	std::vector<paintSnapshot_t> undo;
	std::vector<paintSnapshot_t> redo;
	bool terrainDirty, heightTextureDirty, terrainPainting;
	int terrainBrushMode, terrainBrushShape, terrainBrushRadius;
	float terrainBrushStrength, terrainFlattenHeight, terrainBrushFeather, terrainBrushAspect;
	std::vector<float> heights;
	std::vector< std::vector<float> > terrainUndo;
	std::vector< std::vector<float> > terrainRedo;
	idStrList megatiles;
	char megatileFilter[128];
	bool megatileTextureDirty;
	int selectedMegatile, selectedLayer;
	bool lockLayerScale;
	bool paintMegatile;
	float paintOpacity, paintScale, paintMappingFeather;
	std::vector<byte> megatilePixels;
	std::vector<byte> megatileNormalPixels;
	std::vector<byte> megatileMaskPixels;
	idStrList stencils;
	char stencilFilter[128];
	bool stencilTextureDirty;
	int selectedStencil;
	std::vector<byte> stencilPixels;
	int editMode;
	bool cameraStroke;
	bool cameraBrushValid;
	float cameraBrushSampleX, cameraBrushSampleY;
	float paintStrokePivotX, paintStrokePivotY, paintStrokePhaseU, paintStrokePhaseV;
	MegaTextureRoadBuilder roads;
	bool roadsDirty;
	int selectedRoad, selectedRoadPoint;
	bool roadDrawing, roadDragging;
	char roadTextureFilter[128];
	std::vector<MegaTextureRoadBuilder> roadUndo;
	std::vector<MegaTextureRoadBuilder> roadRedo;
	bool requestNewProjectPopup;
	char heightImagePath[512];
	float heightImportMinimum, heightImportMaximum;
};

static MegaTextureEditorState state;
static char cachedLevelProjectMap[1024];
static char cachedLevelProjectPath[1024];

static void SetStatus( const char *text ) { idStr::Copynz( state.status, text ? text : "", sizeof( state.status ) ); }
static void SetBuildStatus( const char *text, bool error ) {
	idStr::Copynz( state.buildStatus, text ? text : "", sizeof( state.buildStatus ) );
	state.buildStatusError = error;
	SetStatus( text );
}
static bool SaveTerrain( bool reloadModel );

static void RefreshTerrainInRadiant( entity_t *entity ) {
	declManager->Reload( false );
	globalImages->ReloadAllMegaTextures();
	renderModelManager->ReloadModels( true );
	if ( entity ) {
		idRenderModel *model = renderModelManager->FindModel( state.project.terrainModel );
		if ( model && !model->IsDefaultModel() ) {
			idBounds bounds = model->Bounds( NULL );
			for ( int axis = 0; axis < 3; ++axis ) {
				if ( bounds[0][axis] == bounds[1][axis] ) {
					bounds[0][axis] -= 1.0f;
					bounds[1][axis] += 1.0f;
				}
				bounds[0][axis] += entity->origin[axis];
				bounds[1][axis] += entity->origin[axis];
			}
			for ( brush_t *brush = entity->brushes.onext; brush != &entity->brushes; brush = brush->onext ) {
				if ( !brush->modelHandle ) continue;
				brush->modelHandle = model;
				Brush_Resize( brush, bounds[0], bounds[1] );
			}
		}
	}
	Map_BuildBrushData();
	if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) {
		g_pParentWnd->GetCamera()->MarkWorldDirty();
		if ( g_pParentWnd->GetCamera()->GetRenderMode() ) g_pParentWnd->GetCamera()->BuildRendererState();
	}
	Sys_UpdateWindows( W_ALL );
}

static void FocusTerrainCamera() {
	if ( !state.loaded || g_pParentWnd == NULL || g_pParentWnd->GetCamera() == NULL ) return;
	float minimumHeight = 0.0f, maximumHeight = 0.0f;
	if ( !state.heights.empty() ) {
		minimumHeight = maximumHeight = state.heights[0];
		for ( int i = 1; i < (int)state.heights.size(); ++i ) {
			minimumHeight = Min( minimumHeight, state.heights[i] );
			maximumHeight = Max( maximumHeight, state.heights[i] );
		}
	}
	const float viewSize = Max( state.project.terrainSize, ( maximumHeight - minimumHeight ) * 2.0f + 256.0f );
	const idVec3 center( state.project.terrainOrigin[0], state.project.terrainOrigin[1],
		state.project.terrainOrigin[2] + ( minimumHeight + maximumHeight ) * 0.5f );
	const idVec3 cameraOrigin = center + idVec3( -viewSize * 0.65f, -viewSize * 0.65f, viewSize * 0.55f );
	g_pParentWnd->GetCamera()->SetView( cameraOrigin, idAngles( -31.0f, 45.0f, 0.0f ) );
	Sys_UpdateWindows( W_CAMERA | W_XY_OVERLAY );
}

static void EnsureTexture() {
	if ( state.textureDirty ) {
		if ( RadiantImGuiVulkanUploadTexture( &state.texture, state.tile.data(),
			MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE, false, false ) ) {
			state.texture = 1;
			state.textureDirty = false;
		}
	}
}

static void EnsureHeightTexture() {
	if ( !state.heightTextureDirty || state.heights.empty() ) return;
	const int samples = state.project.terrainSamples;
	float minimum = state.heights[0], maximum = state.heights[0];
	for ( int i = 1; i < (int)state.heights.size(); ++i ) {
		if ( state.heights[i] < minimum ) minimum = state.heights[i];
		if ( state.heights[i] > maximum ) maximum = state.heights[i];
	}
	const float range = maximum > minimum ? maximum - minimum : 1.0f;
	std::vector<byte> preview( samples * samples * 4 );
	for ( int y = 0; y < samples; ++y ) for ( int x = 0; x < samples; ++x ) {
		const int left = y * samples + ( x > 0 ? x - 1 : x );
		const int right = y * samples + ( x + 1 < samples ? x + 1 : x );
		const int down = ( y > 0 ? y - 1 : y ) * samples + x;
		const int up = ( y + 1 < samples ? y + 1 : y ) * samples + x;
		const float normalized = ( state.heights[y * samples + x] - minimum ) / range;
		const float slopeLight = idMath::ClampFloat( 0.35f, 1.25f, 0.8f +
			( state.heights[left] - state.heights[right] + state.heights[down] - state.heights[up] ) / ( range * 0.15f + 1.0f ) );
		const int value = idMath::ClampInt( 0, 255, (int)( ( 35.0f + normalized * 210.0f ) * slopeLight ) );
		byte *pixel = preview.data() + ( y * samples + x ) * 4;
		pixel[0] = (byte)( value * 0.72f ); pixel[1] = (byte)( value * 0.88f ); pixel[2] = (byte)value; pixel[3] = 255;
	}
	if ( RadiantImGuiVulkanUploadTexture( &state.heightTexture, preview.data(),
		samples, samples, true, false ) ) {
		state.heightTexture = 1;
		state.heightTextureDirty = false;
	}
}

static void EnsureMegatileTexture() {
	if ( !state.megatileTextureDirty || state.megatilePixels.empty() ) return;
	if ( RadiantImGuiVulkanUploadTexture( &state.megatileTexture,
		state.megatilePixels.data(), MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE,
		true, true ) ) {
		state.megatileTexture = 1;
		state.megatileTextureDirty = false;
	}
}

static void EnsureStencilTexture() {
	if ( !state.stencilTextureDirty || state.stencilPixels.empty() ) return;
	if ( RadiantImGuiVulkanUploadTexture( &state.stencilTexture,
		state.stencilPixels.data(), MEGA_TEXTURE_TILE_SIZE, MEGA_TEXTURE_TILE_SIZE,
		true, false ) ) {
		state.stencilTexture = 1;
		state.stencilTextureDirty = false;
	}
}

static bool LoadResampledImage( const char *path, std::vector<byte> &pixels ) {
	byte *image = NULL;
	int width = 0, height = 0;
	R_LoadImage( path, &image, &width, &height, NULL, false );
	if ( !image || width <= 0 || height <= 0 ) {
		if ( image ) Mem_Free( image );
		return false;
	}
	pixels.resize( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4 );
	for ( int y = 0; y < MEGA_TEXTURE_TILE_SIZE; ++y ) for ( int x = 0; x < MEGA_TEXTURE_TILE_SIZE; ++x ) {
		const int sourceX = idMath::ClampInt( 0, width - 1, x * width / MEGA_TEXTURE_TILE_SIZE );
		const int sourceY = idMath::ClampInt( 0, height - 1, y * height / MEGA_TEXTURE_TILE_SIZE );
		memcpy( pixels.data() + ( y * MEGA_TEXTURE_TILE_SIZE + x ) * 4,
			image + ( sourceY * width + sourceX ) * 4, 4 );
	}
	Mem_Free( image );
	return true;
}

static void RefreshMegatiles() {
	state.megatiles.Clear();
	state.stencils.Clear();
	idFileList *files = fileSystem->ListFilesTree( "megatiles", ".tga", true );
	if ( !files ) return;
	for ( int i = 0; i < files->GetNumFiles(); ++i ) {
		idStr path = files->GetFile( i );
		idStr lower = path; lower.ToLower();
		idStr fileName; lower.ExtractFileName( fileName );
		if ( fileName == "black.tga" || fileName == "white.tga" ||
			( fileName.Length() > 6 && !fileName.Right( 6 ).Icmp( "_d.tga" ) ) ) state.megatiles.Append( path );
		if ( fileName.Length() > 9 && !fileName.Right( 9 ).Icmp( "_mask.tga" ) ) state.stencils.Append( path );
	}
	fileSystem->FreeFileList( files );
}

static bool SelectMegatile( int index ) {
	if ( index < 0 || index >= state.megatiles.Num() ) return false;
	if ( !LoadResampledImage( state.megatiles[index], state.megatilePixels ) ) {
		SetStatus( va( "Could not load %s", state.megatiles[index].c_str() ) );
		return false;
	}
	state.megatileNormalPixels.assign( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4, 0 );
	state.megatileMaskPixels.assign( MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE * 4, 255 );
	for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) {
		state.megatileNormalPixels[i * 4 + 0] = 128;
		state.megatileNormalPixels[i * 4 + 1] = 128;
		state.megatileNormalPixels[i * 4 + 2] = 255;
		state.megatileNormalPixels[i * 4 + 3] = 255;
	}
	idStr base = state.megatiles[index];
	if ( base.Length() > 6 && !base.Right( 6 ).Icmp( "_d.tga" ) ) {
		base = base.Left( base.Length() - 6 );
		LoadResampledImage( base + "_local.tga", state.megatileNormalPixels );
		LoadResampledImage( base + "_mask.tga", state.megatileMaskPixels );
	}
	state.selectedMegatile = index;
	if ( state.loaded && state.editMode == MEGA_EDIT_ROADS ) {
		// The Roads texture library is isolated from RGBA terrain layers. With no
		// active road this selection becomes the preset for New road spline; it
		// must never fall through and replace the selected paint layer.
		if ( state.selectedRoad >= 0 && state.selectedRoad < state.roads.NumRoads() ) {
			megaTextureRoad_t &road = state.roads.EditRoad( state.selectedRoad );
			if ( idStr::Icmp( road.texture, state.megatiles[index] ) ) {
				road.texture = state.megatiles[index];
				state.roadsDirty = true;
			}
		}
	} else if ( state.loaded && state.selectedLayer >= 0 && state.selectedLayer < megaTextureProject_t::MAX_LAYERS &&
		 idStr::Icmp( state.project.layers[state.selectedLayer], state.megatiles[index] ) ) {
		state.project.layers[state.selectedLayer] = state.megatiles[index];
		state.projectDirty = true;
	}
	state.megatileTextureDirty = true;
	if ( state.editMode == MEGA_EDIT_ROADS ) SetStatus( state.selectedRoad >= 0 ?
		va( "Road texture: %s", state.megatiles[index].c_str() ) :
		va( "New-road texture preset: %s", state.megatiles[index].c_str() ) );
	else SetStatus( va( "Layer %d texture: %s", state.selectedLayer + 1, state.megatiles[index].c_str() ) );
	return true;
}

static bool SelectStencil( int index ) {
	if ( index < 0 || index >= state.stencils.Num() ) {
		state.selectedStencil = -1;
		state.stencilPixels.clear();
		return true;
	}
	if ( !LoadResampledImage( state.stencils[index], state.stencilPixels ) ) {
		SetStatus( va( "Could not load stencil %s", state.stencils[index].c_str() ) );
		return false;
	}
	for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) {
		const byte value = (byte)( state.stencilPixels[i * 4 + 0] * 0.2126f +
			state.stencilPixels[i * 4 + 1] * 0.7152f + state.stencilPixels[i * 4 + 2] * 0.0722f );
		state.stencilPixels[i * 4 + 0] = state.stencilPixels[i * 4 + 1] = state.stencilPixels[i * 4 + 2] = value;
		state.stencilPixels[i * 4 + 3] = 255;
	}
	state.selectedStencil = index;
	state.stencilTextureDirty = true;
	SetStatus( va( "Selected sculpt stencil: %s", state.stencils[index].c_str() ) );
	return true;
}

static byte PackNormalXY( float x, float y ) {
	const float lengthSquared = x * x + y * y;
	if ( lengthSquared > 0.999f ) {
		const float scale = 0.999f / idMath::Sqrt( lengthSquared );
		x *= scale; y *= scale;
	}
	const int packedX = idMath::ClampInt( 0, 15, 8 + (int)( x * ( x < 0.0f ? 8.0f : 7.0f ) + ( x < 0.0f ? -0.5f : 0.5f ) ) );
	const int packedY = idMath::ClampInt( 0, 15, 8 + (int)( y * ( y < 0.0f ? 8.0f : 7.0f ) + ( y < 0.0f ? -0.5f : 0.5f ) ) );
	return (byte)( packedX | ( packedY << 4 ) );
}

static void UnpackNormalXY( byte packed, float &x, float &y ) {
	const int packedX = packed & 15;
	const int packedY = ( packed >> 4 ) & 15;
	x = ( packedX - 8 ) / ( packedX < 8 ? 8.0f : 7.0f );
	y = ( packedY - 8 ) / ( packedY < 8 ? 8.0f : 7.0f );
}

#if 0 // Legacy sparse source-tile preview; vertex weights now drive authoring.
static void UpdateEditorPreview() {
	if ( !state.loaded ) return;
	const int previewSize = 256;
	const idStr previewPath = va( "megatextures/%s_preview.tga", state.project.name.c_str() );
	byte *loaded = NULL;
	int width = 0, height = 0;
	R_LoadImage( previewPath, &loaded, &width, &height, NULL, false );
	std::vector<byte> preview( previewSize * previewSize * 4 );
	if ( loaded && width == previewSize && height == previewSize ) {
		memcpy( preview.data(), loaded, preview.size() );
	} else {
		for ( int i = 0; i < previewSize * previewSize; ++i ) {
			preview[i * 4 + 0] = state.project.fill[0];
			preview[i * 4 + 1] = state.project.fill[1];
			preview[i * 4 + 2] = state.project.fill[2];
			preview[i * 4 + 3] = 255;
		}
	}
	if ( loaded ) R_StaticFree( loaded );
	// Tile alpha is compact normal data.  The qer_editorImage is only a color
	// overview and must stay opaque or the terrain looks ghosted in Radiant.
	for ( int i = 0; i < previewSize * previewSize; ++i ) preview[i * 4 + 3] = 255;
	const int axis = state.project.resolution / MEGA_TEXTURE_TILE_SIZE;
	const int minimumX = state.tileX * previewSize / axis;
	const int maximumX = Max( minimumX + 1, ( state.tileX + 1 ) * previewSize / axis );
	const int minimumY = state.tileY * previewSize / axis;
	const int maximumY = Max( minimumY + 1, ( state.tileY + 1 ) * previewSize / axis );
	for ( int y = minimumY; y < Min( maximumY, previewSize ); ++y ) for ( int x = minimumX; x < Min( maximumX, previewSize ); ++x ) {
		const int sourceX = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1,
			( x * axis - state.tileX * previewSize ) * MEGA_TEXTURE_TILE_SIZE / previewSize );
		const int sourceY = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1,
			( y * axis - state.tileY * previewSize ) * MEGA_TEXTURE_TILE_SIZE / previewSize );
		const byte *source = state.tile.data() + ( sourceY * MEGA_TEXTURE_TILE_SIZE + sourceX ) * 4;
		byte *destination = preview.data() + ( y * previewSize + x ) * 4;
		destination[0] = source[0]; destination[1] = source[1]; destination[2] = source[2]; destination[3] = 255;
	}
	// A 32K source tile is only one pixel in the 256px Radiant overview.  Draw
	// a minimum-size authoring mark so a high-resolution brush remains visible
	// while the exact source data continues to be stored in its sparse tile.
	if ( state.previewStrokeValid ) {
		const float centerX = ( state.previewStrokeX + 0.5f ) * previewSize / state.project.resolution;
		const float centerY = ( state.previewStrokeY + 0.5f ) * previewSize / state.project.resolution;
		const float radius = Max( 2.0f, state.brushRadius * previewSize / (float)state.project.resolution );
		const int sourceX = state.previewStrokeX & ( MEGA_TEXTURE_TILE_SIZE - 1 );
		const int sourceY = state.previewStrokeY & ( MEGA_TEXTURE_TILE_SIZE - 1 );
		const byte *source = state.tile.data() + ( sourceY * MEGA_TEXTURE_TILE_SIZE + sourceX ) * 4;
		for ( int y = idMath::ClampInt( 0, previewSize - 1, (int)floorf( centerY - radius ) );
			y <= idMath::ClampInt( 0, previewSize - 1, (int)ceilf( centerY + radius ) ); ++y ) {
			for ( int x = idMath::ClampInt( 0, previewSize - 1, (int)floorf( centerX - radius ) );
				x <= idMath::ClampInt( 0, previewSize - 1, (int)ceilf( centerX + radius ) ); ++x ) {
				const float dx = x + 0.5f - centerX, dy = y + 0.5f - centerY;
				const float distance = idMath::Sqrt( dx * dx + dy * dy );
				if ( distance > radius ) continue;
				const float blend = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - distance / radius );
				byte *destination = preview.data() + ( y * previewSize + x ) * 4;
				for ( int component = 0; component < 3; ++component ) destination[component] =
					(byte)( destination[component] * ( 1.0f - blend ) + source[component] * blend + 0.5f );
				destination[3] = 255;
			}
		}
	}
	state.editorPreviewPixels = preview;
	state.editorPreviewTextureDirty = true;
	globalImages->WriteTGA( previewPath, preview.data(), previewSize, previewSize,
		4, true, false );
	const idMaterial *material = declManager->FindMaterial( state.project.material, false );
	if ( material ) {
		// qer_editorImage is not a material stage, so ReloadImages() alone does
		// not touch it.  Reload it explicitly for live camera painting.
		if ( material->GetEditorImage() ) material->GetEditorImage()->Reload( false, true );
		material->ReloadImages( true );
	}
	if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
	Sys_UpdateWindows( W_CAMERA );
	state.lastPreviewUpdateTime = Sys_Milliseconds();
}
#endif

static void UpdateEditorPreview() {
	if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
	Sys_UpdateWindows( W_CAMERA );
}

static void EnterTexturePaintCameraPreview() {
	if ( g_pParentWnd == NULL || g_pParentWnd->GetCamera() == NULL ) return;
	CCamWnd *camera = g_pParentWnd->GetCamera();
	// F3 renders the compiled streaming asset.  Authoring mode uses reusable
	// layer images modulated by the compact per-vertex RGBA weights instead.
	if ( camera->GetRenderMode() ) camera->ToggleRenderMode();
	Select_Deselect();
	g_PrefsDlg.m_nEntityShowState = ENTITY_SKINNED;
	Texture_SetMode( ID_VIEW_BILINEAR );
	UpdateEditorPreview();
	Sys_UpdateWindows( W_ALL );
}

static bool SaveTile() {
	if ( !state.loaded || !state.dirty ) return true;
	idStr error;
	if ( !MegaTextureWriteTileTGA( state.project, state.tileX, state.tileY, state.tile.data(), error ) ) {
		SetStatus( error ); return false;
	}
	UpdateEditorPreview();
	state.dirty = false;
	SetStatus( va( "Saved tile %d,%d.", state.tileX, state.tileY ) );
	return true;
}

static bool SaveWeights() {
	if ( !state.loaded || !state.dirty ) return true;
	idStr error;
	if ( !MegaTextureWriteTerrainWeights( state.project, state.weights, state.transforms, error ) ) { SetStatus( error ); return false; }
	state.dirty = false;
	SetStatus( "Terrain layer weights and local paint mappings saved." );
	return true;
}

static bool SaveRoads() {
	if ( !state.loaded || !state.roadsDirty ) return true;
	idStr error;
	if ( !state.roads.Save( state.project.roadFile, error ) ) { SetStatus( error ); return false; }
	state.roadsDirty = false;
	SetStatus( va( "Saved %d editable road spline%s.", state.roads.NumRoads(), state.roads.NumRoads() == 1 ? "" : "s" ) );
	return true;
}

static bool SaveProject() {
	if ( !state.loaded ) return false;
	if ( state.terrainDirty && !SaveTerrain( false ) ) return false;
	if ( !SaveWeights() ) return false;
	if ( !SaveRoads() ) return false;
	idStr error;
	state.project.projectPath = state.projectPath;
	if ( !state.project.Save( state.projectPath, error ) || !state.project.WriteMaterial( error ) ) {
		SetStatus( error ); return false;
	}
	state.projectDirty = false;
	SetStatus( "Project and material saved." );
	return true;
}

static bool EnsureProjectMaterialLoaded( bool &reloaded ) {
	reloaded = false;
	if ( !state.loaded || state.project.material.IsEmpty() ) return false;
	const idStr materialPath = state.project.MaterialPath();
	const idMaterial *material = declManager->FindMaterial( state.project.material, false );
	const bool fileMissing = fileSystem->ReadFile( materialPath, NULL, NULL ) < 0;
	const bool declarationMissing = material == NULL || material->GetState() == DS_DEFAULTED;
	bool declarationOutdated = false;
	if ( material != NULL && !declarationMissing ) {
		std::vector<char> declarationText( material->GetTextLength() + 1, '\0' );
		material->GetText( declarationText.data() );
		declarationOutdated = idStr::FindText( declarationText.data(), state.project.OutputPath(), false ) < 0 ||
			idStr::FindText( declarationText.data(), state.project.PreviewPath(), false ) < 0;
	}
	if ( !fileMissing && !declarationMissing && !declarationOutdated ) return true;
	idStr error;
	if ( !state.project.WriteMaterial( error ) ) { SetStatus( error ); return false; }
	declManager->Reload( false );
	globalImages->ReloadAllMegaTextures();
	renderModelManager->ReloadModels( true );
	material = declManager->FindMaterial( state.project.material, false );
	if ( material == NULL || material->GetState() == DS_DEFAULTED ) {
		SetStatus( va( "Created %s, but material %s did not reload correctly.", materialPath.c_str(), state.project.material.c_str() ) );
		return false;
	}
	reloaded = true;
	common->Printf( "MegaTexture: created or updated and reloaded terrain material %s\n", materialPath.c_str() );
	return true;
}

static bool SaveTerrain( bool reloadModel ) {
	if ( !state.loaded || state.heights.empty() ) return false;
	idStr error;
	if ( !MegaTextureWriteTerrainHeightfield( state.project, state.heights, error ) ||
		 !MegaTextureWriteTerrainWeights( state.project, state.weights, state.transforms, error ) ||
		 !MegaTextureWriteTerrainModel( state.project, error ) || !state.project.Save( state.projectPath, error ) ||
		 !state.project.WriteMaterial( error ) ) {
		SetStatus( error ); return false;
	}
	state.terrainDirty = state.dirty = false;
	if ( reloadModel ) {
		const idStr entityName = va( "terrain_%s", state.project.name.c_str() );
		RefreshTerrainInRadiant( FindEntity( "name", entityName ) );
	}
	SetStatus( "Terrain heightfield and render/collision model updated." );
	return true;
}

static bool AddOrUpdateTerrainEntity() {
	if ( !state.loaded || state.project.name.IsEmpty() ) return false;
	const idStr entityName = va( "terrain_%s", state.project.name.c_str() );
	entity_t *entity = FindEntity( "megaTextureTerrain", "1" );
	if ( !entity ) entity = FindEntity( "name", entityName );
	if ( entity ) {
		SetKeyValue( entity, "name", entityName );
		SetKeyValue( entity, "model", state.project.terrainModel );
		SetKeyValue( entity, "megaTextureTerrain", "1" );
		SetKeyValue( entity, "megaTextureProject", state.project.projectPath );
		// The model must still be inlined for proc collision and shadow casting,
		// but its illumination is already in the MegaTexture RGB.
		SetKeyValue( entity, "inline", "1" );
		SetKeyValue( entity, "bakeLightmap", "0" );
		SetKeyValue( entity, "origin", va( "%g %g %g", state.project.terrainOrigin[0], state.project.terrainOrigin[1], state.project.terrainOrigin[2] ) );
		entity->origin.Set( state.project.terrainOrigin[0], state.project.terrainOrigin[1], state.project.terrainOrigin[2] );
	} else {
		idStr mapFragment = va( "Version 2\n{\n\"classname\" \"func_static\"\n\"name\" \"%s\"\n\"model\" \"%s\"\n\"megaTextureTerrain\" \"1\"\n\"megaTextureProject\" \"%s\"\n\"origin\" \"%g %g %g\"\n\"inline\" \"1\"\n\"bakeLightmap\" \"0\"\n}\n",
			entityName.c_str(), state.project.terrainModel.c_str(), state.project.projectPath.c_str(), state.project.terrainOrigin[0], state.project.terrainOrigin[1], state.project.terrainOrigin[2] );
		std::vector<char> buffer( mapFragment.Length() + 1 );
		memcpy( buffer.data(), mapFragment.c_str(), mapFragment.Length() + 1 );
		Map_ImportBuffer( buffer.data(), false );
		entity = FindEntity( "name", entityName );
	}
	RefreshTerrainInRadiant( entity );
	Sys_MarkMapModified();
	SetStatus( va( "Terrain entity '%s' is present in the current map.", entityName.c_str() ) );
	return true;
}

static bool LoadTile( int x, int y ) {
	if ( !SaveTile() ) return false;
	idStr error;
	if ( !MegaTextureLoadTileTGA( state.project, x, y, state.tile.data(), error ) ) {
		SetStatus( error ); return false;
	}
	state.tileX = x; state.tileY = y;
	bool legacyAlpha = true;
	for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) {
		if ( state.tile[i * 4 + 3] != 255 ) { legacyAlpha = false; break; }
	}
	if ( legacyAlpha ) for ( int i = 0; i < MEGA_TEXTURE_TILE_SIZE * MEGA_TEXTURE_TILE_SIZE; ++i ) state.tile[i * 4 + 3] = MEGA_FLAT_NORMAL_ALPHA;
	state.dirty = legacyAlpha; state.textureDirty = true; state.painting = false;
	state.undo.clear(); state.redo.clear();
	SetStatus( va( "Tile %d,%d. Missing source tiles use the project fill color.", x, y ) );
	return true;
}

static bool OpenProject( const char *path ) {
	if ( state.loaded && !SaveProject() ) return false;
	idStr error;
	megaTextureProject_t project;
	if ( !project.Load( path, error ) ) { SetStatus( error ); return false; }
	state.project = project;
	idStr::Copynz( state.projectPath, path, sizeof( state.projectPath ) );
	idStr::Copynz( state.heightImagePath, va( "megatextures/%s_height.tga", project.name.c_str() ), sizeof( state.heightImagePath ) );
	state.loaded = true; state.projectDirty = false;
	if ( !state.roads.Load( state.project.roadFile, error ) ) {
		SetStatus( error ); state.loaded = false; return false;
	}
	state.roadsDirty = false; state.selectedRoad = state.selectedRoadPoint = -1;
	state.roadDrawing = state.roadDragging = false;
	state.roadUndo.clear(); state.roadRedo.clear();
	if ( !MegaTextureLoadTerrainHeightfield( state.project, state.heights, error ) ) {
		state.heights.assign( state.project.terrainSamples * state.project.terrainSamples, 0.0f );
		if ( !MegaTextureWriteTerrainHeightfield( state.project, state.heights, error ) || !MegaTextureWriteTerrainModel( state.project, error ) ) {
			SetStatus( error ); state.loaded = false; return false;
		}
	}
	if ( !MegaTextureLoadTerrainWeights( state.project, state.weights, state.transforms, error ) ) {
		state.weights.assign( state.project.terrainSamples * state.project.terrainSamples * megaTextureProject_t::MAX_LAYERS, 0 );
		for ( int i = 0; i < state.project.terrainSamples * state.project.terrainSamples; ++i ) state.weights[i * megaTextureProject_t::MAX_LAYERS] = 255;
		MegaTextureInitializeTerrainTransforms( state.project, state.transforms );
		if ( !MegaTextureWriteTerrainWeights( state.project, state.weights, state.transforms, error ) || !MegaTextureWriteTerrainModel( state.project, error ) ) {
			SetStatus( error ); state.loaded = false; return false;
		}
	}
	state.terrainDirty = false; state.heightTextureDirty = true; state.terrainPainting = false;
	state.terrainUndo.clear(); state.terrainRedo.clear();
	state.dirty = false; state.undo.clear(); state.redo.clear();
	if ( state.megatiles.Num() == 0 ) RefreshMegatiles();
	state.selectedLayer = 0;
	state.lockLayerScale = idMath::Fabs( state.project.layerScale[0] - state.project.layerScaleY[0] ) < 0.001f;
	static const int buildResolutions[] = { 2048, 4096, 8192, 16384, 32768 };
	state.buildResolutionIndex = 0;
	for ( int i = 0; i < IM_ARRAYSIZE( buildResolutions ); ++i ) if ( project.resolution == buildResolutions[i] ) state.buildResolutionIndex = i;
	for ( int index = 0; index < state.megatiles.Num(); ++index ) if ( !idStr::Icmp( state.megatiles[index], state.project.layers[0] ) ) { SelectMegatile( index ); break; }
	SetStatus( "Terrain project loaded. Texture paint uses four vertex-color layers." );
	if ( state.open ) EnterTexturePaintCameraPreview();
	return true;
}

static bool GetMapProjectInfo( const char *sourceMap, idStr &mapPath, idStr &projectPath, idStr &assetName ) {
	mapPath.Clear(); projectPath.Clear(); assetName.Clear();
	if ( !sourceMap || !sourceMap[0] || !idStr::Icmp( sourceMap, "unnamed.world" ) ) return false;
	mapPath = sourceMap;
	mapPath.BackSlashesToSlashes();
	if ( mapPath.Find( ':' ) >= 0 || ( mapPath.Length() > 0 && mapPath[0] == '/' ) ) {
		const idStr relativePath = fileSystem->OSPathToRelativePath( sourceMap );
		if ( !relativePath.IsEmpty() ) mapPath = relativePath;
		mapPath.BackSlashesToSlashes();
	}
	mapPath.StripLeading( "./" );
	if ( mapPath.Left( 5 ).Icmp( "maps/" ) ) {
		const int maps = idStr::FindText( mapPath, "/maps/", false );
		if ( maps >= 0 ) mapPath = mapPath.Mid( maps + 1, mapPath.Length() - maps - 1 );
		else mapPath = "maps/" + mapPath;
	}
	idStr extension;
	mapPath.ExtractFileExtension( extension );
	if ( extension.IsEmpty() ) mapPath += ".world";
	else if ( extension.Icmp( "world" ) ) return false;
	idStr mapBaseName;
	mapPath.ExtractFileBase( mapBaseName );
	if ( mapBaseName.IsEmpty() || !mapBaseName.Icmp( "unnamed" ) ) return false;
	// currentmap is supplied in several forms by the legacy MRU, command line,
	// and ImGui file picker. A normalized path only represents a saved level if
	// that file is actually present in the active base path.
	if ( fileSystem->ReadFile( mapPath, NULL, NULL ) < 0 ) return false;

	idStr levelRelative = mapPath.Mid( 5, mapPath.Length() - 5 );
	levelRelative.StripFileExtension();
	if ( levelRelative.IsEmpty() ) return false;
	projectPath = "megatextures/levels/";
	projectPath += levelRelative;
	projectPath += ".megaproject";

	levelRelative.ExtractFileBase( assetName );
	for ( int i = 0; i < assetName.Length(); ++i ) {
		const char c = assetName[i];
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) ) assetName[i] = '_';
	}
	return true;
}

static bool GetLevelProjectInfo( idStr &mapPath, idStr &projectPath, idStr &assetName ) {
	if ( GetMapProjectInfo( currentmap, mapPath, projectPath, assetName ) ) return true;
	// Saved maps created by this editor carry an explicit project link on their
	// terrain entity. Use that map-owned identity if a legacy load path left
	// currentmap in an abbreviated or transient form.
	entity_t *terrainEntity = FindEntity( "megaTextureTerrain", "1" );
	if ( terrainEntity ) {
		const char *linkedProject = ValueForKey( terrainEntity, "megaTextureProject" );
		if ( linkedProject && linkedProject[0] ) {
			idStr error;
			megaTextureProject_t linked;
			if ( linked.Load( linkedProject, error ) && GetMapProjectInfo( linked.mapName, mapPath, projectPath, assetName ) ) return true;
		}
	}
	return false;
}

static bool ResolveExistingLevelProject( const idStr &mapPath, const idStr &derivedPath, idStr &resolvedPath ) {
	resolvedPath.Clear();
	if ( fileSystem->ReadFile( derivedPath, NULL, NULL ) >= 0 ) {
		resolvedPath = derivedPath;
		return true;
	}
	entity_t *terrainEntity = FindEntity( "megaTextureTerrain", "1" );
	if ( terrainEntity ) {
		const char *linkedProject = ValueForKey( terrainEntity, "megaTextureProject" );
		if ( linkedProject && linkedProject[0] && fileSystem->ReadFile( linkedProject, NULL, NULL ) >= 0 ) {
			idStr error;
			megaTextureProject_t linked;
			if ( linked.Load( linkedProject, error ) && !idStr::Icmp( linked.mapName, mapPath ) ) {
				resolvedPath = linkedProject;
				return true;
			}
		}
	}

	// Legacy projects may predate level-owned paths. Resolve them only when their
	// stored map exactly matches this level; never fall back to an arbitrary file.
	if ( !idStr::Icmp( cachedLevelProjectMap, mapPath ) ) {
		resolvedPath = cachedLevelProjectPath;
		return !resolvedPath.IsEmpty();
	}
	idStr::Copynz( cachedLevelProjectMap, mapPath, sizeof( cachedLevelProjectMap ) );
	cachedLevelProjectPath[0] = '\0';
	idFileList *projects = fileSystem->ListFilesTree( "megatextures", ".megaproject", true );
	if ( projects ) {
		for ( int index = 0; index < projects->GetNumFiles(); ++index ) {
			idStr error;
			megaTextureProject_t candidate;
			const char *candidatePath = projects->GetFile( index );
			if ( candidate.Load( candidatePath, error ) && !idStr::Icmp( candidate.mapName, mapPath ) ) {
				idStr::Copynz( cachedLevelProjectPath, candidatePath, sizeof( cachedLevelProjectPath ) );
				break;
			}
		}
		fileSystem->FreeFileList( projects );
	}
	resolvedPath = cachedLevelProjectPath;
	return !resolvedPath.IsEmpty();
}

static void ClearLevelProjectState() {
	state.loaded = state.dirty = state.projectDirty = state.terrainDirty = false;
	state.cameraStroke = false;
	state.heights.clear(); state.weights.clear(); state.transforms.clear();
	state.undo.clear(); state.redo.clear(); state.terrainUndo.clear(); state.terrainRedo.clear();
	state.roads.Clear(); state.roadsDirty = false;
	state.selectedRoad = state.selectedRoadPoint = -1;
	state.roadDrawing = state.roadDragging = false;
	state.roadUndo.clear(); state.roadRedo.clear();
}

static bool EnsureLevelProject() {
	idStr mapPath, desiredProjectPath, assetName;
	if ( !GetLevelProjectInfo( mapPath, desiredProjectPath, assetName ) ) {
		if ( state.loaded ) SaveProject();
		ClearLevelProjectState();
		state.projectPath[0] = '\0';
		SetStatus( "Save this level before creating its terrain." );
		return false;
	}
	idStr existingProjectPath;
	const bool projectExists = ResolveExistingLevelProject( mapPath, desiredProjectPath, existingProjectPath );
	const idStr &levelProjectPath = projectExists ? existingProjectPath : desiredProjectPath;
	if ( state.loaded && !idStr::Icmp( state.projectPath, levelProjectPath ) ) {
		if ( idStr::Icmp( state.project.mapName, mapPath ) ) {
			state.project.mapName = mapPath; state.projectDirty = true; SaveProject();
		}
		return true;
	}
	if ( state.loaded && !SaveProject() ) return false;
	ClearLevelProjectState();
	idStr::Copynz( state.projectPath, levelProjectPath, sizeof( state.projectPath ) );
	if ( projectExists ) {
		if ( !OpenProject( levelProjectPath ) ) return false;
		if ( idStr::Icmp( state.project.mapName, mapPath ) ) {
			state.project.mapName = mapPath; state.projectDirty = true; SaveProject();
		}
		return true;
	}
	SetStatus( va( "This saved level has no terrain project yet: %s", desiredProjectPath.c_str() ) );
	return false;
}

static void CreateProject() {
	static const int terrainSamples[] = { 33, 65, 129, 257, 513 };
	idStr mapPath, levelProjectPath, assetName;
	if ( !GetLevelProjectInfo( mapPath, levelProjectPath, assetName ) ) { SetStatus( "Save this level before creating terrain." ); return; }
	if ( fileSystem->ReadFile( levelProjectPath, NULL, NULL ) >= 0 ) { ImGui::CloseCurrentPopup(); EnsureLevelProject(); return; }
	idStr error;
	megaTextureProject_t project;
	if ( !MegaTextureCreateProject( assetName, MEGA_TEXTURE_LEVEL_SIZE, mapPath, project, error, levelProjectPath ) ) {
		SetStatus( error ); return;
	}
	project.terrainSamples = terrainSamples[state.newTerrainSamplesIndex];
	project.terrainSize = state.newTerrainSize;
	for ( int i = 0; i < 3; ++i ) project.terrainOrigin[i] = state.newTerrainOrigin[i];
	std::vector<float> heights( project.terrainSamples * project.terrainSamples, 0.0f );
	std::vector<byte> weights( project.terrainSamples * project.terrainSamples * megaTextureProject_t::MAX_LAYERS, 0 );
	for ( int i = 0; i < project.terrainSamples * project.terrainSamples; ++i ) weights[i * megaTextureProject_t::MAX_LAYERS] = 255;
	std::vector<megaTextureVertexTransform_t> transforms;
	MegaTextureInitializeTerrainTransforms( project, transforms );
	if ( !project.Save( project.projectPath, error ) || !MegaTextureWriteTerrainHeightfield( project, heights, error ) ||
		 !MegaTextureWriteTerrainWeights( project, weights, transforms, error ) || !MegaTextureWriteTerrainModel( project, error ) ) { SetStatus( error ); return; }
	idStr::Copynz( state.projectPath, project.projectPath, sizeof( state.projectPath ) );
	ImGui::CloseCurrentPopup();
	OpenProject( state.projectPath );
	AddOrUpdateTerrainEntity();
	FocusTerrainCamera();
}

static void PushUndo() {
	const int expected = state.project.terrainSamples * state.project.terrainSamples * megaTextureProject_t::MAX_LAYERS;
	if ( (int)state.weights.size() != expected || (int)state.transforms.size() != expected ) {
		SetStatus( "Cannot start paint stroke: terrain layer authoring data is not loaded." );
		return;
	}
	paintSnapshot_t snapshot;
	snapshot.weights = state.weights;
	snapshot.transforms = state.transforms;
	state.undo.push_back( snapshot );
	const size_t snapshotBytes = snapshot.weights.size() + snapshot.transforms.size() * sizeof( megaTextureVertexTransform_t );
	const int maximumSnapshots = idMath::ClampInt( 4, 32, snapshotBytes > 0 ? (int)( 64 * 1024 * 1024 / snapshotBytes ) : 32 );
	while ( (int)state.undo.size() > maximumSnapshots ) state.undo.erase( state.undo.begin() );
	state.redo.clear();
}

static bool ValidPaintSnapshot( const paintSnapshot_t &snapshot ) {
	const int expected = state.project.terrainSamples * state.project.terrainSamples * megaTextureProject_t::MAX_LAYERS;
	if ( (int)snapshot.weights.size() != expected || (int)snapshot.transforms.size() != expected ) return false;
	for ( int vertex = 0; vertex < state.project.terrainSamples * state.project.terrainSamples; ++vertex ) {
		const byte *weights = snapshot.weights.data() + vertex * megaTextureProject_t::MAX_LAYERS;
		if ( weights[0] + weights[1] + weights[2] + weights[3] != 255 ) return false;
	}
	return true;
}

static void Undo() {
	if ( state.undo.empty() ) return;
	if ( !ValidPaintSnapshot( state.undo.back() ) ) {
		state.undo.pop_back();
		SetStatus( "Ignored an invalid paint undo snapshot; current terrain paint was preserved." );
		return;
	}
	paintSnapshot_t current;
	current.weights = state.weights; current.transforms = state.transforms;
	state.redo.push_back( current );
	state.weights = state.undo.back().weights;
	state.transforms = state.undo.back().transforms;
	state.undo.pop_back();
	state.dirty = true; UpdateEditorPreview();
}

static void Redo() {
	if ( state.redo.empty() ) return;
	if ( !ValidPaintSnapshot( state.redo.back() ) ) {
		state.redo.pop_back();
		SetStatus( "Ignored an invalid paint redo snapshot; current terrain paint was preserved." );
		return;
	}
	paintSnapshot_t current;
	current.weights = state.weights; current.transforms = state.transforms;
	state.undo.push_back( current );
	state.weights = state.redo.back().weights;
	state.transforms = state.redo.back().transforms;
	state.redo.pop_back();
	state.dirty = true; UpdateEditorPreview();
}

static void PushRoadUndo() {
	state.roadUndo.push_back( state.roads );
	if ( state.roadUndo.size() > 64 ) state.roadUndo.erase( state.roadUndo.begin() );
	state.roadRedo.clear();
}

static void RoadUndo() {
	if ( state.roadUndo.empty() ) return;
	state.roadRedo.push_back( state.roads );
	state.roads = state.roadUndo.back();
	state.roadUndo.pop_back();
	state.selectedRoad = Min( state.selectedRoad, state.roads.NumRoads() - 1 );
	if ( state.selectedRoad < 0 || state.selectedRoadPoint >= (int)state.roads.GetRoad( state.selectedRoad ).points.size() ) state.selectedRoadPoint = -1;
	state.roadsDirty = true;
	UpdateEditorPreview();
}

static void RoadRedo() {
	if ( state.roadRedo.empty() ) return;
	state.roadUndo.push_back( state.roads );
	state.roads = state.roadRedo.back();
	state.roadRedo.pop_back();
	state.selectedRoad = Min( state.selectedRoad, state.roads.NumRoads() - 1 );
	if ( state.selectedRoad < 0 || state.selectedRoadPoint >= (int)state.roads.GetRoad( state.selectedRoad ).points.size() ) state.selectedRoadPoint = -1;
	state.roadsDirty = true;
	UpdateEditorPreview();
}

#if 0 // Legacy sparse source-tile painting.
static void Paint( int centerX, int centerY ) {
	const int radiusSquared = state.brushRadius * state.brushRadius;
	const int minimumY = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1, centerY - state.brushRadius );
	const int maximumY = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1, centerY + state.brushRadius );
	const int minimumX = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1, centerX - state.brushRadius );
	const int maximumX = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1, centerX + state.brushRadius );
	for ( int y = minimumY; y <= maximumY; ++y ) {
		for ( int x = minimumX; x <= maximumX; ++x ) {
			const int dx = x - centerX, dy = y - centerY;
			if ( dx * dx + dy * dy > radiusSquared ) continue;
			byte *pixel = state.tile.data() + ( y * MEGA_TEXTURE_TILE_SIZE + x ) * 4;
			const float distance = idMath::Sqrt( (float)( dx * dx + dy * dy ) );
			const float falloff = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - distance / state.brushRadius );
			const byte *source = NULL;
			const byte *sourceNormal = NULL;
			const byte *sourceMask = NULL;
			byte solid[4];
			byte flatNormal[4] = { 128, 128, 255, 255 };
			byte whiteMask[4] = { 255, 255, 255, 255 };
			if ( state.paintMegatile && !state.megatilePixels.empty() ) {
				float u = 0.5f + dx * state.paintScale / ( state.brushRadius * 2.0f );
				float v = 0.5f + dy * state.paintScale / ( state.brushRadius * 2.0f );
				u -= floor( u ); v -= floor( v );
				const int sourceX = idMath::ClampInt( 0, 127, (int)( u * 128.0f ) );
				const int sourceY = idMath::ClampInt( 0, 127, (int)( v * 128.0f ) );
				source = state.megatilePixels.data() + ( sourceY * 128 + sourceX ) * 4;
				sourceNormal = state.megatileNormalPixels.empty() ? flatNormal : state.megatileNormalPixels.data() + ( sourceY * 128 + sourceX ) * 4;
				sourceMask = state.megatileMaskPixels.empty() ? whiteMask : state.megatileMaskPixels.data() + ( sourceY * 128 + sourceX ) * 4;
			} else {
				for ( int component = 0; component < 4; ++component ) solid[component] = (byte)idMath::ClampInt( 0, 255, (int)( state.brushColor[component] * 255.0f + 0.5f ) );
				source = solid;
				sourceNormal = flatNormal;
				sourceMask = whiteMask;
			}
			const float mask = ( sourceMask[0] * 0.2126f + sourceMask[1] * 0.7152f + sourceMask[2] * 0.0722f ) / 255.0f;
			const float alpha = idMath::ClampFloat( 0.0f, 1.0f, state.paintOpacity * falloff * source[3] / 255.0f * mask );
			for ( int component = 0; component < 3; ++component ) pixel[component] = (byte)( pixel[component] * ( 1.0f - alpha ) + source[component] * alpha + 0.5f );
			float destinationX, destinationY;
			UnpackNormalXY( pixel[3], destinationX, destinationY );
			const float sourceX = sourceNormal[0] / 127.5f - 1.0f;
			const float sourceY = sourceNormal[1] / 127.5f - 1.0f;
			pixel[3] = PackNormalXY( destinationX * ( 1.0f - alpha ) + sourceX * alpha,
				destinationY * ( 1.0f - alpha ) + sourceY * alpha );
		}
	}
	state.dirty = state.textureDirty = true;
	state.previewStrokeValid = true;
	state.previewStrokeX = state.tileX * MEGA_TEXTURE_TILE_SIZE + centerX;
	state.previewStrokeY = state.tileY * MEGA_TEXTURE_TILE_SIZE + centerY;
	if ( Sys_Milliseconds() - state.lastPreviewUpdateTime >= 80 ) UpdateEditorPreview();
}
#endif

static void Paint( int centerX, int centerY, bool paintBaseLayer = false ) {
	if ( state.weights.empty() || state.transforms.size() != state.weights.size() ) return;
	const int samples = state.project.terrainSamples;
	const int targetLayer = paintBaseLayer ? 0 : state.selectedLayer;
	if ( state.project.layers[targetLayer].IsEmpty() ) {
		SetStatus( va( "Assign a texture to layer %d before painting it.", targetLayer + 1 ) );
		return;
	}
	const int radius = Max( state.brushRadius, 1 );
	const int radiusSquared = radius * radius;
	const int minimumY = idMath::ClampInt( 0, samples - 1, centerY - radius );
	const int maximumY = idMath::ClampInt( 0, samples - 1, centerY + radius );
	const int minimumX = idMath::ClampInt( 0, samples - 1, centerX - radius );
	const int maximumX = idMath::ClampInt( 0, samples - 1, centerX + radius );
	for ( int y = minimumY; y <= maximumY; ++y ) for ( int x = minimumX; x <= maximumX; ++x ) {
		const int dx = x - centerX, dy = y - centerY;
		if ( dx * dx + dy * dy > radiusSquared ) continue;
		const float distance = idMath::Sqrt( (float)( dx * dx + dy * dy ) );
		const float falloff = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - distance / radius );
		const float amount = idMath::ClampFloat( 0.0f, 1.0f, state.paintOpacity * falloff );
		byte *vertexWeights = state.weights.data() + ( y * samples + x ) * megaTextureProject_t::MAX_LAYERS;
		const int oldTarget = vertexWeights[targetLayer];
		const int newTarget = idMath::ClampInt( 0, 255, oldTarget + (int)( ( 255 - oldTarget ) * amount + 0.5f ) );
		if ( !paintBaseLayer && newTarget > 0 && falloff > 0.0f ) {
			const int transformIndex = ( y * samples + x ) * megaTextureProject_t::MAX_LAYERS + targetLayer;
			float oldScaleX, oldScaleY, oldRotation, oldPivotU, oldPivotV, oldPhaseU, oldPhaseV;
			MegaTextureDecodeVertexTransform( state.transforms[transformIndex], oldScaleX, oldScaleY, oldRotation,
				oldPivotU, oldPivotV, oldPhaseU, oldPhaseV );
			// Mapping paint is independent from layer opacity. This is what allows a
			// stroke to rotate/scale a layer whose vertex weight is already 255.
			const float normalizedDistance = distance / radius;
			const float mappingInnerRadius = 1.0f - state.paintMappingFeather;
			const float transformAmount = state.paintMappingFeather <= 0.001f || normalizedDistance <= mappingInnerRadius
				? 1.0f
				: idMath::ClampFloat( 0.0f, 1.0f, ( 1.0f - normalizedDistance ) / state.paintMappingFeather );
			const float paintPivotU = state.paintStrokePivotX / Max( 1, samples - 1 );
			const float paintPivotV = state.paintStrokePivotY / Max( 1, samples - 1 );
			float rotationDelta = state.project.layerRotation[targetLayer] - oldRotation;
			while ( rotationDelta < -180.0f ) rotationDelta += 360.0f;
			while ( rotationDelta > 180.0f ) rotationDelta -= 360.0f;
			float phaseUDelta = state.paintStrokePhaseU - oldPhaseU;
			float phaseVDelta = state.paintStrokePhaseV - oldPhaseV;
			while ( phaseUDelta < -0.5f ) phaseUDelta += 1.0f;
			while ( phaseUDelta > 0.5f ) phaseUDelta -= 1.0f;
			while ( phaseVDelta < -0.5f ) phaseVDelta += 1.0f;
			while ( phaseVDelta > 0.5f ) phaseVDelta -= 1.0f;
			state.transforms[transformIndex] = MegaTextureEncodeVertexTransform(
				oldScaleX + ( state.project.layerScale[targetLayer] - oldScaleX ) * transformAmount,
				oldScaleY + ( state.project.layerScaleY[targetLayer] - oldScaleY ) * transformAmount,
				oldRotation + rotationDelta * transformAmount,
				oldPivotU + ( paintPivotU - oldPivotU ) * transformAmount,
				oldPivotV + ( paintPivotV - oldPivotV ) * transformAmount,
				oldPhaseU + phaseUDelta * transformAmount,
				oldPhaseV + phaseVDelta * transformAmount );
		}
		const int oldOthers = 255 - oldTarget;
		const int newOthers = 255 - newTarget;
		int written = newTarget;
		for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) {
			if ( layer == targetLayer ) { vertexWeights[layer] = (byte)newTarget; continue; }
			const int value = oldOthers > 0 ? vertexWeights[layer] * newOthers / oldOthers : 0;
			vertexWeights[layer] = (byte)value;
			written += value;
		}
		// Put integer normalization remainder in a non-target channel when possible.
		if ( written != 255 ) {
			int remainderLayer = targetLayer == 0 ? 1 : 0;
			vertexWeights[remainderLayer] = (byte)idMath::ClampInt( 0, 255, vertexWeights[remainderLayer] + 255 - written );
		}
	}
	state.dirty = true;
	UpdateEditorPreview();
}

static void PushTerrainUndo() {
	state.terrainUndo.push_back( state.heights );
	if ( state.terrainUndo.size() > 24 ) state.terrainUndo.erase( state.terrainUndo.begin() );
	state.terrainRedo.clear();
}

static void TerrainUndo() {
	if ( state.terrainUndo.empty() ) return;
	state.terrainRedo.push_back( state.heights );
	state.heights = state.terrainUndo.back(); state.terrainUndo.pop_back();
	state.terrainDirty = state.heightTextureDirty = true;
}

static void TerrainRedo() {
	if ( state.terrainRedo.empty() ) return;
	state.terrainUndo.push_back( state.heights );
	state.heights = state.terrainRedo.back(); state.terrainRedo.pop_back();
	state.terrainDirty = state.heightTextureDirty = true;
}

static bool ImportTerrainHeightImage() {
	if ( !state.loaded || !state.heightImagePath[0] ) return false;
	byte *image = NULL;
	int width = 0, height = 0;
	R_LoadImage( state.heightImagePath, &image, &width, &height, NULL, false );
	if ( !image || width <= 0 || height <= 0 ) {
		if ( image ) Mem_Free( image );
		SetStatus( va( "Could not load heightmap image %s", state.heightImagePath ) );
		return false;
	}
	PushTerrainUndo();
	const int samples = state.project.terrainSamples;
	state.heights.resize( samples * samples );
	for ( int y = 0; y < samples; ++y ) {
		const float sourceY = y * ( height - 1 ) / (float)( samples - 1 );
		const int y0 = idMath::ClampInt( 0, height - 1, (int)floorf( sourceY ) );
		const int y1 = idMath::ClampInt( 0, height - 1, y0 + 1 );
		const float fy = sourceY - y0;
		for ( int x = 0; x < samples; ++x ) {
			const float sourceX = x * ( width - 1 ) / (float)( samples - 1 );
			const int x0 = idMath::ClampInt( 0, width - 1, (int)floorf( sourceX ) );
			const int x1 = idMath::ClampInt( 0, width - 1, x0 + 1 );
			const float fx = sourceX - x0;
			float luminance[4];
			const int sourceIndexes[4] = { ( y0 * width + x0 ) * 4, ( y0 * width + x1 ) * 4,
				( y1 * width + x0 ) * 4, ( y1 * width + x1 ) * 4 };
			for ( int corner = 0; corner < 4; ++corner ) {
				const byte *pixel = image + sourceIndexes[corner];
				luminance[corner] = ( pixel[0] * 0.2126f + pixel[1] * 0.7152f + pixel[2] * 0.0722f ) / 255.0f;
			}
			const float top = luminance[0] + ( luminance[1] - luminance[0] ) * fx;
			const float bottom = luminance[2] + ( luminance[3] - luminance[2] ) * fx;
			const float normalized = top + ( bottom - top ) * fy;
			state.heights[y * samples + x] = state.heightImportMinimum +
				( state.heightImportMaximum - state.heightImportMinimum ) * normalized;
		}
	}
	Mem_Free( image );
	state.terrainDirty = state.heightTextureDirty = true;
	SetStatus( va( "Imported %s as %d x %d terrain samples.", state.heightImagePath, samples, samples ) );
	return true;
}

static bool ExportTerrainHeightImage() {
	if ( !state.loaded || state.heights.empty() || !state.heightImagePath[0] ) return false;
	float minimum = state.heights[0], maximum = state.heights[0];
	for ( int i = 1; i < (int)state.heights.size(); ++i ) {
		minimum = Min( minimum, state.heights[i] );
		maximum = Max( maximum, state.heights[i] );
	}
	state.heightImportMinimum = minimum;
	state.heightImportMaximum = maximum;
	const float range = maximum > minimum ? maximum - minimum : 1.0f;
	const int samples = state.project.terrainSamples;
	std::vector<byte> image( samples * samples * 4 );
	for ( int i = 0; i < samples * samples; ++i ) {
		const byte value = (byte)idMath::ClampInt( 0, 255, (int)( ( state.heights[i] - minimum ) * 255.0f / range + 0.5f ) );
		image[i * 4 + 0] = image[i * 4 + 1] = image[i * 4 + 2] = value;
		image[i * 4 + 3] = 255;
	}
	globalImages->WriteTGA( state.heightImagePath, image.data(), samples, samples,
		4, true, false );
	SetStatus( va( "Exported %s (black %.2f, white %.2f).", state.heightImagePath, minimum, maximum ) );
	return true;
}

static float TerrainNoise( int x, int y ) {
	unsigned int value = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u;
	value = ( value ^ ( value >> 13 ) ) * 1274126177u;
	return ( ( value >> 8 ) & 65535 ) / 32767.5f - 1.0f;
}

static void SculptBrushExtents( float &radiusX, float &radiusY ) {
	radiusX = radiusY = (float)Max( state.terrainBrushRadius, 1 );
	if ( state.terrainBrushShape != TERRAIN_SHAPE_RECTANGLE ) return;
	const float aspect = idMath::ClampFloat( 0.25f, 4.0f, state.terrainBrushAspect );
	if ( aspect >= 1.0f ) radiusY /= aspect;
	else radiusX *= aspect;
}

static float SculptEdgeFalloff( float edgeDistance ) {
	if ( edgeDistance > 1.0f ) return 0.0f;
	const float feather = idMath::ClampFloat( 0.0f, 1.0f, state.terrainBrushFeather );
	if ( feather <= 0.0001f || edgeDistance <= 1.0f - feather ) return 1.0f;
	const float value = idMath::ClampFloat( 0.0f, 1.0f, ( 1.0f - edgeDistance ) / feather );
	return value * value * ( 3.0f - 2.0f * value );
}

static float SculptStencilValue( float normalizedX, float normalizedY ) {
	const float edgeDistance = state.terrainBrushShape == TERRAIN_SHAPE_CIRCLE ?
		idMath::Sqrt( normalizedX * normalizedX + normalizedY * normalizedY ) :
		Max( idMath::Fabs( normalizedX ), idMath::Fabs( normalizedY ) );
	const float edgeFalloff = SculptEdgeFalloff( edgeDistance );
	if ( edgeFalloff <= 0.0f ) return 0.0f;
	if ( state.selectedStencil < 0 || state.stencilPixels.empty() ) return edgeFalloff;
	const int x = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1,
		(int)( ( normalizedX * 0.5f + 0.5f ) * ( MEGA_TEXTURE_TILE_SIZE - 1 ) + 0.5f ) );
	const int y = idMath::ClampInt( 0, MEGA_TEXTURE_TILE_SIZE - 1,
		(int)( ( normalizedY * 0.5f + 0.5f ) * ( MEGA_TEXTURE_TILE_SIZE - 1 ) + 0.5f ) );
	return edgeFalloff * state.stencilPixels[( y * MEGA_TEXTURE_TILE_SIZE + x ) * 4] / 255.0f;
}

static void SculptTerrain( int centerX, int centerY, bool invert ) {
	const int samples = state.project.terrainSamples;
	const int radius = state.terrainBrushRadius;
	float radiusX, radiusY;
	SculptBrushExtents( radiusX, radiusY );
	const float amount = state.terrainBrushStrength * ImGui::GetIO().DeltaTime;
	const int minimumX = idMath::ClampInt( 0, samples - 1, centerX - radius );
	const int maximumX = idMath::ClampInt( 0, samples - 1, centerX + radius );
	const int minimumY = idMath::ClampInt( 0, samples - 1, centerY - radius );
	const int maximumY = idMath::ClampInt( 0, samples - 1, centerY + radius );
	std::vector<float> before;
	if ( state.terrainBrushMode == TERRAIN_BRUSH_SMOOTH ) before = state.heights;
	for ( int y = minimumY; y <= maximumY; ++y ) for ( int x = minimumX; x <= maximumX; ++x ) {
		const float dx = (float)( x - centerX ), dy = (float)( y - centerY );
		const float falloff = SculptStencilValue( dx / radiusX, dy / radiusY );
		if ( falloff <= 0.0f ) continue;
		float &height = state.heights[y * samples + x];
		switch ( state.terrainBrushMode ) {
			case TERRAIN_BRUSH_SMOOTH: {
				float average = 0.0f; int count = 0;
				for ( int oy = -1; oy <= 1; ++oy ) for ( int ox = -1; ox <= 1; ++ox ) {
					const int sx = idMath::ClampInt( 0, samples - 1, x + ox );
					const int sy = idMath::ClampInt( 0, samples - 1, y + oy );
					average += before[sy * samples + sx]; ++count;
				}
				height += ( average / count - height ) * idMath::ClampFloat( 0.0f, 1.0f, amount * 0.02f * falloff );
				break;
			}
			case TERRAIN_BRUSH_FLATTEN:
				height += ( state.terrainFlattenHeight - height ) * idMath::ClampFloat( 0.0f, 1.0f, amount * 0.02f * falloff );
				break;
			case TERRAIN_BRUSH_NOISE:
				height += TerrainNoise( x, y ) * amount * falloff * ( invert ? -1.0f : 1.0f );
				break;
			default:
				height += amount * falloff * ( invert ? -1.0f : 1.0f );
				break;
		}
	}
	state.terrainDirty = state.heightTextureDirty = true;
}

static void RenderSculptProfileControls() {
	const char *shapes[] = { "Circle", "Square", "Rectangle" };
	if ( ImGui::Combo( "Brush shape", &state.terrainBrushShape, shapes, IM_ARRAYSIZE( shapes ) ) ) SelectStencil( -1 );
	ImGui::SliderFloat( "Edge feather", &state.terrainBrushFeather, 0.0f, 1.0f, "%.2f" );
	ImGui::TextDisabled( "0.00 = hard edge, 1.00 = feather across the full brush" );
	if ( state.terrainBrushShape == TERRAIN_SHAPE_RECTANGLE ) {
		ImGui::SliderFloat( "Width / height", &state.terrainBrushAspect, 0.25f, 4.0f, "%.2f" );
	}
}

static const idDecl *FindAtmosphereDecl( const char *name ) {
	if ( !name || !name[0] ) return NULL;
	const qhandle_t atmosphereType = declManager->GetDeclTypeHandle(
		declAtmosphereIdentifier );
	const idDecl *decl = declManager->FindType( atmosphereType, name, false );
	return decl && decl->GetState() != DS_DEFAULTED ? decl : NULL;
}

static void RenderAtmosphereBakeSelector() {
	if ( !world_entity ) {
		ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.25f, 1.0f ), "No worldspawn is loaded." );
		return;
	}
	const char *currentName = ValueForKey( world_entity, "atmosphere" );
	const idDecl *currentDecl = FindAtmosphereDecl( currentName );
	ImGui::SetNextItemWidth( 220.0f );
	if ( ImGui::BeginCombo( "Bake atmosphere", currentName[0] ? currentName : "<select atmosphere>" ) ) {
		const qhandle_t atmosphereType = declManager->GetDeclTypeHandle(
			declAtmosphereIdentifier );
		for ( int declIndex = 0; declIndex < declManager->GetNumDecls( atmosphereType ); ++declIndex ) {
			const idDecl *decl = declManager->DeclByIndex( atmosphereType, declIndex, false );
			if ( !decl || decl->GetState() == DS_DEFAULTED || !decl->GetName()[0] ) continue;
			const bool selected = !idStr::Icmp( currentName, decl->GetName() );
			if ( ImGui::Selectable( decl->GetName(), selected ) ) {
				SetKeyValue( world_entity, "atmosphere", decl->GetName() );
				Sys_MarkMapModified();
				SetBuildStatus( va( "Atmosphere '%s' selected. The MegaTexture bake can run now; save the map before dmap.",
					decl->GetName() ), false );
			}
			if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s:%d", decl->GetFileName(), decl->GetLineNum() );
			if ( selected ) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	currentName = ValueForKey( world_entity, "atmosphere" );
	currentDecl = FindAtmosphereDecl( currentName );
	if ( !currentDecl ) {
		ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.25f, 1.0f ), currentName[0] ?
			"'%s' is not an atmosphere declaration. Choose one from the list." :
			"Choose an atmosphere declaration before baking.", currentName );
	}
}

static void RenderBuildStatus() {
	if ( !state.buildStatus[0] ) return;
	const ImVec4 color = state.buildStatusError ? ImVec4( 1.0f, 0.35f, 0.25f, 1.0f ) :
		ImVec4( 0.45f, 0.9f, 0.55f, 1.0f );
	ImGui::TextColored( color, "Build: %s", state.buildStatus );
}

static bool CompileProject( bool bakeLighting ) {
	if ( bakeLighting ) {
		const char *atmosphereName = world_entity ? ValueForKey( world_entity, "atmosphere" ) : "";
		if ( !FindAtmosphereDecl( atmosphereName ) ) {
			SetBuildStatus( atmosphereName[0] ? va( "Cannot bake: '%s' is not an atmosphere declaration.", atmosphereName ) :
				"Cannot bake: select a worldspawn atmosphere first.", true );
			common->Warning( "MegaTexture bake: %s", state.buildStatus );
			return false;
		}
	}
	static const int buildResolutions[] = { 2048, 4096, 8192, 16384, 32768 };
	state.project.resolution = buildResolutions[state.buildResolutionIndex];
	state.project.bakeLighting = bakeLighting;
	state.projectDirty = true;
	if ( !SaveProject() ) {
		SetBuildStatus( state.status, true );
		return false;
	}
	idStr error;
	SetBuildStatus( va( bakeLighting ? "Compiling and atmosphere-baking %dx%d MegaTexture..." :
		"Compiling unlit %dx%d MegaTexture from vertex layers...", state.project.resolution, state.project.resolution ), false );
	common->Printf( "MegaTexture editor: %s\n", state.buildStatus );
	// The renderer keeps the streamed .mega file open while the terrain is visible.
	// Purging also waits for any in-flight tile read before closing the handle, so
	// the compiler can atomically replace the level's existing MegaTexture.
	globalImages->PurgeAllMegaTextures();
	const idDict *worldSpawnOverride = world_entity ? &world_entity->epairs : NULL;
	if ( !MegaTextureCompileProject( state.projectPath, state.project.resolution, bakeLighting, worldSpawnOverride, error ) ) {
		// The old final file is still intact when compilation or replacement fails.
		// Restore it immediately so the editor viewport does not lose its terrain.
		globalImages->ReloadAllMegaTextures();
		SetBuildStatus( error, true );
		common->Warning( "MegaTexture editor build failed: %s", error.c_str() );
		return false;
	}
	globalImages->ReloadAllMegaTextures();
	const idStr entityName = va( "terrain_%s", state.project.name.c_str() );
	RefreshTerrainInRadiant( FindEntity( "name", entityName ) );
	Sys_UpdateWindows( W_ALL );
	SetBuildStatus( bakeLighting ? "Compiled, atmosphere-lit, validated, and reloaded. Dmap was not run." :
		"Compiled unlit, validated, and reloaded.", false );
	common->Printf( "MegaTexture editor: %s\n", state.buildStatus );
	return true;
}

static void QueueCompileProject( bool bakeLighting ) {
	if ( state.pendingCompile ) return;
	if ( bakeLighting ) {
		const char *atmosphereName = world_entity ? ValueForKey( world_entity, "atmosphere" ) : "";
		if ( !FindAtmosphereDecl( atmosphereName ) ) {
			SetBuildStatus( atmosphereName[0] ? va( "Cannot bake: '%s' is not an atmosphere declaration.", atmosphereName ) :
				"Cannot bake: select a worldspawn atmosphere first.", true );
			return;
		}
	}
	state.pendingCompile = true;
	state.pendingCompileBake = bakeLighting;
	state.pendingCompileFrame = ImGui::GetFrameCount();
	SetBuildStatus( bakeLighting ? "Atmosphere bake queued; compilation starts next frame." :
		"Unlit compile queued; compilation starts next frame.", false );
}

static void ProcessPendingCompile() {
	if ( !state.pendingCompile || ImGui::GetFrameCount() <= state.pendingCompileFrame ) return;
	const bool bakeLighting = state.pendingCompileBake;
	state.pendingCompile = false;
	CompileProject( bakeLighting );
}

static void RenderNewProjectPopup() {
	if ( state.requestNewProjectPopup ) {
		ImGui::OpenPopup( "New MegaTexture Project" );
		state.requestNewProjectPopup = false;
	}
	if ( !ImGui::BeginPopupModal( "New MegaTexture Project", NULL, ImGuiWindowFlags_AlwaysAutoResize ) ) return;
	idStr mapPath, levelProjectPath, assetName;
	if ( !GetLevelProjectInfo( mapPath, levelProjectPath, assetName ) ) {
		ImGui::TextWrapped( "Save the level first. Its terrain project will be created automatically from the saved map name." );
		if ( ImGui::Button( "Close" ) ) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}
	ImGui::TextWrapped( "Level: %s\nProject: %s", mapPath.c_str(), levelProjectPath.c_str() );
	const char *terrainSamples[] = { "33 x 33", "65 x 65", "129 x 129", "257 x 257", "513 x 513" };
	ImGui::Combo( "Terrain height samples", &state.newTerrainSamplesIndex, terrainSamples, IM_ARRAYSIZE( terrainSamples ) );
	ImGui::InputFloat( "Terrain world size", &state.newTerrainSize, 64.0f, 512.0f, "%.0f" );
	ImGui::InputFloat3( "Terrain origin", state.newTerrainOrigin, "%.0f" );
	ImGui::TextWrapped( "Creation writes the sculptable heightfield, compact RGBA vertex-layer weights and local paint mappings, and terrain model. Choose the final MegaTexture size later when compiling." );
	if ( ImGui::Button( "Create" ) ) CreateProject();
	ImGui::SameLine();
	if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

#if 0 // Projects are level-owned and can no longer be opened arbitrarily.
static void RenderOpenProjectPopup() {
	if ( state.requestOpenProjectPopup ) {
		ImGui::OpenPopup( "Open MegaTexture Project" );
		state.requestOpenProjectPopup = false;
	}
	ImGui::SetNextWindowSize( ImVec2( 640.0f, 480.0f ), ImGuiCond_Appearing );
	if ( !ImGui::BeginPopupModal( "Open MegaTexture Project", NULL, ImGuiWindowFlags_NoSavedSettings ) ) return;
	ImGui::TextWrapped( "Projects under base/megatextures" );
	bool openSelected = false;
	idFileList *projects = fileSystem->ListFilesTree( "megatextures", ".megaproject", true );
	ImGui::BeginChild( "MegaTextureProjectFiles", ImVec2( 0.0f, 330.0f ), true );
	if ( projects ) {
		for ( int i = 0; i < projects->GetNumFiles(); ++i ) {
			const char *path = projects->GetFile( i );
			const bool selected = !idStr::Icmp( state.openProjectPath, path );
			if ( ImGui::Selectable( path, selected, ImGuiSelectableFlags_AllowDoubleClick ) ) {
				idStr::Copynz( state.openProjectPath, path, sizeof( state.openProjectPath ) );
				openSelected = ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left );
			}
		}
	}
	ImGui::EndChild();
	if ( projects ) fileSystem->FreeFileList( projects );
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputTextWithHint( "##OpenMegaProjectPath", "megatextures/name.megaproject", state.openProjectPath, sizeof( state.openProjectPath ) );
	if ( ImGui::Button( "Open" ) ) openSelected = true;
	ImGui::SameLine();
	if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
	if ( openSelected ) {
		if ( state.openProjectPath[0] && OpenProject( state.openProjectPath ) ) {
			ImGui::CloseCurrentPopup();
		} else if ( !state.openProjectPath[0] ) {
			SetStatus( "Select a .megaproject file first." );
		}
	}
	ImGui::EndPopup();
}
#endif

static ImVec2 ProjectTerrainPoint( int x, int y, float height, float minimumHeight, float heightRange,
								   const ImVec2 &minimum, const ImVec2 &size, int samples ) {
	const float nx = x / (float)( samples - 1 ) - 0.5f;
	const float ny = y / (float)( samples - 1 ) - 0.5f;
	const float nz = ( height - minimumHeight ) / heightRange - 0.5f;
	return ImVec2( minimum.x + size.x * ( 0.5f + ( nx - ny ) * 0.42f ),
		minimum.y + size.y * ( 0.58f + ( nx + ny ) * 0.22f - nz * 0.52f ) );
}

static void RenderTerrainWirePreview() {
	if ( state.heights.empty() ) return;
	const int samples = state.project.terrainSamples;
	float minimumHeight = state.heights[0], maximumHeight = state.heights[0];
	for ( int i = 1; i < (int)state.heights.size(); ++i ) {
		if ( state.heights[i] < minimumHeight ) minimumHeight = state.heights[i];
		if ( state.heights[i] > maximumHeight ) maximumHeight = state.heights[i];
	}
	const float heightRange = maximumHeight > minimumHeight ? maximumHeight - minimumHeight : 1.0f;
	const ImVec2 canvasSize( ImGui::GetContentRegionAvail().x, 280.0f );
	const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton( "##Terrain3DPreview", canvasSize );
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( canvasMin, ImVec2( canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y ), IM_COL32( 14, 19, 24, 255 ), 4.0f );
	const int step = ( samples - 1 ) / 32 > 0 ? ( samples - 1 ) / 32 : 1;
	for ( int y = 0; y < samples; y += step ) {
		for ( int x = step; x < samples; x += step ) {
			const ImVec2 a = ProjectTerrainPoint( x - step, y, state.heights[y * samples + x - step], minimumHeight, heightRange, canvasMin, canvasSize, samples );
			const ImVec2 b = ProjectTerrainPoint( x, y, state.heights[y * samples + x], minimumHeight, heightRange, canvasMin, canvasSize, samples );
			draw->AddLine( a, b, IM_COL32( 80, 150, 120, 155 ), 1.0f );
		}
	}
	for ( int x = 0; x < samples; x += step ) {
		for ( int y = step; y < samples; y += step ) {
			const ImVec2 a = ProjectTerrainPoint( x, y - step, state.heights[( y - step ) * samples + x], minimumHeight, heightRange, canvasMin, canvasSize, samples );
			const ImVec2 b = ProjectTerrainPoint( x, y, state.heights[y * samples + x], minimumHeight, heightRange, canvasMin, canvasSize, samples );
			draw->AddLine( a, b, IM_COL32( 75, 125, 165, 135 ), 1.0f );
		}
	}
	draw->AddText( ImVec2( canvasMin.x + 8, canvasMin.y + 7 ), IM_COL32( 220, 230, 235, 230 ),
		va( "3D heightfield preview   min %.1f   max %.1f", minimumHeight, maximumHeight ) );
}

static void RenderTerrainEditor() {
	ImGui::TextWrapped( "Sculpting updates the same heightfield used by rendering, dmap, and collision. Left mouse applies the brush; right mouse inverts raise/noise." );
	if ( ImGui::InputFloat( "World size", &state.project.terrainSize, 64.0f, 512.0f, "%.0f" ) ) state.projectDirty = state.terrainDirty = true;
	if ( ImGui::InputFloat3( "World origin", state.project.terrainOrigin, "%.0f" ) ) state.projectDirty = true;
	if ( ImGui::Button( "Save / Regenerate Terrain" ) ) SaveTerrain( true );
	ImGui::SameLine();
	if ( ImGui::Button( "Add / Update Terrain in Map" ) && SaveTerrain( false ) ) AddOrUpdateTerrainEntity();
	ImGui::SameLine();
	if ( ImGui::Button( "Focus Camera" ) ) FocusTerrainCamera();
	ImGui::SameLine();
	if ( ImGui::Button( "Undo Sculpt" ) ) TerrainUndo();
	ImGui::SameLine();
	if ( ImGui::Button( "Redo Sculpt" ) ) TerrainRedo();
	if ( ImGui::CollapsingHeader( "Heightmap Import / Export" ) ) {
		ImGui::TextWrapped( "Use a grayscale TGA under base/. Black and white map to the height range below. Import resamples it to the project's terrain grid." );
		ImGui::SetNextItemWidth( 430.0f );
		ImGui::InputText( "Heightmap image", state.heightImagePath, sizeof( state.heightImagePath ) );
		ImGui::InputFloat( "Black height", &state.heightImportMinimum, 1.0f, 64.0f, "%.2f" );
		ImGui::InputFloat( "White height", &state.heightImportMaximum, 1.0f, 64.0f, "%.2f" );
		if ( ImGui::Button( "Import Heightmap" ) ) ImportTerrainHeightImage();
		ImGui::SameLine();
		if ( ImGui::Button( "Export Heightmap" ) ) ExportTerrainHeightImage();
	}
	const char *modes[] = { "Raise / Lower", "Smooth", "Flatten", "Noise" };
	ImGui::Combo( "Sculpt mode", &state.terrainBrushMode, modes, IM_ARRAYSIZE( modes ) );
	ImGui::SliderInt( "Sculpt radius (samples)", &state.terrainBrushRadius, 1, state.project.terrainSamples / 3 );
	ImGui::DragFloat( "Sculpt strength (units/sec)", &state.terrainBrushStrength, 4.0f, 1.0f, 8192.0f, "%.1f" );
	if ( state.terrainBrushMode == TERRAIN_BRUSH_FLATTEN ) ImGui::DragFloat( "Flatten height", &state.terrainFlattenHeight, 1.0f, -32768.0f, 32768.0f, "%.1f" );
	RenderSculptProfileControls();

	EnsureHeightTexture();
	float canvasSize = ImGui::GetContentRegionAvail().x;
	if ( canvasSize > 650.0f ) canvasSize = 650.0f;
	ImGui::Image( RadiantImGuiVulkanTexture( &state.heightTexture ), ImVec2( canvasSize, canvasSize ) );
	const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
	if ( ImGui::IsItemHovered() ) {
		const ImVec2 mouse = ImGui::GetMousePos();
		const int x = idMath::ClampInt( 0, state.project.terrainSamples - 1,
			(int)( ( mouse.x - minimum.x ) * state.project.terrainSamples / ( maximum.x - minimum.x ) ) );
		const int y = idMath::ClampInt( 0, state.project.terrainSamples - 1,
			(int)( ( mouse.y - minimum.y ) * state.project.terrainSamples / ( maximum.y - minimum.y ) ) );
		float radiusX, radiusY;
		SculptBrushExtents( radiusX, radiusY );
		radiusX *= canvasSize / state.project.terrainSamples;
		radiusY *= canvasSize / state.project.terrainSamples;
		ImDrawList *brushDraw = ImGui::GetWindowDrawList();
		const ImU32 outerColor = IM_COL32( 255, 245, 160, 230 );
		if ( state.terrainBrushShape == TERRAIN_SHAPE_CIRCLE ) brushDraw->AddCircle( mouse, radiusX, outerColor, 40, 1.5f );
		else brushDraw->AddRect( ImVec2( mouse.x - radiusX, mouse.y - radiusY ), ImVec2( mouse.x + radiusX, mouse.y + radiusY ), outerColor, 0.0f, 0, 1.5f );
		const float hardCore = 1.0f - state.terrainBrushFeather;
		if ( hardCore > 0.01f && hardCore < 0.99f ) {
			const ImU32 innerColor = IM_COL32( 255, 245, 160, 105 );
			if ( state.terrainBrushShape == TERRAIN_SHAPE_CIRCLE ) brushDraw->AddCircle( mouse, radiusX * hardCore, innerColor, 40, 1.0f );
			else brushDraw->AddRect( ImVec2( mouse.x - radiusX * hardCore, mouse.y - radiusY * hardCore ),
				ImVec2( mouse.x + radiusX * hardCore, mouse.y + radiusY * hardCore ), innerColor, 0.0f, 0, 1.0f );
		}
		ImGui::BeginTooltip(); ImGui::Text( "sample %d,%d  height %.2f", x, y, state.heights[y * state.project.terrainSamples + x] ); ImGui::EndTooltip();
		if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) || ImGui::IsMouseClicked( ImGuiMouseButton_Right ) ) {
			PushTerrainUndo(); state.terrainPainting = true;
		}
		if ( state.terrainPainting && ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) || ImGui::IsMouseDown( ImGuiMouseButton_Right ) ) )
			SculptTerrain( x, y, ImGui::IsMouseDown( ImGuiMouseButton_Right ) );
	}
	if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) && !ImGui::IsMouseDown( ImGuiMouseButton_Right ) ) state.terrainPainting = false;
	RenderTerrainWirePreview();
}

#if 0 // Replaced by 3D vertex-layer authoring in the inspector.
static void RenderPaintEditor() {
	const int axis = state.project.resolution / MEGA_TEXTURE_TILE_SIZE;
	ImGui::BeginChild( "MegatileLibrary", ImVec2( 275.0f, 0.0f ), true );
	ImGui::Text( "Megatile Library" );
	ImGui::InputTextWithHint( "##MegatileFilter", "filter base/megatiles...", state.megatileFilter, sizeof( state.megatileFilter ) );
	if ( ImGui::Button( "Refresh Library" ) ) RefreshMegatiles();
	ImGui::BeginChild( "MegatileList", ImVec2( 0.0f, 300.0f ), true );
	for ( int i = 0; i < state.megatiles.Num(); ++i ) {
		if ( state.megatileFilter[0] && idStr::FindText( state.megatiles[i], state.megatileFilter, false ) < 0 ) continue;
		if ( ImGui::Selectable( state.megatiles[i], state.selectedMegatile == i ) ) SelectMegatile( i );
	}
	ImGui::EndChild();
	EnsureMegatileTexture();
	if ( state.selectedMegatile >= 0 ) {
		ImGui::Image( RadiantImGuiVulkanTexture( &state.megatileTexture ), ImVec2( 192, 192 ) );
		ImGui::TextWrapped( "%s", state.megatiles[state.selectedMegatile].c_str() );
	}
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild( "TerrainPaintCanvas", ImVec2( 0, 0 ), false );
	int newX = state.tileX, newY = state.tileY;
	ImGui::SetNextItemWidth( 90.0f ); ImGui::InputInt( "Tile X", &newX ); ImGui::SameLine();
	ImGui::SetNextItemWidth( 90.0f ); ImGui::InputInt( "Tile Y", &newY ); ImGui::SameLine();
	if ( ImGui::Button( "Load" ) ) LoadTile( idMath::ClampInt( 0, axis - 1, newX ), idMath::ClampInt( 0, axis - 1, newY ) );
	ImGui::SameLine(); if ( ImGui::Button( "Save" ) ) SaveTile();
	ImGui::Checkbox( "Paint selected megatile", &state.paintMegatile );
	ImGui::SliderInt( "Paint radius", &state.brushRadius, 1, 64 );
	ImGui::SliderFloat( "Paint opacity", &state.paintOpacity, 0.01f, 1.0f );
	if ( state.paintMegatile ) ImGui::SliderFloat( "Megatile repeats", &state.paintScale, 0.25f, 8.0f, "%.2f" );
	else ImGui::ColorEdit4( "Paint color", state.brushColor, ImGuiColorEditFlags_AlphaBar );
	EnsureTexture();
	float canvasSize = ImGui::GetContentRegionAvail().x;
	if ( canvasSize > 700.0f ) canvasSize = 700.0f;
	ImGui::Image( RadiantImGuiVulkanTexture( &state.texture ), ImVec2( canvasSize, canvasSize ) );
	const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
	if ( ImGui::IsItemHovered() ) {
		const ImVec2 mouse = ImGui::GetMousePos();
		const int pixelX = idMath::ClampInt( 0, 127, (int)( ( mouse.x - minimum.x ) * 128.0f / ( maximum.x - minimum.x ) ) );
		const int pixelY = idMath::ClampInt( 0, 127, (int)( ( mouse.y - minimum.y ) * 128.0f / ( maximum.y - minimum.y ) ) );
		ImGui::GetWindowDrawList()->AddCircle( mouse, state.brushRadius * canvasSize / 128.0f, IM_COL32( 255, 255, 255, 220 ), 32, 1.5f );
		if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) ) { PushUndo(); state.painting = true; }
		if ( state.painting && ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) Paint( pixelX, pixelY );
	}
	if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) state.painting = false;
	ImGui::EndChild();
}
#endif

static void RenderPaintEditor() {
	ImGui::TextWrapped( "Texture painting now happens directly on the terrain in the 3D camera. Select the MegaTexture inspector tab beside Entity, choose a layer and texture, then left-drag on the terrain." );
	const size_t authoringBytes = state.weights.size() + state.transforms.size() * sizeof( megaTextureVertexTransform_t );
	ImGui::Text( "Authoring memory: %d vertices, four local mappings = %.2f MB", state.project.terrainSamples * state.project.terrainSamples,
		authoringBytes / ( 1024.0f * 1024.0f ) );
}

static float SampleTerrainHeight( float sampleX, float sampleY ) {
	const int samples = state.project.terrainSamples;
	const int x0 = idMath::ClampInt( 0, samples - 1, (int)floorf( sampleX ) );
	const int y0 = idMath::ClampInt( 0, samples - 1, (int)floorf( sampleY ) );
	const int x1 = idMath::ClampInt( 0, samples - 1, x0 + 1 );
	const int y1 = idMath::ClampInt( 0, samples - 1, y0 + 1 );
	const float fx = idMath::ClampFloat( 0.0f, 1.0f, sampleX - x0 );
	const float fy = idMath::ClampFloat( 0.0f, 1.0f, sampleY - y0 );
	const float top = state.heights[y0 * samples + x0] * ( 1.0f - fx ) + state.heights[y0 * samples + x1] * fx;
	const float bottom = state.heights[y1 * samples + x0] * ( 1.0f - fx ) + state.heights[y1 * samples + x1] * fx;
	return top * ( 1.0f - fy ) + bottom * fy;
}

static bool ClipRayAxis( float origin, float direction, float minimum, float maximum, float &nearDistance, float &farDistance ) {
	if ( idMath::Fabs( direction ) < 0.000001f ) return origin >= minimum && origin <= maximum;
	float a = ( minimum - origin ) / direction;
	float b = ( maximum - origin ) / direction;
	if ( a > b ) { const float temporary = a; a = b; b = temporary; }
	nearDistance = Max( nearDistance, a );
	farDistance = Min( farDistance, b );
	return nearDistance <= farDistance;
}

static bool IntersectTerrain( const idVec3 &rayOrigin, const idVec3 &rayDirection,
	idVec3 &hit, float &sampleX, float &sampleY ) {
	if ( !state.loaded || state.heights.empty() || state.project.terrainSize <= 0.0f ) return false;
	float minimumHeight = state.heights[0], maximumHeight = state.heights[0];
	for ( int i = 1; i < (int)state.heights.size(); ++i ) {
		minimumHeight = Min( minimumHeight, state.heights[i] );
		maximumHeight = Max( maximumHeight, state.heights[i] );
	}
	const float halfSize = state.project.terrainSize * 0.5f;
	float nearDistance = 0.0f, farDistance = 10000000.0f;
	if ( !ClipRayAxis( rayOrigin.x, rayDirection.x, state.project.terrainOrigin[0] - halfSize,
		state.project.terrainOrigin[0] + halfSize, nearDistance, farDistance ) ||
		 !ClipRayAxis( rayOrigin.y, rayDirection.y, state.project.terrainOrigin[1] - halfSize,
		state.project.terrainOrigin[1] + halfSize, nearDistance, farDistance ) ||
		 !ClipRayAxis( rayOrigin.z, rayDirection.z, state.project.terrainOrigin[2] + minimumHeight - 1.0f,
		state.project.terrainOrigin[2] + maximumHeight + 1.0f, nearDistance, farDistance ) ) return false;
	nearDistance = Max( nearDistance, 0.0f );
	if ( farDistance < nearDistance ) return false;
	const int samples = state.project.terrainSamples;
	const float spacing = state.project.terrainSize / ( samples - 1 );
	const float horizontalRate = Max( idMath::Fabs( rayDirection.x ), idMath::Fabs( rayDirection.y ) );
	const int steps = idMath::ClampInt( 64, 4096,
		(int)( ( farDistance - nearDistance ) * Max( horizontalRate, 0.05f ) / Max( spacing * 0.4f, 0.01f ) ) + 2 );
	float previousDistance = nearDistance;
	idVec3 previousPoint = rayOrigin + rayDirection * previousDistance;
	float previousX = ( previousPoint.x - ( state.project.terrainOrigin[0] - halfSize ) ) * ( samples - 1 ) / state.project.terrainSize;
	float previousY = ( ( state.project.terrainOrigin[1] + halfSize ) - previousPoint.y ) * ( samples - 1 ) / state.project.terrainSize;
	float previousDifference = previousPoint.z - state.project.terrainOrigin[2] - SampleTerrainHeight( previousX, previousY );
	for ( int step = 1; step <= steps; ++step ) {
		const float distance = nearDistance + ( farDistance - nearDistance ) * step / steps;
		const idVec3 point = rayOrigin + rayDirection * distance;
		const float x = ( point.x - ( state.project.terrainOrigin[0] - halfSize ) ) * ( samples - 1 ) / state.project.terrainSize;
		const float y = ( ( state.project.terrainOrigin[1] + halfSize ) - point.y ) * ( samples - 1 ) / state.project.terrainSize;
		const float difference = point.z - state.project.terrainOrigin[2] - SampleTerrainHeight( x, y );
		if ( difference == 0.0f || ( previousDifference < 0.0f ) != ( difference < 0.0f ) ) {
			float low = previousDistance, high = distance;
			float lowDifference = previousDifference;
			for ( int refinement = 0; refinement < 12; ++refinement ) {
				const float middle = ( low + high ) * 0.5f;
				const idVec3 middlePoint = rayOrigin + rayDirection * middle;
				const float middleX = ( middlePoint.x - ( state.project.terrainOrigin[0] - halfSize ) ) * ( samples - 1 ) / state.project.terrainSize;
				const float middleY = ( ( state.project.terrainOrigin[1] + halfSize ) - middlePoint.y ) * ( samples - 1 ) / state.project.terrainSize;
				const float middleDifference = middlePoint.z - state.project.terrainOrigin[2] - SampleTerrainHeight( middleX, middleY );
				if ( ( lowDifference < 0.0f ) != ( middleDifference < 0.0f ) ) high = middle;
				else { low = middle; lowDifference = middleDifference; }
			}
			hit = rayOrigin + rayDirection * ( ( low + high ) * 0.5f );
			sampleX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ),
				( hit.x - ( state.project.terrainOrigin[0] - halfSize ) ) * ( samples - 1 ) / state.project.terrainSize );
			sampleY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ),
				( ( state.project.terrainOrigin[1] + halfSize ) - hit.y ) * ( samples - 1 ) / state.project.terrainSize );
			return true;
		}
		previousDistance = distance;
		previousDifference = difference;
	}
	return false;
}

static void RenderSculptInspector() {
	ImGui::TextWrapped( "Hovering shows the terrain-projected brush footprint and full-strength boundary. Left-drag to sculpt; hold Ctrl to invert raise/noise. Right and middle mouse still navigate the camera." );
	const char *modes[] = { "Raise / Lower", "Smooth", "Flatten", "Noise" };
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::Combo( "##SculptMode", &state.terrainBrushMode, modes, IM_ARRAYSIZE( modes ) );
	ImGui::SliderInt( "Radius (samples)", &state.terrainBrushRadius, 1, Max( 1, state.project.terrainSamples / 3 ) );
	ImGui::DragFloat( "Strength", &state.terrainBrushStrength, 4.0f, 1.0f, 8192.0f, "%.1f units/sec" );
	if ( state.terrainBrushMode == TERRAIN_BRUSH_FLATTEN ) ImGui::DragFloat( "Flatten height", &state.terrainFlattenHeight, 1.0f, -32768.0f, 32768.0f, "%.1f" );
	RenderSculptProfileControls();
	ImGui::SeparatorText( "Sculpt stencil" );
	ImGui::InputTextWithHint( "##StencilFilter", "filter *_mask.tga...", state.stencilFilter, sizeof( state.stencilFilter ) );
	if ( ImGui::Selectable( "Use procedural brush shape", state.selectedStencil < 0 ) ) SelectStencil( -1 );
	ImGui::BeginChild( "SculptStencilList", ImVec2( 0.0f, 180.0f ), true );
	for ( int i = 0; i < state.stencils.Num(); ++i ) {
		if ( state.stencilFilter[0] && idStr::FindText( state.stencils[i], state.stencilFilter, false ) < 0 ) continue;
		if ( ImGui::Selectable( state.stencils[i], state.selectedStencil == i ) ) SelectStencil( i );
	}
	ImGui::EndChild();
	if ( state.selectedStencil >= 0 ) {
		EnsureStencilTexture();
		ImGui::Image( RadiantImGuiVulkanTexture( &state.stencilTexture ), ImVec2( 112, 112 ) );
		ImGui::SameLine(); ImGui::TextWrapped( "%s", state.stencils[state.selectedStencil].c_str() );
	}
}

static void RenderPaintInspector() {
	ImGui::TextWrapped( "Hovering shows a terrain-projected radius and orientation cross colored for the selected RGBA layer. Every new click anchors the texture under the cursor; painting over a fully weighted copy of the same layer still changes its local scale and rotation. Hold Ctrl to return the area to layer 1. _local normal maps are picked up automatically when compiling." );
	ImGui::SeparatorText( "Paint brush size" );
	const int maximumRadius = Max( 1, state.project.terrainSamples / 3 );
	bool brushSizeChanged = ImGui::SliderInt( "Radius (vertices)", &state.brushRadius, 1, maximumRadius );
	if ( ImGui::Button( "- Smaller  [" ) ) { state.brushRadius = Max( 1, state.brushRadius - 1 ); brushSizeChanged = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "+ Larger  ]" ) ) { state.brushRadius = Min( maximumRadius, state.brushRadius + 1 ); brushSizeChanged = true; }
	const float worldRadius = state.brushRadius * state.project.terrainSize / Max( 1, state.project.terrainSamples - 1 );
	ImGui::TextDisabled( "Approximate world radius: %.1f units", worldRadius );
	if ( brushSizeChanged ) UpdateEditorPreview();
	ImGui::SliderFloat( "Opacity", &state.paintOpacity, 0.01f, 1.0f );
	ImGui::SliderFloat( "Mapping edge feather", &state.paintMappingFeather, 0.0f, 1.0f, "%.2f" );
	ImGui::TextDisabled( "0 is a hard scale/rotation edge; 1 feathers across the full brush." );
	ImGui::SeparatorText( "Terrain layers (RGBA vertex weights)" );
	for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) {
		ImGui::PushID( layer );
		if ( ImGui::RadioButton( "##Layer", state.selectedLayer == layer ) ) {
			state.selectedLayer = layer;
			state.lockLayerScale = idMath::Fabs( state.project.layerScale[layer] - state.project.layerScaleY[layer] ) < 0.001f;
			UpdateEditorPreview();
		}
		ImGui::SameLine();
		ImGui::TextWrapped( "%d: %s", layer + 1, state.project.layers[layer].IsEmpty() ? "<unassigned>" : state.project.layers[layer].c_str() );
		ImGui::PopID();
	}
	ImGui::SeparatorText( va( "Layer %d paint mapping", state.selectedLayer + 1 ) );
	ImGui::TextWrapped( "The next stroke writes this scale and rotation into the painted vertices. Existing areas of this layer keep their own mapping." );
	bool layerTransformChanged = false;
	if ( ImGui::Checkbox( "Uniform X/Y scale", &state.lockLayerScale ) && state.lockLayerScale ) {
		state.project.layerScaleY[state.selectedLayer] = state.project.layerScale[state.selectedLayer];
		layerTransformChanged = true;
	}
	ImGui::SetNextItemWidth( -1.0f );
	if ( ImGui::DragFloat( "X repeats", &state.project.layerScale[state.selectedLayer], 0.25f, 0.25f, 512.0f, "%.2f" ) ) {
		if ( state.lockLayerScale ) state.project.layerScaleY[state.selectedLayer] = state.project.layerScale[state.selectedLayer];
		layerTransformChanged = true;
	}
	if ( !state.lockLayerScale ) {
		ImGui::SetNextItemWidth( -1.0f );
		if ( ImGui::DragFloat( "Y repeats", &state.project.layerScaleY[state.selectedLayer], 0.25f, 0.25f, 512.0f, "%.2f" ) ) layerTransformChanged = true;
	}
	ImGui::SetNextItemWidth( -1.0f );
	if ( ImGui::SliderFloat( "Rotation", &state.project.layerRotation[state.selectedLayer], -180.0f, 180.0f, "%.1f degrees" ) ) layerTransformChanged = true;
	if ( ImGui::Button( "Reset paint mapping" ) ) {
		state.project.layerScale[state.selectedLayer] = state.project.layerScaleY[state.selectedLayer] = 32.0f;
		state.project.layerRotation[state.selectedLayer] = 0.0f;
		layerTransformChanged = true;
	}
	if ( layerTransformChanged ) {
		state.projectDirty = true;
		// The terrain data is unchanged until the next stroke, but the rotated
		// paint-orientation gizmo should reflect the new brush mapping now.
		UpdateEditorPreview();
	}
	ImGui::SeparatorText( va( "Choose texture for layer %d", state.selectedLayer + 1 ) );
	ImGui::InputTextWithHint( "##MegatileFilter", "filter base/megatiles...", state.megatileFilter, sizeof( state.megatileFilter ) );
	if ( ImGui::Button( "Refresh texture library" ) ) RefreshMegatiles();
	ImGui::BeginChild( "InspectorMegatileList", ImVec2( 0.0f, 220.0f ), true );
	for ( int i = 0; i < state.megatiles.Num(); ++i ) {
		if ( state.megatileFilter[0] && idStr::FindText( state.megatiles[i], state.megatileFilter, false ) < 0 ) continue;
		if ( ImGui::Selectable( state.megatiles[i], state.selectedMegatile == i ) ) SelectMegatile( i );
	}
	ImGui::EndChild();
	if ( state.selectedMegatile >= 0 ) {
		EnsureMegatileTexture();
		ImGui::Image( RadiantImGuiVulkanTexture( &state.megatileTexture ), ImVec2( 112, 112 ) );
		ImGui::SameLine(); ImGui::TextWrapped( "%s\nClicking an item assigns it to layer %d.", state.megatiles[state.selectedMegatile].c_str(), state.selectedLayer + 1 );
	}
}

static void RenderRoadInspector() {
	ImGui::TextWrapped( "Create a road, enable Add control points, then click across the terrain. Turn it off to select and drag points. Ctrl-click or Delete removes a point. Roads stay editable in the level .roads file and are composited into the diffuse and _local data when the MegaTexture is compiled." );
	const char *newTexture = state.selectedMegatile >= 0 && state.selectedMegatile < state.megatiles.Num()
		? state.megatiles[state.selectedMegatile].c_str() : "";
	if ( ImGui::Button( "New road spline" ) ) {
		PushRoadUndo();
		state.selectedRoad = state.roads.AddRoad( newTexture );
		state.selectedRoadPoint = -1; state.roadDrawing = true; state.roadsDirty = true;
		SetStatus( newTexture[0] ? "New road created. Click in the 3D view to place control points." :
			"New road created. Choose its texture, then click in the 3D view to place control points." );
		UpdateEditorPreview();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Save roads" ) ) SaveRoads();

	ImGui::SeparatorText( "Road splines" );
	ImGui::BeginChild( "RoadSplineList", ImVec2( 0.0f, 145.0f ), true );
	for ( int index = 0; index < state.roads.NumRoads(); ++index ) {
		const megaTextureRoad_t &road = state.roads.GetRoad( index );
		ImGui::PushID( index );
		const idStr label = va( "%s%s (%d points)", road.enabled ? "" : "[off] ", road.name.c_str(), (int)road.points.size() );
		if ( ImGui::Selectable( label, state.selectedRoad == index ) ) {
			state.selectedRoad = index; state.selectedRoadPoint = -1;
			for ( int texture = 0; texture < state.megatiles.Num(); ++texture ) {
				if ( !idStr::Icmp( state.megatiles[texture], road.texture ) ) { SelectMegatile( texture ); break; }
			}
			UpdateEditorPreview();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	if ( state.selectedRoad >= 0 && state.selectedRoad < state.roads.NumRoads() ) {
		megaTextureRoad_t &road = state.roads.EditRoad( state.selectedRoad );
		char name[128]; idStr::Copynz( name, road.name, sizeof( name ) );
		if ( ImGui::InputText( "Road name", name, sizeof( name ) ) ) {
			for ( char *character = name; *character; ++character ) if ( *character == '"' ) *character = '\'';
			road.name = name; state.roadsDirty = true;
		}
		if ( ImGui::Checkbox( "Enabled in preview and bake", &road.enabled ) ) state.roadsDirty = true;
		if ( ImGui::DragFloat( "Road width", &road.width, 8.0f, 1.0f, 16384.0f, "%.1f world units" ) ) {
			road.feather = Min( road.feather, road.width * 0.5f ); state.roadsDirty = true; UpdateEditorPreview();
		}
		if ( ImGui::DragFloat( "Edge feather", &road.feather, 4.0f, 0.0f, Max( road.width * 0.5f, 0.0f ), "%.1f world units" ) ) {
			state.roadsDirty = true; UpdateEditorPreview();
		}
		if ( ImGui::DragFloat( "Texture repeat length", &road.repeatLength, 8.0f, 1.0f, 16384.0f, "%.1f world units" ) ) {
			state.roadsDirty = true; UpdateEditorPreview();
		}
		if ( ImGui::Checkbox( "Add control points", &state.roadDrawing ) ) {
			state.roadDragging = false;
			SetStatus( state.roadDrawing ? "Click the terrain to append road control points." : "Click and drag a road control point to adjust it." );
		}
		ImGui::TextDisabled( "Selected point: %s", state.selectedRoadPoint >= 0 ? va( "%d", state.selectedRoadPoint + 1 ) : "none" );
		if ( ImGui::Button( "Reverse direction" ) ) {
			PushRoadUndo(); state.roads.ReverseRoad( state.selectedRoad ); state.selectedRoadPoint = -1;
			state.roadsDirty = true; UpdateEditorPreview();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Move up" ) && state.selectedRoad > 0 ) {
			PushRoadUndo(); state.roads.MoveRoad( state.selectedRoad, -1 ); --state.selectedRoad;
			state.roadsDirty = true; UpdateEditorPreview(); return;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Move down" ) && state.selectedRoad + 1 < state.roads.NumRoads() ) {
			PushRoadUndo(); state.roads.MoveRoad( state.selectedRoad, 1 ); ++state.selectedRoad;
			state.roadsDirty = true; UpdateEditorPreview(); return;
		}
		ImGui::TextDisabled( "Later roads are baked over earlier roads at intersections." );
		if ( ImGui::Button( "Delete point" ) && state.selectedRoadPoint >= 0 ) {
			PushRoadUndo(); state.roads.DeletePoint( state.selectedRoad, state.selectedRoadPoint ); state.selectedRoadPoint = -1;
			state.roadsDirty = true; UpdateEditorPreview();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Delete road" ) ) {
			PushRoadUndo(); state.roads.DeleteRoad( state.selectedRoad );
			state.selectedRoad = Min( state.selectedRoad, state.roads.NumRoads() - 1 ); state.selectedRoadPoint = -1;
			state.roadsDirty = true; UpdateEditorPreview();
			return;
		}
		ImGui::TextWrapped( "Texture: %s", road.texture.IsEmpty() ? "<choose one below>" : road.texture.c_str() );
	}

	ImGui::SeparatorText( "Choose road texture" );
	ImGui::InputTextWithHint( "##RoadTextureFilter", "filter base/megatiles...", state.roadTextureFilter, sizeof( state.roadTextureFilter ) );
	if ( ImGui::Button( "Refresh road textures" ) ) RefreshMegatiles();
	ImGui::BeginChild( "RoadTextureList", ImVec2( 0.0f, 220.0f ), true );
	for ( int index = 0; index < state.megatiles.Num(); ++index ) {
		if ( state.roadTextureFilter[0] && idStr::FindText( state.megatiles[index], state.roadTextureFilter, false ) < 0 ) continue;
		if ( ImGui::Selectable( state.megatiles[index], state.selectedMegatile == index ) ) SelectMegatile( index );
	}
	ImGui::EndChild();
	if ( state.selectedMegatile >= 0 ) {
		EnsureMegatileTexture();
		ImGui::Image( RadiantImGuiVulkanTexture( &state.megatileTexture ), ImVec2( 112, 112 ) );
		ImGui::SameLine(); ImGui::TextWrapped( "%s\nSelecting an item assigns it to the active road.", state.megatiles[state.selectedMegatile].c_str() );
	}
}

} // namespace

void MegaTextureEditorImGuiShow( const char *project ) {
	state.open = true;
	EnsureLevelProject();
}

void MegaTextureEditorImGuiHide() { SaveProject(); state.open = false; }
bool MegaTextureEditorImGuiIsOpen() { return state.open; }

void MegaTextureEditorImGuiSetModeActive( bool active ) {
	if ( active == state.open ) return;
	if ( !active ) {
		if ( state.cameraStroke ) {
			if ( state.editMode == MEGA_EDIT_SCULPT ) SaveTerrain( true );
			else if ( state.editMode == MEGA_EDIT_PAINT ) SaveWeights();
			else SaveRoads();
			state.cameraStroke = false;
		}
		SaveProject();
	}
	state.open = active;
	if ( active && state.megatiles.Num() == 0 ) RefreshMegatiles();
	if ( active ) EnsureLevelProject();
	if ( active && state.loaded ) EnterTexturePaintCameraPreview();
}

void MegaTextureEditorImGuiOnMapLoaded() {
	if ( !EnsureLevelProject() || !state.loaded ) return;
	bool materialReloaded = false;
	if ( !EnsureProjectMaterialLoaded( materialReloaded ) ) return;
	const idStr entityName = va( "terrain_%s", state.project.name.c_str() );
	entity_t *terrainEntity = FindEntity( "megaTextureTerrain", "1" );
	if ( !terrainEntity ) terrainEntity = FindEntity( "name", entityName );
	const bool correctLink = terrainEntity &&
		!idStr::Icmp( ValueForKey( terrainEntity, "megaTextureProject" ), state.project.projectPath );
	if ( !correctLink ) {
		AddOrUpdateTerrainEntity();
		SetStatus( "Loaded the level terrain project and restored its terrain entity; save the map to keep the restored entity." );
	} else if ( materialReloaded ) {
		RefreshTerrainInRadiant( terrainEntity );
		SetStatus( va( "Created and reloaded %s so the terrain displays in DoomEdit.", state.project.MaterialPath().c_str() ) );
	}
}

void MegaTextureEditorImGuiOnBlankMapOverwrite( const char *mapFile ) {
	idStr mapPath, canonicalProjectPath, assetName;
	if ( !GetMapProjectInfo( mapFile, mapPath, canonicalProjectPath, assetName ) ) return;
	int removedProjects = 0;
	if ( fileSystem->ReadFile( canonicalProjectPath, NULL, NULL ) >= 0 ) {
		idStr error;
		megaTextureProject_t removedProject;
		if ( removedProject.Load( canonicalProjectPath, error ) && !removedProject.roadFile.IsEmpty() ) fileSystem->RemoveFile( removedProject.roadFile );
		fileSystem->RemoveFile( canonicalProjectPath );
		++removedProjects;
	}
	// Remove a matching legacy location too. Project map identity is the safety
	// boundary; unrelated projects are never touched.
	idFileList *projects = fileSystem->ListFilesTree( "megatextures", ".megaproject", true );
	if ( projects ) {
		for ( int index = 0; index < projects->GetNumFiles(); ++index ) {
			const char *candidatePath = projects->GetFile( index );
			if ( !idStr::Icmp( candidatePath, canonicalProjectPath ) ) continue;
			idStr error;
			megaTextureProject_t candidate;
			if ( candidate.Load( candidatePath, error ) && !idStr::Icmp( candidate.mapName, mapPath ) ) {
				if ( !candidate.roadFile.IsEmpty() ) fileSystem->RemoveFile( candidate.roadFile );
				fileSystem->RemoveFile( candidatePath );
				++removedProjects;
			}
		}
		fileSystem->FreeFileList( projects );
	}
	cachedLevelProjectMap[0] = cachedLevelProjectPath[0] = '\0';
	if ( state.loaded && ( !idStr::Icmp( state.project.mapName, mapPath ) || !idStr::Icmp( state.projectPath, canonicalProjectPath ) ) ) {
		ClearLevelProjectState();
	}
	idStr::Copynz( state.projectPath, canonicalProjectPath, sizeof( state.projectPath ) );
	SetStatus( removedProjects > 0 ?
		"Blank map replaced an existing level. Its terrain project was removed; create new terrain for this level." :
		"Blank map replaced an existing level. No terrain project remained; create new terrain for this level." );
	common->Printf( "MegaTexture: blank map overwrite removed %d project file(s) for %s\n", removedProjects, mapPath.c_str() );
}

bool MegaTextureEditorImGuiHandleCameraInput( CCamWnd *view, int x, int y,
	bool hovered, bool leftClicked, bool leftDown, bool leftReleased, bool invert ) {
	if ( !state.open || !state.loaded || view == NULL ) { state.cameraBrushValid = false; return false; }
	const bool consumesLeft = hovered || state.cameraStroke;
	if ( leftReleased && state.cameraStroke ) {
		if ( state.editMode == MEGA_EDIT_SCULPT ) SaveTerrain( true );
		else if ( state.editMode == MEGA_EDIT_PAINT ) SaveWeights();
		else SaveRoads();
		state.cameraStroke = false;
		state.roadDragging = false;
		return true;
	}
	if ( !hovered ) { state.cameraBrushValid = false; return consumesLeft; }
	camera_t &camera = view->Camera();
	if ( camera.width <= 0 || camera.height <= 0 ) return consumesLeft;
	const float bottomY = (float)( camera.height - 1 - y );
	const float halfWidth = Max( camera.width * 0.5f, 1.0f );
	idVec3 direction = camera.vpn + camera.vright * ( ( x - camera.width * 0.5f ) / halfWidth ) +
		camera.vup * ( ( bottomY - camera.height * 0.5f ) / halfWidth );
	direction.Normalize();
	idVec3 hit;
	float sampleX = 0.0f, sampleY = 0.0f;
	if ( !IntersectTerrain( camera.origin, direction, hit, sampleX, sampleY ) ) { state.cameraBrushValid = false; return consumesLeft; }
	state.cameraBrushValid = true;
	state.cameraBrushSampleX = sampleX;
	state.cameraBrushSampleY = sampleY;
	if ( !leftClicked && !leftDown ) return consumesLeft;
	if ( state.editMode == MEGA_EDIT_ROADS ) {
		const int samples = state.project.terrainSamples;
		const float spacing = state.project.terrainSize / Max( 1, samples - 1 );
		const float halfSize = state.project.terrainSize * 0.5f;
		const idVec2 roadPoint( sampleX * spacing - halfSize, halfSize - sampleY * spacing );
		if ( leftClicked ) {
			if ( invert ) {
				int pointIndex = -1;
				const int roadIndex = state.roads.FindClosestPoint( roadPoint, Max( 24.0f, spacing * 1.5f ), pointIndex );
				if ( roadIndex >= 0 ) {
					PushRoadUndo(); state.roads.DeletePoint( roadIndex, pointIndex );
					state.selectedRoad = roadIndex; state.selectedRoadPoint = -1;
					state.roadsDirty = true; state.cameraStroke = true; UpdateEditorPreview();
				}
			} else if ( state.roadDrawing && state.selectedRoad >= 0 && state.selectedRoad < state.roads.NumRoads() ) {
				PushRoadUndo(); state.roads.AddPoint( state.selectedRoad, roadPoint );
				state.selectedRoadPoint = (int)state.roads.GetRoad( state.selectedRoad ).points.size() - 1;
				state.roadsDirty = true; state.cameraStroke = true; UpdateEditorPreview();
			} else {
				int pointIndex = -1;
				const int roadIndex = state.roads.FindClosestPoint( roadPoint, Max( 24.0f, spacing * 1.5f ), pointIndex );
				if ( roadIndex >= 0 ) {
					PushRoadUndo(); state.selectedRoad = roadIndex; state.selectedRoadPoint = pointIndex;
					state.roadDragging = state.cameraStroke = true;
				} else {
					state.selectedRoad = state.roads.FindClosestRoad( roadPoint, Max( 24.0f, spacing ) );
					state.selectedRoadPoint = -1;
				}
			}
		}
		if ( state.roadDragging && state.cameraStroke && leftDown && state.selectedRoad >= 0 && state.selectedRoadPoint >= 0 ) {
			state.roads.SetPoint( state.selectedRoad, state.selectedRoadPoint, roadPoint );
			state.roadsDirty = true; UpdateEditorPreview();
		}
	} else if ( state.editMode == MEGA_EDIT_PAINT ) {
		if ( leftClicked ) {
			PushUndo(); state.cameraStroke = true;
			state.paintStrokePivotX = sampleX; state.paintStrokePivotY = sampleY;
			const int samples = state.project.terrainSamples;
			const int pivotX = idMath::ClampInt( 0, samples - 1, (int)( sampleX + 0.5f ) );
			const int pivotY = idMath::ClampInt( 0, samples - 1, (int)( sampleY + 0.5f ) );
			const int transformIndex = ( pivotY * samples + pivotX ) * megaTextureProject_t::MAX_LAYERS + state.selectedLayer;
			const megaTextureVertexTransform_t transform = transformIndex < (int)state.transforms.size()
				? state.transforms[transformIndex]
				: MegaTextureEncodeVertexTransform( state.project.layerScale[state.selectedLayer], state.project.layerScaleY[state.selectedLayer], state.project.layerRotation[state.selectedLayer] );
			MegaTextureTransformTexCoord( transform, sampleX / Max( 1, samples - 1 ), sampleY / Max( 1, samples - 1 ),
				state.paintStrokePhaseU, state.paintStrokePhaseV );
			state.paintStrokePhaseU -= floorf( state.paintStrokePhaseU );
			state.paintStrokePhaseV -= floorf( state.paintStrokePhaseV );
		}
		if ( state.cameraStroke && leftDown ) Paint( (int)( sampleX + 0.5f ), (int)( sampleY + 0.5f ), invert );
	} else {
		if ( leftClicked ) { PushTerrainUndo(); state.cameraStroke = true; }
		if ( state.cameraStroke && leftDown ) SculptTerrain( (int)( sampleX + 0.5f ), (int)( sampleY + 0.5f ), invert );
	}
	return consumesLeft;
}

bool MegaTextureEditorImGuiHandleKey( int key, bool down, bool control, bool repeat ) {
	if ( !state.open || !state.loaded ) return false;
	// Windows virtual-key values for the physical [ and ] keys.  Consume both
	// transitions while painting so Radiant cannot interpret them as global
	// editor shortcuts.
	const int KEY_LEFT_BRACKET = 0xDB;
	const int KEY_RIGHT_BRACKET = 0xDD;
	const int KEY_DELETE = 0x2E;
	if ( state.editMode == MEGA_EDIT_ROADS && key == KEY_DELETE ) {
		if ( down && !repeat && state.selectedRoad >= 0 ) {
			PushRoadUndo();
			if ( state.selectedRoadPoint >= 0 ) {
				state.roads.DeletePoint( state.selectedRoad, state.selectedRoadPoint );
				state.selectedRoadPoint = -1;
			} else {
				state.roads.DeleteRoad( state.selectedRoad );
				state.selectedRoad = Min( state.selectedRoad, state.roads.NumRoads() - 1 );
			}
			state.roadsDirty = true; UpdateEditorPreview();
		}
		return true;
	}
	if ( !control && state.editMode == MEGA_EDIT_PAINT && ( key == KEY_LEFT_BRACKET || key == KEY_RIGHT_BRACKET ) ) {
		if ( down ) {
			const int maximumRadius = Max( 1, state.project.terrainSamples / 3 );
			state.brushRadius = idMath::ClampInt( 1, maximumRadius, state.brushRadius + ( key == KEY_LEFT_BRACKET ? -1 : 1 ) );
			const float worldRadius = state.brushRadius * state.project.terrainSize / Max( 1, state.project.terrainSamples - 1 );
			SetStatus( va( "Paint radius: %d vertices (about %.1f world units).", state.brushRadius, worldRadius ) );
			UpdateEditorPreview();
		}
		return true;
	}
	if ( !control || ( key != 'Z' && key != 'Y' && key != 'S' ) ) return false;
	if ( down && !repeat ) {
		if ( key == 'Z' ) {
			state.cameraStroke = false;
			if ( state.editMode == MEGA_EDIT_PAINT ) Undo();
			else if ( state.editMode == MEGA_EDIT_SCULPT ) TerrainUndo();
			else RoadUndo();
		} else if ( key == 'Y' ) {
			state.cameraStroke = false;
			if ( state.editMode == MEGA_EDIT_PAINT ) Redo();
			else if ( state.editMode == MEGA_EDIT_SCULPT ) TerrainRedo();
			else RoadRedo();
		} else {
			SaveProject();
		}
	}
	// Consume both key-down and key-up so Radiant's brush/entity undo stack is
	// never touched while the terrain authoring tab owns these shortcuts.
	return true;
}

static megaTextureVertexTransform_t InterpolatePaintTransform( const int vertexIndices[3], const float barycentric[3], int layer ) {
	float scaleX = 0.0f, scaleY = 0.0f, pivotU = 0.0f, pivotV = 0.0f;
	float rotationCosine = 0.0f, rotationSine = 0.0f;
	float phaseUCosine = 0.0f, phaseUSine = 0.0f, phaseVCosine = 0.0f, phaseVSine = 0.0f;
	for ( int corner = 0; corner < 3; ++corner ) {
		const int transformIndex = vertexIndices[corner] * megaTextureProject_t::MAX_LAYERS + layer;
		const megaTextureVertexTransform_t transform = transformIndex < (int)state.transforms.size()
			? state.transforms[transformIndex]
			: MegaTextureEncodeVertexTransform( state.project.layerScale[layer], state.project.layerScaleY[layer], state.project.layerRotation[layer] );
		float cornerScaleX, cornerScaleY, rotation, cornerPivotU, cornerPivotV, cornerPhaseU, cornerPhaseV;
		MegaTextureDecodeVertexTransform( transform, cornerScaleX, cornerScaleY, rotation,
			cornerPivotU, cornerPivotV, cornerPhaseU, cornerPhaseV );
		scaleX += cornerScaleX * barycentric[corner];
		scaleY += cornerScaleY * barycentric[corner];
		pivotU += cornerPivotU * barycentric[corner];
		pivotV += cornerPivotV * barycentric[corner];
		const float radians = rotation * idMath::M_DEG2RAD;
		rotationCosine += idMath::Cos( radians ) * barycentric[corner];
		rotationSine += idMath::Sin( radians ) * barycentric[corner];
		phaseUCosine += idMath::Cos( cornerPhaseU * idMath::TWO_PI ) * barycentric[corner];
		phaseUSine += idMath::Sin( cornerPhaseU * idMath::TWO_PI ) * barycentric[corner];
		phaseVCosine += idMath::Cos( cornerPhaseV * idMath::TWO_PI ) * barycentric[corner];
		phaseVSine += idMath::Sin( cornerPhaseV * idMath::TWO_PI ) * barycentric[corner];
	}
	const float rotation = idMath::ATan( rotationSine, rotationCosine ) * idMath::M_RAD2DEG;
	float phaseU = idMath::ATan( phaseUSine, phaseUCosine ) / idMath::TWO_PI;
	float phaseV = idMath::ATan( phaseVSine, phaseVCosine ) / idMath::TWO_PI;
	if ( phaseU < 0.0f ) phaseU += 1.0f;
	if ( phaseV < 0.0f ) phaseV += 1.0f;
	return MegaTextureEncodeVertexTransform( scaleX, scaleY, rotation, pivotU, pivotV, phaseU, phaseV );
}

static void EmitPaintPreviewVertex( const srfTriangles_t *tri, int vertexIndex, int layer,
	const megaTextureVertexTransform_t &transform, const idVec3 &origin, const idMat3 &axis ) {
	const idDrawVert &vertex = tri->verts[vertexIndex];
	const int weightIndex = vertexIndex * megaTextureProject_t::MAX_LAYERS + layer;
	const byte weight = weightIndex < (int)state.weights.size() ? state.weights[weightIndex] : vertex.color[layer];
	qglColor4ub( weight, weight, weight, 255 );
	float layerU, layerV;
	// Terrain model T is the inverse of the heightfield/paint row coordinate.
	// Convert back before applying the authored transform so 90-degree mappings
	// match the compiler instead of appearing mirrored or rotated in DoomEdit.
	const idVec2 vertexST = vertex.GetST();
	MegaTextureTransformTexCoord( transform, vertexST.x, 1.0f - vertexST.y,
		layerU, layerV );
	qglTexCoord2f( layerU, layerV );
	const idVec3 position = vertex.xyz * axis + origin;
	qglVertex3fv( position.ToFloatPtr() );
}

static void DrawPaintPreviewTriangle( const srfTriangles_t *tri, const int vertexIndices[3], int layer,
	const idVec3 &origin, const idMat3 &axis ) {
	megaTextureVertexTransform_t cornerTransforms[3];
	for ( int corner = 0; corner < 3; ++corner ) {
		const int transformIndex = vertexIndices[corner] * megaTextureProject_t::MAX_LAYERS + layer;
		cornerTransforms[corner] = transformIndex < (int)state.transforms.size()
			? state.transforms[transformIndex]
			: MegaTextureEncodeVertexTransform( state.project.layerScale[layer], state.project.layerScaleY[layer], state.project.layerRotation[layer] );
	}
	megaTextureVertexTransform_t triangleTransform = cornerTransforms[0];
	if ( memcmp( &cornerTransforms[0], &cornerTransforms[1], sizeof( megaTextureVertexTransform_t ) ) ||
		 memcmp( &cornerTransforms[0], &cornerTransforms[2], sizeof( megaTextureVertexTransform_t ) ) ) {
		const float center[3] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
		triangleTransform = InterpolatePaintTransform( vertexIndices, center, layer );
	}
	for ( int corner = 0; corner < 3; ++corner ) {
		EmitPaintPreviewVertex( tri, vertexIndices[corner], layer, triangleTransform, origin, axis );
	}
}

static idVec3 RoadPreviewPosition( const idVec2 &localXY, float heightOffset, const idVec3 &origin, const idMat3 &axis ) {
	const int samples = state.project.terrainSamples;
	const float spacing = state.project.terrainSize / Max( 1, samples - 1 );
	const float halfSize = state.project.terrainSize * 0.5f;
	const float sampleX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), ( localXY.x + halfSize ) / spacing );
	const float sampleY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), ( halfSize - localXY.y ) / spacing );
	const idVec3 local( localXY.x, localXY.y, SampleTerrainHeight( sampleX, sampleY ) + heightOffset );
	return local * axis + origin;
}

static void DrawRoadPreviews( const idVec3 &origin, const idMat3 &axis ) {
	if ( state.roads.NumRoads() <= 0 ) return;
	qglEnable( GL_BLEND );
	qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	qglDepthMask( GL_FALSE );
	qglEnable( GL_TEXTURE_2D );
	for ( int roadIndex = 0; roadIndex < state.roads.NumRoads(); ++roadIndex ) {
		const megaTextureRoad_t &road = state.roads.GetRoad( roadIndex );
		const std::vector<megaTextureRoadPolylinePoint_t> &line = state.roads.Polyline( roadIndex );
		if ( !road.enabled || road.texture.IsEmpty() || line.size() < 2 ) continue;
		imageParams_t imageParams;
		imageParams.tf = TF_DEFAULT;
		imageParams.trp = TR_REPEAT;
		imageParams.td = TD_DEFAULT;
		idImage *image = globalImages->ImageFromFile( road.texture, imageParams );
		if ( !image ) continue;
		image->Bind();
		const float halfWidth = Max( road.width * 0.5f, 0.5f );
		const float inner = halfWidth - idMath::ClampFloat( 0.0f, halfWidth, road.feather );
		float lateral[4] = { -halfWidth, -inner, inner, halfWidth };
		byte opacity[4] = { (byte)( road.feather > 0.001f ? 0 : 255 ), 255, 255, (byte)( road.feather > 0.001f ? 0 : 255 ) };
		const int bandCount = road.feather > 0.001f ? 3 : 1;
		if ( bandCount == 1 ) { lateral[0] = -halfWidth; lateral[1] = halfWidth; opacity[0] = opacity[1] = 255; }
		for ( int band = 0; band < bandCount; ++band ) {
			const int row0 = bandCount == 1 ? 0 : band;
			const int row1 = bandCount == 1 ? 1 : band + 1;
			qglBegin( GL_TRIANGLE_STRIP );
			for ( int pointIndex = 0; pointIndex < (int)line.size(); ++pointIndex ) {
				const megaTextureRoadPolylinePoint_t &point = line[pointIndex];
				const idVec2 across( -point.tangent.y, point.tangent.x );
				for ( int row = row0; row <= row1; ++row ) {
					const idVec2 position( point.position.x + across.x * lateral[row], point.position.y + across.y * lateral[row] );
					qglColor4ub( 255, 255, 255, opacity[row] );
					qglTexCoord2f( 0.5f + lateral[row] / Max( road.width, 1.0f ), point.distance / Max( road.repeatLength, 1.0f ) );
					const idVec3 world = RoadPreviewPosition( position, 3.0f, origin, axis );
					qglVertex3fv( world.ToFloatPtr() );
				}
			}
			qglEnd();
		}
	}
	if ( state.editMode != MEGA_EDIT_ROADS ) return;

	GLfloat oldLineWidth = 1.0f, oldPointSize = 1.0f;
	qglGetFloatv( GL_LINE_WIDTH, &oldLineWidth );
	qglGetFloatv( GL_POINT_SIZE, &oldPointSize );
	qglDisable( GL_TEXTURE_2D );
	qglLineWidth( 2.0f );
	for ( int roadIndex = 0; roadIndex < state.roads.NumRoads(); ++roadIndex ) {
		const megaTextureRoad_t &road = state.roads.GetRoad( roadIndex );
		const std::vector<megaTextureRoadPolylinePoint_t> &line = state.roads.Polyline( roadIndex );
		qglColor4ub( roadIndex == state.selectedRoad ? 80 : 80, roadIndex == state.selectedRoad ? 255 : 170,
			roadIndex == state.selectedRoad ? 110 : 255, road.enabled ? 245 : 100 );
		if ( line.size() >= 2 ) {
			qglBegin( GL_LINE_STRIP );
			for ( int point = 0; point < (int)line.size(); ++point ) {
				const idVec3 world = RoadPreviewPosition( line[point].position, 5.0f, origin, axis );
				qglVertex3fv( world.ToFloatPtr() );
			}
			qglEnd();
		}
		if ( roadIndex != state.selectedRoad ) continue;
		qglPointSize( 9.0f );
		qglBegin( GL_POINTS );
		for ( int point = 0; point < (int)road.points.size(); ++point ) {
			if ( point == state.selectedRoadPoint ) qglColor4ub( 255, 235, 70, 255 );
			else qglColor4ub( 80, 255, 110, 255 );
			const idVec3 world = RoadPreviewPosition( road.points[point], 7.0f, origin, axis );
			qglVertex3fv( world.ToFloatPtr() );
		}
		qglEnd();
	}

	if ( state.editMode == MEGA_EDIT_ROADS && state.cameraBrushValid &&
		 state.selectedRoad >= 0 && state.selectedRoad < state.roads.NumRoads() ) {
		const megaTextureRoad_t &road = state.roads.GetRoad( state.selectedRoad );
		const int samples = state.project.terrainSamples;
		const float spacing = state.project.terrainSize / Max( 1, samples - 1 );
		const float halfSize = state.project.terrainSize * 0.5f;
		const idVec2 cursor( state.cameraBrushSampleX * spacing - halfSize, halfSize - state.cameraBrushSampleY * spacing );
		idVec2 tangent( 1.0f, 0.0f );
		if ( state.roadDrawing && !road.points.empty() ) tangent = cursor - road.points.back();
		else if ( state.selectedRoadPoint >= 0 && road.points.size() >= 2 ) {
			const int before = Max( 0, state.selectedRoadPoint - 1 );
			const int after = Min( (int)road.points.size() - 1, state.selectedRoadPoint + 1 );
			tangent = road.points[after] - road.points[before];
		}
		if ( tangent.Normalize() <= 0.0001f ) tangent.Set( 1.0f, 0.0f );
		const idVec2 across( -tangent.y, tangent.x );
		qglColor4ub( 255, 235, 70, 255 );
		qglBegin( GL_LINES );
		for ( int side = -1; side <= 1; side += 2 ) {
			const idVec2 edge = cursor + across * ( side * road.width * 0.5f );
			const idVec3 world = RoadPreviewPosition( edge, 8.0f, origin, axis );
			qglVertex3fv( world.ToFloatPtr() );
		}
		if ( state.roadDrawing && !road.points.empty() ) {
			const idVec3 start = RoadPreviewPosition( road.points.back(), 8.0f, origin, axis );
			const idVec3 end = RoadPreviewPosition( cursor, 8.0f, origin, axis );
			qglVertex3fv( start.ToFloatPtr() ); qglVertex3fv( end.ToFloatPtr() );
		}
		qglEnd();
	}
	qglPointSize( oldPointSize );
	qglLineWidth( oldLineWidth );
}

bool MegaTextureEditorImGuiDrawLayeredTerrain( idRenderModel *model, const idVec3 &origin, const idMat3 &axis ) {
	if ( !state.open || !state.loaded || model == NULL || state.weights.empty() ) return false;
	bool matchesTerrain = false;
	for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex ) {
		const modelSurface_t *surface = model->Surface( surfaceIndex );
		if ( surface && surface->material && !idStr::Icmp( surface->material->GetName(), state.project.material ) ) { matchesTerrain = true; break; }
	}
	if ( !matchesTerrain ) return false;

	idVec4 savedColor;
	qglGetFloatv( GL_CURRENT_COLOR, savedColor.ToFloatPtr() );
	const GLboolean blendWasEnabled = qglIsEnabled( GL_BLEND );
	const GLboolean textureWasEnabled = qglIsEnabled( GL_TEXTURE_2D );
	GLboolean depthWriteWasEnabled = GL_TRUE;
	qglGetBooleanv( GL_DEPTH_WRITEMASK, &depthWriteWasEnabled );
	GLint oldBlendSource = GL_ONE, oldBlendDestination = GL_ZERO;
	qglGetIntegerv( GL_BLEND_SRC, &oldBlendSource );
	qglGetIntegerv( GL_BLEND_DST, &oldBlendDestination );

	int pass = 0;
	for ( int layer = 0; layer < megaTextureProject_t::MAX_LAYERS; ++layer ) {
		if ( state.project.layers[layer].IsEmpty() ) continue;
		imageParams_t imageParams;
		imageParams.tf = TF_DEFAULT;
		imageParams.trp = TR_REPEAT;
		imageParams.td = TD_DEFAULT;
		idImage *image = globalImages->ImageFromFile( state.project.layers[layer], imageParams );
		if ( !image ) continue;
		image->Bind();
		if ( pass == 0 ) {
			qglDisable( GL_BLEND );
			qglDepthMask( GL_TRUE );
		} else {
			qglEnable( GL_BLEND );
			qglBlendFunc( GL_ONE, GL_ONE );
			qglDepthMask( GL_FALSE );
		}
		for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex ) {
			const modelSurface_t *surface = model->Surface( surfaceIndex );
			if ( !surface || !surface->material || idStr::Icmp( surface->material->GetName(), state.project.material ) ) continue;
			const srfTriangles_t *tri = surface->geometry;
			qglBegin( GL_TRIANGLES );
			for ( int indexNumber = 0; indexNumber + 2 < tri->numIndexes; indexNumber += 3 ) {
				const int vertexIndices[3] = { tri->indexes[indexNumber], tri->indexes[indexNumber + 1], tri->indexes[indexNumber + 2] };
				DrawPaintPreviewTriangle( tri, vertexIndices, layer, origin, axis );
			}
			qglEnd();
		}
		++pass;
	}
	DrawRoadPreviews( origin, axis );
	if ( pass > 0 && state.cameraBrushValid && state.editMode != MEGA_EDIT_ROADS ) {
		GLfloat oldLineWidth = 1.0f;
		qglGetFloatv( GL_LINE_WIDTH, &oldLineWidth );
		qglDisable( GL_TEXTURE_2D );
		qglEnable( GL_BLEND );
		qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		qglDepthMask( GL_FALSE );
		qglLineWidth( 2.0f );
		const bool sculptGizmo = state.editMode == MEGA_EDIT_SCULPT;
		float radiusX, radiusY;
		if ( sculptGizmo ) SculptBrushExtents( radiusX, radiusY );
		else radiusX = radiusY = (float)Max( state.brushRadius, 1 );
		const int samples = state.project.terrainSamples;
		const float spacing = state.project.terrainSize / ( samples - 1 );
		const float halfSize = state.project.terrainSize * 0.5f;
		const int pointsPerEdge = 16;
		const byte paintColors[megaTextureProject_t::MAX_LAYERS][3] = {
			{ 255, 92, 92 }, { 96, 255, 128 }, { 96, 168, 255 }, { 238, 112, 255 }
		};
		for ( int outline = 0; outline < 2; ++outline ) {
			const float scale = outline == 0 ? 1.0f : 1.0f - state.terrainBrushFeather;
			if ( !sculptGizmo && outline > 0 ) continue;
			if ( outline == 1 && ( scale <= 0.01f || scale >= 0.99f ) ) continue;
			if ( sculptGizmo ) qglColor4ub( 255, 235, 96, outline == 0 ? 235 : 105 );
			else qglColor4ub( paintColors[state.selectedLayer][0], paintColors[state.selectedLayer][1], paintColors[state.selectedLayer][2], 235 );
			qglBegin( GL_LINE_LOOP );
			const int pointCount = !sculptGizmo || state.terrainBrushShape == TERRAIN_SHAPE_CIRCLE ? 64 : pointsPerEdge * 4;
			for ( int point = 0; point < pointCount; ++point ) {
				float nx, ny;
				if ( !sculptGizmo || state.terrainBrushShape == TERRAIN_SHAPE_CIRCLE ) {
					const float angle = idMath::TWO_PI * point / pointCount;
					nx = idMath::Cos( angle ); ny = idMath::Sin( angle );
				} else {
					const int edge = point / pointsPerEdge;
					const float along = ( point % pointsPerEdge ) / (float)pointsPerEdge;
					if ( edge == 0 ) { nx = -1.0f + along * 2.0f; ny = -1.0f; }
					else if ( edge == 1 ) { nx = 1.0f; ny = -1.0f + along * 2.0f; }
					else if ( edge == 2 ) { nx = 1.0f - along * 2.0f; ny = 1.0f; }
					else { nx = -1.0f; ny = 1.0f - along * 2.0f; }
				}
				const float sampleX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), state.cameraBrushSampleX + nx * radiusX * scale );
				const float sampleY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), state.cameraBrushSampleY + ny * radiusY * scale );
				idVec3 local( sampleX * spacing - halfSize, halfSize - sampleY * spacing, SampleTerrainHeight( sampleX, sampleY ) + 2.0f );
				const idVec3 position = local * axis + origin;
				qglVertex3fv( position.ToFloatPtr() );
			}
			qglEnd();
		}
		// Sculpt uses a compact center cross. Paint uses a larger, rotated pair of
		// axes so the mapping orientation is visible before the stroke is applied.
		const float markerRadius = Max( 0.75f, Min( radiusX, radiusY ) * ( sculptGizmo ? 0.12f : 0.55f ) );
		const float paintAngle = -state.project.layerRotation[state.selectedLayer] * idMath::M_DEG2RAD;
		const float paintMinimumScale = Min( state.project.layerScale[state.selectedLayer], state.project.layerScaleY[state.selectedLayer] );
		if ( sculptGizmo ) qglColor4ub( 255, 245, 180, 245 );
		else qglColor4ub( paintColors[state.selectedLayer][0], paintColors[state.selectedLayer][1], paintColors[state.selectedLayer][2], 245 );
		qglBegin( GL_LINES );
		for ( int axisIndex = 0; axisIndex < 2; ++axisIndex ) for ( int side = -1; side <= 1; side += 2 ) {
			const float directionX = sculptGizmo ? ( axisIndex == 0 ? 1.0f : 0.0f )
				: ( axisIndex == 0 ? idMath::Cos( paintAngle ) : -idMath::Sin( paintAngle ) );
			const float directionY = sculptGizmo ? ( axisIndex == 1 ? 1.0f : 0.0f )
				: ( axisIndex == 0 ? idMath::Sin( paintAngle ) : idMath::Cos( paintAngle ) );
			const float mappingScale = sculptGizmo ? 1.0f : idMath::ClampFloat( 0.2f, 1.0f, paintMinimumScale /
				( axisIndex == 0 ? state.project.layerScale[state.selectedLayer] : state.project.layerScaleY[state.selectedLayer] ) );
			const float axisLength = markerRadius * mappingScale * ( !sculptGizmo && axisIndex == 1 ? 0.75f : 1.0f );
			const float sampleX = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), state.cameraBrushSampleX + directionX * side * axisLength );
			const float sampleY = idMath::ClampFloat( 0.0f, (float)( samples - 1 ), state.cameraBrushSampleY + directionY * side * axisLength );
			idVec3 local( sampleX * spacing - halfSize, halfSize - sampleY * spacing, SampleTerrainHeight( sampleX, sampleY ) + 2.5f );
			const idVec3 position = local * axis + origin;
			qglVertex3fv( position.ToFloatPtr() );
		}
		qglEnd();
		qglLineWidth( oldLineWidth );
	}

	qglDepthMask( depthWriteWasEnabled );
	qglBlendFunc( oldBlendSource, oldBlendDestination );
	if ( blendWasEnabled ) qglEnable( GL_BLEND ); else qglDisable( GL_BLEND );
	if ( textureWasEnabled ) qglEnable( GL_TEXTURE_2D ); else qglDisable( GL_TEXTURE_2D );
	qglColor4fv( savedColor.ToFloatPtr() );
	return pass > 0;
}

void MegaTextureEditorImGuiRenderInspector() {
	EnsureLevelProject();
	RenderNewProjectPopup();
	ImGui::BeginChild( "MegaTextureInspectorScroll", ImVec2( 0, 0 ), false );
	ProcessPendingCompile();
	idStr levelMapPath, levelProjectPath, levelAssetName;
	const bool savedLevel = GetLevelProjectInfo( levelMapPath, levelProjectPath, levelAssetName );
	ImGui::TextWrapped( savedLevel ? "Level: %s\nTerrain project: %s" : "Level: <not saved>",
		savedLevel ? levelMapPath.c_str() : "", savedLevel ? levelProjectPath.c_str() : "" );
	if ( !state.loaded ) {
		ImGui::TextWrapped( "%s", state.status );
		if ( savedLevel && ImGui::Button( "Create terrain for this level" ) ) state.requestNewProjectPopup = true;
		ImGui::EndChild();
		return;
	}
	const size_t authoringBytes = state.weights.size() + state.transforms.size() * sizeof( megaTextureVertexTransform_t );
	ImGui::TextWrapped( "%s\n%d x %d heightfield, %.0f world units\n%.2f MB vertex-layer authoring data",
		state.project.material.c_str(), state.project.terrainSamples, state.project.terrainSamples,
		state.project.terrainSize, authoringBytes / ( 1024.0f * 1024.0f ) );
	if ( ImGui::RadioButton( "Sculpt", state.editMode == MEGA_EDIT_SCULPT ) ) state.editMode = MEGA_EDIT_SCULPT;
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Texture Paint", state.editMode == MEGA_EDIT_PAINT ) ) {
		state.editMode = MEGA_EDIT_PAINT;
		EnterTexturePaintCameraPreview();
	}
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Roads", state.editMode == MEGA_EDIT_ROADS ) ) {
		state.editMode = MEGA_EDIT_ROADS; state.cameraStroke = state.roadDragging = false;
		EnterTexturePaintCameraPreview();
	}
	if ( ImGui::Button( "Save" ) ) SaveProject();
	ImGui::SameLine(); if ( ImGui::Button( "Add / Update Map" ) && SaveTerrain( false ) ) AddOrUpdateTerrainEntity();
	ImGui::SameLine(); if ( ImGui::Button( "Focus" ) ) FocusTerrainCamera();
	if ( state.editMode == MEGA_EDIT_SCULPT ) {
		ImGui::SameLine(); if ( ImGui::Button( "Undo" ) ) TerrainUndo();
		ImGui::SameLine(); if ( ImGui::Button( "Redo" ) ) TerrainRedo();
	} else if ( state.editMode == MEGA_EDIT_PAINT ) {
		ImGui::SameLine(); if ( ImGui::Button( "Undo" ) ) Undo();
		ImGui::SameLine(); if ( ImGui::Button( "Redo" ) ) Redo();
	} else {
		ImGui::SameLine(); if ( ImGui::Button( "Undo" ) ) RoadUndo();
		ImGui::SameLine(); if ( ImGui::Button( "Redo" ) ) RoadRedo();
	}
	ImGui::TextDisabled( "Atmosphere bake level: %s", state.project.mapName.c_str() );
	RenderAtmosphereBakeSelector();
	const char *buildSizes[] = { "2048 x 2048", "4096 x 4096", "8192 x 8192", "16384 x 16384", "32768 x 32768" };
	ImGui::SetNextItemWidth( -1.0f );
	if ( ImGui::Combo( "Compiled MegaTexture size", &state.buildResolutionIndex, buildSizes, IM_ARRAYSIZE( buildSizes ) ) ) {
		static const int buildResolutions[] = { 2048, 4096, 8192, 16384, 32768 };
		state.project.resolution = buildResolutions[state.buildResolutionIndex]; state.projectDirty = true;
	}
	if ( ImGui::Button( "Compile MegaTexture (Unlit)" ) ) QueueCompileProject( false );
	ImGui::SameLine(); if ( ImGui::Button( "Compile + Bake Atmosphere" ) ) QueueCompileProject( true );
	RenderBuildStatus();
	ImGui::Separator();
	if ( state.editMode == MEGA_EDIT_SCULPT ) RenderSculptInspector();
	else if ( state.editMode == MEGA_EDIT_PAINT ) RenderPaintInspector();
	else RenderRoadInspector();
	if ( ImGui::CollapsingHeader( "Terrain and heightmap settings" ) ) {
		if ( ImGui::InputFloat( "World size", &state.project.terrainSize, 64.0f, 512.0f, "%.0f" ) ) state.projectDirty = state.terrainDirty = true;
		if ( ImGui::InputFloat3( "World origin", state.project.terrainOrigin, "%.0f" ) ) state.projectDirty = true;
		ImGui::InputText( "Heightmap image", state.heightImagePath, sizeof( state.heightImagePath ) );
		ImGui::InputFloat( "Black height", &state.heightImportMinimum, 1.0f, 64.0f, "%.2f" );
		ImGui::InputFloat( "White height", &state.heightImportMaximum, 1.0f, 64.0f, "%.2f" );
		if ( ImGui::Button( "Import Heightmap" ) ) ImportTerrainHeightImage();
		ImGui::SameLine(); if ( ImGui::Button( "Export Heightmap" ) ) ExportTerrainHeightImage();
	}
	ImGui::Separator();
	ImGui::TextWrapped( "%s", state.status );
	ImGui::EndChild();
}

void MegaTextureEditorImGuiRender() {
	if ( !state.open ) return;
	EnsureLevelProject();
	const char *title = state.dirty || state.projectDirty || state.terrainDirty || state.roadsDirty ? "MegaTexture Terrain Editor*" : "MegaTexture Terrain Editor";
	if ( !ImGui::Begin( title, &state.open, ImGuiWindowFlags_MenuBar ) ) { ImGui::End(); return; }
	ProcessPendingCompile();
	if ( ImGui::BeginMenuBar() ) {
		if ( ImGui::BeginMenu( "File" ) ) {
			if ( ImGui::MenuItem( "Create terrain for this level", NULL, false, !state.loaded && state.projectPath[0] ) ) state.requestNewProjectPopup = true;
			if ( ImGui::MenuItem( "Save", "Ctrl+S", false, state.loaded ) ) SaveProject();
			ImGui::Separator();
			if ( ImGui::MenuItem( "Compile", NULL, false, state.loaded ) ) QueueCompileProject( false );
			if ( ImGui::MenuItem( "Compile + Bake Atmosphere", NULL, false, state.loaded ) ) QueueCompileProject( true );
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Edit" ) ) {
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, !state.undo.empty() ) ) Undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, !state.redo.empty() ) ) Redo();
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	RenderNewProjectPopup();

	idStr levelMapPath, levelProjectPath, levelAssetName;
	const bool savedLevel = GetLevelProjectInfo( levelMapPath, levelProjectPath, levelAssetName );
	ImGui::TextWrapped( savedLevel ? "Level: %s\nTerrain project: %s" : "Save the level before creating terrain.",
		savedLevel ? levelMapPath.c_str() : "", savedLevel ? levelProjectPath.c_str() : "" );
	if ( !state.loaded ) {
		ImGui::TextWrapped( "%s", state.status );
		if ( savedLevel && ImGui::Button( "Create terrain for this level" ) ) state.requestNewProjectPopup = true;
		ImGui::End(); return;
	}

	const size_t authoringBytes = state.weights.size() + state.transforms.size() * sizeof( megaTextureVertexTransform_t );
	ImGui::Text( "%s  |  terrain %d x %d / %.0f units  |  vertex layers %.2f MB",
		state.project.material.c_str(), state.project.terrainSamples, state.project.terrainSamples, state.project.terrainSize,
		authoringBytes / ( 1024.0f * 1024.0f ) );
	ImGui::TextDisabled( "The atmosphere bake is merged into MegaTexture RGB; alpha keeps compact normal XY." );

	ImGui::TextDisabled( "Bake level: %s", state.project.mapName.c_str() );
	RenderAtmosphereBakeSelector();
	const char *buildSizes[] = { "2048", "4096", "8192", "16384", "32768" };
	ImGui::SetNextItemWidth( 110.0f );
	if ( ImGui::Combo( "Build size", &state.buildResolutionIndex, buildSizes, IM_ARRAYSIZE( buildSizes ) ) ) {
		static const int buildResolutions[] = { 2048, 4096, 8192, 16384, 32768 };
		state.project.resolution = buildResolutions[state.buildResolutionIndex]; state.projectDirty = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Compile" ) ) QueueCompileProject( false );
	ImGui::SameLine();
	if ( ImGui::Button( "Compile + Bake" ) ) QueueCompileProject( true );
	RenderBuildStatus();

	ImGui::Separator();
	if ( ImGui::BeginTabBar( "TerrainAuthoringTabs" ) ) {
		if ( ImGui::BeginTabItem( "Sculpt Terrain" ) ) { RenderTerrainEditor(); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Paint MegaTexture" ) ) { RenderPaintEditor(); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
	}
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && ImGui::GetIO().KeyCtrl ) {
		if ( ImGui::IsKeyPressed( ImGuiKey_S ) ) SaveProject();
		if ( ImGui::IsKeyPressed( ImGuiKey_Z ) ) Undo();
		if ( ImGui::IsKeyPressed( ImGuiKey_Y ) ) Redo();
	}
	ImGui::TextWrapped( "%s", state.status );
	ImGui::End();
	if ( !state.open ) SaveProject();
}

void MegaTextureEditorImGuiShutdown() {
	SaveProject();
	if ( state.texture ) RadiantImGuiVulkanDestroyTexture( &state.texture );
	if ( state.heightTexture ) RadiantImGuiVulkanDestroyTexture( &state.heightTexture );
	if ( state.megatileTexture ) RadiantImGuiVulkanDestroyTexture( &state.megatileTexture );
	if ( state.stencilTexture ) RadiantImGuiVulkanDestroyTexture( &state.stencilTexture );
	state.texture = state.heightTexture = state.megatileTexture = state.stencilTexture = 0;
	state.open = state.loaded = false;
}
