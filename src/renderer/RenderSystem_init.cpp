// Copyright (C) 2007 Id Software, Inc.
//
// ETQW renderer startup reconstructed from:
//   quakewars-hexrays/renderer/RenderSystem_init.cpp
//   the Microsoft ETQW PDB type and compiland records.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RendererTypesImpl.h"
#include "RenderSystemBackend.h"
#include "RuntimeSpirvCompiler.h"
#include "VulkanBackend.h"
#include "image_processor.h"
#include "image_resampler.h"
#include "renderbindings.h"
#include "tr_render.h"
#include "Image.h"
#include "ModelManager.h"
#include "VertexCache.h"
#include "megatexture/MegaTexture.h"
#include "megatexture/MegaTextureTileLoader.h"
#include "megatexture/MegaTextureTileDecompressor.h"
#include "../idlib/Singleton.h"
#include "../libs/qglLib/qgl.h"
#include "../libs/qglLib/qcg.h"
#include "../sys/sys_render.h"

#include <GL/gl.h>

glconfig_t glConfig;

namespace {
const char* renderAPIValues[] = { "opengl", "vulkan", NULL };
}

idCVar r_renderAPI(
	"r_renderAPI",
	"opengl",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_NOCHEAT,
	"renderer backend; changes take effect after a renderer restart",
	renderAPIValues
);

idCVar r_lightScale( "r_lightScale", "2", CVAR_RENDERER | CVAR_FLOAT, "all light intensities are multiplied by this" );
idCVar r_elevateForceClear( "r_elevateForceClear", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "" );
idCVar r_showOverDraw(
	"r_showOverDraw", "0", CVAR_RENDERER | CVAR_INTEGER,
	"1 = geometry overdraw, 2 = light interaction overdraw, 3 = geometry and light interaction overdraw", 0, 3
);
idCVar r_ignore( "r_ignore", "0", CVAR_RENDERER, "used for random debugging without defining new vars" );
idCVar r_forceGLFinish( "r_forceGLFinish", "0", CVAR_RENDERER | CVAR_INTEGER, "force finish within backend" );

idCVar r_mode( "r_mode", "12", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "video mode number" );
idCVar r_customWidth( "r_customWidth", "1280", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "custom video mode width" );
idCVar r_customHeight( "r_customHeight", "720", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "custom video mode height" );
idCVar r_fullscreen( "r_fullscreen", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use a fullscreen window" );
idCVar r_displayRefresh( "r_displayRefresh", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "optional display refresh rate" );
idCVar r_multiSamples( "r_multiSamples", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "multisample anti-aliasing sample count" );
idCVar r_znear( "r_znear", "3", CVAR_RENDERER | CVAR_FLOAT, "near Z clip plane distance", 0.001f, 200.0f );
idCVar r_jitter( "r_jitter", "0", CVAR_RENDERER | CVAR_BOOL, "randomly subpixel jitter the projection matrix" );
idCVar r_useCulling( "r_useCulling", "2", CVAR_RENDERER | CVAR_INTEGER, "0 = none, 1 = sphere, 2 = sphere + box", 0, 2 );

idCVar r_glDriverVendor(
	"r_glDriverVendor",
	"unknown",
	CVAR_RENDERER,
	"OpenGL driver vendor"
);

idCVar r_inhibitGLSL(
	"r_inhibitGLSL",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"disable the GLSL renderer path"
);

idCVar r_inhibitEXTGPP(
	"r_inhibitEXTGPP",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"disable GL_EXT_gpu_program_parameters"
);

// Retail ETQW renderer cvars used directly by the main-menu GUI while the
// game DLL is loading.
idCVar r_softParticles(
	"r_softParticles",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"Enable soft particles"
);

idCVar r_megaDrawMethod(
	"r_megaDrawMethod",
	"3",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	""
);

idCVar r_swapInterval(
	"r_swapInterval",
	"0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"changes wglSwapInterval"
);

idCVar r_gamma(
	"r_gamma",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"changes gamma tables",
	0.5f,
	3.0f
);

idCVar r_brightness(
	"r_brightness",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"changes gamma tables",
	0.5f,
	2.0f
);

idCVar r_shadows(
	"r_shadows",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"enable shadows"
);

idCVar r_useAlphaToCoverage(
	"r_useAlphaToCoverage",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"Use alpha to coverage."
);

idCVar r_useStateCaching(
	"r_useStateCaching",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"avoid redundant state changes in GL_*() calls"
);

idCVar r_useConstantMaterials(
	"r_useConstantMaterials",
	"1",
	CVAR_RENDERER | CVAR_BOOL,
	"use pre-calculated material registers if possible"
);

idCVar r_ambientScale(
	"r_ambientScale",
	"1.0",
	CVAR_RENDERER | CVAR_FLOAT,
	"ambient cube mapping brightness"
);

// These renderer controls are owned by RenderSystem_init.obj in the retail
// PDB.  The draw_* compilands consume them but do not instantiate them.
idCVar r_shaderQuality( "r_shaderQuality", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "Sets the level of detail to use for shaders, 0 = highest" );
idCVar r_megatexturePreferALU( "r_megatexturePreferALU", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "Prefer ALU instructions in megatexture shaders" );
idCVar r_shaderPreferALU( "r_shaderPreferALU", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "Use ALU instructions instead of textures in shaders." );
idCVar r_normalizeNormalMaps( "r_normalizeNormalMaps", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "Normalize normalmaps after lookup." );
idCVar r_shaderSkipSpecCubeMaps( "r_shaderSkipSpecCubeMaps", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "Use specular cube maps." );
idCVar r_skipSpecular( "r_skipSpecular", "0", CVAR_RENDERER | CVAR_BOOL, "use black for specular" );
idCVar r_skipBump( "r_skipBump", "0", CVAR_RENDERER | CVAR_BOOL, "uses a flat surface instead of the bump map" );
idCVar r_skipDiffuse( "r_skipDiffuse", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = use black for diffuse, 2 = use white for diffuse", 0.0f, 2.0f );
idCVar r_skipAmbient( "r_skipAmbient", "0", CVAR_RENDERER | CVAR_BOOL, "skip non-light dependent drawing" );
idCVar r_skipInteractions( "r_skipInteractions", "0", CVAR_RENDERER | CVAR_INTEGER, "skip light interactions" );
idCVar r_skipTranslucent( "r_skipTranslucent", "0", CVAR_RENDERER | CVAR_BOOL, "skip translucent interactions" );
idCVar r_skipFogLights( "r_skipFogLights", "0", CVAR_RENDERER | CVAR_BOOL, "skip fog and blend lights" );
idCVar r_skipWaterFogLights( "r_skipWaterFogLights", "1", CVAR_RENDERER | CVAR_BOOL, "temporarily skip water fog lights" );
idCVar r_skipAtmosphere( "r_skipAtmosphere", "0", CVAR_RENDERER | CVAR_BOOL, "skips atmosphere pass" );
idCVar r_skipAtmosInteractions( "r_skipAtmosInteractions", "0", CVAR_RENDERER | CVAR_INTEGER, "skip all atmosphere light/surface interaction drawing" );
idCVar r_skipRefractCopy( "r_skipRefractCopy", "0", CVAR_RENDERER | CVAR_BOOL, "uses copy of frame buffer" );
idCVar r_skipDynamicTextures( "r_skipDynamicTextures", "0", CVAR_RENDERER | CVAR_BOOL, "don't dynamically create textures" );
idCVar r_useScissor( "r_useScissor", "1", CVAR_RENDERER | CVAR_BOOL, "scissor clip as portals and lights are processed" );
idCVar r_useMinimalGuiDraw( "r_useMinimalGuiDraw", "1", CVAR_RENDERER | CVAR_BOOL, "use minimal draw for guis" );
idCVar r_offsetFactor( "r_offsetfactor", "0", CVAR_RENDERER | CVAR_FLOAT, "polygon offset parameter" );
idCVar r_offsetUnits( "r_offsetunits", "-600", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "polygon offset parameter", -2000.0f, 2000.0f );

idCVar r_singleTriangle( "r_singleTriangle", "0", CVAR_RENDERER | CVAR_INTEGER, "only draw a single triangle per primitive" );
idCVar r_useVertexBuffers( "r_useVertexBuffers", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use ARB_vertex_buffer_object for vertexes" );
idCVar r_useIndexBuffers( "r_useIndexBuffers", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use ARB_vertex_buffer_object for indexes" );
idCVar r_useTwoSidedStencil( "r_useTwoSidedStencil", "1", CVAR_RENDERER | CVAR_BOOL, "do stencil shadows in one pass with different ops on each side" );
idCVar r_useExternalShadows( "r_useExternalShadows", "1", CVAR_RENDERER | CVAR_INTEGER, "1 = skip drawing caps when outside the light volume, 2 = force to no caps for testing", 0.0f, 2.0f );
idCVar r_useDepthBoundsTest( "r_useDepthBoundsTest", "1", CVAR_RENDERER | CVAR_BOOL, "use depth bounds test to reduce shadow fill" );
idCVar r_useDitherMask( "r_useDitherMask", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "Dither out fading geometry" );
idCVar r_useShadowDitherMask( "r_useShadowDitherMask", "0", CVAR_RENDERER | CVAR_BOOL, "Dither out fading shadows" );
idCVar r_useSampleCoverage( "r_useSampleCoverage", "1", CVAR_RENDERER | CVAR_BOOL, "Use multisample coverage to fade entities." );
idCVar r_shadowPass( "r_shadowPass", "1", CVAR_RENDERER | CVAR_BOOL, "enable shadow pass" );
idCVar r_showShadows( "r_showShadows", "0", CVAR_RENDERER | CVAR_INTEGER, "1 = visualize the stencil shadow volumes, 2 = draw filled in", 0.0f, 4.0f );
idCVar r_shadowPolygonOffset( "r_shadowPolygonOffset", "-1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "bias value added to depth test for stencil shadow drawing" );
idCVar r_shadowPolygonFactor( "r_shadowPolygonFactor", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "scale value for stencil shadow drawing" );
idCVar r_shadowPolygonOffsetMT( "r_shadowPolygonOffsetMT", "-1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "bias value added to depth test for stencil shadow drawing (megadraw method 3)" );
idCVar r_shadowPolygonFactorMT( "r_shadowPolygonFactorMT", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT, "scale value for stencil shadow drawing (megadraw method 3)" );
idCVar r_useShadowFastParallel( "r_useShadowFastParallel", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use optimized shadow rendering for parallel light sources" );
idCVar r_useShadowInfinite( "r_useShadowInfinite", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL, "use infinite shadows" );

namespace {

bool R_CheckExtension( const char* name ) {
	if ( name == NULL || name[ 0 ] == '\0' ||
		 glConfig.extensions_string == NULL || glConfig.extensions_string[ 0 ] == '\0' ) {
		return false;
	}

	const size_t nameLength = strlen( name );
	const char* scan = glConfig.extensions_string;
	while ( ( scan = strstr( scan, name ) ) != NULL ) {
		const bool startsToken = scan == glConfig.extensions_string || scan[ -1 ] == ' ';
		const char terminator = scan[ nameLength ];
		const bool endsToken = terminator == '\0' || terminator == ' ';
		if ( startsToken && endsToken ) {
			return true;
		}
		scan += nameLength;
	}
	return false;
}

bool R_CoreVersionAtLeast( float version ) {
	return glConfig.glVersion >= version;
}

void R_PrintExtension( const char* name, bool available ) {
	common->Printf( "%s %s\n", available ? "...using" : "X..missing", name );
}

void R_CheckPortableExtensions() {
	glConfig.multitextureAvailable =
		R_CheckExtension( "GL_ARB_multitexture" ) || R_CoreVersionAtLeast( 1.3f );
	glConfig.textureCompressionAvailable =
		R_CheckExtension( "GL_ARB_texture_compression" ) || R_CoreVersionAtLeast( 1.3f );
	glConfig.anisotropicAvailable = R_CheckExtension( "GL_EXT_texture_filter_anisotropic" );
	glConfig.textureLODBiasAvailable =
		R_CheckExtension( "GL_EXT_texture_lod" ) || R_CoreVersionAtLeast( 1.4f );
	glConfig.cubeMapAvailable =
		R_CheckExtension( "GL_ARB_texture_cube_map" ) || R_CoreVersionAtLeast( 1.3f );
	glConfig.texture3DAvailable =
		R_CheckExtension( "GL_EXT_texture3D" ) || R_CoreVersionAtLeast( 1.2f );
	glConfig.rectangleTextureAvailable =
		R_CheckExtension( "GL_ARB_texture_rectangle" ) ||
		R_CheckExtension( "GL_EXT_texture_rectangle" );
	glConfig.sharedTexturePaletteAvailable = R_CheckExtension( "GL_EXT_shared_texture_palette" );
	glConfig.ARBVertexBufferObjectAvailable =
		R_CheckExtension( "GL_ARB_vertex_buffer_object" ) || R_CoreVersionAtLeast( 1.5f );
	glConfig.ARBVertexProgramAvailable = R_CheckExtension( "GL_ARB_vertex_program" );
	glConfig.ARBFragmentProgramAvailable = R_CheckExtension( "GL_ARB_fragment_program" );
	glConfig.twoSidedStencilAvailable = R_CheckExtension( "GL_EXT_stencil_two_side" );
	glConfig.atiTwoSidedStencilAvailable = R_CheckExtension( "GL_ATI_separate_stencil" );
	glConfig.textureNonPowerOfTwoAvailable =
		R_CheckExtension( "GL_ARB_texture_non_power_of_two" ) || R_CoreVersionAtLeast( 2.0f );
	glConfig.depthBoundsTestAvailable = R_CheckExtension( "GL_EXT_depth_bounds_test" );
	glConfig.pointSpriteAvailable =
		R_CheckExtension( "GL_ARB_point_sprite" ) || R_CoreVersionAtLeast( 2.0f );
	glConfig.occlusionQueryAvailable =
		R_CheckExtension( "GL_ARB_occlusion_query" ) || R_CoreVersionAtLeast( 1.5f );
	glConfig.framebufferObjectAvailable =
		R_CheckExtension( "GL_EXT_framebuffer_object" ) || R_CoreVersionAtLeast( 3.0f );
	glConfig.EXTPackedDepthStencilAvailable =
		R_CheckExtension( "GL_EXT_packed_depth_stencil" ) || R_CoreVersionAtLeast( 3.0f );
	glConfig.blendEquationAvailable =
		R_CheckExtension( "GL_EXT_blend_minmax" ) || R_CoreVersionAtLeast( 1.4f );
	glConfig.multiSampleAvailable =
		R_CheckExtension( "GL_ARB_multisample" ) || R_CoreVersionAtLeast( 1.3f );
	glConfig.ARBShaderObjectsAvailable =
		!r_inhibitGLSL.GetBool() &&
		( R_CheckExtension( "GL_ARB_shader_objects" ) || R_CoreVersionAtLeast( 2.0f ) );
	glConfig.ARBVertexShaderAvailable =
		!r_inhibitGLSL.GetBool() &&
		( R_CheckExtension( "GL_ARB_vertex_shader" ) || R_CoreVersionAtLeast( 2.0f ) );
	glConfig.ARBFragmentShaderAvailable =
		!r_inhibitGLSL.GetBool() &&
		( R_CheckExtension( "GL_ARB_fragment_shader" ) || R_CoreVersionAtLeast( 2.0f ) );
	glConfig.EXTGpuProgramParametersAvailable =
		!r_inhibitEXTGPP.GetBool() && R_CheckExtension( "GL_EXT_gpu_program_parameters" );
	glConfig.shadowMappingHardwareAvailable =
		R_CheckExtension( "GL_ARB_fragment_program_shadow" ) &&
		( R_CheckExtension( "GL_ARB_shadow" ) || R_CoreVersionAtLeast( 1.4f ) ) &&
		( R_CheckExtension( "GL_ARB_depth_texture" ) || R_CoreVersionAtLeast( 1.4f ) );
	glConfig.timerQueryAvailable = R_CheckExtension( "GL_EXT_timer_query" );
	glConfig.stringMarkerAvailable = R_CheckExtension( "GL_GREMEDY_string_marker" );
	glConfig.textureCompressionLATCAvailable = R_CheckExtension( "GL_EXT_texture_compression_latc" );
	glConfig.textureCompression3DCAvailable = false;
	glConfig.nvFloatBufferAvailable = false;
	glConfig.atiPixelFormatFloatAvailable = false;
	glConfig.ARBPixelFormatFloatAvailable = false;
	glConfig.csaaAvailable = false;

	// Windows only exports OpenGL 1.1 entry points from opengl32.dll.  The
	// retail renderer resolves every ARB program/GLSL entry point after the
	// context is current; leaving these null silently reduced all material
	// programs to white fixed-function passes.
#define LOAD_QGL( name ) name = reinterpret_cast< decltype( name ) >( sys3D->ExtensionPointer( #name + 1 ) )
	LOAD_QGL( qglActiveTextureARB );
	LOAD_QGL( qglClientActiveTextureARB );
	LOAD_QGL( qglBindBufferARB );
	LOAD_QGL( qglDeleteBuffersARB );
	LOAD_QGL( qglGenBuffersARB );
	LOAD_QGL( qglIsBufferARB );
	LOAD_QGL( qglBufferDataARB );
	LOAD_QGL( qglBufferSubDataARB );
	LOAD_QGL( qglGetBufferSubDataARB );
	LOAD_QGL( qglMapBufferARB );
	LOAD_QGL( qglUnmapBufferARB );
	LOAD_QGL( qglGetBufferParameterivARB );
	LOAD_QGL( qglGetBufferPointervARB );
	LOAD_QGL( qglCompressedTexImage2DARB );
	LOAD_QGL( qglCompressedTexSubImage2DARB );
	LOAD_QGL( qglVertexAttribPointerARB );
	LOAD_QGL( qglEnableVertexAttribArrayARB );
	LOAD_QGL( qglDisableVertexAttribArrayARB );
	LOAD_QGL( qglVertexAttrib3fvARB );
	LOAD_QGL( qglVertexAttrib4fvARB );
	LOAD_QGL( qglGetProgramivARB );
	LOAD_QGL( qglProgramStringARB );
	LOAD_QGL( qglBindProgramARB );
	LOAD_QGL( qglDeleteProgramsARB );
	LOAD_QGL( qglGenProgramsARB );
	LOAD_QGL( qglProgramEnvParameter4fARB );
	LOAD_QGL( qglProgramEnvParameter4fvARB );
	LOAD_QGL( qglProgramLocalParameter4fARB );
	LOAD_QGL( qglProgramLocalParameter4fvARB );
	LOAD_QGL( qglGetVertexAttribPointervARB );

	LOAD_QGL( qglDeleteObjectARB );
	LOAD_QGL( qglGetHandleARB );
	LOAD_QGL( qglDetachObjectARB );
	LOAD_QGL( qglCreateShaderObjectARB );
	LOAD_QGL( qglShaderSourceARB );
	LOAD_QGL( qglCompileShaderARB );
	LOAD_QGL( qglCreateProgramObjectARB );
	LOAD_QGL( qglAttachObjectARB );
	LOAD_QGL( qglLinkProgramARB );
	LOAD_QGL( qglUseProgramObjectARB );
	LOAD_QGL( qglValidateProgramARB );
	LOAD_QGL( qglUniform1iARB );
	LOAD_QGL( qglUniform4fvARB );
	LOAD_QGL( qglGetObjectParameterivARB );
	LOAD_QGL( qglGetInfoLogARB );
	LOAD_QGL( qglGetUniformLocationARB );
	LOAD_QGL( qglBindAttribLocationARB );
	LOAD_QGL( qglProgramEnvParameters4fvEXT );
	LOAD_QGL( qglProgramLocalParameters4fvEXT );
#undef LOAD_QGL
	// Core OpenGL 1.5 drivers are allowed to omit the ARB-suffixed aliases.
	// Windows still requires these addresses to be resolved from the current
	// context rather than imported from opengl32.dll.
#define LOAD_QGL_CORE_FALLBACK( name, coreName ) \
	if ( name == NULL ) name = reinterpret_cast< decltype( name ) >( sys3D->ExtensionPointer( coreName ) )
	LOAD_QGL_CORE_FALLBACK( qglBindBufferARB, "glBindBuffer" );
	LOAD_QGL_CORE_FALLBACK( qglDeleteBuffersARB, "glDeleteBuffers" );
	LOAD_QGL_CORE_FALLBACK( qglGenBuffersARB, "glGenBuffers" );
	LOAD_QGL_CORE_FALLBACK( qglIsBufferARB, "glIsBuffer" );
	LOAD_QGL_CORE_FALLBACK( qglBufferDataARB, "glBufferData" );
	LOAD_QGL_CORE_FALLBACK( qglBufferSubDataARB, "glBufferSubData" );
	LOAD_QGL_CORE_FALLBACK( qglGetBufferSubDataARB, "glGetBufferSubData" );
	LOAD_QGL_CORE_FALLBACK( qglMapBufferARB, "glMapBuffer" );
	LOAD_QGL_CORE_FALLBACK( qglUnmapBufferARB, "glUnmapBuffer" );
	LOAD_QGL_CORE_FALLBACK( qglGetBufferParameterivARB, "glGetBufferParameteriv" );
	LOAD_QGL_CORE_FALLBACK( qglGetBufferPointervARB, "glGetBufferPointerv" );
#undef LOAD_QGL_CORE_FALLBACK
	if ( qglBindBufferARB == NULL || qglGenBuffersARB == NULL ||
		 qglBufferDataARB == NULL || qglBufferSubDataARB == NULL ) {
		glConfig.ARBVertexBufferObjectAvailable = false;
	}
	if ( qglCompressedTexImage2DARB == NULL ) {
		qglCompressedTexImage2DARB = reinterpret_cast< PFNGLCOMPRESSEDTEXIMAGE2DARBPROC >(
			sys3D->ExtensionPointer( "glCompressedTexImage2D" ) );
	}
	if ( qglCompressedTexSubImage2DARB == NULL ) {
		qglCompressedTexSubImage2DARB = reinterpret_cast< PFNGLCOMPRESSEDTEXSUBIMAGE2DARBPROC >(
			sys3D->ExtensionPointer( "glCompressedTexSubImage2D" ) );
	}
	if ( qglCompressedTexImage2DARB == NULL || qglCompressedTexSubImage2DARB == NULL ) {
		glConfig.textureCompressionAvailable = false;
	}

	if ( qglProgramStringARB == NULL || qglBindProgramARB == NULL || qglGenProgramsARB == NULL ) {
		glConfig.ARBVertexProgramAvailable = false;
		glConfig.ARBFragmentProgramAvailable = false;
	}
	if ( qglCreateShaderObjectARB == NULL || qglCreateProgramObjectARB == NULL ||
		qglShaderSourceARB == NULL || qglCompileShaderARB == NULL || qglLinkProgramARB == NULL ) {
		glConfig.ARBShaderObjectsAvailable = false;
		glConfig.ARBVertexShaderAvailable = false;
		glConfig.ARBFragmentShaderAvailable = false;
	}

	qglBeginQueryARB = reinterpret_cast< PFNGLBEGINQUERYARBPROC >(
		sys3D->ExtensionPointer( "glBeginQueryARB" ) );
	qglEndQueryARB = reinterpret_cast< PFNGLENDQUERYARBPROC >(
		sys3D->ExtensionPointer( "glEndQueryARB" ) );
	qglGetQueryObjectivARB = reinterpret_cast< PFNGLGETQUERYOBJECTIVARBPROC >(
		sys3D->ExtensionPointer( "glGetQueryObjectivARB" ) );
	qglGetQueryObjectui64vEXT = reinterpret_cast< PFNGLGETQUERYOBJECTUI64VEXTPROC >(
		sys3D->ExtensionPointer( "glGetQueryObjectui64vEXT" ) );
	if ( qglGetQueryObjectui64vEXT == NULL ) {
		glConfig.timerQueryAvailable = false;
	}

	glGetIntegerv( 0x84E2 /* GL_MAX_TEXTURE_UNITS_ARB */, &glConfig.maxTextureUnits );
	glGetIntegerv( 0x8871 /* GL_MAX_TEXTURE_COORDS_ARB */, &glConfig.maxTextureCoords );
	glGetIntegerv( 0x8872 /* GL_MAX_TEXTURE_IMAGE_UNITS_ARB */, &glConfig.maxTextureImageUnits );
	if ( glConfig.anisotropicAvailable ) {
		glGetFloatv( 0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &glConfig.maxTextureAnisotropy );
	} else {
		glConfig.maxTextureAnisotropy = 1.0f;
	}
	if ( glConfig.ARBVertexProgramAvailable ) {
		glGetIntegerv( 0x8869 /* GL_MAX_VERTEX_ATTRIBS_ARB */, &glConfig.maxVertexAttribs );
		glGetIntegerv( 0x88B4 /* GL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB */, &glConfig.maxProgramLocalParameters );
		glGetIntegerv( 0x88B5 /* GL_MAX_PROGRAM_ENV_PARAMETERS_ARB */, &glConfig.maxProgramEnvParameters );
	}

	R_PrintExtension( "GL_ARB_multitexture", glConfig.multitextureAvailable );
	R_PrintExtension( "GL_EXT_texture3D", glConfig.texture3DAvailable );
	R_PrintExtension( "GL_ARB_texture_rectangle", glConfig.rectangleTextureAvailable );
	R_PrintExtension( "GL_ARB_occlusion_query", glConfig.occlusionQueryAvailable );
	R_PrintExtension( "GL_ARB_vertex_program", glConfig.ARBVertexProgramAvailable );
	R_PrintExtension( "GL_ARB_fragment_program", glConfig.ARBFragmentProgramAvailable );

	idStr unsupported;
	if ( !glConfig.multitextureAvailable ) {
		unsupported.Append( " GL_ARB_multitexture" );
	}
	if ( !glConfig.texture3DAvailable ) {
		unsupported.Append( " GL_EXT_texture3D" );
	}
	if ( !glConfig.rectangleTextureAvailable ) {
		unsupported.Append( " GL_ARB_texture_rectangle" );
	}
	if ( !glConfig.occlusionQueryAvailable ) {
		unsupported.Append( " GL_ARB_occlusion_query" );
	}
	if ( !glConfig.ARBVertexProgramAvailable ) {
		unsupported.Append( " GL_ARB_vertex_program" );
	}
	if ( !glConfig.ARBFragmentProgramAvailable ) {
		unsupported.Append( " GL_ARB_fragment_program" );
	}
	if ( !unsupported.IsEmpty() ) {
		common->Error(
			"The current video card / driver combination does not support the necessary features:%s",
			unsupported.c_str()
		);
	}
}

}

void R_ARB2_Init();
void R_ARB2_Shutdown();
void R_ARB2_ReparseRenderPrograms();

void R_InitOpenGL() {
	common->Printf( "----- R_InitOpenGL -----\n" );
	if ( glConfig.isInitialized ) {
		common->FatalError( "R_InitOpenGL called while active" );
	}

	const char* vendor = reinterpret_cast< const char* >( glGetString( GL_VENDOR ) );
	const char* renderer = reinterpret_cast< const char* >( glGetString( GL_RENDERER ) );
	const char* version = reinterpret_cast< const char* >( glGetString( GL_VERSION ) );
	const char* extensions = reinterpret_cast< const char* >( glGetString( GL_EXTENSIONS ) );
	glConfig.vendor_string = vendor != NULL ? vendor : "";
	glConfig.renderer_string = renderer != NULL ? renderer : "";
	glConfig.version_string = version != NULL ? version : "";
	glConfig.extensions_string = extensions != NULL ? extensions : "";
	glConfig.wgl_extensions_string = "";
	glConfig.glVersion = static_cast< float >( atof( glConfig.version_string ) );

	if ( idStr::FindText( glConfig.vendor_string, "nvidia", false ) >= 0 ) {
		r_glDriverVendor.SetString( "NVIDIA" );
	} else if ( idStr::FindText( glConfig.vendor_string, "ati", false ) >= 0 ||
				idStr::FindText( glConfig.vendor_string, "amd", false ) >= 0 ) {
		r_glDriverVendor.SetString( "ATI" );
	} else if ( idStr::FindText( glConfig.vendor_string, "intel", false ) >= 0 ) {
		r_glDriverVendor.SetString( "Intel" );
	} else {
		r_glDriverVendor.SetString( "unknown" );
	}

	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );
	if ( glConfig.maxTextureSize <= 0 ) {
		glConfig.maxTextureSize = 256;
	}
	if ( glConfig.maxTextureSize <= 512 ) {
		common->Error(
			"The current video card / driver combination does not support the necessary features: "
			"Max Texture Size greater than 512"
		);
	}

	glConfig.isInitialized = true;
	R_CheckPortableExtensions();

	if ( glConfig.multiSampleAvailable ) {
		glGetIntegerv( 0x80A9 /* GL_SAMPLES_ARB */, &glConfig.samples );
	} else {
		glConfig.samples = 0;
	}
	common->Printf( "GL_VENDOR: %s\n", glConfig.vendor_string );
	common->Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	common->Printf( "GL_VERSION: %s\n", glConfig.version_string );
	common->Printf( "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	R_ARB2_Init();
	vertexCache.Init();
	common->Printf( "------------------------\n" );
}

void idRenderSystemLocal::Init() {
	common->Printf( "------- Initializing renderSystem --------\n" );
	if ( initialized ) {
		common->Warning( "idRenderSystemLocal::Init called more than once" );
		return;
	}

	cvarSystem->Register( &r_softParticles );
	cvarSystem->Register( &r_megaDrawMethod );
	cvarSystem->Register( &r_swapInterval );
	cvarSystem->Register( &r_gamma );
	cvarSystem->Register( &r_brightness );
	cvarSystem->Register( &r_shadows );
	cvarSystem->Register( &r_useAlphaToCoverage );
	cvarSystem->Register( &r_useStateCaching );

	renderSystemBackend.Init();
	R_InitRuntimeSpirvCompiler();
	openGLRunning = false;
	if ( sys3D != NULL && sys3D->GetGameRenderContext() != NULL ) {
		if ( !sys3D->MakeCurrent( sys3D->GetGameWindow() ) ) {
			common->Error( "idRenderSystemLocal::Init: failed to make the game OpenGL context current" );
		}
		const glimpParms_t& parms = sys3D->GetGameWindowParms();
		windowWidth = parms.width;
		windowHeight = parms.height;
		glConfig.colorBits = 32;
		glConfig.depthBits = 24;
		glConfig.stencilBits = 8;
		// Vulkan owns GPU resources from the first vertex-cache/image upload.
		// Keep the legacy context alive during the staged port, but establish the
		// selected Vulkan device before either resource manager is initialized.
		if ( R_UseVulkanBackend() && !vulkanBackend.Init(
			sys3D->GetGameWindowHandle(), parms.width, parms.height ) ) {
			common->FatalError( "Failed to initialize the Vulkan renderer" );
		}
		R_InitOpenGL();
		openGLRunning = true;
	} else {
		common->Printf( "renderSystem running without OpenGL (dedicated or skipped renderer)\n" );
	}
	sdSingleton< sdImageResampler >::GetInstance().Init();
	sdSingleton< sdImageProcessor >::GetInstance().Init();
	if ( globalImages != NULL ) {
		globalImages->Init();
	}
	if ( openGLRunning ) {
		R_ARB2_ReparseRenderPrograms();
	}
	rbinds->Init();
	// Retail starts both MegaTexture workers after images/material bindings are
	// available and before level resources can become active.
	megaTextureTileDecompressor->Init();
	megaTextureTileLoader->Init();
	cmdSystem->AddCommand( "megaTextureInfo", idMegaTexture::MegaTextureInfo_f, CMD_FL_RENDERER,
		"shows the active MegaTexture view, streaming, decode, and upload state" );
	cmdSystem->AddCommand( "megaTestStreamingPerformance", idMegaTexture::MegaTestStreamingPerformance_f, CMD_FL_RENDERER,
		"shows active MegaTexture streaming performance" );
	cmdSystem->AddCommand( "megaShowMemoryUsage", idMegaTexture::MegaShowMemoryUsage_f, CMD_FL_RENDERER,
		"shows active MegaTexture memory usage" );
	if ( renderModelManager != NULL ) {
		renderModelManager->Init();
	}

	initialized = true;
	synced = true;
	common->Printf( "renderSystem initialized.\n" );
	common->Printf( "------------------------------------------\n" );
}

void idRenderSystemLocal::Shutdown() {
	if ( !initialized ) {
		return;
	}

	common->Printf( "idRenderSystem::Shutdown()\n" );
	if ( worlds.Num() != 0 ) {
		common->Warning( "Leaked %d renderWorlds", worlds.Num() );
	}

	if ( logFile != NULL ) {
		fprintf( logFile, "*** CLOSING LOG ***\n" );
		fclose( logFile );
		logFile = NULL;
	}

	for ( int i = 0; i < worlds.Num(); i++ ) {
		delete worlds[ i ];
	}
	worlds.Clear();
	registeredPtrs.Clear();

	// Stop streaming before purging images/MegaTextures; both workers retain a
	// pointer to the active resource while an I/O or decode job is in flight.
	megaTextureTileLoader->Shutdown();
	megaTextureTileDecompressor->Shutdown();
	if ( renderModelManager != NULL ) {
		renderModelManager->Shutdown();
	}
	if ( globalImages != NULL ) {
		globalImages->Shutdown();
	}
	renderSystemBackend.Shutdown();
	vulkanBackend.Shutdown();

	ShutdownOpenGL();
	initialized = false;
}

void idRenderSystemLocal::ShutdownOpenGL() {
	if ( !glConfig.isInitialized ) {
		openGLRunning = false;
		return;
	}

	R_FreeOcclussionQueries();
	vertexCache.Shutdown();
	R_ARB2_Shutdown();
	glConfig.backendInitialized = false;
	glConfig.isInitialized = false;
	openGLRunning = false;
}

bool idRenderSystemLocal::IsOpenGLRunning() const {
	return glConfig.isInitialized;
}

void idRenderSystemLocal::BeginLevelLoad() {
	if ( renderModelManager != NULL ) {
		renderModelManager->BeginLevelLoad();
	}
	if ( globalImages != NULL ) {
		globalImages->BeginLevelLoad();
	}
}

void idRenderSystemLocal::EndLevelLoad() {
	if ( renderModelManager != NULL ) {
		renderModelManager->EndLevelLoad();
	}
	if ( globalImages != NULL ) {
		globalImages->EndLevelLoad();
	}
}

void idRenderSystemLocal::LevelStart() {
	if ( globalImages != NULL ) {
		globalImages->LevelStart();
	}
}

void R_ScreenshotFilename( int& lastNumber, const char* base, idStr& fileName ) {
	const char* safeBase = base != NULL && base[ 0 ] != '\0' ? base : "screenshots/shot";
	fileName = va( "%s%05d.tga", safeBase, lastNumber++ );
}
