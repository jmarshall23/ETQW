// Copyright (C) 2007 Id Software, Inc.
//
// ETQW's GUI renderer records front-end commands and consumes them at the
// render-system frame boundary.  The command IDs and state transitions here
// follow the recovered sdGuiModel implementation; this first backend uses the
// compatibility OpenGL path while render programs/images are reconstructed.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "GuiModel.h"
#include "Material.h"
#include "Image.h"

#include <GL/gl.h>

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
	clipRects.SetGranularity( 8 );
}

void sdGuiModel::BeginFrame() {
	primitives.SetNum( 0, false );
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

void sdGuiModel::DrawTextA( const wchar_t*, const sdBounds2D&, unsigned int ) {
	// Glyph emission is supplied by FontManager.cpp.  Keeping this as a command
	// boundary prevents the device context from silently losing its state.
	writePosition += sizeof( int ) * 8;
}

void sdGuiModel::SubmitFrame( int, int ) {
	if ( primitives.Num() == 0 || !fullScreen ) {
		return;
	}

	glPushAttrib( GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT |
		GL_ENABLE_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT | GL_TEXTURE_BIT );
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
		idImage* image = primitive.material != NULL ? primitive.material->GetEditorImage() : NULL;
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

	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopAttrib();
}
