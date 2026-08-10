// Copyright (C) 2007 Id Software, Inc.
//
// Backend-neutral implementation of ETQW's public 2D device context.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "DeviceContext.h"

namespace {

class sdDeviceContextBootstrap : public sdDeviceContext {
public:
	sdDeviceContextBootstrap() : color( colorWhite ), colorMultiplier( colorWhite ), aspectCorrection( 1.0f ) {}

	virtual void Reset() {
		color = colorWhite;
		colorMultiplier = colorWhite;
		aspectCorrection = 1.0f;
	}
	virtual void BeginEmitToCurrentView( const float[ 16 ], const int, const bool ) {}
	virtual void BeginEmitFullScreen() {}
	virtual void End() {}
	virtual void SetColor( const idVec4& value ) { color = value; }
	virtual void SetColor( const float r, const float g, const float b, const float a ) { color.Set( r, g, b, a ); }
	virtual idVec4 SetColorMultiplier( const idVec4& value ) {
		const idVec4 previous = colorMultiplier;
		colorMultiplier = value;
		return previous;
	}
	virtual void SetRegister( const int, const float ) {}
	virtual void SetRegisters( const float* ) {}
	virtual void EnableClipping( bool ) {}
	virtual void PushClipRect( const sdBounds2D& ) {}
	virtual void PopClipRect() {}
	virtual void DrawRect( float, float, float, float, float, float, float, float, const idMaterial*, float ) {}
	virtual void DrawClippedRect( float, float, float, float, float, float, float, float, const idMaterial*, float ) {}
	virtual void DrawMaskedClippedRect( float, float, float, float, float, float, float, float, float, float, float, float, const idMaterial*, float ) {}
	virtual void DrawCinematic( float, float, float, float, float, float, float, float, const idMaterial*, idSoundEmitter*, float ) {}
	virtual void DrawClippedWinding( const idWinding2D&, const idMaterial* ) {}
	virtual void DrawClippedWindingMasked( const idWinding2D&, const idMaterial*, float, float, float, float ) {}
	virtual void DrawMaskedMaterial( float, float, float, float, float, float, float, float, const idMaterial*, const idVec4&, float, float, float, float, float ) {}
	virtual void DrawMaterial( float, float, float, float, const idMaterial*, const idVec4&, float, float, float, float, float ) {}
	virtual void DrawMaterial( const idVec4&, const idMaterial*, const idVec4&, const idVec2&, const idVec2&, float ) {}
	virtual void DrawMaterial( const sdBounds2D&, const idMaterial*, const idVec4&, const idVec2&, const idVec2&, float ) {}
	virtual void DrawMaterial( float, float, float, float, const idMaterial*, const idVec4&, const idVec2&, const idVec2& ) {}
	virtual void DrawRotatedMaterial( float, idVec2, idVec2, const idMaterial*, const idVec4& ) {}
	virtual void DrawWindingMaterial( const idWinding2D&, const idMaterial*, const idVec4& ) {}
	virtual void DrawRect( float, float, float, float, const idVec4& ) {}
	virtual void DrawClippedRect( float, float, float, float, const idVec4& ) {}
	virtual void DrawBox( float, float, float, float, float, const idVec4& ) {}
	virtual void DrawClippedBox( float, float, float, float, float, const idVec4& ) {}
	virtual void DrawCircleMaterial( const float, const float, const idVec2&, const int, const idVec4&, const idMaterial*, const idVec4&, float ) {}
	virtual void DrawCircleMaterialMasked( const float, const float, const idVec2&, const int, const idVec4&, const idMaterial*, const idVec4&, float, float, float, float, float ) {}
	virtual void DrawCircle( const float, const float, const idVec2&, const float, const int, const idVec4& ) {}
	virtual void DrawLineMaterial( const idVec2&, const idVec2&, const float, const idMaterial*, const idVec4& ) {}
	virtual void DrawLine( const idVec2&, const idVec2&, const float, const idVec4& ) {}
	virtual void DrawFilledArc( const float, const float, const float, int, float, const idVec4&, float, const idMaterial* ) {}
	virtual void DrawFilledArcMasked( const float, const float, const float, int, float, const idVec4&, float, float, float, float, float, const idMaterial* ) {}
	virtual void DrawArc( const float, const float, const float, const float, const int, const float, const idVec4&, const float ) {}
	virtual void DrawTimer( const float, const float, const float, const float, float, const idVec4&, const idMaterial*, bool, const idVec2&, const idVec2& ) {}
	virtual qhandle_t FindFont( const char* ) { return 0; }
	virtual void FreeFont( const qhandle_t ) {}
	virtual const int GetFontHeight( const qhandle_t, const int pointSize ) { return pointSize; }
	virtual void SetFont( const qhandle_t ) {}
	virtual void SetFontSize( const int ) {}
	virtual void DrawText( const wchar_t*, const sdBounds2D&, unsigned int ) {}
	virtual void GetTextDimensions( const wchar_t* text, const sdBounds2D&, unsigned int, const qhandle_t, const int pointSize, int& width, int& height, float* scale, int** charAdvances, idList< int >* lineBreaks ) {
		width = text != NULL ? static_cast< int >( wcslen( text ) ) * pointSize / 2 : 0;
		height = pointSize;
		if ( scale != NULL ) {
			*scale = 1.0f;
		}
		if ( charAdvances != NULL ) {
			*charAdvances = NULL;
		}
		if ( lineBreaks != NULL ) {
			lineBreaks->Clear();
		}
	}
	virtual void OverrideAspectRationCorrection( bool setOverride ) { aspectCorrection = setOverride ? 1.0f : 1.0f; }
	virtual float GetAspectRatioCorrection() const { return aspectCorrection; }

private:
	idVec4 color;
	idVec4 colorMultiplier;
	float aspectCorrection;
};

sdDeviceContextBootstrap deviceContextBootstrap;

}

sdDeviceContext* deviceContext = &deviceContextBootstrap;
