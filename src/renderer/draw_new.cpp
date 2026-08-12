// Copyright (C) 2007 Id Software, Inc.
//
// Vertex attribute state tracking reconstructed from renderer/draw_new.cpp
// in the ETQW PDB and Hex-Rays output.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderSystem.h"
#include "RenderWorld_local.h"
#include "draw_local.h"
#include "Image.h"
#include "DeviceContext.h"
#include "Material.h"
#include "Model.h"
#include "VertexCache.h"
#include "tr_render.h"
#include "renderbindings.h"
#include "megatexture/MegaTexture.h"
#include "../decllib/declAmbientCubeMap.h"
#include "../decllib/declAtmosphere.h"
#include "../decllib/declRenderProgram.h"
#include "../decllib/declRenderBinding.h"
#include "../decllib/declTypeHolder.h"
#include "../libs/qglLib/qgl.h"

#include <GL/gl.h>

extern glconfig_t glConfig;
extern idCVar com_gpuSpec;
extern idCVar r_megaDrawMethod;
extern idCVar r_softParticles;
extern idCVar r_useAlphaToCoverage;
extern idCVar r_shadowPolygonOffset;
extern idCVar r_shadowPolygonFactor;
extern idCVar r_shadowPolygonOffsetMT;
extern idCVar r_shadowPolygonFactorMT;
extern idCVar r_useShadowFastParallel;
extern idCVar r_useShadowInfinite;
extern idCVar r_skipAtmosInteractions;
extern idCVar r_skipAmbient;
extern idCVar r_skipInteractions;
extern idCVar r_skipTranslucent;
extern idCVar r_skipRefractCopy;
extern idCVar r_useSampleCoverage;
extern idCVar r_useDitherMask;
extern idCVar r_useShadowDitherMask;
extern idCVar r_useScissor;
extern idCVar r_useMinimalGuiDraw;
extern idCVar r_offsetFactor;
extern idCVar r_offsetUnits;
extern idCVar r_lightScale;
extern idCVar r_shadows;

idCVar r_renderProgramLodDistance(
	"r_renderProgramLodDistance",
	"200",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Sets global render program lod distance",
	-1.0f,
	1000.0f
);

idCVar r_renderProgramLodFade(
	"r_renderProgramLodFade",
	"50",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Sets global render program fade distance",
	1.0f,
	2000.0f
);

idCVar r_depthFillNoColour( "r_depthFillNoColour", "1", CVAR_RENDERER, "Disable depth fill colour write" );
idCVar r_32ByteVtx( "r_32ByteVtx", "1", CVAR_RENDERER, "Uses 32bit vtx" );
idCVar r_skinningDualQuaternion( "r_skinningDualQuaternion", "0", CVAR_RENDERER | CVAR_BOOL, "use dual-quaternion hardware skinning" );
idCVar r_depthFillCutoff( "r_depthFillCutoff", "1000", CVAR_RENDERER, "Screen Rect Area required to render" );

namespace {
	int enabledVertexAttribBits = 0;
	bool spaceActive = false;
	const viewEntity_s* activeDrawSpace = NULL;
	idVec4 activeLightColor( 1.0f, 1.0f, 1.0f, 1.0f );
	bool activeLightHasTextureMatrix = false;
	float activeLightTextureMatrix[ 16 ] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	bool postProcessFrameBufferReady = false;
	bool currentRenderCopied = false;
	rbDrawPass_t activePass = RBP_SHADER;
	const renderEntity_t* activeEntity = NULL;
	const idMaterial* activeMaterial = NULL;
	const idMaterial* vertexAttribMaterial = NULL;
	const sdDeclRenderProgram* activeProgram = NULL;
	const materialStage_t* activeStage = NULL;
	viewLight_s* activeViewLight = NULL;
	const drawSurf_s* activeSurface = NULL;
	bool activeShadowSelection = false;
	bool depthFillNoColour = false;
	bool activeSurfaceUsesAlphaToCoverage = false;
	idVec3 activeLocalViewOrigin( 0.0f, 0.0f, 0.0f );
	const vertCache_s* activeAmbientCache = NULL;
	const vertCache_s* activeWeightCache = NULL;
	const srfTriangles_t* activeSkinnedTriangles = NULL;
	bool activeHardwareSkinning = false;
	bool activeHardSkinning = false;

	const void* CachePosition( const vertCache_s* cache, bool indexBuffer ) {
		if ( cache == NULL ) return NULL;
		if ( cache->vbo != 0 && qglBindBufferARB != NULL ) {
			qglBindBufferARB( indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB, cache->vbo );
			return reinterpret_cast< const void* >( static_cast< UINT_PTR >( cache->offset ) );
		}
		if ( qglBindBufferARB != NULL ) qglBindBufferARB( indexBuffer ? GL_ELEMENT_ARRAY_BUFFER_ARB : GL_ARRAY_BUFFER_ARB, 0 );
		return cache->virtMem != NULL ? static_cast< const byte* >( cache->virtMem ) + cache->offset : NULL;
	}

	const byte* VertexPointerOffset( const byte* base, size_t offset ) {
		return reinterpret_cast< const byte* >( reinterpret_cast< UINT_PTR >( base ) + offset );
	}

	void BindProgramTextures( const sdDeclRenderProgram* program ) {
		if ( program == NULL ) return;
		for ( int unit = 0; unit < program->GetNumTextureBindings(); ++unit ) {
			const sdDeclRenderBinding* binding = program->GetTextureBinding( unit );
			if ( binding == NULL ) continue;
			binding->Evaluate();
			idImage* image = binding->GetImage();
			if ( image == NULL && globalImages != NULL ) {
				common->Warning( "RB_ARB2_SetupProgram : NULL image for render binding %s", binding->GetName() );
				image = globalImages->defaultImage;
			}
			if ( image != NULL ) image->BindFragment( unit );
		}
		GL_SelectTexture( 0 );
	}

	idPlane GlobalPlaneToLocal( const idPlane& global, const float modelMatrix[ 16 ] ) {
		return idPlane(
			global[ 0 ] * modelMatrix[ 0 ] + global[ 1 ] * modelMatrix[ 1 ] + global[ 2 ] * modelMatrix[ 2 ],
			global[ 0 ] * modelMatrix[ 4 ] + global[ 1 ] * modelMatrix[ 5 ] + global[ 2 ] * modelMatrix[ 6 ],
			global[ 0 ] * modelMatrix[ 8 ] + global[ 1 ] * modelMatrix[ 9 ] + global[ 2 ] * modelMatrix[ 10 ],
			global[ 3 ] + global[ 0 ] * modelMatrix[ 12 ] + global[ 1 ] * modelMatrix[ 13 ] + global[ 2 ] * modelMatrix[ 14 ]
		);
	}

	void SetAmbientCubeMapBindings( const viewEntity_s* space ) {
		const sdDeclAmbientCubeMap* cubeMap = space != NULL ? space->ambientCubeMap : NULL;
		if ( cubeMap != NULL ) {
			rbinds->ambientCubeMap->Set( cubeMap->GetAmbientCubeMap() != NULL ? cubeMap->GetAmbientCubeMap() : globalImages->blackCubeMapImage );
			rbinds->ambientCubeMapSun->Set( cubeMap->GetLightCubeMap() != NULL ? cubeMap->GetLightCubeMap() : globalImages->blackCubeMapImage );
			rbinds->specularCubeMap->Set( cubeMap->GetSpecularCubeMap() != NULL ? cubeMap->GetSpecularCubeMap() : globalImages->blackCubeMapImage );
			rbinds->environmentCubeMap->Set( cubeMap->GetEnvironmentCubeMap() != NULL ? cubeMap->GetEnvironmentCubeMap() : globalImages->blackCubeMapImage );
			rbinds->gradientMap->Set( cubeMap->GetGradientMap() != NULL ? cubeMap->GetGradientMap() : globalImages->blackImage );
			rbinds->ambientBrightness->Set( cubeMap->GetBrightness() );
			const idVec3 ambientAverage = cubeMap->GetMinSpecAmbientColor();
			rbinds->ambientAvgColor->Set( ambientAverage.x, ambientAverage.y, ambientAverage.z, 0.0f );
		} else {
			rbinds->ambientCubeMap->Set( globalImages->blackCubeMapImage );
			rbinds->ambientCubeMapSun->Set( globalImages->blackCubeMapImage );
			rbinds->specularCubeMap->Set( globalImages->blackCubeMapImage );
			rbinds->environmentCubeMap->Set( globalImages->blackCubeMapImage );
			rbinds->gradientMap->Set( globalImages->blackImage );
			rbinds->ambientBrightness->Set( 1.0f );
			rbinds->ambientAvgColor->Set( 0.0f, 0.0f, 0.0f, 0.0f );
		}
	}

	const sdDeclRenderProgram* SelectShadowProgram( const viewLight_s* light ) {
		if ( rbinds == NULL ) return NULL;
		if ( light != NULL && light->lightDef != NULL && light->lightDef->flags.parallel && r_useShadowFastParallel.GetBool() ) {
			return r_useShadowInfinite.GetBool() ? rbinds->shadowParallelProgramInfinite : rbinds->shadowParallelProgramProjected;
		}
		return r_useShadowInfinite.GetBool() ? rbinds->shadowProgram : rbinds->shadowProgramProjected;
	}

	void DisableSampleCoverage() {
		if ( glConfig.samples > 0 && qglSampleCoverageARB != NULL ) glDisable( GL_SAMPLE_COVERAGE_ARB );
	}

	bool SetupActiveLightStage( const materialStage_t* stage ) {
		if ( activeViewLight == NULL || activeViewLight->lightRegisters == NULL || stage == NULL ||
				activeViewLight->lightRegisters[ stage->conditionRegister ] == 0.0f || stage->depthStage ) {
			return false;
		}

		const float lightScale = r_lightScale.GetFloat() * activeViewLight->fadeFraction;
		if ( stage->colorVector != NULL ) {
			const int* registers = stage->colorVector->registers;
			activeLightColor.Set(
				activeViewLight->lightRegisters[ registers[ 0 ] ] * lightScale,
				activeViewLight->lightRegisters[ registers[ 1 ] ] * lightScale,
				activeViewLight->lightRegisters[ registers[ 2 ] ] * lightScale,
				activeViewLight->lightRegisters[ registers[ 3 ] ]
			);
		} else {
			activeLightColor.Set( lightScale, lightScale, lightScale, 1.0f );
		}

		for ( int textureIndex = 0; textureIndex < stage->numTextures; ++textureIndex ) {
			stage->textures[ textureIndex ].renderBinding->Set( stage->textures[ textureIndex ].image );
		}
		if ( activeViewLight->falloffImage != NULL ) {
			rbinds->lightFalloffMap->Set( activeViewLight->falloffImage );
		}

		activeLightHasTextureMatrix = stage->diffuseTextureMatrix != NULL;
		if ( activeLightHasTextureMatrix ) {
			RB_GetShaderTextureMatrix( stage->diffuseTextureMatrix, activeViewLight->lightRegisters, activeLightTextureMatrix );
		}
		return true;
	}
}

void GL_EnableVertexAttribs( int requiredVertexAttribBits ) {
	int changedBits = requiredVertexAttribBits ^ enabledVertexAttribBits;
	if ( changedBits == 0 ) {
		return;
	}

	const int maxAttribs = Min( glConfig.maxVertexAttribs, 32 );
	for ( int attrib = 1; attrib < maxAttribs && changedBits != 0; ++attrib ) {
		const int bit = 1 << attrib;
		if ( ( changedBits & bit ) == 0 ) {
			continue;
		}
		if ( enabledVertexAttribBits & bit ) {
			qglDisableVertexAttribArrayARB( attrib );
		} else {
			qglEnableVertexAttribArrayARB( attrib );
		}
		changedBits &= ~bit;
	}
	enabledVertexAttribBits = requiredVertexAttribBits;
}

void RB_ARB2_ClearSpace() {
	if ( activeDrawSpace != NULL && ( activeDrawSpace->weaponDepthHack || activeDrawSpace->modelDepthHack != 0.0f ) ) {
		RB_LeaveDepthHack();
	}
	activeDrawSpace = NULL;
	// unk_893E18 in the retail back end is invalidated whenever a draw list
	// releases its active space.  Program alternates can require different
	// arrays for the same material in the ambient, interaction and shader
	// passes, so this cache cannot survive the pass boundary.
	vertexAttribMaterial = NULL;
	spaceActive = false;
}

void RB_ARB2_ResetDrawCaches() {
	// The retail shadow pass invalidates these four back-end caches because
	// shadow vertices and programs bypass the ordinary ambient draw path.
	activeAmbientCache = NULL;
	activeProgram = NULL;
	vertexAttribMaterial = NULL;
	activeSkinnedTriangles = NULL;
	activeDrawSpace = NULL;
	spaceActive = false;
}

const viewEntity_s* RB_GetActiveDrawSpace() {
	return activeDrawSpace;
}

viewLight_s* RB_GetActiveViewLight() {
	return activeViewLight;
}

void RB_ARB2_SetShadowSurfaceContext( const drawSurf_s* surface, bool active ) {
	activeShadowSelection = active;
	activeSurface = active ? surface : NULL;
	activeSurfaceUsesAlphaToCoverage = false;
	activeEntity = active && surface != NULL && surface->space != NULL ? surface->space->entityDef : NULL;
	activeMaterial = active && surface != NULL ? surface->material : NULL;
	activeStage = NULL;
}

void RB_ARB2_SetSpace( const float modelMatrix[ 16 ], const idVec3& globalViewOrigin, float coverage ) {
	if ( modelMatrix == NULL || rbinds == NULL ) {
		return;
	}
	RB_SetCurrentBindingSpace( modelMatrix );

	// RB_ARB2_SetSpace in the retail back end transposes the local-to-world
	// matrix into four render bindings.  ARB and Cg programs both consume
	// these values even though OpenGL's compatibility model-view is active.
	rbinds->transposedModelMatrix_x->Set( modelMatrix[ 0 ], modelMatrix[ 4 ], modelMatrix[ 8 ], modelMatrix[ 12 ] );
	rbinds->transposedModelMatrix_y->Set( modelMatrix[ 1 ], modelMatrix[ 5 ], modelMatrix[ 9 ], modelMatrix[ 13 ] );
	rbinds->transposedModelMatrix_z->Set( modelMatrix[ 2 ], modelMatrix[ 6 ], modelMatrix[ 10 ], modelMatrix[ 14 ] );
	rbinds->transposedModelMatrix_w->Set( modelMatrix[ 3 ], modelMatrix[ 7 ], modelMatrix[ 11 ], modelMatrix[ 15 ] );

	coverage = idMath::ClampFloat( 0.0f, 1.0f, coverage );
	rbinds->coverage->Set( coverage );

	const idVec3 delta(
		globalViewOrigin.x - modelMatrix[ 12 ],
		globalViewOrigin.y - modelMatrix[ 13 ],
		globalViewOrigin.z - modelMatrix[ 14 ]
	);
	const idVec3 localViewOrigin(
		delta.x * modelMatrix[ 0 ] + delta.y * modelMatrix[ 1 ] + delta.z * modelMatrix[ 2 ],
		delta.x * modelMatrix[ 4 ] + delta.y * modelMatrix[ 5 ] + delta.z * modelMatrix[ 6 ],
		delta.x * modelMatrix[ 8 ] + delta.y * modelMatrix[ 9 ] + delta.z * modelMatrix[ 10 ]
	);
	activeLocalViewOrigin = localViewOrigin;
	rbinds->viewOrigin->Set( localViewOrigin.x, localViewOrigin.y, localViewOrigin.z, 1.0f );
	spaceActive = true;
}

void RB_ARB2_SetSpace( const viewEntity_s* space, bool useSampleCoverage ) {
	const renderView_t* view = RB_GetDrawView();
	if ( space == NULL || view == NULL ) return;
	if ( activeDrawSpace != space ) {
		if ( activeDrawSpace != NULL && ( activeDrawSpace->weaponDepthHack || activeDrawSpace->modelDepthHack != 0.0f ) ) {
			RB_LeaveDepthHack();
		}
		activeDrawSpace = space;
		if ( space->weaponDepthHack ) RB_EnterWeaponDepthHack( space->weaponDepthHackFOV_x, space->weaponDepthHackFOV_y );
		if ( space->modelDepthHack != 0.0f ) RB_EnterModelDepthHack( space->modelDepthHack );
	}
	glMatrixMode( GL_MODELVIEW );
	glLoadMatrixf( space->modelViewMatrix );
	if ( glConfig.samples > 0 && r_useSampleCoverage.GetBool() && qglSampleCoverageARB != NULL ) {
		if ( useSampleCoverage && space->coverage != 1.0f ) {
			glEnable( GL_SAMPLE_COVERAGE_ARB );
			qglSampleCoverageARB( space->coverage, GL_FALSE );
		} else {
			glDisable( GL_SAMPLE_COVERAGE_ARB );
		}
	}
	if ( r_useDitherMask.GetBool() && globalImages != NULL ) {
		const int ditherIndex = idMath::ClampInt( 0, 15, static_cast< int >( space->coverage * 16.0f ) );
		rbinds->dithermask->Set( globalImages->dither[ ditherIndex ] );
	}
	RB_ARB2_SetSpace( space->modelMatrix, view->vieworg, space->coverage );
}

void RB_ARB2_SetupPostProcessingFrameBuffer() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || globalImages == NULL ) return;
	const int width = Max( 1, ( view->viewport.x2 - view->viewport.x1 + 1 ) >> 2 );
	const int height = Max( 1, ( view->viewport.y2 - view->viewport.y1 + 1 ) >> 2 );
	idImage* first = globalImages->postProcessBuffer[ 0 ];
	if ( first != NULL && first->uploadWidth == width && first->uploadHeight == height ) {
		postProcessFrameBufferReady = true;
		return;
	}
	for ( int index = 0; index < 2; ++index ) {
		idImage* image = globalImages->postProcessBuffer[ index ];
		if ( image == NULL ) continue;
		image->FromParameters( width, height, GL_RGBA8, TT_RECT, TF_LINEAR, TR_CLAMP );
		image->BindFragment( 0 );
		glCopyTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 0, 0, width, height );
	}
	postProcessFrameBufferReady = true;
}

void RB_ARB2_CopyFramebufferColor() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || globalImages == NULL || globalImages->currentRenderImage == NULL ) return;
	const int width = view->viewport.x2 - view->viewport.x1 + 1;
	const int height = view->viewport.y2 - view->viewport.y1 + 1;
	if ( rbinds != NULL ) {
		rbinds->currentRenderTexelSize->Set( static_cast< float >( width ), static_cast< float >( height ), 1.0f / Max( width, 1 ), 1.0f / Max( height, 1 ) );
		const float aspectCorrection = deviceContext != NULL ? deviceContext->GetAspectRatioCorrection() : 1.0f;
		rbinds->aspectSize->Set( 1.0f / Max( aspectCorrection, 0.001f ), 0.0f, 0.0f, 0.0f );
	}
	globalImages->currentRenderImage->CopyFramebuffer( 0, 0, width, height, false );
	currentRenderCopied = true;
}

bool RB_ARB2_HasCurrentRenderCopy() {
	return currentRenderCopied;
}

void RB_ARB2_BeginViewFrame() {
	// These are the per-view back-end caches reset by retail
	// RB_BeginDrawingView.  In particular, framebuffer readiness cannot leak
	// from a subview or the preceding frame into this view.
	postProcessFrameBufferReady = false;
	currentRenderCopied = false;
	activeAmbientCache = NULL;
	activeWeightCache = NULL;
	activeSkinnedTriangles = NULL;
	activeHardwareSkinning = false;
	activeHardSkinning = false;
	activeProgram = NULL;
	vertexAttribMaterial = NULL;
	activeMaterial = NULL;
	activeStage = NULL;
	activeSurface = NULL;
}

void RB_ARB2_ResetPostProcessingFrameBuffer() {
	// Retail clears the persistent framebuffer-ready latch immediately before
	// the final SS_LAST pass, not inside RB_ARB2_DrawShaderPasses.  Fullscreen
	// utility draws also use SS_LAST and must not implicitly reset this latch.
	postProcessFrameBufferReady = false;
}

void RB_ARB2_SetVertexPointers( const srfTriangles_t* triangles, bool& weightCacheModified ) {
	weightCacheModified = false;
	activeHardwareSkinning = false;
	activeHardSkinning = false;
	if ( triangles == NULL || rbinds == NULL || qglVertexAttribPointerARB == NULL ) return;
	const byte* vertexBase = NULL;
	if ( triangles->ambientCache != NULL ) {
		vertexBase = static_cast< const byte* >( CachePosition( triangles->ambientCache, false ) );
		activeAmbientCache = triangles->ambientCache;
	} else {
		if ( qglBindBufferARB != NULL ) qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		vertexBase = reinterpret_cast< const byte* >( triangles->verts );
		activeAmbientCache = NULL;
	}
	const bool residentVertexOffset = triangles->ambientCache != NULL && triangles->ambientCache->vbo != 0;
	if ( vertexBase == NULL && !residentVertexOffset ) return;
	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 3, GL_FLOAT, DRAWVERT_SIZE, vertexBase );
	qglVertexAttribPointerARB( rbinds->texCoordAttrib->GetAttribIndex(), 2, GL_SHORT, GL_FALSE, DRAWVERT_SIZE, VertexPointerOffset( vertexBase, 28 ) );
	qglVertexAttribPointerARB( rbinds->tangentAttrib->GetAttribIndex(), 2, GL_SHORT, GL_FALSE, DRAWVERT_SIZE, VertexPointerOffset( vertexBase, 20 ) );
	qglVertexAttribPointerARB( rbinds->normalAttrib->GetAttribIndex(), 2, GL_SHORT, GL_FALSE, DRAWVERT_SIZE, VertexPointerOffset( vertexBase, 16 ) );
	qglVertexAttribPointerARB( rbinds->colorAttrib->GetAttribIndex(), 4, GL_UNSIGNED_BYTE, GL_TRUE, DRAWVERT_SIZE, VertexPointerOffset( vertexBase, 12 ) );
	qglVertexAttribPointerARB( rbinds->signAttrib->GetAttribIndex(), 4, GL_UNSIGNED_BYTE, GL_FALSE, DRAWVERT_SIZE, VertexPointerOffset( vertexBase, 24 ) );

	const bool hardSkinning = ( triangles->dsFlags & 4 ) != 0;
	if ( triangles->weightCache == NULL && !hardSkinning ) {
		weightCacheModified = activeWeightCache != NULL;
		activeWeightCache = NULL;
		return;
	}
	activeHardwareSkinning = !hardSkinning;
	activeHardSkinning = hardSkinning;

	// ARB vertex programs receive the retail joint palette through program
	// environment registers 32..; GLSL additionally mirrors it to its uniform
	// array in RB_ARB2_SetupProgram.
	if ( !hardSkinning && triangles->joints != NULL && triangles->numJoints > 0 ) {
		if ( r_skinningDualQuaternion.GetBool() && qglProgramEnvParameter4fvARB != NULL ) {
			for ( int jointIndex = 0; jointIndex < triangles->numJoints; ++jointIndex ) {
				float dualQuat[ 2 ][ 4 ];
				triangles->joints[ jointIndex ].ToDualQuat( dualQuat );
				qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 32 + jointIndex * 3, dualQuat[ 0 ] );
				qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 33 + jointIndex * 3, dualQuat[ 1 ] );
			}
		} else if ( glConfig.EXTGpuProgramParametersAvailable && qglProgramEnvParameters4fvEXT != NULL ) {
			qglProgramEnvParameters4fvEXT( GL_VERTEX_PROGRAM_ARB, 32, 3 * triangles->numJoints, triangles->joints[ 0 ].ToFloatPtr() );
		} else if ( qglProgramEnvParameter4fvARB != NULL ) {
			for ( int jointIndex = 0; jointIndex < triangles->numJoints; ++jointIndex ) {
				const float* matrix = triangles->joints[ jointIndex ].ToFloatPtr();
				qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 32 + jointIndex * 3, matrix );
				qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 33 + jointIndex * 3, matrix + 4 );
				qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 34 + jointIndex * 3, matrix + 8 );
			}
		}
	}

	if ( triangles->weightCache != NULL && !hardSkinning ) {
		const byte* weights = static_cast< const byte* >( CachePosition( triangles->weightCache, false ) );
		if ( weights != NULL || triangles->weightCache->vbo != 0 ) {
			qglVertexAttribPointerARB( rbinds->weightIndexAttrib->GetAttribIndex(), 4, GL_UNSIGNED_BYTE, GL_FALSE, 8, weights );
			qglVertexAttribPointerARB( rbinds->weightValueAttrib->GetAttribIndex(), 4, GL_UNSIGNED_BYTE, GL_TRUE, 8, VertexPointerOffset( weights, 4 ) );
			weightCacheModified = activeWeightCache != triangles->weightCache;
			activeWeightCache = triangles->weightCache;
		}
	} else if ( hardSkinning && triangles->ambientCache != NULL ) {
		weightCacheModified = activeWeightCache != triangles->ambientCache;
		activeWeightCache = triangles->ambientCache;
	}
}

void RB_ARB2_SetupProgram( const sdDeclRenderProgram* program, int stateBits, cullType_t cullType, const srfTriangles_t* triangles ) {
	const sdDeclRenderProgram* selectedProgram = RB_ARB2_SelectProgram( program, activePass, activeEntity, activeMaterial );
	if ( selectedProgram == NULL ) return;
	selectedProgram->SetState( stateBits, cullType );
	// RB_ARB2_DrawDepth sets the retail depth-fill override around the whole
	// pass.  Applying it through GL_State is essential: issuing a raw
	// glColorMask here leaves the cached state vector out of sync and can keep
	// every later interaction colour-masked.
	if ( depthFillNoColour ) GL_State( 0x1E00 );
	if ( activeProgram != selectedProgram ) {
		selectedProgram->Bind();
		activeProgram = selectedProgram;
	}
	if ( ( activeHardwareSkinning || activeHardSkinning ) && triangles != NULL && triangles != activeSkinnedTriangles ) {
		selectedProgram->UpdateHWSkinningParameters( triangles->joints, triangles->numJoints );
		activeSkinnedTriangles = triangles;
	}
	selectedProgram->UpdateParameters();
	BindProgramTextures( selectedProgram );
}

void RB_ARB2_DrawWithProgram( const srfTriangles_t* triangles, const sdDeclRenderProgram* program, int stateBits, cullType_t cullType ) {
	RB_ARB2_SetupProgram( program, stateBits, cullType, triangles );
	RB_DrawElementsWithCounters( triangles );
}

void RB_ARB2_SetupProgram_Simple( const sdDeclRenderProgram* program, int stateBits, cullType_t cullType, const srfTriangles_t* triangles ) {
	// The retail "Simple" path receives a program that has already been
	// selected and deliberately does not traverse the alternate chain again.
	if ( program == NULL ) return;
	program->SetState( stateBits, cullType );
	if ( depthFillNoColour ) GL_State( 0x1E00 );
	if ( activeProgram != program ) {
		program->Bind();
		activeProgram = program;
	}
	if ( ( activeHardwareSkinning || activeHardSkinning ) && triangles != NULL && triangles != activeSkinnedTriangles ) {
		program->UpdateHWSkinningParameters( triangles->joints, triangles->numJoints );
		activeSkinnedTriangles = triangles;
	}
	program->UpdateParameters();
	BindProgramTextures( program );
}

void RB_ARB2_SetupLightSpace( const renderLight_t* light, const float modelMatrix[ 16 ] ) {
	if ( light == NULL || modelMatrix == NULL || rbinds == NULL ) {
		return;
	}
	idPlane lightProject[ 4 ];
	idVec3 globalLightOrigin;
	const idMaterial* lightMaterial;
	idImage* falloffImage;
	R_DeriveLightData( *light, lightProject, globalLightOrigin, lightMaterial, falloffImage );

	const idVec3 delta(
		globalLightOrigin.x - modelMatrix[ 12 ],
		globalLightOrigin.y - modelMatrix[ 13 ],
		globalLightOrigin.z - modelMatrix[ 14 ]
	);
	const idVec3 localLightOrigin(
		delta.x * modelMatrix[ 0 ] + delta.y * modelMatrix[ 1 ] + delta.z * modelMatrix[ 2 ],
		delta.x * modelMatrix[ 4 ] + delta.y * modelMatrix[ 5 ] + delta.z * modelMatrix[ 6 ],
		delta.x * modelMatrix[ 8 ] + delta.y * modelMatrix[ 9 ] + delta.z * modelMatrix[ 10 ]
	);
	rbinds->lightOrigin->Set( localLightOrigin.x, localLightOrigin.y, localLightOrigin.z, 0.0f );

	idVec3 globalDirection = light->flags.parallel ? light->lightCenter : light->axis[ 0 ];
	globalDirection.Normalize();
	const idVec3 localDirection(
		globalDirection.x * modelMatrix[ 0 ] + globalDirection.y * modelMatrix[ 1 ] + globalDirection.z * modelMatrix[ 2 ],
		globalDirection.x * modelMatrix[ 4 ] + globalDirection.y * modelMatrix[ 5 ] + globalDirection.z * modelMatrix[ 6 ],
		globalDirection.x * modelMatrix[ 8 ] + globalDirection.y * modelMatrix[ 9 ] + globalDirection.z * modelMatrix[ 10 ]
	);
	rbinds->lightDirection->Set( localDirection.x, localDirection.y, localDirection.z, light->lightRadius.Length() );

	const idPlane localS = GlobalPlaneToLocal( lightProject[ 0 ], modelMatrix );
	const idPlane localT = GlobalPlaneToLocal( lightProject[ 1 ], modelMatrix );
	const idPlane localQ = GlobalPlaneToLocal( lightProject[ 2 ], modelMatrix );
	const idPlane localFalloff = GlobalPlaneToLocal( lightProject[ 3 ], modelMatrix );
	rbinds->lightProject_s->Set( localS.ToFloatPtr() );
	rbinds->lightProject_t->Set( localT.ToFloatPtr() );
	rbinds->lightProject_q->Set( localQ.ToFloatPtr() );
	rbinds->lightFalloff_s->Set( localFalloff.ToFloatPtr() );
	rbinds->lightRadius->Set( light->lightRadius.x, light->lightRadius.y, light->lightRadius.z, 0.0f );
	if ( falloffImage != NULL ) {
		rbinds->lightFalloffMap->Set( falloffImage );
	}
}

void RB_ARB2_SetupLightSpace( const drawSurf_s* surface ) {
	if ( surface == NULL || surface->space == NULL || activeViewLight == NULL || rbinds == NULL ) return;
	const float* modelMatrix = surface->space->modelMatrix;
	const idVec3 delta(
		activeViewLight->globalLightOrigin.x - modelMatrix[ 12 ],
		activeViewLight->globalLightOrigin.y - modelMatrix[ 13 ],
		activeViewLight->globalLightOrigin.z - modelMatrix[ 14 ]
	);
	rbinds->lightOrigin->Set(
		delta.x * modelMatrix[ 0 ] + delta.y * modelMatrix[ 1 ] + delta.z * modelMatrix[ 2 ],
		delta.x * modelMatrix[ 4 ] + delta.y * modelMatrix[ 5 ] + delta.z * modelMatrix[ 6 ],
		delta.x * modelMatrix[ 8 ] + delta.y * modelMatrix[ 9 ] + delta.z * modelMatrix[ 10 ],
		0.0f
	);
	const idVec3& direction = activeViewLight->globalLightDirection;
	const float shadowDistance = activeViewLight->lightDef != NULL && activeViewLight->lightDef->flags.atmosphereLight ?
		surface->shadowProjectDist : activeViewLight->lightRadiusLength;
	rbinds->lightDirection->Set(
		direction.x * modelMatrix[ 0 ] + direction.y * modelMatrix[ 1 ] + direction.z * modelMatrix[ 2 ],
		direction.x * modelMatrix[ 4 ] + direction.y * modelMatrix[ 5 ] + direction.z * modelMatrix[ 6 ],
		direction.x * modelMatrix[ 8 ] + direction.y * modelMatrix[ 9 ] + direction.z * modelMatrix[ 10 ],
		shadowDistance
	);
	idPlane localS = GlobalPlaneToLocal( activeViewLight->lightProject[ 0 ], modelMatrix );
	idPlane localT = GlobalPlaneToLocal( activeViewLight->lightProject[ 1 ], modelMatrix );
	const idPlane localQ = GlobalPlaneToLocal( activeViewLight->lightProject[ 2 ], modelMatrix );
	const idPlane localFalloff = GlobalPlaneToLocal( activeViewLight->lightProject[ 3 ], modelMatrix );
	if ( activeLightHasTextureMatrix ) {
		const idPlane inputProject[ 3 ] = { localS, localT, localQ };
		idPlane outputProject[ 2 ];
		RB_BakeTextureMatrixIntoTexgenAligned( outputProject, inputProject, activeLightTextureMatrix );
		localS = outputProject[ 0 ];
		localT = outputProject[ 1 ];
	}
	rbinds->lightProject_s->Set( localS.ToFloatPtr() );
	rbinds->lightProject_t->Set( localT.ToFloatPtr() );
	rbinds->lightProject_q->Set( localQ.ToFloatPtr() );
	rbinds->lightFalloff_s->Set( localFalloff.ToFloatPtr() );
	if ( activeViewLight->falloffImage != NULL ) rbinds->lightFalloffMap->Set( activeViewLight->falloffImage );
}

bool RB_ARB2_UseStage( const materialStage_t* stage, const float* materialRegisters, rbDrawPass_t pass ) {
	if ( stage == NULL || materialRegisters == NULL || materialRegisters[ stage->conditionRegister ] == 0.0f ) {
		return false;
	}
	const bool interaction = stage->renderProgram != NULL && stage->renderProgram->IsInteraction();
	switch ( pass ) {
		case RBP_DEPTH:
		case RBP_AMBIENT:
			return stage->depthStage || ( stage->renderProgram != NULL && stage->renderProgram->GetAmbientProgram() != NULL );
		case RBP_INTERACTION:
			return !stage->depthStage && interaction;
		case RBP_SHADER:
			return !stage->depthStage && !interaction;
	}
	return false;
}

bool RB_ARB2_UseStage( const materialStage_t* stage, const float* materialRegisters ) {
	return RB_ARB2_UseStage( stage, materialRegisters, activePass );
}

const sdDeclRenderProgram* RB_ARB2_SelectProgram( const sdDeclRenderProgram* inProgram, rbDrawPass_t pass, const renderEntity_t* entity, const idMaterial* material ) {
	const sdDeclRenderProgram* program = inProgram;
	for ( int iteration = 0; program != NULL; ++iteration ) {
		if ( iteration == 20 ) {
			common->Warning( "Select render program '%s' exceeded the alternate-program chain limit",
				inProgram != NULL ? inProgram->GetName() : "<null>" );
			return rbinds != NULL ? rbinds->trivialProgram : NULL;
		}
		const sdDeclRenderProgram* alternate = NULL;
		const bool hardSkinning = !activeShadowSelection && activeSurface != NULL && activeSurface->geo != NULL &&
			( activeSurface->geo->dsFlags & 4 ) != 0;
		const bool hardwareSkinning = !activeShadowSelection && !hardSkinning && activeSurface != NULL &&
			activeSurface->geo != NULL && activeSurface->geo->weightCache != NULL;
		const bool instancing = !activeShadowSelection && activeSurface != NULL && activeSurface->space != NULL && activeSurface->space->numInsts > 0;
		const bool alphaToCoverage = activeSurfaceUsesAlphaToCoverage &&
			glConfig.samples > 0 && r_useAlphaToCoverage.GetBool();
		const bool coverage = activeSurface != NULL && activeSurface->space != NULL &&
			!( glConfig.samples > 0 && r_useSampleCoverage.GetBool() ) &&
			( activeShadowSelection
				? ( r_useShadowDitherMask.GetBool() && activeSurface->space->coverage <= 0.93333334f )
				: ( r_useDitherMask.GetBool() && activeSurface->space->coverage < 1.0f ) );
		const bool depthProgram = activeSurface != NULL && ( activeSurface->dsFlags & 2 ) != 0 && r_softParticles.GetBool();
		bool lodProgram = false;
		if ( activeSurface != NULL && activeSurface->geo != NULL && activeSurface->space != NULL && RB_GetDrawView() != NULL ) {
			float distance;
			if ( activeSurface->space->insts != NULL && entity != NULL ) {
				distance = entity->bounds.ShortestDistance( RB_GetDrawView()->vieworg );
			} else {
				distance = activeSurface->geo->bounds.ShortestDistance( activeLocalViewOrigin );
			}
			lodProgram = distance > r_renderProgramLodDistance.GetFloat();
		}
		const bool earlyCull = activeSurface != NULL && activeSurface->space != NULL && activeSurface->space->foliageDepthHack && RB_GetDrawView() != NULL && RB_GetDrawView()->foliageDepthHack > 0.0f;
		const bool ambientLit = activeViewLight != NULL && activeViewLight->lightDef != NULL &&
			activeViewLight->lightDef->flags.atmosphereLight && r_megaDrawMethod.GetInteger() != 0;
		if ( ( pass == RBP_AMBIENT || pass == RBP_DEPTH ) && program->GetAmbientProgram() != NULL ) {
			alternate = program->GetAmbientProgram();
		} else if ( hardwareSkinning && program->GetHardwareSkinningProgram() != NULL ) {
			alternate = program->GetHardwareSkinningProgram();
		} else if ( hardSkinning && program->GetHardSkinningProgram() != NULL ) {
			alternate = program->GetHardSkinningProgram();
		} else if ( instancing && program->GetInstanceProgram() != NULL ) {
			alternate = program->GetInstanceProgram();
		} else if ( alphaToCoverage && program->GetAlphaToCoverageProgram() != NULL ) {
			alternate = program->GetAlphaToCoverageProgram();
		} else if ( coverage && program->GetCoverageProgram() != NULL ) {
			alternate = program->GetCoverageProgram();
		} else if ( depthProgram && program->GetDepthProgram() != NULL ) {
			alternate = program->GetDepthProgram();
		} else if ( lodProgram && program->GetLODProgram() != NULL ) {
			alternate = program->GetLODProgram();
		} else if ( earlyCull && program->GetEarlyCullProgram() != NULL ) {
			alternate = program->GetEarlyCullProgram();
		} else if ( !activeShadowSelection && program->GetFallbackProgram() != NULL &&
			Max( com_gpuSpec.GetInteger(), activeSurface != NULL && activeSurface->space != NULL ? static_cast< int >( activeSurface->space->minGpuSpec ) : 0 ) < program->GetMachineSpec() ) {
			alternate = program->GetFallbackProgram();
		} else if ( !activeShadowSelection && ambientLit && program->GetAmbientLitProgram() != NULL ) {
			alternate = program->GetAmbientLitProgram();
		}
		if ( alternate == NULL || alternate == program ) {
			return program;
		}
		program = alternate;
	}
	return rbinds != NULL ? rbinds->trivialProgram : NULL;
}

const sdDeclRenderProgram* RB_ARB2_SelectProgram( const sdDeclRenderProgram* program ) {
	return RB_ARB2_SelectProgram( program, activePass, activeEntity, activeMaterial );
}

void RB_ARB2_DrawSurface( const drawSurf_s* surface, const idMaterial* material, const float* materialRegisters,
		void ( *customSpaceCallback )( const drawSurf_s* ) ) {
	if ( surface == NULL || surface->geo == NULL || surface->space == NULL || material == NULL || materialRegisters == NULL ) return;
	if ( surface->space->culled ) return;
	if ( activePass == RBP_AMBIENT && material->Coverage() == MC_TRANSLUCENT ) return;
	if ( activePass == RBP_DEPTH && material->Coverage() != MC_OPAQUE ) return;

	activeEntity = surface->space->entityDef;
	activeMaterial = material;
	activeSurface = surface;
	activeStage = NULL;
	activeSurfaceUsesAlphaToCoverage = false;
	byte enabledStages[ MAX_SHADER_STAGES ];
	for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
		const materialStage_t* stage = material->GetStage( stageIndex );
		const bool enabled = RB_ARB2_UseStage( stage, materialRegisters );
		enabledStages[ stageIndex ] = enabled ? 1 : 0;
		if ( enabled && ( stage->drawStateBits & 0x4000 ) != 0 ) {
			activeSurfaceUsesAlphaToCoverage = true;
		}
	}
	bool weightCacheModified = false;
	RB_ARB2_SetVertexPointers( surface->geo, weightCacheModified );
	// Retail chooses the complete vertex-array mask once per material/weight
	// layout.  Changing it for each stage leaves later stages with a subset of
	// the arrays selected by their alternate programs and is particularly
	// destructive for the megatexture and frontend ambient programs.
	if ( weightCacheModified || vertexAttribMaterial != material ) {
		int requiredVertexAttribs = 0;
		for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
			if ( enabledStages[ stageIndex ] == 0 ) continue;
			const materialStage_t* stage = material->GetStage( stageIndex );
			const sdDeclRenderProgram* selectedProgram = RB_ARB2_SelectProgram( stage->renderProgram );
			if ( selectedProgram != NULL ) requiredVertexAttribs |= selectedProgram->GetRequiredVertexAttribs();
		}
		GL_EnableVertexAttribs( requiredVertexAttribs );
		vertexAttribMaterial = material;
	}
	const bool spaceChanged = activeDrawSpace != surface->space;
	RB_ARB2_SetSpace( surface->space, true );
	if ( spaceChanged ) {
		if ( customSpaceCallback != NULL ) customSpaceCallback( surface );
		SetAmbientCubeMapBindings( surface->space );
		if ( surface->space->envCubemap != NULL ) rbinds->environmentCubeMap->Set( surface->space->envCubemap );
	}
	idVec4 stuffParameters = rbinds->stuffParameters->GetVec4();
	stuffParameters.w = surface->geo->params[ TRIPARM_DISTANCESCALE ];
	rbinds->stuffParameters->Set( stuffParameters );
	if ( r_useScissor.GetBool() && RB_GetViewDef() != NULL ) {
		glScissor(
			surface->scissorRect.x1 + RB_GetViewDef()->viewport.x1,
			surface->scissorRect.y1 + RB_GetViewDef()->viewport.y1,
			surface->scissorRect.x2 - surface->scissorRect.x1 + 1,
			surface->scissorRect.y2 - surface->scissorRect.y1 + 1
		);
	}

	// Retail applies material/stage polygon-offset and destination-buffer state
	// only on the non-light (or ambient-lighting) branch.  Interaction stages
	// inherit the light pass state and must not redirect or bias that pass.
	const bool materialPolygonOffset = activePass != RBP_INTERACTION && material->TestMaterialFlag( MF_POLYGONOFFSET );
	if ( materialPolygonOffset ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * material->GetPolygonOffset() );
	}

	auto drawMaterialStages = [&]() {
		for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
			const materialStage_t* stage = material->GetStage( stageIndex );
		if ( enabledStages[ stageIndex ] == 0 ) continue;
		activeStage = stage;
		if ( stage->megaTexture != NULL ) {
			// SetRenderBindings owns the retail world/view update.  This
			// reconstructed megatexture also keeps a per-surface fallback for
			// maps without the retail ST mapping grid.
			stage->megaTexture->SetMappingForSurface( surface->geo );
		}
		if ( activePass != RBP_INTERACTION ) {
			rbinds->diffuseColor->Set( 1.0f, 1.0f, 1.0f, 1.0f );
		}
		idMaterial::SetRenderBindings( stage, materialRegisters, surface->geo->texCoordScale );
		if ( activePass == RBP_INTERACTION ) {
			const bool bakedAtmosphereColor = material->TestMaterialFlag( MF_BAKEDINATMOSLIGHTCOL ) &&
				activeViewLight != NULL && activeViewLight->lightDef != NULL && activeViewLight->lightDef->flags.atmosphereLight;
			if ( !bakedAtmosphereColor ) {
				idVec4 diffuse = rbinds->diffuseColor->GetVec4();
				idVec4 specular = rbinds->specularColor->GetVec4();
				diffuse.Set( diffuse.x * activeLightColor.x, diffuse.y * activeLightColor.y, diffuse.z * activeLightColor.z, diffuse.w * activeLightColor.w );
				specular.Set( specular.x * activeLightColor.x, specular.y * activeLightColor.y, specular.z * activeLightColor.z, specular.w * activeLightColor.w );
				rbinds->diffuseColor->Set( diffuse );
				rbinds->specularColor->Set( specular );
			}
		}
		const bool ordinaryStageState = activePass != RBP_INTERACTION;
		if ( ordinaryStageState && ( stage->drawStateBits & 0x2000 ) ) glLineWidth( stage->lineWidth );
		if ( ordinaryStageState && stage->privatePolygonOffset != 0.0f ) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * stage->privatePolygonOffset );
		}
		idImage* destination = NULL;
		viewDef_s* view = RB_GetViewDef();
		if ( ordinaryStageState && stage->destinationBuffer >= 0 && stage->destinationBuffer < 2 && globalImages != NULL ) {
			destination = globalImages->postProcessBuffer[ stage->destinationBuffer ];
			if ( destination != NULL ) glViewport( 0, 0, destination->uploadWidth, destination->uploadHeight );
		}
		// RB_ARB2_SetupProgram owns the one retail alternate-program traversal.
		// Passing an already selected program here walks the alternate chain a
		// second time (ambient -> ambient, skinning -> skinning, etc.) and can
		// bind a variant that does not match this stage's vertex layout.
		RB_ARB2_SetupProgram( stage->renderProgram, stage->drawStateBits, stage->cullType, surface->geo );
		RB_DrawElementsWithCounters( surface->geo );
		currentRenderCopied = false;
		if ( ordinaryStageState && ( stage->drawStateBits & 0x2000 ) ) glLineWidth( 1.0f );
		if ( ordinaryStageState && stage->privatePolygonOffset != 0.0f && !materialPolygonOffset ) glDisable( GL_POLYGON_OFFSET_FILL );
		if ( destination != NULL && view != NULL ) {
			destination->CopyFramebuffer( 0, 0, destination->uploadWidth, destination->uploadHeight, false );
			glViewport( view->viewport.x1, view->viewport.y1,
				view->viewport.x2 - view->viewport.x1 + 1, view->viewport.y2 - view->viewport.y1 + 1 );
		}
		// unk_893DB1 in the retail back end is the depth/ambient selector.
		// That path deliberately submits the first eligible stage only; drawing
		// every ambient alternate stacks later stages over the lit result and is
		// a direct cause of uniform/flat-looking materials.
		if ( activePass == RBP_AMBIENT || activePass == RBP_DEPTH ) break;
		}
	};

	if ( activePass == RBP_INTERACTION && activeViewLight != NULL && activeViewLight->material != NULL ) {
		for ( int lightStageIndex = 0; lightStageIndex < activeViewLight->material->GetNumStages(); ++lightStageIndex ) {
			const materialStage_t* lightStage = activeViewLight->material->GetStage( lightStageIndex );
			if ( !SetupActiveLightStage( lightStage ) ) continue;
			// The space callback establishes the unmodified local projection once.
			// A light-stage texture matrix is then baked for this stage, matching
			// the retail inner light-stage loop.
			RB_ARB2_SetupLightSpace( surface );
			drawMaterialStages();
		}
		activeLightHasTextureMatrix = false;
	} else {
		drawMaterialStages();
	}
	if ( materialPolygonOffset ) glDisable( GL_POLYGON_OFFSET_FILL );
	activeEntity = NULL;
	activeMaterial = NULL;
	activeSurface = NULL;
	activeStage = NULL;
	activeSurfaceUsesAlphaToCoverage = false;
}

void RB_ARB2_DrawSurface_Simple( const drawSurf_s* surface, const idMaterial* material, const float* materialRegisters ) {
	if ( surface == NULL || surface->geo == NULL || surface->space == NULL || material == NULL || materialRegisters == NULL || surface->space->culled ) return;
	activeEntity = surface->space->entityDef;
	activeMaterial = material;
	activeSurface = surface;
	activeStage = NULL;
	bool weightCacheModified = false;
	RB_ARB2_SetVertexPointers( surface->geo, weightCacheModified );
	if ( weightCacheModified || vertexAttribMaterial != material ) {
		int requiredVertexAttribs = 0;
		for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
			const materialStage_t* stage = material->GetStage( stageIndex );
			if ( materialRegisters[ stage->conditionRegister ] == 0.0f || stage->depthStage ||
					stage->renderProgram == NULL || stage->renderProgram->IsInteraction() ) continue;
			requiredVertexAttribs |= stage->renderProgram->GetRequiredVertexAttribs();
		}
		GL_EnableVertexAttribs( requiredVertexAttribs );
		vertexAttribMaterial = material;
	}

	for ( int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex ) {
		const materialStage_t* stage = material->GetStage( stageIndex );
		if ( materialRegisters[ stage->conditionRegister ] == 0.0f || stage->depthStage ||
				stage->renderProgram == NULL || stage->renderProgram->IsInteraction() ) continue;
		activeStage = stage;
		idImage* destination = NULL;
		viewDef_s* view = RB_GetViewDef();
		if ( stage->destinationBuffer >= 0 && stage->destinationBuffer < 2 && globalImages != NULL ) {
			destination = globalImages->postProcessBuffer[ stage->destinationBuffer ];
			if ( destination != NULL ) glViewport( 0, 0, destination->uploadWidth, destination->uploadHeight );
		}
		rbinds->diffuseColor->Set( 1.0f, 1.0f, 1.0f, 1.0f );
		idMaterial::SetRenderBindings( stage, materialRegisters, surface->geo->texCoordScale );
		RB_ARB2_SetupProgram_Simple( stage->renderProgram, stage->drawStateBits, stage->cullType, surface->geo );
		RB_DrawElementsWithCounters( surface->geo );
		currentRenderCopied = false;
		if ( destination != NULL && view != NULL ) {
			destination->CopyFramebuffer( 0, 0, destination->uploadWidth, destination->uploadHeight, false );
			glViewport( view->viewport.x1, view->viewport.y1,
				view->viewport.x2 - view->viewport.x1 + 1, view->viewport.y2 - view->viewport.y1 + 1 );
		}
	}
	activeEntity = NULL;
	activeMaterial = NULL;
	activeSurface = NULL;
	activeStage = NULL;
	activeSurfaceUsesAlphaToCoverage = false;
}

void RB_ARB2_DrawSurfacePass( const drawSurf_s* surface, const idMaterial* material, const float* materialRegisters,
		rbDrawPass_t pass, void ( *customSpaceCallback )( const drawSurf_s* ) ) {
	const rbDrawPass_t savedPass = activePass;
	activePass = pass;
	RB_ARB2_DrawSurface( surface, material, materialRegisters, customSpaceCallback );
	activePass = savedPass;
}

void RB_ARB2_CreateDrawInteractions( const drawSurf_s* surface, bool ambient ) {
	if ( r_skipInteractions.GetInteger() == 2 ) return;
	const rbDrawPass_t savedPass = activePass;
	activePass = RBP_INTERACTION;
	void ( *lightSpaceCallback )( const drawSurf_s* ) = static_cast< void ( * )( const drawSurf_s* ) >( RB_ARB2_SetupLightSpace );
	for ( const drawSurf_s* current = surface; current != NULL; current = current->nextOnLight ) {
		if ( ambient && !current->material->TestMaterialFlag( MF_HASMEGA ) ) continue;
		RB_ARB2_DrawSurface( current, current->material, current->materialRegisters, lightSpaceCallback );
	}
	activePass = savedPass;
	RB_ARB2_ClearSpace();
}

namespace {

void DrawLightInteractions( viewLight_s* light, bool& stencilInitialized ) {
	if ( light == NULL || light->culled || light->material == NULL || light->material->IsFogLight() || light->material->IsBlendLight() ) return;
	if ( light->lightDef != NULL && light->lightDef->flags.atmosphereLight && r_skipAtmosInteractions.GetBool() ) return;
	if ( light->localInteractions == NULL && light->globalInteractions == NULL && light->translucentInteractions == NULL ) return;
	activeViewLight = light;
	if ( light->falloffImage != NULL ) rbinds->lightFalloffMap->Set( light->falloffImage );
	if ( ( light->globalShadows != NULL || light->localShadows != NULL ) && stencilInitialized ) {
		if ( r_useScissor.GetBool() && RB_GetViewDef() != NULL ) {
			glScissor(
				light->scissorRect.x1 + RB_GetViewDef()->viewport.x1,
				light->scissorRect.y1 + RB_GetViewDef()->viewport.y1,
				light->scissorRect.x2 - light->scissorRect.x1 + 1,
				light->scissorRect.y2 - light->scissorRect.y1 + 1
			);
		}
		glClear( GL_STENCIL_BUFFER_BIT );
	} else {
		glStencilFunc( GL_ALWAYS, 128, 0xFF );
		stencilInitialized = true;
	}
	DisableSampleCoverage();
	RB_ARB2_StencilShadowPass( light->globalShadows, SelectShadowProgram( light ), rbinds->shadowInvariantProgram,
		light->lightDef != NULL && light->lightDef->flags.atmosphereLight,
		r_shadowPolygonFactor.GetFloat(), r_shadowPolygonOffset.GetFloat() );
	RB_ARB2_CreateDrawInteractions( light->localInteractions, false );
	DisableSampleCoverage();
	RB_ARB2_StencilShadowPass( light->localShadows, SelectShadowProgram( light ), rbinds->shadowInvariantProgram,
		light->lightDef != NULL && light->lightDef->flags.atmosphereLight,
		r_shadowPolygonFactor.GetFloat(), r_shadowPolygonOffset.GetFloat() );
	RB_ARB2_CreateDrawInteractions( light->globalInteractions, false );
	if ( !r_skipTranslucent.GetBool() ) {
		glStencilFunc( GL_ALWAYS, 128, 0xFF );
		RB_ARB2_CreateDrawInteractions( light->translucentInteractions, false );
	}
	activeLightHasTextureMatrix = false;
	activeViewLight = NULL;
}

}

void RB_ARB2_DrawDepth() {
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) RB_ARB2_DrawDepth( view->drawSurfs, view->numDrawSurfs );
}

void RB_ARB2_DrawDepth( drawSurf_s** drawSurfs, int numDrawSurfs ) {
	if ( drawSurfs == NULL || numDrawSurfs <= 0 || RB_GetViewDef() == NULL ) return;
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 0xFF );
	activePass = RBP_DEPTH;
	depthFillNoColour = r_depthFillNoColour.GetBool();
	for ( int index = 0; index < numDrawSurfs; ++index ) {
		drawSurf_s* surface = drawSurfs[ index ];
		if ( surface->material->GetSort() == SS_SUBVIEW || surface->material->Coverage() != MC_OPAQUE ) continue;
		if ( surface->space == NULL || surface->space->scissorRect.Area() < r_depthFillCutoff.GetInteger() ) continue;
		RB_ARB2_DrawSurface( surface, surface->material, surface->materialRegisters, NULL );
	}
	RB_ARB2_ClearSpace();
	depthFillNoColour = false;
	activePass = RBP_SHADER;
}

void R_FillDepthAmbient() {
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) R_FillDepthAmbient( view->drawSurfs, view->numDrawSurfs );
}

void R_FillDepthAmbient( drawSurf_s** drawSurfs, int numDrawSurfs ) {
	if ( drawSurfs == NULL || numDrawSurfs <= 0 ) return;
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 0xFF );
	activePass = RBP_AMBIENT;
	for ( int index = 0; index < numDrawSurfs; ++index ) {
		drawSurf_s* surface = drawSurfs[ index ];
		if ( surface->material->Coverage() == MC_TRANSLUCENT ) continue;
		if ( r_megaDrawMethod.GetInteger() != 0 && surface->space != NULL &&
				surface->space->ambSurf != NULL && surface->surfID >= 0 &&
				surface->surfID < surface->space->maxSurfID &&
				( surface->space->ambSurf[ surface->surfID >> 5 ] & ( 1u << ( surface->surfID & 31 ) ) ) != 0 ) {
			continue;
		}
		RB_ARB2_DrawSurface( surface, surface->material, surface->materialRegisters, NULL );
	}
	DisableSampleCoverage();
	RB_ARB2_ClearSpace();
	activePass = RBP_SHADER;
}

void R_DrawMTInteractions() {
	if ( r_megaDrawMethod.GetInteger() == 0 ) return;
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || view->atmosphereLight == NULL ) return;
	viewLight_s* light = view->atmosphereLight;
	if ( light->culled || light->material == NULL || light->material->IsFogLight() || light->material->IsBlendLight() ) return;
	activeViewLight = light;
	if ( light->falloffImage != NULL ) rbinds->lightFalloffMap->Set( light->falloffImage );
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 128, 0xFF );
	idVec4 savedFogDepths;
	bool drawAtmosphereLast = false;
	if ( rbinds != NULL && rbinds->fogDepths != NULL ) {
		savedFogDepths = rbinds->fogDepths->GetVec4();
		drawAtmosphereLast = view->atmosphere != NULL && view->atmosphere->DrawAtmosphereLast() && r_shadows.GetBool();
		if ( drawAtmosphereLast ) rbinds->fogDepths->Set( 0.0f, 0.0f, 0.0f, 0.0f );
	}
	RB_ARB2_CreateDrawInteractions( light->mtInteractions, false );
	activeLightHasTextureMatrix = false;
	DisableSampleCoverage();
	RB_ARB2_StencilShadowPass( light->globalShadows, SelectShadowProgram( light ), rbinds->shadowInvariantProgram,
		light->lightDef != NULL && light->lightDef->flags.atmosphereLight, r_shadowPolygonFactorMT.GetFloat(), r_shadowPolygonOffsetMT.GetFloat() );
	RB_ARB2_StencilShadowPass( light->localShadows, SelectShadowProgram( light ), rbinds->shadowInvariantProgram,
		light->lightDef != NULL && light->lightDef->flags.atmosphereLight, r_shadowPolygonFactorMT.GetFloat(), r_shadowPolygonOffsetMT.GetFloat() );
	activeViewLight = NULL;
	glStencilFunc( GL_LESS, 128, 0xFF );
	const idMaterial* shadowMultiply = declHolder.FindMaterial( "shadow/multiplitive", true );
	if ( shadowMultiply != NULL && light->lightDef != NULL ) {
		RB_DrawFullscreenQuad( shadowMultiply, light->lightDef->minSpecShadowColor );
	}
	glStencilFunc( GL_ALWAYS, 128, 0xFF );
	if ( drawAtmosphereLast ) {
		rbinds->fogDepths->Set( savedFogDepths );
		RB_ARB2_DrawAtmosphere( light->mtInteractions );
	}
}

void RB_ARB2_DrawInteractions() {
	if ( r_skipInteractions.GetInteger() == 1 ) {
		glStencilFunc( GL_ALWAYS, 128, 0xFF );
		return;
	}
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	bool stencilInitialized = r_megaDrawMethod.GetInteger() != 0;
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next ) {
		DrawLightInteractions( light, stencilInitialized );
	}
	activeViewLight = NULL;
	glStencilFunc( GL_ALWAYS, 128, 0xFF );
}

int RB_ARB2_DrawShaderPasses( int phase ) {
	viewDef_s* view = RB_GetViewDef();
	return view != NULL ? RB_ARB2_DrawShaderPasses( view->drawSurfs, view->numDrawSurfs, phase ) : 0;
}

int RB_ARB2_DrawShaderPasses( drawSurf_s** drawSurfs, int numDrawSurfs, int phase ) {
	if ( numDrawSurfs <= 0 || drawSurfs == NULL ) return 0;
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL && view->viewEntities != NULL && r_skipAmbient.GetBool() ) return numDrawSurfs;
	activePass = RBP_SHADER;

	const float firstSort = drawSurfs[ 0 ]->material->GetSort();
	if ( !postProcessFrameBufferReady && firstSort < static_cast< float >( phase ) &&
		( ( firstSort >= SS_REFRACTION && firstSort < SS_FAR_PRE_ATMOS ) || firstSort >= SS_POST_PROCESS ) &&
		!r_skipRefractCopy.GetBool() ) {
		RB_ARB2_SetupPostProcessingFrameBuffer();
		RB_ARB2_CopyFramebufferColor();
	}

	int consumed = 0;
	if ( phase == SS_LAST && view != NULL && view->viewEntities == NULL && r_useMinimalGuiDraw.GetBool() ) {
		if ( r_useScissor.GetBool() ) {
			glScissor( view->viewport.x1 + view->scissor.x1, view->viewport.y1 + view->scissor.y1,
				view->scissor.x2 - view->scissor.x1 + 1, view->scissor.y2 - view->scissor.y1 + 1 );
		}
		RB_ARB2_ClearSpace();
		RB_ARB2_SetSpace( &view->worldSpace, true );
		for ( ; consumed < numDrawSurfs; ++consumed ) {
			drawSurf_s* surface = drawSurfs[ consumed ];
			if ( surface->material->TestMaterialFlag( MF_UPDATECURRENTRENDER ) && !currentRenderCopied ) {
				RB_ARB2_SetupPostProcessingFrameBuffer();
				RB_ARB2_CopyFramebufferColor();
			}
			RB_ARB2_DrawSurface_Simple( surface, surface->material, surface->materialRegisters );
			currentRenderCopied = false;
		}
	} else {
		for ( ; consumed < numDrawSurfs; ++consumed ) {
			drawSurf_s* surface = drawSurfs[ consumed ];
			if ( surface->material->GetSort() >= static_cast< float >( phase ) ) break;
			if ( surface->material->TestMaterialFlag( MF_UPDATECURRENTRENDER ) && !currentRenderCopied ) {
				RB_ARB2_SetupPostProcessingFrameBuffer();
				RB_ARB2_CopyFramebufferColor();
			}
			RB_ARB2_DrawSurface( surface, surface->material, surface->materialRegisters, NULL );
			currentRenderCopied = false;
		}
	}

	RB_ARB2_ClearSpace();
	return consumed;
}
