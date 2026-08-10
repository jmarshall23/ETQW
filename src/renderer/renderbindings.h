// Copyright (C) 2007 Id Software, Inc.
//
// The member order and offsets in this file are reconstructed from the
// original ETQW PDB.  Init is inline in the retail build and is visible in
// the decompilation of idRenderSystemLocal::Init.

#ifndef __RENDERER_RENDERBINDINGS_H__
#define __RENDERER_RENDERBINDINGS_H__

#include "../decllib/declTypeHolder.h"
#include "../framework/Common_public.h"

class sdDeclRenderBinding;
class sdDeclRenderProgram;

class sdRenderBindings {
public:
	void Init();

	const sdDeclRenderBinding* diffuseMap;
	const sdDeclRenderBinding* bumpMap;
	const sdDeclRenderBinding* specularMap;
	const sdDeclRenderBinding* heightMap;
	const sdDeclRenderBinding* map;
	const sdDeclRenderBinding* dithermask;
	const sdDeclRenderBinding* detailMult;
	const sdDeclRenderBinding* diffuseDetailMap;
	const sdDeclRenderBinding* specDetailMap;
	const sdDeclRenderBinding* bumpDetailMap;
	const sdDeclRenderBinding* cinematicY;
	const sdDeclRenderBinding* cinematicU;
	const sdDeclRenderBinding* cinematicV;
	const sdDeclRenderBinding* lightProjectionMap;
	const sdDeclRenderBinding* lightFalloffMap;
	const sdDeclRenderBinding* fogMap;
	const sdDeclRenderBinding* fogEnterMap;
	const sdDeclRenderBinding* ambientCubeMap;
	const sdDeclRenderBinding* ambientCubeMapSun;
	const sdDeclRenderBinding* specularCubeMap;
	const sdDeclRenderBinding* environmentCubeMap;
	const sdDeclRenderBinding* skyGradientCubeMap;
	const sdDeclRenderBinding* gradientMap;
	const sdDeclRenderBinding* ambientBrightness;
	const sdDeclRenderBinding* ambientAvgColor;
	const sdDeclRenderBinding* currentRenderTexelSize;
	const sdDeclRenderBinding* aspectSize;
	const sdDeclRenderBinding* texCoordAttrib;
	const sdDeclRenderBinding* tangentAttrib;
	const sdDeclRenderBinding* normalAttrib;
	const sdDeclRenderBinding* colorAttrib;
	const sdDeclRenderBinding* signAttrib;
	const sdDeclRenderBinding* weightIndexAttrib;
	const sdDeclRenderBinding* weightValueAttrib;
	const sdDeclRenderBinding* diffuseColor;
	const sdDeclRenderBinding* specularColor;
	const sdDeclRenderBinding* imageSize;
	const sdDeclRenderBinding* windWorld;
	const sdDeclRenderBinding* lightOrigin;
	const sdDeclRenderBinding* lightDirection;
	const sdDeclRenderBinding* viewUpWorld;
	const sdDeclRenderBinding* viewRightWorld;
	const sdDeclRenderBinding* viewDirectionWorld;
	const sdDeclRenderBinding* viewOrigin;
	const sdDeclRenderBinding* viewOriginWorld;
	const sdDeclRenderBinding* viewMovement;
	const sdDeclRenderBinding* lightProject_s;
	const sdDeclRenderBinding* lightProject_t;
	const sdDeclRenderBinding* lightProject_q;
	const sdDeclRenderBinding* lightFalloff_s;
	const sdDeclRenderBinding* bumpMatrix_s;
	const sdDeclRenderBinding* bumpMatrix_t;
	const sdDeclRenderBinding* diffuseMatrix_s;
	const sdDeclRenderBinding* diffuseMatrix_t;
	const sdDeclRenderBinding* specularMatrix_s;
	const sdDeclRenderBinding* specularMatrix_t;
	const sdDeclRenderBinding* colorModulate;
	const sdDeclRenderBinding* colorAdd;
	const sdDeclRenderBinding* specularPower;
	const sdDeclRenderBinding* alphaThresh;
	const sdDeclRenderBinding* fadeFraction;
	const sdDeclRenderBinding* coverage;
	const sdDeclRenderBinding* transposedProjectionMatrix_x;
	const sdDeclRenderBinding* transposedProjectionMatrix_y;
	const sdDeclRenderBinding* transposedProjectionMatrix_z;
	const sdDeclRenderBinding* transposedProjectionMatrix_w;
	const sdDeclRenderBinding* transposedModelMatrix_x;
	const sdDeclRenderBinding* transposedModelMatrix_y;
	const sdDeclRenderBinding* transposedModelMatrix_z;
	const sdDeclRenderBinding* transposedModelMatrix_w;
	const sdDeclRenderBinding* mvptMatrix_x;
	const sdDeclRenderBinding* mvptMatrix_y;
	const sdDeclRenderBinding* mvptMatrix_z;
	const sdDeclRenderBinding* mvptMatrix_w;
	const sdDeclRenderBinding* waveAmplitude;
	const sdDeclRenderBinding* wavePhase;
	const sdDeclRenderBinding* waveFrequency;
	const sdDeclRenderBinding* waveDirX;
	const sdDeclRenderBinding* waveDirY;
	const sdDeclRenderBinding* waveDirXQ;
	const sdDeclRenderBinding* waveDirYQ;
	const sdDeclRenderBinding* waveDirXW;
	const sdDeclRenderBinding* waveDirYW;
	const sdDeclRenderBinding* waveDirXYQW;
	const sdDeclRenderBinding* waveDirYYQW;
	const sdDeclRenderBinding* waveDirXXQW;
	const sdDeclRenderBinding* waveQW;
	const sdDeclRenderBinding* boxMins;
	const sdDeclRenderBinding* boxMaxs;
	const sdDeclRenderBinding* stuffParams;
	const sdDeclRenderBinding* proj2View;
	const sdDeclRenderBinding* pos2View;
	const sdDeclRenderBinding* detailFade;
	const sdDeclRenderBinding* lightRadius;
	const sdDeclRenderBinding* postTint;
	const sdDeclRenderBinding* postSaturationContrast;
	const sdDeclRenderBinding* postGlareParameters;
	const sdDeclRenderBinding* postScratch0Corr;
	const sdDeclRenderBinding* postScratch1Corr;
	const sdDeclRenderBinding* postScratch1TexelX;
	const sdDeclRenderBinding* postScratch0TexelY;
	const sdDeclRenderBinding* fogParams;
	const sdDeclRenderBinding* fogDepths;
	const sdDeclRenderBinding* fogColor;
	const sdDeclRenderBinding* fogRotation_x;
	const sdDeclRenderBinding* fogRotation_y;
	const sdDeclRenderBinding* fogRotation_z;
	const sdDeclRenderBinding* sunDirection;
	const sdDeclRenderBinding* sunDirectionWorld;
	const sdDeclRenderBinding* sunColor;
	const sdDeclRenderBinding* sunHaloParameters;
	const sdDeclRenderBinding* ambientScale;
	const sdDeclRenderBinding* stuffParameters;
	const sdDeclRenderBinding* fogViewMatrix_x;
	const sdDeclRenderBinding* fogViewMatrix_y;
	const sdDeclRenderBinding* fogViewMatrix_z;
	const sdDeclRenderBinding* fogEyePrecalc;
	const sdDeclRenderBinding* fogUpInView;
	const sdDeclRenderBinding* megaMaskParams[ 6 ];
	const sdDeclRenderBinding* megaTextureParams[ 6 ];
	const sdDeclRenderBinding* megaTextureLevel[ 6 ];
	const sdDeclRenderBinding* megaDetailTextureMask;
	const sdDeclRenderBinding* megaDetailTexture;
	const sdDeclRenderBinding* megaDetailTextureParams;
	const sdDeclRenderBinding* megaTextureOpacity15;
	const sdDeclRenderBinding* megaBlendOutDotP;
	const sdDeclRenderBinding* imgSequenceCur;
	const sdDeclRenderBinding* imgSequenceNext;
	const sdDeclRenderBinding* imgSequenceBlend;
	const sdDeclRenderBinding* lightColor_0;
	const sdDeclRenderBinding* lightColor_1;
	const sdDeclRenderBinding* lightColor_2;
	const sdDeclRenderBinding* lightColor_3;
	const sdDeclRenderBinding* lightOrigin_0;
	const sdDeclRenderBinding* lightOrigin_1;
	const sdDeclRenderBinding* lightOrigin_2;
	const sdDeclRenderBinding* lightOrigin_3;
	const sdDeclRenderBinding* lightFalloff_0;
	const sdDeclRenderBinding* lightFalloff_1;
	const sdDeclRenderBinding* lightFalloff_2;
	const sdDeclRenderBinding* lightFalloff_3;
	const sdDeclRenderBinding* foliageHackDistance;
	const sdDeclRenderProgram* trivialProgram;
	const sdDeclRenderProgram* trivialWithTextureMatrixProgram;
	const sdDeclRenderProgram* depthOnlyProgram;
	const sdDeclRenderProgram* depthAlphaProgram;
	const sdDeclRenderProgram* interactionBasicProgram;
	const sdDeclRenderProgram* interactionBasicDetailProgram;
	const sdDeclRenderProgram* interactionBasicAlphatestProgram;
	const sdDeclRenderProgram* interactionBasicDetailAlphatestProgram;
	const sdDeclRenderProgram* grass_alphatest;
	const sdDeclRenderProgram* shadowProgram;
	const sdDeclRenderProgram* shadowProgramProjected;
	const sdDeclRenderProgram* shadowInvariantProgram;
	const sdDeclRenderProgram* shadowParallelProgramProjected;
	const sdDeclRenderProgram* shadowParallelProgramInfinite;
	const sdDeclRenderProgram* fogLightProgram;
	const sdDeclRenderProgram* blendLightProgram;
	const sdDeclRenderProgram* occlusionProgram;
	const sdDeclRenderProgram* trivialWithTextureMatrix_BlendBlendAtmosphere;
	const sdDeclRenderProgram* trivialWithTextureMatrix_BlendFilterAtmosphere;
	const sdDeclRenderProgram* trivialWithTextureMatrix_BlendAddAtmosphere;
};

extern sdRenderBindings renderBindings;
extern sdRenderBindings* rbinds;

static_assert( sizeof( sdRenderBindings ) == 708, "sdRenderBindings must match the ETQW PDB layout" );

ID_INLINE void sdRenderBindings::Init() {
#define INIT_RENDER_BINDING( member ) \
	member = declHolder.FindRenderBinding( #member, false ); \
	if ( member == NULL ) { common->FatalError( "Unable to find render binding '%s'", #member ); }

	INIT_RENDER_BINDING( diffuseMap );
	INIT_RENDER_BINDING( bumpMap );
	INIT_RENDER_BINDING( specularMap );
	INIT_RENDER_BINDING( heightMap );
	INIT_RENDER_BINDING( map );
	INIT_RENDER_BINDING( dithermask );
	INIT_RENDER_BINDING( detailMult );
	INIT_RENDER_BINDING( diffuseDetailMap );
	INIT_RENDER_BINDING( specDetailMap );
	INIT_RENDER_BINDING( bumpDetailMap );
	INIT_RENDER_BINDING( cinematicY );
	INIT_RENDER_BINDING( cinematicU );
	INIT_RENDER_BINDING( cinematicV );
	INIT_RENDER_BINDING( lightProjectionMap );
	INIT_RENDER_BINDING( lightFalloffMap );
	INIT_RENDER_BINDING( fogMap );
	INIT_RENDER_BINDING( fogEnterMap );
	INIT_RENDER_BINDING( skyGradientCubeMap );
	INIT_RENDER_BINDING( ambientCubeMapSun );
	INIT_RENDER_BINDING( specularCubeMap );
	INIT_RENDER_BINDING( environmentCubeMap );
	INIT_RENDER_BINDING( ambientCubeMap );
	INIT_RENDER_BINDING( gradientMap );
	INIT_RENDER_BINDING( ambientBrightness );
	INIT_RENDER_BINDING( ambientAvgColor );
	INIT_RENDER_BINDING( currentRenderTexelSize );
	INIT_RENDER_BINDING( aspectSize );
	INIT_RENDER_BINDING( texCoordAttrib );
	INIT_RENDER_BINDING( tangentAttrib );
	INIT_RENDER_BINDING( normalAttrib );
	INIT_RENDER_BINDING( colorAttrib );
	INIT_RENDER_BINDING( signAttrib );
	INIT_RENDER_BINDING( weightIndexAttrib );
	INIT_RENDER_BINDING( weightValueAttrib );
	INIT_RENDER_BINDING( diffuseColor );
	INIT_RENDER_BINDING( specularColor );
	INIT_RENDER_BINDING( windWorld );
	INIT_RENDER_BINDING( lightOrigin );
	INIT_RENDER_BINDING( lightDirection );
	INIT_RENDER_BINDING( viewUpWorld );
	INIT_RENDER_BINDING( viewRightWorld );
	INIT_RENDER_BINDING( viewDirectionWorld );
	INIT_RENDER_BINDING( viewOrigin );
	INIT_RENDER_BINDING( viewOriginWorld );
	INIT_RENDER_BINDING( viewMovement );
	INIT_RENDER_BINDING( lightProject_s );
	INIT_RENDER_BINDING( lightProject_t );
	INIT_RENDER_BINDING( lightProject_q );
	INIT_RENDER_BINDING( lightFalloff_s );
	INIT_RENDER_BINDING( bumpMatrix_s );
	INIT_RENDER_BINDING( bumpMatrix_t );
	INIT_RENDER_BINDING( diffuseMatrix_s );
	INIT_RENDER_BINDING( diffuseMatrix_t );
	INIT_RENDER_BINDING( specularMatrix_s );
	INIT_RENDER_BINDING( specularMatrix_t );
	INIT_RENDER_BINDING( colorModulate );
	INIT_RENDER_BINDING( colorAdd );
	INIT_RENDER_BINDING( specularPower );
	INIT_RENDER_BINDING( alphaThresh );
	INIT_RENDER_BINDING( fadeFraction );
	INIT_RENDER_BINDING( coverage );
	INIT_RENDER_BINDING( transposedProjectionMatrix_x );
	INIT_RENDER_BINDING( transposedProjectionMatrix_y );
	INIT_RENDER_BINDING( transposedProjectionMatrix_z );
	INIT_RENDER_BINDING( transposedProjectionMatrix_w );
	INIT_RENDER_BINDING( transposedModelMatrix_x );
	INIT_RENDER_BINDING( transposedModelMatrix_y );
	INIT_RENDER_BINDING( transposedModelMatrix_z );
	INIT_RENDER_BINDING( transposedModelMatrix_w );
	INIT_RENDER_BINDING( mvptMatrix_x );
	INIT_RENDER_BINDING( mvptMatrix_y );
	INIT_RENDER_BINDING( mvptMatrix_z );
	INIT_RENDER_BINDING( mvptMatrix_w );
	INIT_RENDER_BINDING( waveAmplitude );
	INIT_RENDER_BINDING( wavePhase );
	INIT_RENDER_BINDING( waveFrequency );
	INIT_RENDER_BINDING( waveDirX );
	INIT_RENDER_BINDING( waveDirY );
	INIT_RENDER_BINDING( waveDirXQ );
	INIT_RENDER_BINDING( waveDirYQ );
	INIT_RENDER_BINDING( waveDirXW );
	INIT_RENDER_BINDING( waveDirYW );
	INIT_RENDER_BINDING( waveDirXYQW );
	INIT_RENDER_BINDING( waveDirYYQW );
	INIT_RENDER_BINDING( waveDirXXQW );
	INIT_RENDER_BINDING( waveQW );
	INIT_RENDER_BINDING( boxMins );
	INIT_RENDER_BINDING( boxMaxs );
	INIT_RENDER_BINDING( stuffParams );
	INIT_RENDER_BINDING( proj2View );
	INIT_RENDER_BINDING( pos2View );
	INIT_RENDER_BINDING( detailFade );
	INIT_RENDER_BINDING( lightRadius );
	INIT_RENDER_BINDING( postTint );
	INIT_RENDER_BINDING( postSaturationContrast );
	INIT_RENDER_BINDING( postGlareParameters );
	INIT_RENDER_BINDING( postScratch0Corr );
	INIT_RENDER_BINDING( postScratch1Corr );
	INIT_RENDER_BINDING( postScratch1TexelX );
	INIT_RENDER_BINDING( postScratch0TexelY );
	INIT_RENDER_BINDING( fogParams );
	INIT_RENDER_BINDING( fogDepths );
	INIT_RENDER_BINDING( fogColor );
	INIT_RENDER_BINDING( fogRotation_x );
	INIT_RENDER_BINDING( fogRotation_y );
	INIT_RENDER_BINDING( fogRotation_z );
	INIT_RENDER_BINDING( sunDirection );
	INIT_RENDER_BINDING( sunDirectionWorld );
	INIT_RENDER_BINDING( sunColor );
	INIT_RENDER_BINDING( sunHaloParameters );
	INIT_RENDER_BINDING( ambientScale );
	INIT_RENDER_BINDING( stuffParameters );
	INIT_RENDER_BINDING( fogViewMatrix_x );
	INIT_RENDER_BINDING( fogViewMatrix_y );
	INIT_RENDER_BINDING( fogViewMatrix_z );
	INIT_RENDER_BINDING( fogEyePrecalc );
	INIT_RENDER_BINDING( fogUpInView );

	for ( int i = 0; i < 6; i++ ) {
		const char* name = va( "megaMaskParams_%d", i );
		megaMaskParams[ i ] = declHolder.FindRenderBinding( name, false );
		if ( megaMaskParams[ i ] == NULL ) { common->FatalError( "Unable to find render binding '%s'", name ); }

		name = va( "megaTextureParams_%d", i );
		megaTextureParams[ i ] = declHolder.FindRenderBinding( name, false );
		if ( megaTextureParams[ i ] == NULL ) { common->FatalError( "Unable to find render binding '%s'", name ); }

		name = va( "megaTextureLevel_%d", i );
		megaTextureLevel[ i ] = declHolder.FindRenderBinding( name, false );
		if ( megaTextureLevel[ i ] == NULL ) { common->FatalError( "Unable to find render binding '%s'", name ); }
	}

	megaTextureOpacity15 = declHolder.FindRenderBinding( "megaTextureLevelOpacity_1_5", false );
	if ( megaTextureOpacity15 == NULL ) { common->FatalError( "Unable to find render binding '%s'", "megaTextureLevelOpacity_1_5" ); }
	INIT_RENDER_BINDING( megaDetailTextureMask );
	INIT_RENDER_BINDING( megaDetailTexture );
	INIT_RENDER_BINDING( megaDetailTextureParams );
	INIT_RENDER_BINDING( megaBlendOutDotP );
	INIT_RENDER_BINDING( imgSequenceCur );
	INIT_RENDER_BINDING( imgSequenceNext );
	INIT_RENDER_BINDING( imgSequenceBlend );
	INIT_RENDER_BINDING( imageSize );
	INIT_RENDER_BINDING( lightColor_0 );
	INIT_RENDER_BINDING( lightColor_1 );
	INIT_RENDER_BINDING( lightColor_2 );
	INIT_RENDER_BINDING( lightColor_3 );
	INIT_RENDER_BINDING( lightOrigin_0 );
	INIT_RENDER_BINDING( lightOrigin_1 );
	INIT_RENDER_BINDING( lightOrigin_2 );
	INIT_RENDER_BINDING( lightOrigin_3 );
	INIT_RENDER_BINDING( lightFalloff_0 );
	INIT_RENDER_BINDING( lightFalloff_1 );
	INIT_RENDER_BINDING( lightFalloff_2 );
	INIT_RENDER_BINDING( lightFalloff_3 );
	INIT_RENDER_BINDING( foliageHackDistance );

#undef INIT_RENDER_BINDING

#define INIT_RENDER_PROGRAM( member, name ) \
	member = declHolder.FindRenderProgram( name, false ); \
	if ( member == NULL ) { common->FatalError( "Unable to find render program '%s'", name ); }

	INIT_RENDER_PROGRAM( trivialProgram, "trivial" );
	INIT_RENDER_PROGRAM( trivialWithTextureMatrixProgram, "trivialWithTextureMatrix" );
	INIT_RENDER_PROGRAM( trivialWithTextureMatrix_BlendFilterAtmosphere, "trivialWithTextureMatrix_BlendFilterAtmosphere" );
	INIT_RENDER_PROGRAM( trivialWithTextureMatrix_BlendAddAtmosphere, "trivialWithTextureMatrix_BlendAddAtmosphere" );
	INIT_RENDER_PROGRAM( trivialWithTextureMatrix_BlendBlendAtmosphere, "trivialWithTextureMatrix_BlendBlendAtmosphere" );
	INIT_RENDER_PROGRAM( depthOnlyProgram, "depthOnly" );
	INIT_RENDER_PROGRAM( depthAlphaProgram, "depthAlpha" );
	INIT_RENDER_PROGRAM( interactionBasicProgram, "interaction/basic" );
	INIT_RENDER_PROGRAM( interactionBasicAlphatestProgram, "interaction/basic_alphatest" );
	INIT_RENDER_PROGRAM( grass_alphatest, "stuff/grass_alphatest" );
	INIT_RENDER_PROGRAM( interactionBasicDetailProgram, "interaction/basic_detail" );
	INIT_RENDER_PROGRAM( interactionBasicDetailAlphatestProgram, "interaction/basic_detail_alphatest" );
	INIT_RENDER_PROGRAM( shadowProgram, "shadow" );
	INIT_RENDER_PROGRAM( shadowProgramProjected, "shadowParallelProject" );
	INIT_RENDER_PROGRAM( shadowInvariantProgram, "shadow_invariant" );
	INIT_RENDER_PROGRAM( shadowParallelProgramInfinite, "shadowParallel" );
	INIT_RENDER_PROGRAM( shadowParallelProgramProjected, "shadowParallelNotInfinite" );
	INIT_RENDER_PROGRAM( fogLightProgram, "fogLight" );
	INIT_RENDER_PROGRAM( blendLightProgram, "blendLight" );
	INIT_RENDER_PROGRAM( occlusionProgram, "occlusion" );

#undef INIT_RENDER_PROGRAM
}

#endif /* !__RENDERER_RENDERBINDINGS_H__ */
