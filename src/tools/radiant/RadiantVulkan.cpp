// Copyright (C) 2007 Id Software, Inc.
//
// Radiant predates programmable render APIs and draws its native child windows
// with OpenGL fixed-function calls.  This file preserves that source-level API
// while translating the editor's immediate-mode geometry to Vulkan triangle
// lists.  The original qgl entry points remain the fallback for OpenGL mode.

#include "../../framework/precompiled.h"
#pragma hdrstop

#define ETQW_RADIANT_VULKAN_IMPLEMENTATION
#include "RadiantVulkan.h"
#include "../../renderer/VulkanBackend.h"

namespace {

struct rvMatrix_t {
	float m[ 16 ];
};

struct rvInputVertex_t {
	float xyz[ 3 ];
	float st[ 2 ];
	float color[ 4 ];
};

struct rvDrawState_t {
	float color[ 4 ];
	float texCoord[ 2 ];
	float lineWidth;
	float pointSize;
	bool depthTest;
	bool depthWrite;
	bool blend;
	GLenum blendSource;
	GLenum blendDestination;
	bool texture2D;
	bool scissorTest;
	GLenum polygonMode;
	int scissor[ 4 ];
};

struct rvDisplayPrimitive_t {
	GLenum primitive;
	rvDrawState_t draw;
	idList< rvInputVertex_t > vertices;
};

struct rvDisplayList_t {
	GLuint name;
	idList< rvDisplayPrimitive_t > primitives;
};

struct rvFont_t {
	GLuint base;
	DWORD first;
	DWORD count;
	int cellWidth;
	int cellHeight;
	int columns;
	int rows;
};

void MatrixIdentity( rvMatrix_t& matrix ) {
	memset( matrix.m, 0, sizeof( matrix.m ) );
	matrix.m[ 0 ] = matrix.m[ 5 ] = matrix.m[ 10 ] = matrix.m[ 15 ] = 1.0f;
}

rvMatrix_t MatrixMultiply( const rvMatrix_t& left, const rvMatrix_t& right ) {
	rvMatrix_t result;
	for ( int column = 0; column < 4; ++column ) {
		for ( int row = 0; row < 4; ++row ) {
			result.m[ column * 4 + row ] = 0.0f;
			for ( int index = 0; index < 4; ++index ) {
				result.m[ column * 4 + row ] +=
					left.m[ index * 4 + row ] * right.m[ column * 4 + index ];
			}
		}
	}
	return result;
}

struct rvVulkanState_t {
	HWND window;
	int width;
	int height;
	int regionX;
	int regionTop;
	int regionWidth;
	int regionHeight;
	int viewport[ 4 ];
	float clearColor[ 4 ];
	GLenum matrixMode;
	rvMatrix_t modelView;
	rvMatrix_t projection;
	idList< rvMatrix_t > modelViewStack;
	idList< rvMatrix_t > projectionStack;
	rvDrawState_t draw;
	idList< rvDrawState_t > attribStack;
	GLenum primitive;
	idList< rvInputVertex_t > vertices;
	bool compilingList;
	GLuint currentList;
	GLuint nextList;
	GLuint listBase;
	idList< rvDisplayList_t* > displayLists;
	idList< rvFont_t* > fonts;
	rvInputVertex_t rasterPosition;
	bool rasterValid;

	rvVulkanState_t() {
		window = NULL;
		width = height = 0;
		regionX = regionTop = 0;
		regionWidth = regionHeight = 1;
		viewport[ 0 ] = viewport[ 1 ] = 0;
		viewport[ 2 ] = viewport[ 3 ] = 1;
		clearColor[ 0 ] = clearColor[ 1 ] = clearColor[ 2 ] = 0.0f;
		clearColor[ 3 ] = 1.0f;
		matrixMode = GL_MODELVIEW;
		MatrixIdentity( modelView );
		MatrixIdentity( projection );
		draw.color[ 0 ] = draw.color[ 1 ] = draw.color[ 2 ] = draw.color[ 3 ] = 1.0f;
		draw.texCoord[ 0 ] = draw.texCoord[ 1 ] = 0.0f;
		draw.lineWidth = 1.0f;
		draw.pointSize = 1.0f;
		draw.depthTest = false;
		draw.depthWrite = true;
		draw.blend = false;
		draw.blendSource = GL_ONE;
		draw.blendDestination = GL_ZERO;
		draw.texture2D = false;
		draw.scissorTest = false;
		draw.polygonMode = GL_FILL;
		memset( draw.scissor, 0, sizeof( draw.scissor ) );
		primitive = 0;
		compilingList = false;
		currentList = 0;
		nextList = 1;
		listBase = 0;
		memset( &rasterPosition, 0, sizeof( rasterPosition ) );
		rasterValid = false;
	}
};

// The engine tears idLib's allocator down before C++ static destructors run.
// Keep this process-lifetime compatibility state allocated so its idList
// members are not destroyed after the allocator has already shut down.
rvVulkanState_t* rvStateStorage = new rvVulkanState_t;
#define rvState ( *rvStateStorage )

rvDisplayList_t* FindDisplayList( GLuint name ) {
	for ( int i = 0; i < rvState.displayLists.Num(); ++i ) {
		if ( rvState.displayLists[ i ]->name == name ) {
			return rvState.displayLists[ i ];
		}
	}
	return NULL;
}

rvFont_t* FindFontForList( GLuint name ) {
	for ( int i = 0; i < rvState.fonts.Num(); ++i ) {
		if ( name >= rvState.fonts[ i ]->base &&
			name < rvState.fonts[ i ]->base + rvState.fonts[ i ]->count ) {
			return rvState.fonts[ i ];
		}
	}
	return NULL;
}

bool VulkanToolMode() {
	return R_UseVulkanBackend();
}

rvMatrix_t& CurrentMatrix() {
	return rvState.matrixMode == GL_PROJECTION ?
		rvState.projection : rvState.modelView;
}

void PostMultiplyCurrentMatrix( const rvMatrix_t& matrix ) {
	rvMatrix_t& current = CurrentMatrix();
	current = MatrixMultiply( current, matrix );
}

bool EnsureToolFrame() {
	if ( !VulkanToolMode() || rvState.window == NULL ) {
		return false;
	}
	if ( vulkanBackend.IsToolWindowActive() ) {
		return true;
	}
	RECT rect;
	if ( GetClientRect( rvState.window, &rect ) ) {
		rvState.width = rect.right - rect.left;
		rvState.height = rect.bottom - rect.top;
	}
	if ( rvState.width <= 0 || rvState.height <= 0 ) {
		return false;
	}
	if ( rvState.viewport[ 2 ] <= 1 && rvState.viewport[ 3 ] <= 1 ) {
		rvState.viewport[ 2 ] = rvState.width;
		rvState.viewport[ 3 ] = rvState.height;
	}
	const bool began = vulkanBackend.BeginToolWindow( rvState.window,
		rvState.width, rvState.height, rvState.clearColor );
	if ( began && rvState.draw.scissorTest ) {
		const int regionBottom = rvState.height - rvState.regionTop - rvState.regionHeight;
		vulkanBackend.SetToolScissor( rvState.regionX + rvState.draw.scissor[ 0 ],
			regionBottom + rvState.draw.scissor[ 1 ], rvState.draw.scissor[ 2 ],
			rvState.draw.scissor[ 3 ] );
	}
	return began;
}

sdVulkanToolVertex TransformVertex( const rvInputVertex_t& input ) {
	const rvMatrix_t combined = MatrixMultiply( rvState.projection,
		rvState.modelView );
	const float vector[ 4 ] = { input.xyz[ 0 ], input.xyz[ 1 ], input.xyz[ 2 ], 1.0f };
	float clip[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for ( int row = 0; row < 4; ++row ) {
		for ( int column = 0; column < 4; ++column ) {
			clip[ row ] += combined.m[ column * 4 + row ] * vector[ column ];
		}
	}
	const float inverseW = idMath::Fabs( clip[ 3 ] ) > 1e-8f ?
		1.0f / clip[ 3 ] : 1.0f;
	float ndcX = clip[ 0 ] * inverseW;
	float ndcY = clip[ 1 ] * inverseW;
	const float fullWidth = static_cast< float >( Max( rvState.width, 1 ) );
	const float fullHeight = static_cast< float >( Max( rvState.height, 1 ) );
	const float regionBottom = static_cast< float >(
		rvState.height - rvState.regionTop - rvState.regionHeight );
	const float pixelX = rvState.regionX + rvState.viewport[ 0 ] +
		( ndcX + 1.0f ) * 0.5f * rvState.viewport[ 2 ];
	const float pixelY = regionBottom + rvState.viewport[ 1 ] +
		( ndcY + 1.0f ) * 0.5f * rvState.viewport[ 3 ];
	ndcX = pixelX * 2.0f / fullWidth - 1.0f;
	ndcY = pixelY * 2.0f / fullHeight - 1.0f;
	sdVulkanToolVertex output;
	output.x = ndcX;
	output.y = ndcY;
	output.z = clip[ 2 ] * inverseW * 0.5f + 0.5f;
	output.s = input.st[ 0 ];
	output.t = input.st[ 1 ];
	output.r = input.color[ 0 ];
	output.g = input.color[ 1 ];
	output.b = input.color[ 2 ];
	output.a = input.color[ 3 ];
	return output;
}

void AppendTriangle( idList< sdVulkanToolVertex >& output,
	const sdVulkanToolVertex& a, const sdVulkanToolVertex& b,
	const sdVulkanToolVertex& c ) {
	output.Append( a );
	output.Append( b );
	output.Append( c );
}

void AppendLine( idList< sdVulkanToolVertex >& output,
	const sdVulkanToolVertex& a, const sdVulkanToolVertex& b ) {
	const float dxPixels = ( b.x - a.x ) * Max( rvState.width, 1 ) * 0.5f;
	const float dyPixels = ( b.y - a.y ) * Max( rvState.height, 1 ) * 0.5f;
	const float length = idMath::Sqrt( dxPixels * dxPixels + dyPixels * dyPixels );
	if ( length < 1e-5f ) {
		return;
	}
	const float halfWidth = Max( rvState.draw.lineWidth, 1.0f ) * 0.5f;
	const float offsetX = -dyPixels / length * halfWidth * 2.0f /
		Max( rvState.width, 1 );
	const float offsetY = dxPixels / length * halfWidth * 2.0f /
		Max( rvState.height, 1 );
	sdVulkanToolVertex a0 = a;
	sdVulkanToolVertex a1 = a;
	sdVulkanToolVertex b0 = b;
	sdVulkanToolVertex b1 = b;
	a0.x += offsetX; a0.y += offsetY;
	a1.x -= offsetX; a1.y -= offsetY;
	b0.x += offsetX; b0.y += offsetY;
	b1.x -= offsetX; b1.y -= offsetY;
	AppendTriangle( output, a0, a1, b1 );
	AppendTriangle( output, a0, b1, b0 );
}

void AppendPoint( idList< sdVulkanToolVertex >& output,
	const sdVulkanToolVertex& vertex ) {
	const float dx = Max( rvState.draw.pointSize, 1.0f ) /
		Max( rvState.width, 1 );
	const float dy = Max( rvState.draw.pointSize, 1.0f ) /
		Max( rvState.height, 1 );
	sdVulkanToolVertex a = vertex;
	sdVulkanToolVertex b = vertex;
	sdVulkanToolVertex c = vertex;
	sdVulkanToolVertex d = vertex;
	a.x -= dx; a.y -= dy;
	b.x += dx; b.y -= dy;
	c.x += dx; c.y += dy;
	d.x -= dx; d.y += dy;
	AppendTriangle( output, a, b, c );
	AppendTriangle( output, a, c, d );
}

void AppendPolygonEdges( idList< sdVulkanToolVertex >& output,
	const idList< sdVulkanToolVertex >& vertices, int first, int count ) {
	for ( int i = 0; i < count; ++i ) {
		AppendLine( output, vertices[ first + i ],
			vertices[ first + ( i + 1 ) % count ] );
	}
}

void SubmitPrimitive() {
	if ( rvState.vertices.Num() == 0 || rvState.compilingList ||
		!EnsureToolFrame() ) {
		return;
	}
	idList< sdVulkanToolVertex > transformed;
	transformed.SetNum( rvState.vertices.Num() );
	for ( int i = 0; i < rvState.vertices.Num(); ++i ) {
		transformed[ i ] = TransformVertex( rvState.vertices[ i ] );
	}
	idList< sdVulkanToolVertex > triangles;
	const int count = transformed.Num();
	const bool wireframe = rvState.draw.polygonMode == GL_LINE;
	switch ( rvState.primitive ) {
		case GL_POINTS:
			for ( int i = 0; i < count; ++i ) AppendPoint( triangles, transformed[ i ] );
			break;
		case GL_LINES:
			for ( int i = 0; i + 1 < count; i += 2 ) AppendLine( triangles, transformed[ i ], transformed[ i + 1 ] );
			break;
		case GL_LINE_STRIP:
			for ( int i = 0; i + 1 < count; ++i ) AppendLine( triangles, transformed[ i ], transformed[ i + 1 ] );
			break;
		case GL_LINE_LOOP:
			for ( int i = 0; i < count; ++i ) AppendLine( triangles, transformed[ i ], transformed[ ( i + 1 ) % count ] );
			break;
		case GL_TRIANGLES:
			for ( int i = 0; i + 2 < count; i += 3 ) {
				if ( wireframe ) AppendPolygonEdges( triangles, transformed, i, 3 );
				else AppendTriangle( triangles, transformed[ i ], transformed[ i + 1 ], transformed[ i + 2 ] );
			}
			break;
		case GL_TRIANGLE_STRIP:
			for ( int i = 0; i + 2 < count; ++i ) {
				const int a = ( i & 1 ) ? i + 1 : i;
				const int b = ( i & 1 ) ? i : i + 1;
				if ( wireframe ) {
					AppendLine( triangles, transformed[ a ], transformed[ b ] );
					AppendLine( triangles, transformed[ b ], transformed[ i + 2 ] );
					AppendLine( triangles, transformed[ i + 2 ], transformed[ a ] );
				} else AppendTriangle( triangles, transformed[ a ], transformed[ b ], transformed[ i + 2 ] );
			}
			break;
		case GL_QUADS:
			for ( int i = 0; i + 3 < count; i += 4 ) {
				if ( wireframe ) AppendPolygonEdges( triangles, transformed, i, 4 );
				else {
					AppendTriangle( triangles, transformed[ i ], transformed[ i + 1 ], transformed[ i + 2 ] );
					AppendTriangle( triangles, transformed[ i ], transformed[ i + 2 ], transformed[ i + 3 ] );
				}
			}
			break;
		case GL_QUAD_STRIP:
			for ( int i = 0; i + 3 < count; i += 2 ) {
				if ( wireframe ) {
					AppendLine( triangles, transformed[ i ], transformed[ i + 1 ] );
					AppendLine( triangles, transformed[ i + 1 ], transformed[ i + 3 ] );
					AppendLine( triangles, transformed[ i + 3 ], transformed[ i + 2 ] );
					AppendLine( triangles, transformed[ i + 2 ], transformed[ i ] );
				} else {
					AppendTriangle( triangles, transformed[ i ], transformed[ i + 1 ], transformed[ i + 3 ] );
					AppendTriangle( triangles, transformed[ i ], transformed[ i + 3 ], transformed[ i + 2 ] );
				}
			}
			break;
		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
			if ( wireframe ) AppendPolygonEdges( triangles, transformed, 0, count );
			else for ( int i = 1; i + 1 < count; ++i ) AppendTriangle( triangles, transformed[ 0 ], transformed[ i ], transformed[ i + 1 ] );
			break;
		default:
			break;
	}
	if ( triangles.Num() != 0 ) {
		vulkanBackend.DrawToolTriangles( triangles.Begin(), triangles.Num(),
			rvState.draw.depthTest, rvState.draw.blend );
	}
}

void ResetToolStateForWindow( HWND window ) {
	rvState.window = window;
	RECT rect;
	if ( window != NULL && GetClientRect( window, &rect ) ) {
		rvState.width = rect.right - rect.left;
		rvState.height = rect.bottom - rect.top;
		rvState.regionX = rvState.regionTop = 0;
		rvState.regionWidth = Max( rvState.width, 1 );
		rvState.regionHeight = Max( rvState.height, 1 );
		rvState.viewport[ 0 ] = rvState.viewport[ 1 ] = 0;
		rvState.viewport[ 2 ] = Max( rvState.width, 1 );
		rvState.viewport[ 3 ] = Max( rvState.height, 1 );
	}
}

} // namespace

bool RadiantVulkanBeginFrame( HWND window, const float clearColor[ 4 ] ) {
	if ( !VulkanToolMode() || window == NULL ) {
		return false;
	}
	if ( vulkanBackend.IsToolWindowActive() ) {
		vulkanBackend.EndToolWindow();
	}
	ResetToolStateForWindow( window );
	if ( clearColor != NULL ) {
		memcpy( rvState.clearColor, clearColor, sizeof( rvState.clearColor ) );
	}
	rvState.draw.scissorTest = false;
	if ( !EnsureToolFrame() ) {
		return false;
	}
	// A legacy MFC frame can have DPI-virtualized client coordinates even though
	// Vulkan WSI always reports the physical swapchain extent. Keep the fixed-
	// function bridge in the same physical coordinate system as that target.
	int framebufferWidth = 0;
	int framebufferHeight = 0;
	if ( vulkanBackend.GetActiveToolWindowExtent( framebufferWidth,
		framebufferHeight ) ) {
		rvState.width = framebufferWidth;
		rvState.height = framebufferHeight;
		rvState.regionX = rvState.regionTop = 0;
		rvState.regionWidth = framebufferWidth;
		rvState.regionHeight = framebufferHeight;
		rvState.viewport[ 0 ] = rvState.viewport[ 1 ] = 0;
		rvState.viewport[ 2 ] = framebufferWidth;
		rvState.viewport[ 3 ] = framebufferHeight;
	}
	return true;
}

bool RadiantVulkanBeginViewTarget( const void* owner, int width, int height ) {
	if ( !VulkanToolMode() || owner == NULL || width <= 0 || height <= 0 ) {
		return false;
	}
	const float clearColor[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if ( !vulkanBackend.BeginToolRenderTarget( owner, width, height,
		clearColor ) ) {
		return false;
	}
	rvState.width = width;
	rvState.height = height;
	rvState.regionX = rvState.regionTop = 0;
	rvState.regionWidth = width;
	rvState.regionHeight = height;
	rvState.viewport[ 0 ] = rvState.viewport[ 1 ] = 0;
	rvState.viewport[ 2 ] = width;
	rvState.viewport[ 3 ] = height;
	rvState.draw.scissorTest = false;
	memset( rvState.draw.scissor, 0, sizeof( rvState.draw.scissor ) );
	vulkanBackend.SetToolScissor( 0, 0, width, height );
	return true;
}

void RadiantVulkanEndViewTarget() {
	if ( !VulkanToolMode() ) {
		return;
	}
	vulkanBackend.EndToolRenderTarget();
	int width = 0;
	int height = 0;
	if ( vulkanBackend.GetActiveToolWindowExtent( width, height ) ) {
		rvState.width = width;
		rvState.height = height;
		rvState.regionX = rvState.regionTop = 0;
		rvState.regionWidth = width;
		rvState.regionHeight = height;
		rvState.viewport[ 0 ] = rvState.viewport[ 1 ] = 0;
		rvState.viewport[ 2 ] = width;
		rvState.viewport[ 3 ] = height;
		rvState.draw.scissorTest = false;
		memset( rvState.draw.scissor, 0, sizeof( rvState.draw.scissor ) );
	}
}

void RadiantVulkanSetEmbeddedRegion( int x, int y, int width, int height ) {
	if ( !VulkanToolMode() || !vulkanBackend.IsToolWindowActive() ) {
		return;
	}
	rvState.regionX = idMath::ClampInt( 0, Max( rvState.width - 1, 0 ), x );
	rvState.regionTop = idMath::ClampInt( 0, Max( rvState.height - 1, 0 ), y );
	rvState.regionWidth = idMath::ClampInt( 1,
		Max( rvState.width - rvState.regionX, 1 ), width );
	rvState.regionHeight = idMath::ClampInt( 1,
		Max( rvState.height - rvState.regionTop, 1 ), height );
	rvState.viewport[ 0 ] = rvState.viewport[ 1 ] = 0;
	rvState.viewport[ 2 ] = rvState.regionWidth;
	rvState.viewport[ 3 ] = rvState.regionHeight;
	rvState.draw.scissorTest = false;
	memset( rvState.draw.scissor, 0, sizeof( rvState.draw.scissor ) );
	const int regionBottom = rvState.height - rvState.regionTop - rvState.regionHeight;
	vulkanBackend.SetToolScissor( rvState.regionX, regionBottom,
		rvState.regionWidth, rvState.regionHeight );
}

void RadiantVulkanEndEmbeddedRegion() {
	if ( !VulkanToolMode() ) {
		return;
	}
	rvState.regionX = rvState.regionTop = 0;
	rvState.regionWidth = Max( rvState.width, 1 );
	rvState.regionHeight = Max( rvState.height, 1 );
	rvState.viewport[ 0 ] = rvState.viewport[ 1 ] = 0;
	rvState.viewport[ 2 ] = rvState.regionWidth;
	rvState.viewport[ 3 ] = rvState.regionHeight;
	rvState.draw.scissorTest = false;
	if ( vulkanBackend.IsToolWindowActive() ) {
		vulkanBackend.SetToolScissor( 0, 0, rvState.width, rvState.height );
	}
}

void RadiantVulkanEndFrame() {
	if ( VulkanToolMode() && vulkanBackend.IsToolWindowActive() ) {
		vulkanBackend.EndToolWindow();
	}
}

BOOL WINAPI RVWglMakeCurrent( HDC dc, HGLRC context ) {
	if ( !VulkanToolMode() ) {
		return ::qwglMakeCurrent != NULL ? ::qwglMakeCurrent( dc, context ) : FALSE;
	}
	if ( vulkanBackend.IsToolWindowActive() ) {
		vulkanBackend.EndToolWindow();
	}
	ResetToolStateForWindow( dc != NULL ? WindowFromDC( dc ) : NULL );
	return TRUE;
}

BOOL WINAPI RVWglSwapBuffers( HDC dc ) {
	if ( !VulkanToolMode() ) {
		return ::SwapBuffers( dc );
	}
	if ( !vulkanBackend.IsToolWindowActive() ) {
		EnsureToolFrame();
	}
	vulkanBackend.EndToolWindow();
	return TRUE;
}

BOOL WINAPI RVWglUseFontBitmaps( HDC dc, DWORD first, DWORD count, DWORD base ) {
	if ( !VulkanToolMode() ) {
		return ::qwglUseFontBitmaps != NULL ? ::qwglUseFontBitmaps( dc, first, count, base ) : FALSE;
	}
	if ( dc == NULL || count == 0 || !vulkanBackend.IsInitialized() ) {
		return FALSE;
	}
	TEXTMETRICA metrics;
	memset( &metrics, 0, sizeof( metrics ) );
	if ( !GetTextMetricsA( dc, &metrics ) ) {
		return FALSE;
	}
	rvFont_t* font = new rvFont_t;
	font->base = base;
	font->first = first;
	font->count = count;
	font->cellWidth = Max( static_cast< int >( metrics.tmMaxCharWidth ), 4 ) + 2;
	font->cellHeight = Max( static_cast< int >( metrics.tmHeight ), 4 ) + 2;
	font->columns = Min( static_cast< int >( count ), 16 );
	font->rows = ( count + font->columns - 1 ) / font->columns;
	const int atlasWidth = font->cellWidth * font->columns;
	const int atlasHeight = font->cellHeight * font->rows;
	BITMAPINFO bitmapInfo;
	memset( &bitmapInfo, 0, sizeof( bitmapInfo ) );
	bitmapInfo.bmiHeader.biSize = sizeof( bitmapInfo.bmiHeader );
	bitmapInfo.bmiHeader.biWidth = atlasWidth;
	bitmapInfo.bmiHeader.biHeight = -atlasHeight;
	bitmapInfo.bmiHeader.biPlanes = 1;
	bitmapInfo.bmiHeader.biBitCount = 32;
	bitmapInfo.bmiHeader.biCompression = BI_RGB;
	void* dibPixels = NULL;
	HBITMAP bitmap = CreateDIBSection( dc, &bitmapInfo, DIB_RGB_COLORS,
		&dibPixels, NULL, 0 );
	HDC memoryDC = CreateCompatibleDC( dc );
	if ( bitmap == NULL || memoryDC == NULL || dibPixels == NULL ) {
		if ( memoryDC != NULL ) DeleteDC( memoryDC );
		if ( bitmap != NULL ) DeleteObject( bitmap );
		delete font;
		return FALSE;
	}
	HGDIOBJ previousBitmap = SelectObject( memoryDC, bitmap );
	HGDIOBJ sourceFont = GetCurrentObject( dc, OBJ_FONT );
	HGDIOBJ previousFont = sourceFont != NULL ? SelectObject( memoryDC, sourceFont ) : NULL;
	PatBlt( memoryDC, 0, 0, atlasWidth, atlasHeight, BLACKNESS );
	SetBkMode( memoryDC, TRANSPARENT );
	SetTextColor( memoryDC, RGB( 255, 255, 255 ) );
	for ( DWORD characterIndex = 0; characterIndex < count; ++characterIndex ) {
		const char character = static_cast< char >( first + characterIndex );
		const int column = characterIndex % font->columns;
		const int row = characterIndex / font->columns;
		TextOutA( memoryDC, column * font->cellWidth + 1,
			row * font->cellHeight + 1, &character, 1 );
	}
	idList< byte > rgba;
	rgba.SetNum( atlasWidth * atlasHeight * 4 );
	const byte* bgra = static_cast< const byte* >( dibPixels );
	for ( int pixel = 0; pixel < atlasWidth * atlasHeight; ++pixel ) {
		const byte alpha = Max( bgra[ pixel * 4 + 0 ],
			Max( bgra[ pixel * 4 + 1 ], bgra[ pixel * 4 + 2 ] ) );
		rgba[ pixel * 4 + 0 ] = 255;
		rgba[ pixel * 4 + 1 ] = 255;
		rgba[ pixel * 4 + 2 ] = 255;
		rgba[ pixel * 4 + 3 ] = alpha;
	}
	if ( previousFont != NULL ) SelectObject( memoryDC, previousFont );
	SelectObject( memoryDC, previousBitmap );
	DeleteDC( memoryDC );
	DeleteObject( bitmap );
	if ( !vulkanBackend.UploadImage2D( font, rgba.Begin(), atlasWidth,
		atlasHeight, 1, true, false ) ) {
		delete font;
		return FALSE;
	}
	rvState.fonts.Append( font );
	return TRUE;
}

BOOL WINAPI RVWglUseFontOutlines( HDC dc, DWORD first, DWORD count, DWORD base,
	FLOAT deviation, FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics ) {
	if ( !VulkanToolMode() ) {
		return ::qwglUseFontOutlines != NULL ? ::qwglUseFontOutlines( dc, first,
			count, base, deviation, extrusion, format, metrics ) : FALSE;
	}
	if ( metrics != NULL ) {
		memset( metrics, 0, count * sizeof( *metrics ) );
	}
	return RVWglUseFontBitmaps( dc, first, count, base );
}

void APIENTRY RVGlBegin( GLenum mode ) {
	if ( !VulkanToolMode() ) { if ( ::qglBegin != NULL ) ::qglBegin( mode ); return; }
	rvState.primitive = mode;
	rvState.vertices.Clear();
}

void APIENTRY RVGlEnd() {
	if ( !VulkanToolMode() ) { if ( ::qglEnd != NULL ) ::qglEnd(); return; }
	if ( rvState.compilingList ) {
		rvDisplayList_t* list = FindDisplayList( rvState.currentList );
		if ( list != NULL && rvState.vertices.Num() != 0 ) {
			rvDisplayPrimitive_t primitive;
			primitive.primitive = rvState.primitive;
			primitive.draw = rvState.draw;
			primitive.vertices = rvState.vertices;
			list->primitives.Append( primitive );
		}
	} else {
		SubmitPrimitive();
	}
	rvState.vertices.Clear();
	rvState.primitive = 0;
}

void APIENTRY RVGlVertex3f( GLfloat x, GLfloat y, GLfloat z ) {
	if ( !VulkanToolMode() ) { if ( ::qglVertex3f != NULL ) ::qglVertex3f( x, y, z ); return; }
	rvInputVertex_t vertex;
	vertex.xyz[ 0 ] = x; vertex.xyz[ 1 ] = y; vertex.xyz[ 2 ] = z;
	memcpy( vertex.st, rvState.draw.texCoord, sizeof( vertex.st ) );
	memcpy( vertex.color, rvState.draw.color, sizeof( vertex.color ) );
	rvState.vertices.Append( vertex );
}

void APIENTRY RVGlVertex2f( GLfloat x, GLfloat y ) { RVGlVertex3f( x, y, 0.0f ); }
void APIENTRY RVGlVertex3fv( const GLfloat* v ) { if ( v != NULL ) RVGlVertex3f( v[ 0 ], v[ 1 ], v[ 2 ] ); }

void APIENTRY RVGlColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a ) {
	if ( !VulkanToolMode() ) { if ( ::qglColor4f != NULL ) ::qglColor4f( r, g, b, a ); return; }
	rvState.draw.color[ 0 ] = r; rvState.draw.color[ 1 ] = g;
	rvState.draw.color[ 2 ] = b; rvState.draw.color[ 3 ] = a;
}
void APIENTRY RVGlColor3f( GLfloat r, GLfloat g, GLfloat b ) { RVGlColor4f( r, g, b, 1.0f ); }
void APIENTRY RVGlColor3fv( const GLfloat* c ) { if ( c != NULL ) RVGlColor3f( c[ 0 ], c[ 1 ], c[ 2 ] ); }
void APIENTRY RVGlColor4fv( const GLfloat* c ) { if ( c != NULL ) RVGlColor4f( c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ] ); }
void APIENTRY RVGlColor4ub( GLubyte r, GLubyte g, GLubyte b, GLubyte a ) { RVGlColor4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f ); }

void APIENTRY RVGlTexCoord2f( GLfloat s, GLfloat t ) {
	if ( !VulkanToolMode() ) { if ( ::qglTexCoord2f != NULL ) ::qglTexCoord2f( s, t ); return; }
	rvState.draw.texCoord[ 0 ] = s; rvState.draw.texCoord[ 1 ] = t;
}
void APIENTRY RVGlTexCoord2fv( const GLfloat* st ) { if ( st != NULL ) RVGlTexCoord2f( st[ 0 ], st[ 1 ] ); }

void APIENTRY RVGlClearColor( GLclampf r, GLclampf g, GLclampf b, GLclampf a ) {
	if ( !VulkanToolMode() ) { if ( ::qglClearColor != NULL ) ::qglClearColor( r, g, b, a ); return; }
	rvState.clearColor[ 0 ] = r; rvState.clearColor[ 1 ] = g;
	rvState.clearColor[ 2 ] = b; rvState.clearColor[ 3 ] = a;
}
void APIENTRY RVGlClear( GLbitfield mask ) {
	if ( !VulkanToolMode() ) { if ( ::qglClear != NULL ) ::qglClear( mask ); return; }
	if ( EnsureToolFrame() ) {
		const int regionBottom = rvState.height - rvState.regionTop - rvState.regionHeight;
		vulkanBackend.SetToolScissor( rvState.regionX, regionBottom,
			rvState.regionWidth, rvState.regionHeight );
		vulkanBackend.ClearToolRegion( rvState.clearColor,
			( mask & GL_COLOR_BUFFER_BIT ) != 0,
			( mask & GL_DEPTH_BUFFER_BIT ) != 0 );
	}
}

void APIENTRY RVGlMatrixMode( GLenum mode ) {
	if ( !VulkanToolMode() ) { if ( ::qglMatrixMode != NULL ) ::qglMatrixMode( mode ); return; }
	if ( mode == GL_MODELVIEW || mode == GL_PROJECTION ) rvState.matrixMode = mode;
}
void APIENTRY RVGlLoadIdentity() {
	if ( !VulkanToolMode() ) { if ( ::qglLoadIdentity != NULL ) ::qglLoadIdentity(); return; }
	MatrixIdentity( CurrentMatrix() );
}
void APIENTRY RVGlLoadMatrixf( const GLfloat* matrix ) {
	if ( !VulkanToolMode() ) { if ( ::qglLoadMatrixf != NULL ) ::qglLoadMatrixf( matrix ); return; }
	if ( matrix != NULL ) memcpy( CurrentMatrix().m, matrix, sizeof( CurrentMatrix().m ) );
}
void APIENTRY RVGlPushMatrix() {
	if ( !VulkanToolMode() ) { if ( ::qglPushMatrix != NULL ) ::qglPushMatrix(); return; }
	if ( rvState.matrixMode == GL_PROJECTION ) rvState.projectionStack.Append( rvState.projection );
	else rvState.modelViewStack.Append( rvState.modelView );
}
void APIENTRY RVGlPopMatrix() {
	if ( !VulkanToolMode() ) { if ( ::qglPopMatrix != NULL ) ::qglPopMatrix(); return; }
	idList< rvMatrix_t >& stack = rvState.matrixMode == GL_PROJECTION ? rvState.projectionStack : rvState.modelViewStack;
	if ( stack.Num() != 0 ) { CurrentMatrix() = stack[ stack.Num() - 1 ]; stack.SetNum( stack.Num() - 1 ); }
}
void APIENTRY RVGlTranslatef( GLfloat x, GLfloat y, GLfloat z ) {
	if ( !VulkanToolMode() ) { if ( ::qglTranslatef != NULL ) ::qglTranslatef( x, y, z ); return; }
	rvMatrix_t matrix; MatrixIdentity( matrix );
	matrix.m[ 12 ] = x; matrix.m[ 13 ] = y; matrix.m[ 14 ] = z;
	PostMultiplyCurrentMatrix( matrix );
}
void APIENTRY RVGlRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z ) {
	if ( !VulkanToolMode() ) { if ( ::qglRotatef != NULL ) ::qglRotatef( angle, x, y, z ); return; }
	const float length = idMath::Sqrt( x * x + y * y + z * z );
	if ( length <= 1e-8f ) return;
	x /= length; y /= length; z /= length;
	const float radians = DEG2RAD( angle );
	const float c = idMath::Cos( radians );
	const float s = idMath::Sin( radians );
	const float oneMinusC = 1.0f - c;
	rvMatrix_t matrix; MatrixIdentity( matrix );
	matrix.m[ 0 ] = x * x * oneMinusC + c;
	matrix.m[ 1 ] = y * x * oneMinusC + z * s;
	matrix.m[ 2 ] = x * z * oneMinusC - y * s;
	matrix.m[ 4 ] = x * y * oneMinusC - z * s;
	matrix.m[ 5 ] = y * y * oneMinusC + c;
	matrix.m[ 6 ] = y * z * oneMinusC + x * s;
	matrix.m[ 8 ] = x * z * oneMinusC + y * s;
	matrix.m[ 9 ] = y * z * oneMinusC - x * s;
	matrix.m[ 10 ] = z * z * oneMinusC + c;
	PostMultiplyCurrentMatrix( matrix );
}
void APIENTRY RVGlOrtho( GLdouble left, GLdouble right, GLdouble bottom,
	GLdouble top, GLdouble nearValue, GLdouble farValue ) {
	if ( !VulkanToolMode() ) { if ( ::qglOrtho != NULL ) ::qglOrtho( left, right, bottom, top, nearValue, farValue ); return; }
	rvMatrix_t matrix; MatrixIdentity( matrix );
	matrix.m[ 0 ] = static_cast< float >( 2.0 / ( right - left ) );
	matrix.m[ 5 ] = static_cast< float >( 2.0 / ( top - bottom ) );
	matrix.m[ 10 ] = static_cast< float >( -2.0 / ( farValue - nearValue ) );
	matrix.m[ 12 ] = static_cast< float >( -( right + left ) / ( right - left ) );
	matrix.m[ 13 ] = static_cast< float >( -( top + bottom ) / ( top - bottom ) );
	matrix.m[ 14 ] = static_cast< float >( -( farValue + nearValue ) / ( farValue - nearValue ) );
	PostMultiplyCurrentMatrix( matrix );
}

void APIENTRY RVGlViewport( GLint x, GLint y, GLsizei width, GLsizei height ) {
	if ( !VulkanToolMode() ) { if ( ::qglViewport != NULL ) ::qglViewport( x, y, width, height ); return; }
	rvState.viewport[ 0 ] = x; rvState.viewport[ 1 ] = y;
	rvState.viewport[ 2 ] = width; rvState.viewport[ 3 ] = height;
}
void APIENTRY RVGlScissor( GLint x, GLint y, GLsizei width, GLsizei height ) {
	if ( !VulkanToolMode() ) { if ( ::qglScissor != NULL ) ::qglScissor( x, y, width, height ); return; }
	rvState.draw.scissor[ 0 ] = x; rvState.draw.scissor[ 1 ] = y;
	rvState.draw.scissor[ 2 ] = width; rvState.draw.scissor[ 3 ] = height;
	if ( rvState.draw.scissorTest && vulkanBackend.IsToolWindowActive() ) {
		const int regionBottom = rvState.height - rvState.regionTop - rvState.regionHeight;
		vulkanBackend.SetToolScissor( rvState.regionX + x, regionBottom + y,
			width, height );
	}
}

void APIENTRY RVGlEnable( GLenum capability ) {
	if ( !VulkanToolMode() ) { if ( ::qglEnable != NULL ) ::qglEnable( capability ); return; }
	if ( capability == GL_DEPTH_TEST ) rvState.draw.depthTest = true;
	else if ( capability == GL_BLEND ) rvState.draw.blend = true;
	else if ( capability == GL_TEXTURE_2D ) rvState.draw.texture2D = true;
	else if ( capability == GL_SCISSOR_TEST ) { rvState.draw.scissorTest = true; if ( vulkanBackend.IsToolWindowActive() ) RVGlScissor( rvState.draw.scissor[ 0 ], rvState.draw.scissor[ 1 ], rvState.draw.scissor[ 2 ], rvState.draw.scissor[ 3 ] ); }
}
void APIENTRY RVGlDisable( GLenum capability ) {
	if ( !VulkanToolMode() ) { if ( ::qglDisable != NULL ) ::qglDisable( capability ); return; }
	if ( capability == GL_DEPTH_TEST ) rvState.draw.depthTest = false;
	else if ( capability == GL_BLEND ) rvState.draw.blend = false;
	else if ( capability == GL_TEXTURE_2D ) { rvState.draw.texture2D = false; vulkanBackend.SetToolImage( NULL ); }
	else if ( capability == GL_SCISSOR_TEST ) {
		rvState.draw.scissorTest = false;
		if ( vulkanBackend.IsToolWindowActive() ) {
			const int regionBottom = rvState.height - rvState.regionTop - rvState.regionHeight;
			vulkanBackend.SetToolScissor( rvState.regionX, regionBottom,
				rvState.regionWidth, rvState.regionHeight );
		}
	}
}

void APIENTRY RVGlLineWidth( GLfloat width ) { if ( !VulkanToolMode() ) { if ( ::qglLineWidth != NULL ) ::qglLineWidth( width ); } else rvState.draw.lineWidth = width; }
void APIENTRY RVGlPointSize( GLfloat size ) { if ( !VulkanToolMode() ) { if ( ::qglPointSize != NULL ) ::qglPointSize( size ); } else rvState.draw.pointSize = size; }
void APIENTRY RVGlPolygonMode( GLenum face, GLenum mode ) { if ( !VulkanToolMode() ) { if ( ::qglPolygonMode != NULL ) ::qglPolygonMode( face, mode ); } else rvState.draw.polygonMode = mode; }
void APIENTRY RVGlPushAttrib( GLbitfield mask ) { if ( !VulkanToolMode() ) { if ( ::qglPushAttrib != NULL ) ::qglPushAttrib( mask ); } else rvState.attribStack.Append( rvState.draw ); }
void APIENTRY RVGlPopAttrib() { if ( !VulkanToolMode() ) { if ( ::qglPopAttrib != NULL ) ::qglPopAttrib(); } else if ( rvState.attribStack.Num() != 0 ) { rvState.draw = rvState.attribStack[ rvState.attribStack.Num() - 1 ]; rvState.attribStack.SetNum( rvState.attribStack.Num() - 1 ); } }

void APIENTRY RVGlRectf( GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2 ) { RVGlBegin( GL_QUADS ); RVGlVertex2f( x1, y1 ); RVGlVertex2f( x2, y1 ); RVGlVertex2f( x2, y2 ); RVGlVertex2f( x1, y2 ); RVGlEnd(); }
void APIENTRY RVGlRasterPos2f( GLfloat x, GLfloat y ) { RVGlRasterPos3f( x, y, 0.0f ); }
void APIENTRY RVGlRasterPos3f( GLfloat x, GLfloat y, GLfloat z ) {
	if ( !VulkanToolMode() ) { if ( ::qglRasterPos3f != NULL ) ::qglRasterPos3f( x, y, z ); return; }
	rvState.rasterPosition.xyz[ 0 ] = x;
	rvState.rasterPosition.xyz[ 1 ] = y;
	rvState.rasterPosition.xyz[ 2 ] = z;
	memcpy( rvState.rasterPosition.color, rvState.draw.color,
		sizeof( rvState.rasterPosition.color ) );
	rvState.rasterValid = true;
}
void APIENTRY RVGlRasterPos3fv( const GLfloat* position ) { if ( position != NULL ) RVGlRasterPos3f( position[ 0 ], position[ 1 ], position[ 2 ] ); }

GLuint APIENTRY RVGlGenLists( GLsizei range ) { if ( !VulkanToolMode() ) return ::qglGenLists != NULL ? ::qglGenLists( range ) : 0; const GLuint result = rvState.nextList; rvState.nextList += Max( range, 1 ); return result; }
void APIENTRY RVGlNewList( GLuint list, GLenum mode ) {
	if ( !VulkanToolMode() ) { if ( ::qglNewList != NULL ) ::qglNewList( list, mode ); return; }
	rvDisplayList_t* displayList = FindDisplayList( list );
	if ( displayList == NULL ) {
		displayList = new rvDisplayList_t;
		displayList->name = list;
		rvState.displayLists.Append( displayList );
	} else {
		displayList->primitives.Clear();
	}
	rvState.currentList = list;
	rvState.compilingList = true;
}
void APIENTRY RVGlEndList() {
	if ( !VulkanToolMode() ) { if ( ::qglEndList != NULL ) ::qglEndList(); return; }
	rvState.compilingList = false;
	rvState.currentList = 0;
}
void APIENTRY RVGlCallList( GLuint list ) {
	if ( !VulkanToolMode() ) { if ( ::qglCallList != NULL ) ::qglCallList( list ); return; }
	rvDisplayList_t* displayList = FindDisplayList( list );
	if ( displayList == NULL ) return;
	const rvDrawState_t savedDraw = rvState.draw;
	const GLenum savedPrimitive = rvState.primitive;
	idList< rvInputVertex_t > savedVertices = rvState.vertices;
	for ( int i = 0; i < displayList->primitives.Num(); ++i ) {
		rvState.draw = displayList->primitives[ i ].draw;
		rvState.primitive = displayList->primitives[ i ].primitive;
		rvState.vertices = displayList->primitives[ i ].vertices;
		SubmitPrimitive();
	}
	rvState.draw = savedDraw;
	rvState.primitive = savedPrimitive;
	rvState.vertices = savedVertices;
}
void APIENTRY RVGlCallLists( GLsizei count, GLenum type, const GLvoid* lists ) {
	if ( !VulkanToolMode() ) { if ( ::qglCallLists != NULL ) ::qglCallLists( count, type, lists ); return; }
	if ( count <= 0 || lists == NULL || !rvState.rasterValid ||
		( type != GL_UNSIGNED_BYTE && type != GL_BYTE ) || !EnsureToolFrame() ) return;
	const byte* characters = static_cast< const byte* >( lists );
	sdVulkanToolVertex cursor = TransformVertex( rvState.rasterPosition );
	for ( int characterIndex = 0; characterIndex < count; ++characterIndex ) {
		const GLuint listName = rvState.listBase + characters[ characterIndex ];
		rvFont_t* font = FindFontForList( listName );
		if ( font == NULL ) continue;
		const int glyph = listName - font->base;
		const int column = glyph % font->columns;
		const int row = glyph / font->columns;
		const float xAdvance = font->cellWidth * 2.0f / Max( rvState.width, 1 );
		const float yAdvance = font->cellHeight * 2.0f / Max( rvState.height, 1 );
		const float s0 = static_cast< float >( column ) / font->columns;
		const float t0 = static_cast< float >( row ) / font->rows;
		const float s1 = static_cast< float >( column + 1 ) / font->columns;
		const float t1 = static_cast< float >( row + 1 ) / font->rows;
		sdVulkanToolVertex vertices[ 6 ];
		for ( int vertex = 0; vertex < 6; ++vertex ) vertices[ vertex ] = cursor;
		vertices[ 0 ].s = s0; vertices[ 0 ].t = t1;
		vertices[ 1 ].x += xAdvance; vertices[ 1 ].s = s1; vertices[ 1 ].t = t1;
		vertices[ 2 ].x += xAdvance; vertices[ 2 ].y += yAdvance; vertices[ 2 ].s = s1; vertices[ 2 ].t = t0;
		vertices[ 3 ] = vertices[ 0 ];
		vertices[ 4 ] = vertices[ 2 ];
		vertices[ 5 ].y += yAdvance; vertices[ 5 ].s = s0; vertices[ 5 ].t = t0;
		vulkanBackend.SetToolImage( font );
		vulkanBackend.DrawToolTriangles( vertices, 6, false, true );
		cursor.x += xAdvance;
	}
	vulkanBackend.SetToolImage( NULL );
}
void APIENTRY RVGlDeleteLists( GLuint list, GLsizei range ) {
	if ( !VulkanToolMode() ) { if ( ::qglDeleteLists != NULL ) ::qglDeleteLists( list, range ); return; }
	for ( int i = rvState.displayLists.Num() - 1; i >= 0; --i ) {
		if ( rvState.displayLists[ i ]->name >= list &&
			rvState.displayLists[ i ]->name < list + static_cast< GLuint >( range ) ) {
			delete rvState.displayLists[ i ];
			rvState.displayLists.RemoveIndex( i );
		}
	}
}
void APIENTRY RVGlListBase( GLuint base ) { if ( !VulkanToolMode() ) { if ( ::qglListBase != NULL ) ::qglListBase( base ); } else rvState.listBase = base; }

void APIENTRY RVGlGetFloatv( GLenum name, GLfloat* values ) {
	if ( !VulkanToolMode() ) { if ( ::qglGetFloatv != NULL ) ::qglGetFloatv( name, values ); return; }
	if ( values == NULL ) return;
	if ( name == GL_CURRENT_COLOR ) memcpy( values, rvState.draw.color, sizeof( rvState.draw.color ) );
	else if ( name == GL_MODELVIEW_MATRIX ) memcpy( values, rvState.modelView.m, sizeof( rvState.modelView.m ) );
	else if ( name == GL_PROJECTION_MATRIX ) memcpy( values, rvState.projection.m, sizeof( rvState.projection.m ) );
	else if ( name == GL_LINE_WIDTH ) *values = rvState.draw.lineWidth;
	else if ( name == GL_POINT_SIZE ) *values = rvState.draw.pointSize;
}
void APIENTRY RVGlGetBooleanv( GLenum name, GLboolean* values ) {
	if ( !VulkanToolMode() ) { ::glGetBooleanv( name, values ); return; }
	if ( values == NULL ) return;
	if ( name == GL_DEPTH_WRITEMASK ) *values = rvState.draw.depthWrite ? GL_TRUE : GL_FALSE;
	else *values = GL_FALSE;
}
void APIENTRY RVGlGetIntegerv( GLenum name, GLint* values ) {
	if ( !VulkanToolMode() ) { ::glGetIntegerv( name, values ); return; }
	if ( values == NULL ) return;
	if ( name == GL_BLEND_SRC ) *values = static_cast< GLint >( rvState.draw.blendSource );
	else if ( name == GL_BLEND_DST ) *values = static_cast< GLint >( rvState.draw.blendDestination );
	else *values = 0;
}
GLboolean APIENTRY RVGlIsEnabled( GLenum capability ) {
	if ( !VulkanToolMode() ) return ::glIsEnabled( capability );
	if ( capability == GL_DEPTH_TEST ) return rvState.draw.depthTest ? GL_TRUE : GL_FALSE;
	if ( capability == GL_BLEND ) return rvState.draw.blend ? GL_TRUE : GL_FALSE;
	if ( capability == GL_TEXTURE_2D ) return rvState.draw.texture2D ? GL_TRUE : GL_FALSE;
	if ( capability == GL_SCISSOR_TEST ) return rvState.draw.scissorTest ? GL_TRUE : GL_FALSE;
	return GL_FALSE;
}
GLenum APIENTRY RVGlGetError() { return VulkanToolMode() ? GL_NO_ERROR : ( ::qglGetError != NULL ? ::qglGetError() : GL_NO_ERROR ); }
const GLubyte* APIENTRY RVGlGetString( GLenum name ) {
	if ( !VulkanToolMode() ) return ::qglGetString != NULL ? ::qglGetString( name ) : NULL;
	if ( name == GL_VENDOR ) return reinterpret_cast< const GLubyte* >( "ETQW Vulkan" );
	if ( name == GL_RENDERER ) return reinterpret_cast< const GLubyte* >( vulkanBackend.GetDeviceName() );
	if ( name == GL_VERSION ) return reinterpret_cast< const GLubyte* >( "Radiant Vulkan compatibility 1.0" );
	return reinterpret_cast< const GLubyte* >( "" );
}

void APIENTRY RVGlBlendFunc( GLenum source, GLenum destination ) {
	if ( !VulkanToolMode() ) { if ( ::qglBlendFunc != NULL ) ::qglBlendFunc( source, destination ); return; }
	rvState.draw.blendSource = source;
	rvState.draw.blendDestination = destination;
}
void APIENTRY RVGlCullFace( GLenum mode ) { if ( !VulkanToolMode() && ::qglCullFace != NULL ) ::qglCullFace( mode ); }
void APIENTRY RVGlDepthFunc( GLenum function ) { if ( !VulkanToolMode() && ::qglDepthFunc != NULL ) ::qglDepthFunc( function ); }
void APIENTRY RVGlDepthMask( GLboolean enabled ) {
	if ( !VulkanToolMode() ) { if ( ::qglDepthMask != NULL ) ::qglDepthMask( enabled ); return; }
	rvState.draw.depthWrite = enabled != GL_FALSE;
}
void APIENTRY RVGlEnableClientState( GLenum array ) { if ( !VulkanToolMode() && ::qglEnableClientState != NULL ) ::qglEnableClientState( array ); }
void APIENTRY RVGlFinish() { if ( !VulkanToolMode() && ::qglFinish != NULL ) ::qglFinish(); }
void APIENTRY RVGlFlush() { if ( !VulkanToolMode() && ::qglFlush != NULL ) ::qglFlush(); }
void APIENTRY RVGlLineStipple( GLint factor, GLushort pattern ) { if ( !VulkanToolMode() && ::qglLineStipple != NULL ) ::qglLineStipple( factor, pattern ); }
void APIENTRY RVGlPolygonOffset( GLfloat factor, GLfloat units ) { if ( !VulkanToolMode() && ::qglPolygonOffset != NULL ) ::qglPolygonOffset( factor, units ); }
void APIENTRY RVGlPolygonStipple( const GLubyte* mask ) { if ( !VulkanToolMode() && ::qglPolygonStipple != NULL ) ::qglPolygonStipple( mask ); }
void APIENTRY RVGlShadeModel( GLenum mode ) { if ( !VulkanToolMode() && ::qglShadeModel != NULL ) ::qglShadeModel( mode ); }
