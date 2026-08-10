// Copyright (C) 2007 Id Software, Inc.
//
// ETQW's GUI renderer records front-end commands and consumes them at the
// render-system frame boundary.  The command IDs and state transitions here
// follow the recovered sdGuiModel implementation; this first backend uses the
// compatibility OpenGL path while render programs/images are reconstructed.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "GuiModel.h"
#include "DeviceContext.h"
#include "Material.h"
#include "Image.h"
#include "../sound/SoundEmitter.h"

#include <GL/gl.h>

namespace {

struct bitmapFontCache_t {
	int		pixelHeight;
	GLuint	listBase;
	HFONT	font;
};

idList< bitmapFontCache_t > bitmapFontCache;
HGLRC bitmapFontContext = NULL;

bitmapFontCache_t* GetBitmapFont( HDC deviceContext, int pixelHeight ) {
	const HGLRC currentContext = wglGetCurrentContext();
	if ( currentContext == NULL || deviceContext == NULL ) {
		return NULL;
	}
	if ( currentContext != bitmapFontContext ) {
		for ( int i = 0; i < bitmapFontCache.Num(); i++ ) {
			if ( bitmapFontCache[ i ].font != NULL ) {
				DeleteObject( bitmapFontCache[ i ].font );
			}
		}
		bitmapFontCache.Clear();
		bitmapFontContext = currentContext;
	}

	pixelHeight = idMath::ClampInt( 6, 192, pixelHeight );
	for ( int i = 0; i < bitmapFontCache.Num(); i++ ) {
		if ( bitmapFontCache[ i ].pixelHeight == pixelHeight ) {
			return &bitmapFontCache[ i ];
		}
	}

	bitmapFontCache_t cache;
	cache.pixelHeight = pixelHeight;
	cache.listBase = glGenLists( 256 );
	cache.font = CreateFontW(
		-pixelHeight,
		0,
		0,
		0,
		FW_MEDIUM,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_TT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY,
		FF_DONTCARE | VARIABLE_PITCH,
		L"Arial Narrow"
	);
	if ( cache.listBase == 0 || cache.font == NULL ) {
		if ( cache.listBase != 0 ) {
			glDeleteLists( cache.listBase, 256 );
		}
		if ( cache.font != NULL ) {
			DeleteObject( cache.font );
		}
		return NULL;
	}

	HGDIOBJ oldFont = SelectObject( deviceContext, cache.font );
	const BOOL generated = wglUseFontBitmapsW( deviceContext, 0, 256, cache.listBase );
	SelectObject( deviceContext, oldFont );
	if ( !generated ) {
		glDeleteLists( cache.listBase, 256 );
		DeleteObject( cache.font );
		return NULL;
	}

	const int index = bitmapFontCache.Append( cache );
	return &bitmapFontCache[ index ];
}

void BuildDisplayText( const wchar_t* source, idWStr& displayText ) {
	displayText.Clear();
	if ( source == NULL ) {
		return;
	}
	for ( int i = 0; source[ i ] != L'\0'; i++ ) {
		if ( source[ i ] == L'^' && source[ i + 1 ] != L'\0' ) {
			i++;
			continue;
		}
		wchar_t c = source[ i ];
		if ( c > 255 ) {
			c = L'?';
		}
		displayText.Append( c );
	}
}

byte ClampCinematicChannel( int value ) {
	return static_cast< byte >( idMath::ClampInt( 0, 255, value ) );
}

idImage* ResolveGuiImage( const idMaterial* material, idSoundEmitter* referenceSound ) {
	if ( referenceSound == NULL ) {
		return material != NULL ? material->GetEditorImage() : NULL;
	}

	// ETQW's RB_Evaluator_UpdateCinematicImageYUV obtains the video frame from
	// the surface's reference sound.  A missing/stopped frame is black with
	// neutral chroma; it is never the diagnostic default image.
	if ( globalImages == NULL || !referenceSound->CurrentlyPlaying() ) {
		return globalImages != NULL ? globalImages->blackImage : NULL;
	}

	const cinData_t frame = referenceSound->ImageForTime( 0 );
	if ( frame.imageWidth <= 0 || frame.imageHeight <= 0 ||
		 frame.imageWidth > 8192 || frame.imageHeight > 8192 ||
		 frame.image[ 0 ] == NULL ) {
		return globalImages->blackImage;
	}

	if ( frame.image[ 1 ] == NULL || frame.image[ 2 ] == NULL ) {
		globalImages->cinematicImage->UploadScratch( frame.image[ 0 ], frame.imageWidth, frame.imageHeight );
		return globalImages->cinematicImage;
	}

	// The compatibility GUI backend is single-texture fixed-function OpenGL.
	// Convert the Theora 4:2:0 planes to the same RGBA result produced by the
	// retail trivialCinematicYUV program, then use the shared cinematic image.
	static idList< byte > rgba;
	const int pixelCount = frame.imageWidth * frame.imageHeight;
	rgba.SetNum( pixelCount * 4, false );
	const int chromaWidth = Max( 1, frame.imageWidth >> 1 );
	for ( int y = 0; y < frame.imageHeight; y++ ) {
		for ( int x = 0; x < frame.imageWidth; x++ ) {
			const int lumaIndex = y * frame.imageWidth + x;
			const int chromaIndex = ( y >> 1 ) * chromaWidth + ( x >> 1 );
			const int c = Max( 0, static_cast< int >( frame.image[ 0 ][ lumaIndex ] ) - 16 );
			const int d = static_cast< int >( frame.image[ 1 ][ chromaIndex ] ) - 128;
			const int e = static_cast< int >( frame.image[ 2 ][ chromaIndex ] ) - 128;
			byte* pixel = &rgba[ lumaIndex * 4 ];
			pixel[ 0 ] = ClampCinematicChannel( ( 298 * c + 409 * e + 128 ) >> 8 );
			pixel[ 1 ] = ClampCinematicChannel( ( 298 * c - 100 * d - 208 * e + 128 ) >> 8 );
			pixel[ 2 ] = ClampCinematicChannel( ( 298 * c + 516 * d + 128 ) >> 8 );
			pixel[ 3 ] = 255;
		}
	}
	globalImages->cinematicImage->UploadScratch( rgba.Begin(), frame.imageWidth, frame.imageHeight );
	return globalImages->cinematicImage;
}

}

sdGuiModel guiModel;

sdGuiModel::sdGuiModel() :
	currentColor( 0xFFFFFFFF ),
	clippingEnabled( true ),
	fullScreen( true ),
	currentFont( -1 ),
	currentFontSize( 0 ),
	writePosition( 0 ) {
	memset( materialParms, 0, sizeof( materialParms ) );
	materialParms[ 0 ] = materialParms[ 1 ] = materialParms[ 2 ] = materialParms[ 3 ] = 1.0f;
	primitives.SetGranularity( 256 );
	texts.SetGranularity( 128 );
	clipRects.SetGranularity( 8 );
}

void sdGuiModel::BeginFrame() {
	primitives.SetNum( 0, false );
	texts.SetNum( 0, false );
	clipRects.SetNum( 0, false );
	writePosition = 0;
	fullScreen = true;
}

void sdGuiModel::BeginRender() {
}

int sdGuiModel::GetWritePos() const {
	return writePosition;
}

void sdGuiModel::BeginEmitToCurrentView( const float*, int, bool ) {
	fullScreen = false;
}

void sdGuiModel::BeginEmitFullScreen() {
	fullScreen = true;
}

void sdGuiModel::End() {
	writePosition += sizeof( int );
}

void sdGuiModel::RenderScene() {
	writePosition += sizeof( int );
}

void sdGuiModel::SetColor( unsigned int color ) {
	currentColor = color;
	writePosition += sizeof( int ) * 2;
}

void sdGuiModel::SetRegister( int index, float value ) {
	if ( index >= 0 && index < MAX_ENTITY_SHADER_PARMS ) {
		materialParms[ index ] = value;
	}
	writePosition += sizeof( int ) * 3;
}

void sdGuiModel::SetRegisters( const float* values ) {
	if ( values != NULL ) {
		memcpy( materialParms + 4, values, sizeof( float ) * 8 );
	}
	writePosition += sizeof( int ) + sizeof( float ) * 8;
}

void sdGuiModel::EnableClipping( bool enable ) {
	clippingEnabled = enable;
	writePosition += sizeof( int );
}

void sdGuiModel::PushClipRect( const sdBounds2D& rect ) {
	clipRects.Append( rect );
	writePosition += sizeof( int ) + sizeof( sdBounds2D );
}

void sdGuiModel::PopClipRect() {
	if ( clipRects.Num() > 0 ) {
		clipRects.SetNum( clipRects.Num() - 1, false );
	}
	writePosition += sizeof( int );
}

bool sdGuiModel::ClipWinding( idWinding2D& winding ) const {
	if ( !clippingEnabled || clipRects.Num() == 0 ) {
		return winding.GetNumPoints() >= 3;
	}
	for ( int i = 0; i < clipRects.Num(); i++ ) {
		if ( !winding.ClipByBounds( clipRects[ i ] ) ) {
			return false;
		}
	}
	return winding.GetNumPoints() >= 3;
}

void sdGuiModel::QueueRect(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	float ms1, float mt1, float ms2, float mt2,
	const idMaterial* material, idSoundEmitter* referenceSound,
	float angle, bool clipped, bool masked
) {
	idWinding2D winding;
	winding.AddPoint( x, y, s1, t1 );
	winding.AddPoint( x + w, y, s2, t1 );
	winding.AddPoint( x + w, y + h, s2, t2 );
	winding.AddPoint( x, y + h, s1, t2 );

	if ( angle != 0.0f ) {
		const idVec2 center( x + w * 0.5f, y + h * 0.5f );
		winding.Rotation( center, angle * idMath::M_DEG2RAD );
	}

	if ( clipped && !ClipWinding( winding ) ) {
		return;
	}

	guiPrimitive_t primitive;
	memset( &primitive, 0, sizeof( primitive ) );
	primitive.material = material;
	primitive.referenceSound = referenceSound;
	primitive.color = currentColor;
	primitive.masked = masked;
	primitive.numVerts = Min( winding.GetNumPoints(), idWinding2D::MAX_POINTS );
	for ( int i = 0; i < primitive.numVerts; i++ ) {
		primitive.verts[ i ].xy = winding[ i ];
		primitive.verts[ i ].st = winding.GetST( i );
		const float fx = w != 0.0f ? ( winding[ i ].x - x ) / w : 0.0f;
		const float fy = h != 0.0f ? ( winding[ i ].y - y ) / h : 0.0f;
		primitive.verts[ i ].maskST.Set(
			ms1 + ( ms2 - ms1 ) * fx,
			mt1 + ( mt2 - mt1 ) * fy
		);
	}
	primitives.Append( primitive );
	writePosition += sizeof( primitive );
}

void sdGuiModel::QueueWinding(
	const idWinding2D& source,
	const idMaterial* material,
	idSoundEmitter* referenceSound,
	bool masked,
	float minx, float miny, float width, float height
) {
	idWinding2D winding = source;
	if ( !ClipWinding( winding ) ) {
		return;
	}

	guiPrimitive_t primitive;
	memset( &primitive, 0, sizeof( primitive ) );
	primitive.material = material;
	primitive.referenceSound = referenceSound;
	primitive.color = currentColor;
	primitive.masked = masked;
	primitive.numVerts = Min( winding.GetNumPoints(), idWinding2D::MAX_POINTS );
	for ( int i = 0; i < primitive.numVerts; i++ ) {
		primitive.verts[ i ].xy = winding[ i ];
		primitive.verts[ i ].st = winding.GetST( i );
		primitive.verts[ i ].maskST.Set(
			width != 0.0f ? ( winding[ i ].x - minx ) / width : 0.0f,
			height != 0.0f ? ( winding[ i ].y - miny ) / height : 0.0f
		);
	}
	primitives.Append( primitive );
	writePosition += sizeof( primitive );
}

void sdGuiModel::DrawRect(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	const idMaterial* material, float angle
) {
	QueueRect( x, y, w, h, s1, t1, s2, t2, 0.0f, 0.0f, 1.0f, 1.0f,
		material, NULL, angle, false, false );
}

void sdGuiModel::DrawClippedRect(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	const idMaterial* material, float angle
) {
	QueueRect( x, y, w, h, s1, t1, s2, t2, 0.0f, 0.0f, 1.0f, 1.0f,
		material, NULL, angle, true, false );
}

void sdGuiModel::DrawMaskedClippedRect(
	float x, float y, float w, float h,
	float s01, float t01, float s02, float t02,
	float s11, float t11, float s12, float t12,
	const idMaterial* material, float angle
) {
	QueueRect( x, y, w, h, s01, t01, s02, t02, s11, t11, s12, t12,
		material, NULL, angle, true, true );
}

void sdGuiModel::DrawCinematic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	const idMaterial* material, idSoundEmitter* referenceSound, float angle
) {
	QueueRect( x, y, w, h, s1, t1, s2, t2, 0.0f, 0.0f, 1.0f, 1.0f,
		material, referenceSound, angle, true, false );
}

void sdGuiModel::DrawClippedWinding( const idWinding2D& winding, const idMaterial* material ) {
	QueueWinding( winding, material, NULL, false, 0.0f, 0.0f, 1.0f, 1.0f );
}

void sdGuiModel::DrawClippedWindingMasked(
	const idWinding2D& winding,
	const idMaterial* material,
	float minx, float miny, float width, float height
) {
	QueueWinding( winding, material, NULL, true, minx, miny, width, height );
}

void sdGuiModel::SetFont( int font ) {
	currentFont = font;
	writePosition += sizeof( int ) * 2;
}

void sdGuiModel::SetFontSize( int pointSize ) {
	currentFontSize = pointSize;
	writePosition += sizeof( int ) * 2;
}

void sdGuiModel::DrawTextA( const wchar_t* text, const sdBounds2D& rect, unsigned int flags ) {
	if ( text == NULL || text[ 0 ] == L'\0' ) {
		return;
	}
	guiText_t command;
	command.text = text;
	command.rect = rect;
	command.flags = flags;
	command.color = currentColor;
	command.font = currentFont;
	command.pointSize = currentFontSize;
	texts.Append( command );
	writePosition += sizeof( int ) * 8;
}

void sdGuiModel::SubmitFrame( int windowWidth, int windowHeight ) {
	if ( ( primitives.Num() == 0 && texts.Num() == 0 ) || !fullScreen ) {
		return;
	}

	glPushAttrib( GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT |
		GL_ENABLE_BIT | GL_LIST_BIT | GL_PIXEL_MODE_BIT | GL_POLYGON_BIT |
		GL_SCISSOR_BIT | GL_TEXTURE_BIT );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_ALPHA_TEST );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0.0, 640.0, 480.0, 0.0, -1.0, 1.0 );
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();

	for ( int i = 0; i < primitives.Num(); i++ ) {
		const guiPrimitive_t& primitive = primitives[ i ];
		idImage* image = ResolveGuiImage( primitive.material, primitive.referenceSound );
		// A missing optional GUI overlay (for example the retail-only scanline
		// image) must not turn into an opaque diagnostic quad over the frontend.
		if ( image != NULL && image->defaulted ) {
			continue;
		}
		if ( image != NULL && image->texnum != idImage::TEXTURE_NOT_LOADED ) {
			glEnable( GL_TEXTURE_2D );
			glBindTexture( GL_TEXTURE_2D, image->texnum );
		} else {
			glDisable( GL_TEXTURE_2D );
		}

		const byte* rgba = reinterpret_cast< const byte* >( &primitive.color );
		glColor4ub( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ], rgba[ 3 ] );
		glBegin( GL_TRIANGLE_FAN );
		for ( int j = 0; j < primitive.numVerts; j++ ) {
			glTexCoord2f( primitive.verts[ j ].st.x, primitive.verts[ j ].st.y );
			glVertex2f( primitive.verts[ j ].xy.x, primitive.verts[ j ].xy.y );
		}
		glEnd();
	}

	if ( texts.Num() > 0 ) {
		HDC windowDC = wglGetCurrentDC();
		const float pixelScale = Max( 1.0f, static_cast< float >( windowHeight ) / 480.0f );
		glDisable( GL_TEXTURE_2D );
		for ( int i = 0; i < texts.Num(); i++ ) {
			const guiText_t& command = texts[ i ];
			idWStr displayText;
			BuildDisplayText( command.text.c_str(), displayText );
			if ( displayText.IsEmpty() ) {
				continue;
			}

			const int pointSize = command.pointSize > 0 ? command.pointSize : 12;
			bitmapFontCache_t* font = GetBitmapFont( windowDC, idMath::Ftoi( pointSize * pixelScale ) );
			if ( font == NULL ) {
				continue;
			}

			HGDIOBJ oldFont = SelectObject( windowDC, font->font );
			SIZE extent;
			extent.cx = extent.cy = 0;
			GetTextExtentPoint32W( windowDC, displayText.c_str(), displayText.Length(), &extent );
			SelectObject( windowDC, oldFont );

			const float virtualWidth = extent.cx / pixelScale;
			const float virtualHeight = extent.cy / pixelScale;
			float x = command.rect.GetMins().x;
			float y = command.rect.GetMins().y;
			if ( command.flags & DTF_CENTER ) {
				x += ( command.rect.GetWidth() - virtualWidth ) * 0.5f;
			} else if ( command.flags & DTF_RIGHT ) {
				x += command.rect.GetWidth() - virtualWidth;
			}
			if ( command.flags & DTF_VCENTER ) {
				y += ( command.rect.GetHeight() - virtualHeight ) * 0.5f;
			} else if ( command.flags & DTF_BOTTOM ) {
				y += command.rect.GetHeight() - virtualHeight;
			}

			const byte* rgba = reinterpret_cast< const byte* >( &command.color );
			glColor4ub( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ], rgba[ 3 ] );
			glRasterPos2f( x, y + virtualHeight );
			glListBase( font->listBase );
			glCallLists( displayText.Length(), GL_UNSIGNED_SHORT, displayText.c_str() );
		}
	}

	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopAttrib();
}

void sdGuiModel::FlushFrame( int windowWidth, int windowHeight ) {
	SubmitFrame( windowWidth, windowHeight );
	primitives.SetNum( 0, false );
	texts.SetNum( 0, false );
}
