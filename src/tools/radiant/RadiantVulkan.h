// Copyright (C) 2007 Id Software, Inc.
// Vulkan compatibility entry points for the legacy Radiant drawing code.

#ifndef __RADIANT_VULKAN_H__
#define __RADIANT_VULKAN_H__

// The Radiant shell owns a single Vulkan swapchain. Legacy editor views render
// through the fixed-function bridge into independent sampled images; ImGui only
// composites those completed images into the shell.
bool RadiantVulkanBeginFrame( HWND window, const float clearColor[ 4 ] );
void RadiantVulkanSetEmbeddedRegion( int x, int y, int width, int height );
void RadiantVulkanEndEmbeddedRegion();
void RadiantVulkanReservePrimitiveVertices( int vertexCount );
bool RadiantVulkanDrawIndexedTriangles( const idDrawVert* vertices,
	int vertexCount, const glIndex_t* indices, int indexCount,
	const idVec3& origin, const idMat3& axis, const void* vertexCache,
	const void* indexCache, bool lowRangeTexCoords );
bool RadiantVulkanBeginViewTarget( const void* owner, int width, int height );
void RadiantVulkanEndViewTarget();
void RadiantVulkanEndFrame();

BOOL WINAPI RVWglMakeCurrent( HDC dc, HGLRC context );
BOOL WINAPI RVWglSwapBuffers( HDC dc );
BOOL WINAPI RVWglUseFontBitmaps( HDC dc, DWORD first, DWORD count, DWORD base );
BOOL WINAPI RVWglUseFontOutlines( HDC dc, DWORD first, DWORD count, DWORD base,
	FLOAT deviation, FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics );

void APIENTRY RVGlBegin( GLenum mode );
void APIENTRY RVGlBlendFunc( GLenum source, GLenum destination );
void APIENTRY RVGlCallList( GLuint list );
void APIENTRY RVGlCallLists( GLsizei count, GLenum type, const GLvoid* lists );
void APIENTRY RVGlClear( GLbitfield mask );
void APIENTRY RVGlClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha );
void APIENTRY RVGlColor3f( GLfloat red, GLfloat green, GLfloat blue );
void APIENTRY RVGlColor3fv( const GLfloat* color );
void APIENTRY RVGlColor4f( GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha );
void APIENTRY RVGlColor4fv( const GLfloat* color );
void APIENTRY RVGlColor4ub( GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha );
void APIENTRY RVGlCullFace( GLenum mode );
void APIENTRY RVGlDeleteLists( GLuint list, GLsizei range );
void APIENTRY RVGlDepthFunc( GLenum function );
void APIENTRY RVGlDepthMask( GLboolean enabled );
void APIENTRY RVGlDisable( GLenum capability );
void APIENTRY RVGlEnable( GLenum capability );
void APIENTRY RVGlEnableClientState( GLenum array );
void APIENTRY RVGlEnd();
void APIENTRY RVGlEndList();
void APIENTRY RVGlFinish();
void APIENTRY RVGlFlush();
GLuint APIENTRY RVGlGenLists( GLsizei range );
GLenum APIENTRY RVGlGetError();
void APIENTRY RVGlGetFloatv( GLenum name, GLfloat* values );
void APIENTRY RVGlGetBooleanv( GLenum name, GLboolean* values );
void APIENTRY RVGlGetIntegerv( GLenum name, GLint* values );
GLboolean APIENTRY RVGlIsEnabled( GLenum capability );
const GLubyte* APIENTRY RVGlGetString( GLenum name );
void APIENTRY RVGlLineStipple( GLint factor, GLushort pattern );
void APIENTRY RVGlLineWidth( GLfloat width );
void APIENTRY RVGlListBase( GLuint base );
void APIENTRY RVGlLoadIdentity();
void APIENTRY RVGlLoadMatrixf( const GLfloat* matrix );
void APIENTRY RVGlMatrixMode( GLenum mode );
void APIENTRY RVGlNewList( GLuint list, GLenum mode );
void APIENTRY RVGlOrtho( GLdouble left, GLdouble right, GLdouble bottom,
	GLdouble top, GLdouble nearValue, GLdouble farValue );
void APIENTRY RVGlPointSize( GLfloat size );
void APIENTRY RVGlPolygonMode( GLenum face, GLenum mode );
void APIENTRY RVGlPolygonOffset( GLfloat factor, GLfloat units );
void APIENTRY RVGlPolygonStipple( const GLubyte* mask );
void APIENTRY RVGlPopAttrib();
void APIENTRY RVGlPopMatrix();
void APIENTRY RVGlPushAttrib( GLbitfield mask );
void APIENTRY RVGlPushMatrix();
void APIENTRY RVGlRasterPos2f( GLfloat x, GLfloat y );
void APIENTRY RVGlRasterPos3f( GLfloat x, GLfloat y, GLfloat z );
void APIENTRY RVGlRasterPos3fv( const GLfloat* position );
void APIENTRY RVGlRectf( GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2 );
void APIENTRY RVGlRotatef( GLfloat angle, GLfloat x, GLfloat y, GLfloat z );
void APIENTRY RVGlScissor( GLint x, GLint y, GLsizei width, GLsizei height );
void APIENTRY RVGlShadeModel( GLenum mode );
void APIENTRY RVGlTexCoord2f( GLfloat s, GLfloat t );
void APIENTRY RVGlTexCoord2fv( const GLfloat* coordinate );
void APIENTRY RVGlTranslatef( GLfloat x, GLfloat y, GLfloat z );
void APIENTRY RVGlVertex2f( GLfloat x, GLfloat y );
void APIENTRY RVGlVertex3f( GLfloat x, GLfloat y, GLfloat z );
void APIENTRY RVGlVertex3fv( const GLfloat* vertex );
void APIENTRY RVGlViewport( GLint x, GLint y, GLsizei width, GLsizei height );

#if !defined( ETQW_RADIANT_VULKAN_IMPLEMENTATION )
	#define qwglMakeCurrent RVWglMakeCurrent
	#define qwglSwapBuffers RVWglSwapBuffers
	#define qwglUseFontBitmaps RVWglUseFontBitmaps
	#define qwglUseFontOutlines RVWglUseFontOutlines
	#define qglBegin RVGlBegin
	#define qglBlendFunc RVGlBlendFunc
	#define qglCallList RVGlCallList
	#define qglCallLists RVGlCallLists
	#define qglClear RVGlClear
	#define qglClearColor RVGlClearColor
	#define qglColor3f RVGlColor3f
	#define qglColor3fv RVGlColor3fv
	#define qglColor4f RVGlColor4f
	#define qglColor4fv RVGlColor4fv
	#define qglColor4ub RVGlColor4ub
	#define qglCullFace RVGlCullFace
	#define qglDeleteLists RVGlDeleteLists
	#define qglDepthFunc RVGlDepthFunc
	#define qglDepthMask RVGlDepthMask
	#define qglDisable RVGlDisable
	#define qglEnable RVGlEnable
	#define qglEnableClientState RVGlEnableClientState
	#define qglEnd RVGlEnd
	#define qglEndList RVGlEndList
	#define qglFinish RVGlFinish
	#define qglFlush RVGlFlush
	#define qglGenLists RVGlGenLists
	#define qglGetError RVGlGetError
	#define qglGetFloatv RVGlGetFloatv
	#define qglGetBooleanv RVGlGetBooleanv
	#define qglGetIntegerv RVGlGetIntegerv
	#define qglIsEnabled RVGlIsEnabled
	#define qglGetString RVGlGetString
	#define qglLineStipple RVGlLineStipple
	#define qglLineWidth RVGlLineWidth
	#define qglListBase RVGlListBase
	#define qglLoadIdentity RVGlLoadIdentity
	#define qglLoadMatrixf RVGlLoadMatrixf
	#define qglMatrixMode RVGlMatrixMode
	#define qglNewList RVGlNewList
	#define qglOrtho RVGlOrtho
	#define qglPointSize RVGlPointSize
	#define qglPolygonMode RVGlPolygonMode
	#define qglPolygonOffset RVGlPolygonOffset
	#define qglPolygonStipple RVGlPolygonStipple
	#define qglPopAttrib RVGlPopAttrib
	#define qglPopMatrix RVGlPopMatrix
	#define qglPushAttrib RVGlPushAttrib
	#define qglPushMatrix RVGlPushMatrix
	#define qglRasterPos2f RVGlRasterPos2f
	#define qglRasterPos3f RVGlRasterPos3f
	#define qglRasterPos3fv RVGlRasterPos3fv
	#define qglRectf RVGlRectf
	#define qglRotatef RVGlRotatef
	#define qglScissor RVGlScissor
	#define qglShadeModel RVGlShadeModel
	#define qglTexCoord2f RVGlTexCoord2f
	#define qglTexCoord2fv RVGlTexCoord2fv
	#define qglTranslatef RVGlTranslatef
	#define qglVertex2f RVGlVertex2f
	#define qglVertex3f RVGlVertex3f
	#define qglVertex3fv RVGlVertex3fv
	#define qglViewport RVGlViewport
#endif

#endif
