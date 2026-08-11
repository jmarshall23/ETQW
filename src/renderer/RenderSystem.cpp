// Copyright (C) 2007 Id Software, Inc.
//
// ETQW render-system front end. Function ownership follows RenderSystem.obj
// in the retail Microsoft PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RendererTypesImpl.h"
#include "Image.h"
#include "ModelManager.h"
#include "RenderSystemBackend.h"
#include "GuiModel.h"
#include "DeviceContext.h"
#include "tr_render.h"
#include "VertexCache.h"
#include "../sys/sys_render.h"

#include <GL/gl.h>

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
		SetDefaultGLState();
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
		vertexCache.EndFrame();
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
void idRenderSystemLocal::SetGLState( int stateVector ) { GL_State( stateVector ); }
void idRenderSystemLocal::SetGLTexEnv( int env ) { GL_TexEnv( env ); }
void idRenderSystemLocal::SelectTextureUnit( int unit ) { GL_SelectTexture( unit ); }

void idRenderSystemLocal::SetDefaultGLState() {
	RB_SetDefaultGLState();
}

void idRenderSystemLocal::SetGL2D() {
	RB_SetGL2D();
}
void idRenderSystemLocal::SetCull( int cullType ) { GL_Cull( cullType ); }
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
