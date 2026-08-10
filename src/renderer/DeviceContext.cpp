// Copyright (C) 2007 Id Software, Inc.
//
// sdDeviceContextLocal reconstructed from the Microsoft PDB and
// quakewars-hexrays/renderer/DeviceContext.cpp.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "DeviceContext.h"
#include "GuiModel.h"
#include "Material.h"

namespace {

static const float MIN_VISIBLE_ALPHA = 1.1920929e-7f;

class sdDeviceContextLocal : public sdDeviceContext {
public:
	sdDeviceContextLocal() :
		defaultMaterial( NULL ),
		currentColor( 0xFFFFFFFF ),
		colorMultiplier( colorWhite ),
		clippingEnabled( true ),
		currentFont( -1 ),
		currentFontSize( 0 ),
		overrideAspectRatio( false ) {
		memset( materialParms, 0, sizeof( materialParms ) );
		materialParms[ 0 ] = materialParms[ 1 ] = materialParms[ 2 ] = materialParms[ 3 ] = 1.0f;
	}

	virtual void Reset();
	virtual void BeginEmitToCurrentView( const float modelMatrix[ 16 ], int allowInViewID, bool weaponDepthHack );
	virtual void BeginEmitFullScreen();
	virtual void End();
	virtual void SetColor( const idVec4& color );
	virtual void SetColor( float r, float g, float b, float a );
	virtual idVec4 SetColorMultiplier( const idVec4& c );
	virtual void SetRegister( int index, float value );
	virtual void SetRegisters( const float* values );
	virtual void EnableClipping( bool enable );
	virtual void PushClipRect( const sdBounds2D& bounds );
	virtual void PopClipRect();
	virtual void DrawRect( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial* material, float angle );
	virtual void DrawClippedRect( float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial* material, float angle );
	virtual void DrawMaskedClippedRect( float x, float y, float w, float h,
		float s01, float t01, float s02, float t02,
		float s11, float t11, float s12, float t12,
		const idMaterial* material, float angle );
	virtual void DrawCinematic( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2,
		const idMaterial* material, idSoundEmitter* referenceSound, float angle );
	virtual void DrawClippedWinding( const idWinding2D& winding, const idMaterial* material );
	virtual void DrawClippedWindingMasked( const idWinding2D& winding, const idMaterial* material, float minx, float miny, float width, float height );
	virtual void DrawMaskedMaterial( float x, float y, float w, float h,
		float u0, float v0, float u1, float v1,
		const idMaterial* material, const idVec4& color,
		float scaleX, float scaleY, float offsetX, float offsetY, float angle );
	virtual void DrawMaterial( float x, float y, float w, float h,
		const idMaterial* material, const idVec4& color,
		float scaleX, float scaleY, float offsetX, float offsetY, float angle );
	virtual void DrawMaterial( const idVec4& rect, const idMaterial* material, const idVec4& color, const idVec2& scale, const idVec2& offset, float angle );
	virtual void DrawMaterial( const sdBounds2D& rect, const idMaterial* material, const idVec4& color, const idVec2& scale, const idVec2& offset, float angle );
	virtual void DrawMaterial( float x, float y, float w, float h, const idMaterial* material, const idVec4& color, const idVec2& st0, const idVec2& st1 );
	virtual void DrawRotatedMaterial( float angle, idVec2 topLeft, idVec2 extents, const idMaterial* material, const idVec4& color );
	virtual void DrawWindingMaterial( const idWinding2D& winding, const idMaterial* material, const idVec4& color );
	virtual void DrawRect( float x, float y, float w, float h, const idVec4& color );
	virtual void DrawClippedRect( float x, float y, float w, float h, const idVec4& color );
	virtual void DrawBox( float x, float y, float w, float h, float size, const idVec4& color );
	virtual void DrawClippedBox( float x, float y, float w, float h, float size, const idVec4& color );
	virtual void DrawCircleMaterial( float x, float y, const idVec2& radius, int numSides, const idVec4& tcInfo, const idMaterial* material, const idVec4& color, float rotation );
	virtual void DrawCircleMaterialMasked( float x, float y, const idVec2& radius, int numSides, const idVec4& tcInfo, const idMaterial* material, const idVec4& color, float rotation, float minx, float miny, float width, float height );
	virtual void DrawCircle( float x, float y, const idVec2& radius, float width, int numSides, const idVec4& color );
	virtual void DrawLineMaterial( const idVec2& start, const idVec2& end, float width, const idMaterial* material, const idVec4& color );
	virtual void DrawLine( const idVec2& start, const idVec2& end, float width, const idVec4& color );
	virtual void DrawFilledArc( float x, float y, float radius, int numSides, float percent, const idVec4& color, float startAngle, const idMaterial* material );
	virtual void DrawFilledArcMasked( float x, float y, float radius, int numSides, float percent, const idVec4& color,
		float minx, float miny, float width, float height, float startAngle, const idMaterial* material );
	virtual void DrawArc( float x, float y, float radius, float width, int numSides, float percent, const idVec4& color, float startAngle );
	virtual void DrawTimer( float x, float y, float w, float h, float percent, const idVec4& color, const idMaterial* material, bool invert, const idVec2& st0, const idVec2& st1 );
	virtual qhandle_t FindFont( const char* fontName );
	virtual void FreeFont( qhandle_t font );
	virtual const int GetFontHeight( qhandle_t font, int pointSize );
	virtual void SetFont( qhandle_t font );
	virtual void SetFontSize( int pointSize );
	virtual void DrawText( const wchar_t* text, const sdBounds2D& rect, unsigned int flags );
	virtual void GetTextDimensions( const wchar_t* text, const sdBounds2D& rect, unsigned int flags,
		qhandle_t font, int pointSize, int& width, int& height,
		float* scale, int** charAdvances, idList< int >* lineBreaks );
	virtual void OverrideAspectRationCorrection( bool setOverride );
	virtual float GetAspectRatioCorrection() const;

private:
	void GenerateCircle( idWinding2D& winding, float x, float y, const idVec2& radius, int numSides, const idVec4& tcInfo ) const;
	void DrawWindingMaterialMasked( const idWinding2D& winding, const idMaterial* material, const idVec4& color,
		float minx, float miny, float width, float height );

	const idMaterial*	defaultMaterial;
	unsigned int		currentColor;
	idVec4				colorMultiplier;
	float				materialParms[ MAX_ENTITY_SHADER_PARMS ];
	bool				clippingEnabled;
	int					currentFont;
	int					currentFontSize;
	bool				overrideAspectRatio;
};

idStrList recoveredFontNames;
sdDeviceContextLocal deviceContextLocal;

}

sdDeviceContext* deviceContext = &deviceContextLocal;

void sdDeviceContextLocal::Reset() {
	defaultMaterial = declHolder.FindMaterial( "_whiteVertexColor", true );
	currentColor = sdColor4::PackColor( colorWhite );
	colorMultiplier = colorWhite;
	memset( materialParms, 0, sizeof( materialParms ) );
	materialParms[ 0 ] = materialParms[ 1 ] = materialParms[ 2 ] = materialParms[ 3 ] = 1.0f;
	clippingEnabled = true;
	currentFont = -1;
	currentFontSize = 0;
	overrideAspectRatio = false;
	guiModel.SetColor( currentColor );
	guiModel.EnableClipping( true );
}

void sdDeviceContextLocal::BeginEmitToCurrentView( const float modelMatrix[ 16 ], int allowInViewID, bool weaponDepthHack ) {
	Reset();
	guiModel.BeginEmitToCurrentView( modelMatrix, allowInViewID, weaponDepthHack );
}

void sdDeviceContextLocal::BeginEmitFullScreen() {
	Reset();
	guiModel.BeginEmitFullScreen();
}

void sdDeviceContextLocal::End() {
	guiModel.End();
}

idVec4 sdDeviceContextLocal::SetColorMultiplier( const idVec4& c ) {
	const idVec4 previous = colorMultiplier;
	colorMultiplier = c;
	return previous;
}

void sdDeviceContextLocal::SetColor( const idVec4& color ) {
	const idVec4 multiplied(
		color.x * colorMultiplier.x,
		color.y * colorMultiplier.y,
		color.z * colorMultiplier.z,
		color.w * colorMultiplier.w
	);
	const unsigned int packed = sdColor4::PackColor( multiplied );
	if ( packed != currentColor ) {
		currentColor = packed;
		guiModel.SetColor( packed );
	}
}

void sdDeviceContextLocal::SetColor( float r, float g, float b, float a ) {
	SetColor( idVec4( r, g, b, a ) );
}

void sdDeviceContextLocal::SetRegister( int index, float value ) {
	if ( index < 0 || index >= MAX_ENTITY_SHADER_PARMS || materialParms[ index ] == value ) {
		return;
	}
	materialParms[ index ] = value;
	guiModel.SetRegister( index, value );
}

void sdDeviceContextLocal::SetRegisters( const float* values ) {
	if ( values == NULL || memcmp( materialParms + 4, values, sizeof( float ) * 8 ) == 0 ) {
		return;
	}
	memcpy( materialParms + 4, values, sizeof( float ) * 8 );
	guiModel.SetRegisters( values );
}

void sdDeviceContextLocal::EnableClipping( bool enable ) {
	if ( enable != clippingEnabled ) {
		clippingEnabled = enable;
		guiModel.EnableClipping( enable );
	}
}

void sdDeviceContextLocal::PushClipRect( const sdBounds2D& bounds ) {
	guiModel.PushClipRect( bounds );
}

void sdDeviceContextLocal::PopClipRect() {
	guiModel.PopClipRect();
}

void sdDeviceContextLocal::DrawRect(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	const idMaterial* material, float angle
) {
	guiModel.DrawRect( x, y, w, h, s1, t1, s2, t2, material, angle );
}

void sdDeviceContextLocal::DrawClippedRect(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	const idMaterial* material, float angle
) {
	guiModel.DrawClippedRect( x, y, w, h, s1, t1, s2, t2, material, angle );
}

void sdDeviceContextLocal::DrawMaskedClippedRect(
	float x, float y, float w, float h,
	float s01, float t01, float s02, float t02,
	float s11, float t11, float s12, float t12,
	const idMaterial* material, float angle
) {
	guiModel.DrawMaskedClippedRect( x, y, w, h, s01, t01, s02, t02,
		s11, t11, s12, t12, material, angle );
}

void sdDeviceContextLocal::DrawCinematic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	const idMaterial* material, idSoundEmitter* referenceSound, float angle
) {
	guiModel.DrawCinematic( x, y, w, h, s1, t1, s2, t2, material, referenceSound, angle );
}

void sdDeviceContextLocal::DrawClippedWinding( const idWinding2D& winding, const idMaterial* material ) {
	guiModel.DrawClippedWinding( winding, material );
}

void sdDeviceContextLocal::DrawClippedWindingMasked(
	const idWinding2D& winding,
	const idMaterial* material,
	float minx, float miny, float width, float height
) {
	guiModel.DrawClippedWindingMasked( winding, material, minx, miny, width, height );
}

void sdDeviceContextLocal::DrawMaterial(
	float x, float y, float w, float h,
	const idMaterial* material, const idVec4& color,
	float scaleX, float scaleY, float offsetX, float offsetY, float angle
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );

	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = scaleX;
	float t2 = scaleY;
	if ( scaleX < 0.0f ) {
		w = -w;
		s1 = -scaleX;
		s2 = 0.0f;
	}
	if ( scaleY < 0.0f ) {
		h = -h;
		t1 = -scaleY;
		t2 = 0.0f;
	}
	if ( w < 0.0f ) {
		w = -w;
		Swap( s1, s2 );
	}
	if ( h < 0.0f ) {
		h = -h;
		Swap( t1, t2 );
	}
	DrawClippedRect( x, y, w, h, s1 + offsetX, t1 + offsetY, s2 + offsetX, t2 + offsetY, material, angle );
}

void sdDeviceContextLocal::DrawMaskedMaterial(
	float x, float y, float w, float h,
	float u0, float v0, float u1, float v1,
	const idMaterial* material, const idVec4& color,
	float scaleX, float scaleY, float offsetX, float offsetY, float angle
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = scaleX;
	float t2 = scaleY;
	if ( scaleX < 0.0f ) {
		w = -w;
		s1 = -scaleX;
		s2 = 0.0f;
	}
	if ( scaleY < 0.0f ) {
		h = -h;
		t1 = -scaleY;
		t2 = 0.0f;
	}
	if ( w < 0.0f ) {
		w = -w;
		Swap( s1, s2 );
	}
	if ( h < 0.0f ) {
		h = -h;
		Swap( t1, t2 );
	}
	DrawMaskedClippedRect( x, y, w, h,
		s1 + offsetX, t1 + offsetY, s2 + offsetX, t2 + offsetY,
		u0, v0, u1, v1, material, angle );
}

void sdDeviceContextLocal::DrawMaterial(
	const idVec4& rect,
	const idMaterial* material,
	const idVec4& color,
	const idVec2& scale,
	const idVec2& offset,
	float angle
) {
	DrawMaterial( rect.x, rect.y, rect.z, rect.w, material, color, scale.x, scale.y, offset.x, offset.y, angle );
}

void sdDeviceContextLocal::DrawMaterial(
	const sdBounds2D& rect,
	const idMaterial* material,
	const idVec4& color,
	const idVec2& scale,
	const idVec2& offset,
	float angle
) {
	DrawMaterial( rect.GetMins().x, rect.GetMins().y, rect.GetWidth(), rect.GetHeight(),
		material, color, scale.x, scale.y, offset.x, offset.y, angle );
}

void sdDeviceContextLocal::DrawMaterial(
	float x, float y, float w, float h,
	const idMaterial* material,
	const idVec4& color,
	const idVec2& st0,
	const idVec2& st1
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	DrawClippedRect( x, y, w, h, st0.x, st0.y, st1.x, st1.y, material, 0.0f );
}

void sdDeviceContextLocal::DrawRotatedMaterial(
	float angle,
	idVec2 topLeft,
	idVec2 extents,
	const idMaterial* material,
	const idVec4& color
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	float s1 = extents.x < 0.0f ? 1.0f : 0.0f;
	float s2 = extents.x < 0.0f ? 0.0f : 1.0f;
	float t1 = extents.y < 0.0f ? 1.0f : 0.0f;
	float t2 = extents.y < 0.0f ? 0.0f : 1.0f;
	extents.x = idMath::Fabs( extents.x );
	extents.y = idMath::Fabs( extents.y );
	idWinding2D winding;
	winding.AddPoint( topLeft.x, topLeft.y, s1, t1 );
	winding.AddPoint( topLeft.x + extents.x, topLeft.y, s2, t1 );
	winding.AddPoint( topLeft.x + extents.x, topLeft.y + extents.y, s2, t2 );
	winding.AddPoint( topLeft.x, topLeft.y + extents.y, s1, t2 );
	winding.RotationST( idVec2( 0.5f, 0.5f ), angle * idMath::M_DEG2RAD );
	DrawClippedWinding( winding, material );
}

void sdDeviceContextLocal::DrawWindingMaterial(
	const idWinding2D& winding,
	const idMaterial* material,
	const idVec4& color
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	DrawClippedWinding( winding, material );
}

void sdDeviceContextLocal::DrawWindingMaterialMasked(
	const idWinding2D& winding,
	const idMaterial* material,
	const idVec4& color,
	float minx, float miny, float width, float height
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	DrawClippedWindingMasked( winding, material, minx, miny, width, height );
}

void sdDeviceContextLocal::DrawRect( float x, float y, float w, float h, const idVec4& color ) {
	if ( color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	DrawRect( x, y, w, h, 0.0f, 0.0f, 0.0f, 0.0f, defaultMaterial, 0.0f );
}

void sdDeviceContextLocal::DrawClippedRect( float x, float y, float w, float h, const idVec4& color ) {
	if ( color.w < MIN_VISIBLE_ALPHA ) {
		return;
	}
	SetColor( color );
	DrawClippedRect( x, y, w, h, 0.0f, 0.0f, 0.0f, 0.0f, defaultMaterial, 0.0f );
}

void sdDeviceContextLocal::DrawBox( float x, float y, float w, float h, float size, const idVec4& color ) {
	if ( color.w < MIN_VISIBLE_ALPHA || size <= 0.0f ) {
		return;
	}
	DrawRect( x, y, w, size, color );
	DrawRect( x, y + h - size, w, size, color );
	DrawRect( x, y + size, size, h - size * 2.0f, color );
	DrawRect( x + w - size, y + size, size, h - size * 2.0f, color );
}

void sdDeviceContextLocal::DrawClippedBox( float x, float y, float w, float h, float size, const idVec4& color ) {
	if ( color.w < MIN_VISIBLE_ALPHA || size <= 0.0f ) {
		return;
	}
	DrawClippedRect( x, y, w, size, color );
	DrawClippedRect( x, y + h - size, w, size, color );
	DrawClippedRect( x, y + size, size, h - size * 2.0f, color );
	DrawClippedRect( x + w - size, y + size, size, h - size * 2.0f, color );
}

void sdDeviceContextLocal::DrawLineMaterial(
	const idVec2& start,
	const idVec2& end,
	float width,
	const idMaterial* material,
	const idVec4& color
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA || width <= 0.0f ) {
		return;
	}
	idVec2 direction = end - start;
	const float length = direction.Normalize();
	if ( length <= 0.0f ) {
		return;
	}
	const idVec2 side( -direction.y * width, direction.x * width );
	idWinding2D line;
	line.AddPoint( start - side, idVec2( 0.0f, 1.0f ) );
	line.AddPoint( start + side, idVec2( 0.0f, 0.0f ) );
	line.AddPoint( end + side, idVec2( 1.0f, 0.0f ) );
	line.AddPoint( end - side, idVec2( 1.0f, 1.0f ) );
	DrawWindingMaterial( line, material, color );
}

void sdDeviceContextLocal::DrawLine( const idVec2& start, const idVec2& end, float width, const idVec4& color ) {
	DrawLineMaterial( start, end, width, defaultMaterial, color );
}

void sdDeviceContextLocal::GenerateCircle(
	idWinding2D& winding,
	float x,
	float y,
	const idVec2& radius,
	int numSides,
	const idVec4& tcInfo
) const {
	winding.Clear();
	numSides = idMath::ClampInt( 3, idWinding2D::MAX_POINTS, numSides );
	for ( int i = 0; i < numSides; i++ ) {
		const float angle = idMath::TWO_PI * static_cast< float >( i ) / static_cast< float >( numSides );
		const float c = idMath::Cos( angle );
		const float s = idMath::Sin( angle );
		winding.AddPoint(
			x + s * radius.x,
			y + c * radius.y,
			tcInfo.x + s * tcInfo.z,
			tcInfo.y + c * tcInfo.w
		);
	}
}

void sdDeviceContextLocal::DrawCircleMaterial(
	float x, float y, const idVec2& radius, int numSides,
	const idVec4& tcInfo, const idMaterial* material,
	const idVec4& color, float rotation
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA || radius.x < 1.0f || radius.y < 1.0f ) {
		return;
	}
	idWinding2D winding;
	GenerateCircle( winding, x, y, radius, numSides, tcInfo );
	winding.RotationST( idVec2( tcInfo.x, tcInfo.y ), rotation * idMath::M_DEG2RAD );
	DrawWindingMaterial( winding, material, color );
}

void sdDeviceContextLocal::DrawCircleMaterialMasked(
	float x, float y, const idVec2& radius, int numSides,
	const idVec4& tcInfo, const idMaterial* material,
	const idVec4& color, float rotation,
	float minx, float miny, float width, float height
) {
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA || radius.x < 1.0f || radius.y < 1.0f ) {
		return;
	}
	idWinding2D winding;
	GenerateCircle( winding, x, y, radius, numSides, tcInfo );
	winding.RotationST( idVec2( tcInfo.x, tcInfo.y ), rotation * idMath::M_DEG2RAD );
	DrawWindingMaterialMasked( winding, material, color, minx, miny, width, height );
}

void sdDeviceContextLocal::DrawCircle(
	float x, float y, const idVec2& radius,
	float width, int numSides, const idVec4& color
) {
	if ( color.w < MIN_VISIBLE_ALPHA || radius.x < 1.0f || radius.y < 1.0f ) {
		return;
	}
	numSides = idMath::ClampInt( 3, idWinding2D::MAX_POINTS, numSides );
	idVec2 previous(
		x,
		y + radius.y
	);
	for ( int i = 1; i <= numSides; i++ ) {
		const float angle = idMath::TWO_PI * static_cast< float >( i % numSides ) / static_cast< float >( numSides );
		const idVec2 next( x + idMath::Sin( angle ) * radius.x, y + idMath::Cos( angle ) * radius.y );
		DrawLine( previous, next, width, color );
		previous = next;
	}
}

void sdDeviceContextLocal::DrawFilledArc(
	float x, float y, float radius, int numSides,
	float percent, const idVec4& color,
	float startAngle, const idMaterial* material
) {
	numSides = idMath::ClampInt( 3, 31, numSides );
	percent = idMath::ClampFloat( 0.0f, 1.0f, percent );
	startAngle -= idMath::Floor( startAngle / 360.0f ) * 360.0f;
	idWinding2D winding;
	winding.AddPoint( x, y, 0.5f, 0.5f );
	for ( int i = 0; i < numSides; i++ ) {
		const float fraction = static_cast< float >( i ) / static_cast< float >( numSides - 1 );
		const float angle = ( startAngle - 360.0f * percent * fraction ) * idMath::M_DEG2RAD;
		const float c = idMath::Cos( angle );
		const float s = idMath::Sin( angle );
		winding.AddPoint( x + c * radius, y + s * radius, ( c + 1.0f ) * 0.5f, ( s + 1.0f ) * 0.5f );
	}
	DrawWindingMaterial( winding, material != NULL ? material : defaultMaterial, color );
}

void sdDeviceContextLocal::DrawFilledArcMasked(
	float x, float y, float radius, int numSides,
	float percent, const idVec4& color,
	float minx, float miny, float width, float height,
	float startAngle, const idMaterial* material
) {
	numSides = idMath::ClampInt( 3, 31, numSides );
	percent = idMath::ClampFloat( 0.0f, 1.0f, percent );
	idWinding2D winding;
	winding.AddPoint( x, y, 0.5f, 0.5f );
	for ( int i = 0; i < numSides; i++ ) {
		const float fraction = static_cast< float >( i ) / static_cast< float >( numSides - 1 );
		const float angle = ( startAngle - 360.0f * percent * fraction ) * idMath::M_DEG2RAD;
		const float c = idMath::Cos( angle );
		const float s = idMath::Sin( angle );
		winding.AddPoint( x + c * radius, y + s * radius, ( c + 1.0f ) * 0.5f, ( s + 1.0f ) * 0.5f );
	}
	DrawWindingMaterialMasked( winding, material != NULL ? material : defaultMaterial, color, minx, miny, width, height );
}

void sdDeviceContextLocal::DrawArc(
	float x, float y, float radius, float width,
	int numSides, float percent,
	const idVec4& color, float startAngle
) {
	if ( radius < 1.0f ) {
		return;
	}
	numSides = idMath::ClampInt( 3, 32, numSides );
	percent = idMath::ClampFloat( 0.0f, 1.0f, percent );
	const float start = startAngle * idMath::M_DEG2RAD + idMath::PI;
	idVec2 previous( x + idMath::Sin( start ) * radius, y + idMath::Cos( start ) * radius );
	for ( int i = 1; i < numSides; i++ ) {
		const float fraction = static_cast< float >( i ) / static_cast< float >( numSides - 1 );
		const float angle = start + idMath::TWO_PI * percent * fraction;
		const idVec2 next( x + idMath::Sin( angle ) * radius, y + idMath::Cos( angle ) * radius );
		DrawLine( previous, next, width, color );
		previous = next;
	}
}

void sdDeviceContextLocal::DrawTimer(
	float x, float y, float w, float h,
	float percent, const idVec4& color,
	const idMaterial* material, bool invert,
	const idVec2& st0, const idVec2& st1
) {
	percent = idMath::ClampFloat( 0.0f, 1.0f, percent );
	if ( invert ) {
		percent = 1.0f - percent;
	}
	if ( material == NULL || color.w < MIN_VISIBLE_ALPHA || percent <= 0.0f ) {
		return;
	}

	const int sides = 32;
	idWinding2D winding;
	winding.AddPoint( x, y, ( st0.x + st1.x ) * 0.5f, ( st0.y + st1.y ) * 0.5f );
	for ( int i = 0; i <= sides * percent; i++ ) {
		const float fraction = static_cast< float >( i ) / static_cast< float >( sides );
		const float angle = -idMath::HALF_PI + idMath::TWO_PI * fraction;
		const float c = idMath::Cos( angle );
		const float s = idMath::Sin( angle );
		winding.AddPoint(
			x + c * w,
			y + s * h,
			st0.x + ( c + 1.0f ) * 0.5f * ( st1.x - st0.x ),
			st0.y + ( s + 1.0f ) * 0.5f * ( st1.y - st0.y )
		);
	}
	DrawWindingMaterial( winding, material, color );
}

qhandle_t sdDeviceContextLocal::FindFont( const char* fontName ) {
	if ( fontName == NULL || fontName[ 0 ] == '\0' ) {
		return -1;
	}
	for ( int i = 0; i < recoveredFontNames.Num(); i++ ) {
		if ( !recoveredFontNames[ i ].Icmp( fontName ) ) {
			return i;
		}
	}
	return recoveredFontNames.Append( fontName );
}

void sdDeviceContextLocal::FreeFont( qhandle_t ) {
}

const int sdDeviceContextLocal::GetFontHeight( qhandle_t, int pointSize ) {
	return Max( 0, pointSize );
}

void sdDeviceContextLocal::SetFont( qhandle_t font ) {
	if ( currentFont != font ) {
		currentFont = font;
		guiModel.SetFont( font );
	}
}

void sdDeviceContextLocal::SetFontSize( int pointSize ) {
	if ( currentFontSize != pointSize ) {
		currentFontSize = pointSize;
		guiModel.SetFontSize( pointSize );
	}
}

void sdDeviceContextLocal::DrawText( const wchar_t* text, const sdBounds2D& rect, unsigned int flags ) {
	guiModel.DrawTextA( text, rect, flags );
}

void sdDeviceContextLocal::GetTextDimensions(
	const wchar_t* text,
	const sdBounds2D& rect,
	unsigned int flags,
	qhandle_t,
	int pointSize,
	int& width,
	int& height,
	float* scale,
	int** charAdvances,
	idList< int >* lineBreaks
) {
	width = 0;
	height = Max( 0, pointSize );
	if ( scale != NULL ) {
		*scale = 1.0f;
	}
	if ( charAdvances != NULL ) {
		*charAdvances = NULL;
	}
	if ( lineBreaks != NULL ) {
		lineBreaks->Clear();
	}
	if ( text == NULL ) {
		return;
	}

	const int advance = Max( 1, pointSize / 2 );
	int lineWidth = 0;
	int lines = 1;
	const int maxWidth = Max( 0, static_cast< int >( rect.GetWidth() ) );
	for ( int i = 0; text[ i ] != L'\0'; i++ ) {
		if ( text[ i ] == L'\n' && ( flags & DTF_SINGLELINE ) == 0 ) {
			width = Max( width, lineWidth );
			lineWidth = 0;
			lines++;
			if ( lineBreaks != NULL ) {
				lineBreaks->Append( i );
			}
			continue;
		}
		if ( ( flags & DTF_WORDWRAP ) != 0 && maxWidth > 0 && lineWidth + advance > maxWidth ) {
			width = Max( width, lineWidth );
			lineWidth = 0;
			lines++;
			if ( lineBreaks != NULL ) {
				lineBreaks->Append( i );
			}
		}
		lineWidth += advance;
	}
	width = Max( width, lineWidth );
	height = lines * Max( 0, pointSize );
}

void sdDeviceContextLocal::OverrideAspectRationCorrection( bool setOverride ) {
	overrideAspectRatio = setOverride;
}

float sdDeviceContextLocal::GetAspectRatioCorrection() const {
	if ( overrideAspectRatio || cvarSystem == NULL ) {
		return 1.0f;
	}
	switch ( cvarSystem->GetCVarInteger( "r_aspectRatio" ) ) {
		case -1: {
			const float h = cvarSystem->GetCVarFloat( "r_customAspectRatioH" );
			const float v = cvarSystem->GetCVarFloat( "r_customAspectRatioV" );
			return h != 0.0f ? ( 4.0f / 3.0f ) / ( h / Max( v, 0.001f ) ) : 1.0f;
		}
		case 1: return 0.75f;
		case 2: return 0.83333337f;
		case 3: return 1.0666667f;
		default: return 1.0f;
	}
}

#if defined( _M_IX86 )
static_assert( sizeof( sdDeviceContextLocal ) == 0x5C, "sdDeviceContextLocal must match the Microsoft PDB layout" );
#endif
