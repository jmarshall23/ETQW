// Copyright (C) 2007 Id Software, Inc.
//
// Compile-first ETQW renderer implementation.  It supplies the exact public
// SDK method boundary and safe world bookkeeping without depending on the
// incompatible Doom 3 private renderer.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RendererBootstrap.h"
#include "Image.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "RenderSystemBackend.h"
#include "GuiModel.h"
#include "DeviceContext.h"
#include "../sys/sys_render.h"

#include <GL/gl.h>

idCVar r_mode( "r_mode", "12", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "video mode number" );
idCVar r_customWidth( "r_customWidth", "1280", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "custom video mode width" );
idCVar r_customHeight( "r_customHeight", "720", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "custom video mode height" );
idCVar r_fullscreen( "r_fullscreen", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use a fullscreen window" );
idCVar r_displayRefresh( "r_displayRefresh", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "optional display refresh rate" );
idCVar r_multiSamples( "r_multiSamples", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "multisample anti-aliasing sample count" );

namespace {

template< class T >
qhandle_t AddDefinition( idList< T* >& definitions, const T* value ) {
	if ( value == NULL ) {
		return -1;
	}
	for ( int i = 0; i < definitions.Num(); i++ ) {
		if ( definitions[ i ] == NULL ) {
			definitions[ i ] = new T( *value );
			return i;
		}
	}
	definitions.Append( new T( *value ) );
	return definitions.Num() - 1;
}

template< class T >
void UpdateDefinition( idList< T* >& definitions, qhandle_t handle, const T* value ) {
	if ( value == NULL || handle < 0 || handle >= definitions.Num() || definitions[ handle ] == NULL ) {
		return;
	}
	*definitions[ handle ] = *value;
}

template< class T >
void FreeDefinition( idList< T* >& definitions, qhandle_t handle ) {
	if ( handle < 0 || handle >= definitions.Num() ) {
		return;
	}
	delete definitions[ handle ];
	definitions[ handle ] = NULL;
}

template< class T >
void ClearDefinitions( idList< T* >& definitions ) {
	for ( int i = 0; i < definitions.Num(); i++ ) {
		delete definitions[ i ];
	}
	definitions.Clear();
}

template< class T >
T* DefinitionForHandle( idList< T* >& definitions, qhandle_t handle ) {
	if ( handle < 0 || handle >= definitions.Num() ) {
		return NULL;
	}
	return definitions[ handle ];
}

template< class T >
const T* DefinitionForHandle( const idList< T* >& definitions, qhandle_t handle ) {
	if ( handle < 0 || handle >= definitions.Num() ) {
		return NULL;
	}
	return definitions[ handle ];
}

}

idRenderWorldLocal::idRenderWorldLocal() :
	hasRenderView( false ),
	areaPortalFlags( 0 ),
	ambientCubeMap( NULL ),
	atmosphere( NULL ),
	writeDemo( NULL ) {
	memset( &currentRenderView, 0, sizeof( currentRenderView ) );
}

idRenderWorldLocal::~idRenderWorldLocal() {
	Clear();
}

void idRenderWorldLocal::Clear() {
	ClearDefinitions( entityDefs );
	ClearDefinitions( lightDefs );
	ClearDefinitions( effectDefs );
	ClearDefinitions( occlusionTests );
	stoppedEffects.Clear();
	mapName.Clear();
	hasRenderView = false;
	areaPortalFlags = 0;
	ambientCubeMap = NULL;
	atmosphere = NULL;
	writeDemo = NULL;
}

bool idRenderWorldLocal::InitFromMap( const char *name ) {
	Clear();
	mapName = name != NULL ? name : "";
	return true;
}

void idRenderWorldLocal::LinkCullSectorsToArea( int area ) {
}

qhandle_t idRenderWorldLocal::AddEntityDef( const renderEntity_t *re ) {
	return AddDefinition( entityDefs, re );
}

void idRenderWorldLocal::UpdateEntityDef( qhandle_t handle, const renderEntity_t *re ) {
	UpdateDefinition( entityDefs, handle, re );
}

void idRenderWorldLocal::FreeEntityDef( qhandle_t handle ) {
	FreeDefinition( entityDefs, handle );
}

const renderEntity_t *idRenderWorldLocal::GetRenderEntity( qhandle_t handle ) const {
	return DefinitionForHandle( entityDefs, handle );
}

renderEntity_t *idRenderWorldLocal::GetRenderEntity( qhandle_t handle ) {
	return DefinitionForHandle( entityDefs, handle );
}

qhandle_t idRenderWorldLocal::AddLightDef( const renderLight_t *light ) {
	return AddDefinition( lightDefs, light );
}

void idRenderWorldLocal::UpdateLightDef( qhandle_t handle, const renderLight_t *light ) {
	UpdateDefinition( lightDefs, handle, light );
}

void idRenderWorldLocal::FreeLightDef( qhandle_t handle ) {
	FreeDefinition( lightDefs, handle );
}

const renderLight_t *idRenderWorldLocal::GetRenderLight( qhandle_t handle ) const {
	return DefinitionForHandle( lightDefs, handle );
}

qhandle_t idRenderWorldLocal::AddEffectDef( const renderEffect_t *effect, int time ) {
	const qhandle_t handle = AddDefinition( effectDefs, effect );
	if ( handle >= 0 ) {
		while ( stoppedEffects.Num() <= handle ) {
			stoppedEffects.Append( false );
		}
		stoppedEffects[ handle ] = false;
	}
	return handle;
}

bool idRenderWorldLocal::UpdateEffectDef( qhandle_t handle, const renderEffect_t *effect, int time ) {
	if ( DefinitionForHandle( effectDefs, handle ) == NULL || effect == NULL ) {
		return false;
	}
	UpdateDefinition( effectDefs, handle, effect );
	return true;
}

void idRenderWorldLocal::StopEffectDef( qhandle_t handle ) {
	if ( handle >= 0 && handle < stoppedEffects.Num() ) {
		stoppedEffects[ handle ] = true;
	}
}

void idRenderWorldLocal::RestartEffectDef( qhandle_t handle ) {
	if ( handle >= 0 && handle < stoppedEffects.Num() ) {
		stoppedEffects[ handle ] = false;
	}
}

void idRenderWorldLocal::FreeEffectDef( qhandle_t handle ) {
	FreeDefinition( effectDefs, handle );
	if ( handle >= 0 && handle < stoppedEffects.Num() ) {
		stoppedEffects[ handle ] = false;
	}
}

void idRenderWorldLocal::FreeStoppedEffectDefs() {
	for ( int i = 0; i < stoppedEffects.Num(); i++ ) {
		if ( stoppedEffects[ i ] ) {
			FreeEffectDef( i );
		}
	}
}

qhandle_t idRenderWorldLocal::AddOcclusionTestDef( const occlusionTest_t *test ) {
	return AddDefinition( occlusionTests, test );
}

void idRenderWorldLocal::UpdateOcclusionTestDef( qhandle_t handle, const occlusionTest_t *test ) {
	UpdateDefinition( occlusionTests, handle, test );
}

void idRenderWorldLocal::UpdateOcclusionTestDefViewID( qhandle_t handle, int viewID ) {
	occlusionTest_t* test = DefinitionForHandle( occlusionTests, handle );
	if ( test != NULL ) {
		test->view = viewID;
	}
}

bool idRenderWorldLocal::IsVisibleOcclusionTestDef( qhandle_t handle ) {
	return DefinitionForHandle( occlusionTests, handle ) != NULL;
}

void idRenderWorldLocal::FreeOcclusionTestDef( qhandle_t handle ) {
	FreeDefinition( occlusionTests, handle );
}

int idRenderWorldLocal::CountVisibleOcclusionTestDef( qhandle_t handle ) {
	return IsVisibleOcclusionTestDef( handle ) ? 1 : 0;
}

bool idRenderWorldLocal::IsVisibleEntity( int viewID, int occid ) {
	return GetRenderEntity( occid ) != NULL;
}

void idRenderWorldLocal::UpdateOcclusionTests() {
}

void idRenderWorldLocal::GenerateAllInteractions() {
}

idRenderModel *idRenderWorldLocal::GetEntityHandleDynamicModel( qhandle_t handle ) {
	renderEntity_t* entity = GetRenderEntity( handle );
	return entity != NULL ? entity->hModel : NULL;
}

idRenderModel *idRenderWorldLocal::CreateDecalModel() {
	return NULL;
}

void idRenderWorldLocal::AddToProjectedDecal( const idFixedWinding&, const idVec3&, const bool, const idVec4&, idRenderModel*, int, const idMaterial**, const int ) {
}

void idRenderWorldLocal::ResetDecalModel( idRenderModel* ) {
}

void idRenderWorldLocal::FinishDecal( idRenderModel* ) {
}

void idRenderWorldLocal::ProjectDecalOntoWorld( const idFixedWinding&, const idVec3&, const bool, const float, const idMaterial*, const int, const int ) {
}

void idRenderWorldLocal::ProjectDecal( qhandle_t, const idFixedWinding&, const idVec3&, const bool, const float, const idMaterial*, const int, const int ) {
}

void idRenderWorldLocal::ProjectOverlay( qhandle_t, const idPlane[2], const idMaterial* ) {
}

void idRenderWorldLocal::RemoveDecals( qhandle_t ) {
}

void idRenderWorldLocal::AddCheapDecal( qhandle_t, const cheapDecalParameters_t&, float ) {
}

void idRenderWorldLocal::ClearDecals() {
}

void idRenderWorldLocal::AddEnvBounds( idVec3 const&, idVec3 const&, const char* ) {
}

void idRenderWorldLocal::SetRenderView( const renderView_t *renderView ) {
	if ( renderView == NULL ) {
		hasRenderView = false;
		return;
	}
	currentRenderView = *renderView;
	hasRenderView = true;
}

void idRenderWorldLocal::RenderScene( const renderView_t *renderView ) {
	SetRenderView( renderView );
	PerformRenderScene( renderView );
}

void idRenderWorldLocal::PerformRenderScene( const renderView_t *renderView ) {
	SetRenderView( renderView );
	if ( renderView == NULL || !glConfig.isInitialized || wglGetCurrentContext() == NULL ) {
		return;
	}

	// Retail emits any fullscreen GUI commands accumulated before a nested
	// render world, then resumes GUI collection after the scene.  This is what
	// keeps a renderWorld window's backdrop behind its models instead of being
	// submitted over them at EndFrame.
	guiModel.FlushFrame( renderSystem->GetScreenWidth(), renderSystem->GetScreenHeight() );

	idScreenRect viewport;
	renderSystemBackend.RenderViewToViewport( renderView, &viewport );
	const int viewportWidth = Max( 1, viewport.x2 - viewport.x1 + 1 );
	const int viewportHeight = Max( 1, viewport.y2 - viewport.y1 + 1 );

	glPushAttrib( GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT |
		GL_ENABLE_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT | GL_TEXTURE_BIT |
		GL_VIEWPORT_BIT );
	glViewport( viewport.x1, viewport.y1, viewportWidth, viewportHeight );
	glEnable( GL_SCISSOR_TEST );
	glScissor( viewport.x1, viewport.y1, viewportWidth, viewportHeight );
	glClear( GL_DEPTH_BUFFER_BIT );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_CULL_FACE );
	glDisable( GL_ALPHA_TEST );
	glShadeModel( GL_SMOOTH );

	const float zNear = renderView->nearPlane > 0.0f ? renderView->nearPlane : 1.0f;
	const float zFar = renderView->farPlane > zNear ? renderView->farPlane : 100000.0f;
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	if ( renderView->size_x > 0.0f && renderView->size_y > 0.0f ) {
		glOrtho( -renderView->size_x, renderView->size_x,
			-renderView->size_y, renderView->size_y, zNear, zFar );
	} else {
		const float fovX = renderView->fov_x > 0.0f ? renderView->fov_x : 90.0f;
		const float fovY = renderView->fov_y > 0.0f ? renderView->fov_y : 90.0f;
		const double xmax = zNear * tan( fovX * idMath::M_DEG2RAD * 0.5f );
		const double ymax = zNear * tan( fovY * idMath::M_DEG2RAD * 0.5f );
		glFrustum( -xmax, xmax, -ymax, ymax, zNear, zFar );
	}

	const idVec3& forward = renderView->viewaxis[ 0 ];
	const idVec3& left = renderView->viewaxis[ 1 ];
	const idVec3& up = renderView->viewaxis[ 2 ];
	const idVec3& origin = renderView->vieworg;
	const float viewMatrix[ 16 ] = {
		-left.x, up.x, -forward.x, 0.0f,
		-left.y, up.y, -forward.y, 0.0f,
		-left.z, up.z, -forward.z, 0.0f,
		left * origin, -( up * origin ), forward * origin, 1.0f
	};
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadMatrixf( viewMatrix );

	for ( int entityIndex = 0; entityIndex < entityDefs.Num(); entityIndex++ ) {
		renderEntity_t* entity = entityDefs[ entityIndex ];
		if ( entity == NULL ) {
			continue;
		}
		if ( entity->suppressSurfaceInViewID != 0 && entity->suppressSurfaceInViewID == renderView->viewID ) {
			continue;
		}
		if ( entity->allowSurfaceInViewID != 0 && entity->allowSurfaceInViewID != renderView->viewID ) {
			continue;
		}
		if ( entity->callback != NULL ) {
			int lastModifiedGameTime = 0;
			entity->callback( entity, renderView, lastModifiedGameTime );
		}
		idRenderModel* model = entity->hModel;
		if ( model == NULL ) {
			continue;
		}

		const float entityMatrix[ 16 ] = {
			entity->axis[ 0 ].x, entity->axis[ 0 ].y, entity->axis[ 0 ].z, 0.0f,
			entity->axis[ 1 ].x, entity->axis[ 1 ].y, entity->axis[ 1 ].z, 0.0f,
			entity->axis[ 2 ].x, entity->axis[ 2 ].y, entity->axis[ 2 ].z, 0.0f,
			entity->origin.x, entity->origin.y, entity->origin.z, 1.0f
		};
		glPushMatrix();
		glMultMatrixf( entityMatrix );

		for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); surfaceIndex++ ) {
			if ( surfaceIndex < MAX_SURFACE_BITS - 1 && entity->hideSurfaceMask.Get( surfaceIndex ) != 0 ) {
				continue;
			}
			const modelSurface_t* surface = model->Surface( surfaceIndex );
			if ( surface == NULL || surface->geometry == NULL ||
				 surface->geometry->verts == NULL || surface->geometry->indexes == NULL ) {
				continue;
			}

			const idMaterial* material = renderView->globalMaterial != NULL ? renderView->globalMaterial :
				( entity->customShader != NULL ? entity->customShader : surface->material );
			if ( material != NULL && !material->IsDrawn() ) {
				continue;
			}
			idImage* image = material != NULL ? material->GetEditorImage() : NULL;
			if ( image != NULL && image->texnum != idImage::TEXTURE_NOT_LOADED && !image->defaulted ) {
				glEnable( GL_TEXTURE_2D );
				glBindTexture( GL_TEXTURE_2D, image->texnum );
			} else {
				glDisable( GL_TEXTURE_2D );
			}
			if ( material != NULL && material->Coverage() == MC_TRANSLUCENT ) {
				glEnable( GL_BLEND );
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				glDepthMask( GL_FALSE );
			} else {
				glDisable( GL_BLEND );
				glDepthMask( GL_TRUE );
			}

			const srfTriangles_t* triangles = surface->geometry;
			glBegin( triangles->mode == PM_POINTSPRITE ? GL_POINTS : GL_TRIANGLES );
			for ( int index = 0; index < triangles->numIndexes; index++ ) {
				const int vertexIndex = triangles->indexes[ index ];
				if ( vertexIndex < 0 || vertexIndex >= triangles->numVerts ) {
					continue;
				}
				const idDrawVert& vertex = triangles->verts[ vertexIndex ];
				const idVec3 normal = vertex.GetNormal();
				const idVec2 st = vertex.GetST();
				glColor4ub(
					static_cast< byte >( idMath::ClampInt( 0, 255, idMath::Ftoi( vertex.color[ 0 ] * entity->shaderParms[ 0 ] ) ) ),
					static_cast< byte >( idMath::ClampInt( 0, 255, idMath::Ftoi( vertex.color[ 1 ] * entity->shaderParms[ 1 ] ) ) ),
					static_cast< byte >( idMath::ClampInt( 0, 255, idMath::Ftoi( vertex.color[ 2 ] * entity->shaderParms[ 2 ] ) ) ),
					static_cast< byte >( idMath::ClampInt( 0, 255, idMath::Ftoi( vertex.color[ 3 ] * entity->shaderParms[ 3 ] ) ) )
				);
				glNormal3fv( normal.ToFloatPtr() );
				glTexCoord2fv( st.ToFloatPtr() );
				glVertex3fv( vertex.xyz.ToFloatPtr() );
			}
			glEnd();
		}
		glPopMatrix();
	}

	glDepthMask( GL_TRUE );
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopAttrib();
}

int idRenderWorldLocal::NumPortals() const {
	return 0;
}

qhandle_t idRenderWorldLocal::FindPortal( const idBounds& ) const {
	return 0;
}

void idRenderWorldLocal::SetPortalState( qhandle_t, int ) {
}

int idRenderWorldLocal::GetPortalState( qhandle_t ) {
	return PS_BLOCK_NONE;
}

void idRenderWorldLocal::UpdatePortalOccTestView( int ) {
}

bool idRenderWorldLocal::AreasAreConnected( int areaNum1, int areaNum2, portalConnection_t ) {
	return areaNum1 == 0 && areaNum2 == 0;
}

bool idRenderWorldLocal::AreasAreConnected( int areaNum1, int areaNum2, portalFlags_t ) {
	return areaNum1 == 0 && areaNum2 == 0;
}

bool idRenderWorldLocal::AreasAreConnected( int areaNum1, int areaNum2 ) {
	return areaNum1 == 0 && areaNum2 == 0;
}

int idRenderWorldLocal::NumAreas() const {
	return 1;
}

int idRenderWorldLocal::PointInArea( const idVec3& ) const {
	return 0;
}

int idRenderWorldLocal::BoundsInAreas( const idBounds&, int *areas, int maxAreas ) const {
	if ( areas == NULL || maxAreas <= 0 ) {
		return 0;
	}
	areas[ 0 ] = 0;
	return 1;
}

int idRenderWorldLocal::NumPortalsInArea( int ) {
	return 0;
}

exitPortal_t idRenderWorldLocal::GetPortal( int, int ) {
	exitPortal_t portal;
	memset( &portal, 0, sizeof( portal ) );
	return portal;
}

void idRenderWorldLocal::SetAreaPortalFlags( int areaNum, int flags ) {
	if ( areaNum == 0 ) {
		areaPortalFlags = flags;
	}
}

int idRenderWorldLocal::GetAreaPortalFlags( int areaNum ) const {
	return areaNum == 0 ? areaPortalFlags : 0;
}

void idRenderWorldLocal::SetAreaAmbientCubeMap( int areaNum, const sdDeclAmbientCubeMap *cubeMapDecl ) {
	if ( areaNum == 0 ) {
		ambientCubeMap = cubeMapDecl;
	}
}

const sdDeclAmbientCubeMap *idRenderWorldLocal::GetAreaAmbientCubeMap( int areaNum ) {
	return areaNum == 0 ? ambientCubeMap : NULL;
}

void idRenderWorldLocal::SetCubemapSunProperties( const sdDeclAmbientCubeMap*, const idVec3&, const idVec3& ) {
}

void idRenderWorldLocal::ClearTrace( modelTrace_t &trace, const idVec3 &end ) const {
	memset( &trace, 0, sizeof( trace ) );
	trace.fraction = 1.0f;
	trace.point = end;
}

bool idRenderWorldLocal::ModelTrace( modelTrace_t &trace, qhandle_t, const idVec3&, const idVec3 &end, const float, int ) const {
	ClearTrace( trace, end );
	return false;
}

bool idRenderWorldLocal::Trace( modelTrace_t &trace, const idVec3&, const idVec3 &end, const float, bool ) const {
	ClearTrace( trace, end );
	return false;
}

bool idRenderWorldLocal::FastWorldTrace( modelTrace_t &trace, const idVec3&, const idVec3 &end ) const {
	ClearTrace( trace, end );
	return false;
}

void idRenderWorldLocal::StartWritingDemo( idDemoFile *demo ) {
	writeDemo = demo;
}

void idRenderWorldLocal::StopWritingDemo() {
	writeDemo = NULL;
}

bool idRenderWorldLocal::ProcessDemoCommand( idDemoFile*, renderView_t*, int* ) {
	return false;
}

void idRenderWorldLocal::RegenerateWorld() {
}

void idRenderWorldLocal::DebugClearLines( int ) {}
void idRenderWorldLocal::DebugLine( const idVec4&, const idVec3&, const idVec3&, const int, const bool ) {}
void idRenderWorldLocal::DebugArrow( const idVec4&, const idVec3&, const idVec3&, int, const int, bool ) {}
void idRenderWorldLocal::DebugWinding( const idVec4&, const idWinding&, const idVec3&, const idMat3&, const int, const bool ) {}
void idRenderWorldLocal::DebugCircle( const idVec4&, const idVec3&, const idVec3&, const float, const int, const int, const bool ) {}
void idRenderWorldLocal::DebugSphere( const idVec4&, const idSphere&, const int, const bool ) {}
void idRenderWorldLocal::DebugBounds( const idVec4&, const idBounds&, const idVec3&, const idMat3&, const int ) {}
void idRenderWorldLocal::DebugBox( const idVec4&, const idBox&, const int ) {}
void idRenderWorldLocal::DebugFrustum( const idVec4&, const idFrustum&, const bool, const int ) {}
void idRenderWorldLocal::DebugCone( const idVec4&, const idVec3&, const idVec3&, float, float, const int ) {}
void idRenderWorldLocal::DebugAxis( const idVec3&, const idMat3&, int ) {}
void idRenderWorldLocal::DebugClearPolygons( int ) {}
void idRenderWorldLocal::DebugPolygon( const idVec4&, const idWinding&, const int, const bool, idImage* ) {}
void idRenderWorldLocal::DrawText( const char*, const idVec3&, float, const idVec4&, const idMat3&, const int, const int ) {}

void idRenderWorldLocal::SetAtmosphere( const sdDeclAtmosphere* value ) {
	atmosphere = value;
}

const sdDeclAtmosphere* idRenderWorldLocal::GetAtmosphere() const {
	return atmosphere;
}

void idRenderWorldLocal::SetupMatrices( const renderView_t*, float* projectionMatrix, float* modelViewMatrix, const bool ) {
	if ( projectionMatrix != NULL ) {
		memset( projectionMatrix, 0, sizeof( float ) * 16 );
		projectionMatrix[ 0 ] = projectionMatrix[ 5 ] = projectionMatrix[ 10 ] = projectionMatrix[ 15 ] = 1.0f;
	}
	if ( modelViewMatrix != NULL ) {
		memset( modelViewMatrix, 0, sizeof( float ) * 16 );
		modelViewMatrix[ 0 ] = modelViewMatrix[ 5 ] = modelViewMatrix[ 10 ] = modelViewMatrix[ 15 ] = 1.0f;
	}
}

void idRenderWorldLocal::SetMegaTextureSTGrid( const idBounds&, const idVec2*, int, int ) {
}

atmosLightProjection_t *idRenderWorldLocal::FindAtmosLightProjection( int ) {
	return NULL;
}

idRenderSystemLocal tr;
idRenderSystem* renderSystem = &tr;

idRenderSystemLocal::idRenderSystemLocal() :
	initialized( false ),
	openGLRunning( false ),
	synced( true ),
	threadsLocked( false ),
	windowWidth( SCREEN_WIDTH ),
	windowHeight( SCREEN_HEIGHT ),
	syncNum( 0 ),
	doubleBufferIndex( 0 ),
	captureBuffer( NULL ),
	frameBuffer( NULL ),
	logFile( NULL ) {
}

idRenderSystemLocal::~idRenderSystemLocal() {
	Shutdown();
}

int idRenderSystemLocal::GetScreenWidth() const {
	return windowWidth;
}

int idRenderSystemLocal::GetScreenHeight() const {
	return windowHeight;
}

idRenderWorld *idRenderSystemLocal::AllocRenderWorld() {
	idRenderWorld* world = new idRenderWorldLocal;
	worlds.Append( world );
	return world;
}

void idRenderSystemLocal::FreeRenderWorld( idRenderWorld *world ) {
	for ( int i = 0; i < worlds.Num(); i++ ) {
		if ( worlds[ i ] == world ) {
			delete worlds[ i ];
			worlds.RemoveIndex( i );
			return;
		}
	}
}

void idRenderSystemLocal::DrawChar( int charWidth, int charHeight, int x, int y, int ch, const idMaterial* material ) {
	ch &= 255;
	if ( ch == ' ' || y < -charHeight ) {
		return;
	}

	const int row = ch >> 4;
	const int column = ch & 15;
	const float atlasCell = 1.0f / 16.0f;
	const float s = column * atlasCell;
	const float t = row * atlasCell;
	deviceContext->DrawRect(
		static_cast< float >( x ), static_cast< float >( y ),
		static_cast< float >( charWidth ), static_cast< float >( charHeight ),
		s, t, s + atlasCell, t + atlasCell, material, 0.0f
	);
}

void idRenderSystemLocal::DrawStringExt( int charWidth, int charHeight, int x, int y, const char* string, const idVec4& setColor, bool forceColor, const idMaterial* material ) {
	if ( string == NULL ) {
		return;
	}

	const unsigned char* cursor = reinterpret_cast< const unsigned char* >( string );
	int drawX = x;
	deviceContext->SetColor( setColor );
	while ( *cursor != '\0' ) {
		if ( idStr::IsColor( reinterpret_cast< const char* >( cursor ) ) ) {
			if ( !forceColor ) {
				idVec4 color = idStr::ColorForChar( cursor[ 1 ] );
				color.w = setColor.w;
				deviceContext->SetColor( color );
			}
			cursor += 2;
			continue;
		}

		DrawChar( charWidth, charHeight, drawX, y, *cursor, material );
		drawX += charWidth;
		cursor++;
	}
	deviceContext->SetColor( colorWhite );
}
void idRenderSystemLocal::DrawSmallChar( int x, int y, int ch, const idMaterial *material ) { DrawChar( SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, x, y, ch, material ); }
void idRenderSystemLocal::DrawSmallStringExt( int x, int y, const char *string, const idVec4 &color, bool forceColor, const idMaterial *material ) { DrawStringExt( SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, x, y, string, color, forceColor, material ); }
void idRenderSystemLocal::DrawBigChar( int x, int y, int ch, const idMaterial *material ) { DrawChar( BIGCHAR_WIDTH, BIGCHAR_HEIGHT, x, y, ch, material ); }
void idRenderSystemLocal::DrawBigStringExt( int x, int y, const char *string, const idVec4 &color, bool forceColor, const idMaterial *material ) { DrawStringExt( BIGCHAR_WIDTH, BIGCHAR_HEIGHT, x, y, string, color, forceColor, material ); }
void idRenderSystemLocal::WriteDemoPics() {}
void idRenderSystemLocal::DrawDemoPics() {}

void idRenderSystemLocal::BeginFrame( int width, int height ) {
	if ( width > 0 ) {
		windowWidth = width;
	}
	if ( height > 0 ) {
		windowHeight = height;
	}
	renderSystemBackend.BeginFrame( windowWidth, windowHeight );
	guiModel.BeginFrame();
	if ( openGLRunning && sys3D != NULL && sys3D->MakeCurrent( sys3D->GetGameWindow() ) ) {
		glViewport( 0, 0, windowWidth, windowHeight );
		glClearColor( 0.04f, 0.05f, 0.07f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	}
	synced = false;
}

void idRenderSystemLocal::EndFrame( bool swapBuffers ) {
	if ( openGLRunning ) {
		guiModel.SubmitFrame( windowWidth, windowHeight );
		glFlush();
		if ( swapBuffers && sys3D != NULL ) {
			sys3D->SwapBuffers();
		}
	}
	synced = true;
	syncNum++;
	doubleBufferIndex ^= 1;
}

void idRenderSystemLocal::SetCaptureBuffer( sdFrameBuffer* value ) { captureBuffer = value; }
sdFrameBuffer* idRenderSystemLocal::GetCaptureBuffer() { return captureBuffer; }
bool idRenderSystemLocal::TakeScreenshot( int, int, const char*, int, renderView_s*, bool, bool ) { return false; }
void idRenderSystemLocal::CropRenderSize( int width, int height, bool makePowerOfTwo ) {
	renderSystemBackend.CropRenderSize( width, height, makePowerOfTwo );
}
void idRenderSystemLocal::CaptureRenderToImage( const char*, int, copyBuffer_t ) {}
void idRenderSystemLocal::SetFrameBuffer( sdFrameBuffer *value ) { frameBuffer = value; }
void idRenderSystemLocal::UnCrop() { renderSystemBackend.UnCrop(); }
void idRenderSystemLocal::GetCardCaps( bool &oldCard ) { oldCard = false; }
bool idRenderSystemLocal::UploadImage( const char* name, const byte* data, int width, int height, bool mipMap, bool allowDownSize ) {
	if ( globalImages == NULL || name == NULL || data == NULL || width <= 0 || height <= 0 ) {
		return false;
	}
	idImage* image = globalImages->GetImage( name );
	if ( image == NULL ) {
		image = globalImages->AllocImage( name );
	}
	image->GenerateImageEx(
		data,
		width,
		height,
		mipMap ? TF_DEFAULT : TF_LINEAR,
		allowDownSize,
		TR_REPEAT,
		TD_DEFAULT,
		0,
		mipMap ? -1 : 1
	);
	return image->IsLoaded();
}

void idRenderSystemLocal::BindImage( textureType_t type, GLuint image ) {
	GLenum target = GL_TEXTURE_2D;
	switch ( type ) {
		case TT_3D:
			target = GL_TEXTURE_3D;
			break;
		case TT_CUBIC:
			target = GL_TEXTURE_CUBE_MAP_ARB;
			break;
		case TT_RECT:
			target = GL_TEXTURE_RECTANGLE_ARB;
			break;
		case TT_2D:
		default:
			break;
	}
	glBindTexture( target, image );
}
void idRenderSystemLocal::SetGLState( int ) {}
void idRenderSystemLocal::SetGLTexEnv( int ) {}
void idRenderSystemLocal::SelectTextureUnit( int unit ) {
	typedef void ( APIENTRY * activeTextureProc_t )( GLenum texture );
	static activeTextureProc_t activeTexture =
		reinterpret_cast< activeTextureProc_t >( wglGetProcAddress( "glActiveTextureARB" ) );
	if ( activeTexture != NULL ) {
		activeTexture( GL_TEXTURE0_ARB + unit );
	}
}

void idRenderSystemLocal::SetDefaultGLState() {
	glClearDepth( 1.0 );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LEQUAL );
}

void idRenderSystemLocal::SetGL2D() {
	glViewport( 0, 0, windowWidth, windowHeight );
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0.0, 640.0, 480.0, 0.0, -1.0, 1.0 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
}
void idRenderSystemLocal::SetCull( int ) {}
FILE* idRenderSystemLocal::GetLogFileHandle() { return logFile; }
void idRenderSystemLocal::SetLogFileHandle( FILE* value ) { logFile = value; }

void idRenderSystemLocal::LoadImage( const char* name, byte **pic, int *width, int *height, unsigned *timestamp, bool makePowerOfTwo ) {
	if ( globalImages != NULL ) {
		globalImages->LoadImage( name, pic, width, height, timestamp, makePowerOfTwo );
		return;
	}
	if ( pic != NULL ) {
		*pic = NULL;
	}
}

void idRenderSystemLocal::FlushGLErrors( bool ) {}
int idRenderSystemLocal::CheckGLForErrors( bool ) { return 0; }
idRenderModel* idRenderSystemLocal::InstantiateDynamicModel( idRenderModel* model, renderEntity_t* ) { return model; }
const glconfig_t& idRenderSystemLocal::GLConfig() const { return glConfig; }
void idRenderSystemLocal::SyncRenderSystem() { synced = true; }

bool idRenderSystemLocal::BeginRenderSync() {
	const bool wasSynced = synced;
	synced = true;
	return wasSynced;
}

void idRenderSystemLocal::EndRenderSync() {
	synced = true;
}

idImage *idRenderSystemLocal::LoadImageFromFile( const char* name, imageParams_t& params ) {
	return globalImages != NULL ? globalImages->ImageFromFile( name, params ) : NULL;
}
bool idRenderSystemLocal::IsDisplayModeAvailable( int width, int height ) const { return width > 0 && height > 0; }
int idRenderSystemLocal::GetNumMSAAModes() const { return 1; }

const char *idRenderSystemLocal::GetMSAAMode( int idx, int &val ) const {
	if ( idx != 0 ) {
		val = 0;
		return NULL;
	}
	val = 0;
	return "Off";
}

bool idRenderSystemLocal::IsMSAACountAvailable( int msaa ) const { return msaa == 0; }
void idRenderSystemLocal::LockThreads() { threadsLocked = true; }
void idRenderSystemLocal::UnlockThreads() { threadsLocked = false; }
int idRenderSystemLocal::GetDoubleBufferIndex() { return doubleBufferIndex; }
int idRenderSystemLocal::GetSyncNum() { return syncNum; }
bool idRenderSystemLocal::IsSMPEnabled() { return false; }
void idRenderSystemLocal::FreeOcclussionQueries() {}

int idRenderSystemLocal::RegisterPtr( void *ptr ) {
	if ( ptr == NULL ) {
		return 0;
	}
	for ( int i = 0; i < registeredPtrs.Num(); i++ ) {
		if ( registeredPtrs[ i ] == NULL ) {
			registeredPtrs[ i ] = ptr;
			return i + 1;
		}
	}
	registeredPtrs.Append( ptr );
	return registeredPtrs.Num();
}

void idRenderSystemLocal::UnregisterPtr( int uid ) {
	if ( uid > 0 && uid <= registeredPtrs.Num() ) {
		registeredPtrs[ uid - 1 ] = NULL;
	}
}

void* idRenderSystemLocal::PtrForUID( int uid ) {
	return uid > 0 && uid <= registeredPtrs.Num() ? registeredPtrs[ uid - 1 ] : NULL;
}

unsigned int idRenderSystemLocal::Run( void* ) {
	return 0;
}

void R_RenderLightFrustum( const renderLight_t&, idPlane lightFrustum[6] ) {
	for ( int i = 0; i < 6; i++ ) {
		lightFrustum[ i ].Zero();
	}
}

void R_ScreenshotFilename( int &lastNumber, const char *base, idStr &fileName ) {
	const char* safeBase = ( base != NULL && base[ 0 ] != '\0' ) ? base : "screenshots/shot";
	fileName = va( "%s%05d.tga", safeBase, lastNumber++ );
}
