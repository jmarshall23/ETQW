// Copyright (C) 2007 Id Software, Inc.
//
// ETQW renderer startup reconstructed from:
//   quakewars-hexrays/renderer/RenderSystem_init.cpp
//   the Microsoft ETQW PDB type and compiland records.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RendererBootstrap.h"
#include "RenderSystemBackend.h"
#include "image_processor.h"
#include "image_resampler.h"
#include "renderbindings.h"
#include "Image.h"
#include "ModelManager.h"
#include "../idlib/Singleton.h"
#include "../libs/qglLib/qgl.h"
#include "../sys/sys_render.h"

#include <GL/gl.h>

glconfig_t glConfig;

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

	qglProgramEnvParameter4fvARB = reinterpret_cast< PFNGLPROGRAMENVPARAMETER4FVARBPROC >(
		sys3D->ExtensionPointer( "glProgramEnvParameter4fvARB" ) );
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

void R_ARB2_Init() {
	common->Printf( "---------- R_ARB2_Init ----------\n" );
	if ( !glConfig.ARBVertexProgramAvailable || !glConfig.ARBFragmentProgramAvailable ) {
		common->Error( "ARB2 renderer path is unavailable" );
	}
	glConfig.allowCgPath = false;
	common->Printf( "ARB2 renderer path available\n" );
	common->Printf( "---------------------------------\n" );
}

void R_ARB2_Shutdown() {
}

}

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
	glConfig.backendInitialized = true;

	common->Printf( "GL_VENDOR: %s\n", glConfig.vendor_string );
	common->Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	common->Printf( "GL_VERSION: %s\n", glConfig.version_string );
	common->Printf( "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	R_ARB2_Init();
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

	renderSystemBackend.Init();
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
	rbinds->Init();
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

	if ( renderModelManager != NULL ) {
		renderModelManager->Shutdown();
	}
	if ( globalImages != NULL ) {
		globalImages->Shutdown();
	}
	renderSystemBackend.Shutdown();

	for ( int i = 0; i < worlds.Num(); i++ ) {
		delete worlds[ i ];
	}
	worlds.Clear();
	registeredPtrs.Clear();

	ShutdownOpenGL();
	initialized = false;
}

void idRenderSystemLocal::ShutdownOpenGL() {
	if ( !glConfig.isInitialized ) {
		openGLRunning = false;
		return;
	}

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
