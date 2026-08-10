// Copyright (C) 2007 Id Software, Inc.
//
// ETQW image-manager lifecycle reconstructed from:
//   quakewars-hexrays/renderer/Image_init.cpp
//
// Built-in texture generation and GPU upload are intentionally kept in their
// PDB-owned units (Image_load.cpp/Image_process.cpp) as those units are
// translated. This file owns manager state and level-load lifetime.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Image.h"

namespace {
	bool imageManagerInitialized = false;

	void GenerateSolidImage( idImage* image, byte red, byte green, byte blue, byte alpha ) {
		byte data[ 16 * 16 * 4 ];
		for ( int i = 0; i < 16 * 16; i++ ) {
			data[ i * 4 + 0 ] = red;
			data[ i * 4 + 1 ] = green;
			data[ i * 4 + 2 ] = blue;
			data[ i * 4 + 3 ] = alpha;
		}
		image->GenerateImage( data, 16, 16, TF_DEFAULT, false, TR_REPEAT, TD_HIGH_QUALITY );
	}

	void GenerateDefaultImage( idImage* image ) {
		image->MakeDefault();
		image->defaulted = false;
	}

	void GenerateDefaultMaterialImage( idImage* image ) {
		byte data[ 16 * 16 * 4 ];
		for ( int y = 0; y < 16; y++ ) {
			for ( int x = 0; x < 16; x++ ) {
				const bool checker = ( ( x >> 2 ) ^ ( y >> 2 ) ) & 1;
				byte* pixel = data + ( y * 16 + x ) * 4;
				pixel[ 0 ] = checker ? 96 : 32;
				pixel[ 1 ] = checker ? 96 : 32;
				pixel[ 2 ] = checker ? 96 : 32;
				pixel[ 3 ] = 255;
			}
		}
		image->GenerateImage( data, 16, 16, TF_DEFAULT, false, TR_REPEAT, TD_HIGH_QUALITY );
	}

	void GenerateWhiteImage( idImage* image ) {
		GenerateSolidImage( image, 255, 255, 255, 255 );
	}

	void GenerateGrayImage( idImage* image ) {
		GenerateSolidImage( image, 119, 119, 119, 255 );
	}

	void GenerateBlackImage( idImage* image ) {
		GenerateSolidImage( image, 0, 0, 0, 255 );
	}

	void GenerateFlatNormalImage( idImage* image ) {
		GenerateSolidImage( image, 128, 128, 255, 255 );
	}

	void GenerateBorderClampImage( idImage* image ) {
		byte data[ 16 * 16 * 4 ];
		for ( int y = 0; y < 16; y++ ) {
			for ( int x = 0; x < 16; x++ ) {
				const byte value = x == 0 || y == 0 || x == 15 || y == 15 ? 0 : 255;
				byte* pixel = data + ( y * 16 + x ) * 4;
				pixel[ 0 ] = pixel[ 1 ] = pixel[ 2 ] = pixel[ 3 ] = value;
			}
		}
		image->GenerateImage( data, 16, 16, TF_LINEAR, false, TR_CLAMP_TO_BORDER, TD_HIGH_QUALITY );
	}

	void GenerateRampImage( idImage* image ) {
		byte data[ 256 * 4 ];
		for ( int i = 0; i < 256; i++ ) {
			data[ i * 4 + 0 ] = static_cast< byte >( i );
			data[ i * 4 + 1 ] = static_cast< byte >( i );
			data[ i * 4 + 2 ] = static_cast< byte >( i );
			data[ i * 4 + 3 ] = static_cast< byte >( i );
		}
		image->GenerateImage( data, 256, 1, TF_LINEAR, false, TR_CLAMP, TD_HIGH_QUALITY );
	}

	void GenerateAlphaRampImage( idImage* image ) {
		byte data[ 256 * 4 ];
		for ( int i = 0; i < 256; i++ ) {
			data[ i * 4 + 0 ] = 255;
			data[ i * 4 + 1 ] = 255;
			data[ i * 4 + 2 ] = 255;
			data[ i * 4 + 3 ] = static_cast< byte >( i );
		}
		image->GenerateImage( data, 256, 1, TF_LINEAR, false, TR_CLAMP, TD_HIGH_QUALITY );
	}

	void GenerateAlphaNotchImage( idImage* image ) {
		const byte data[ 8 ] = { 255, 255, 255, 0, 255, 255, 255, 255 };
		image->GenerateImage( data, 2, 1, TF_NEAREST, false, TR_CLAMP, TD_HIGH_QUALITY );
	}

	void GenerateNormalCubeImage( idImage* image ) {
		byte faces[ 6 ][ 4 * 4 * 4 ];
		const byte colors[ 6 ][ 3 ] = {
			{ 255, 128, 128 }, { 0, 128, 128 },
			{ 128, 255, 128 }, { 128, 0, 128 },
			{ 128, 128, 255 }, { 128, 128, 0 }
		};
		const byte* pointers[ 6 ];
		for ( int face = 0; face < 6; face++ ) {
			pointers[ face ] = faces[ face ];
			for ( int i = 0; i < 16; i++ ) {
				faces[ face ][ i * 4 + 0 ] = colors[ face ][ 0 ];
				faces[ face ][ i * 4 + 1 ] = colors[ face ][ 1 ];
				faces[ face ][ i * 4 + 2 ] = colors[ face ][ 2 ];
				faces[ face ][ i * 4 + 3 ] = 255;
			}
		}
		image->GenerateCubeImage( pointers, 4, TF_LINEAR, false, TD_HIGH_QUALITY );
	}

	void GenerateBlackCubeImage( idImage* image ) {
		byte faceData[ 4 * 4 * 4 ];
		for ( int i = 0; i < 16; i++ ) {
			faceData[ i * 4 + 0 ] = 0;
			faceData[ i * 4 + 1 ] = 0;
			faceData[ i * 4 + 2 ] = 0;
			faceData[ i * 4 + 3 ] = 255;
		}
		const byte* pointers[ 6 ] = {
			faceData, faceData, faceData, faceData, faceData, faceData
		};
		image->GenerateCubeImage( pointers, 4, TF_LINEAR, false, TD_HIGH_QUALITY );
	}

	idImageGeneratorFunctorGlobal defaultImageFunctor( GenerateDefaultImage );
	idImageGeneratorFunctorGlobal defaultMaterialImageFunctor( GenerateDefaultMaterialImage );
	idImageGeneratorFunctorGlobal whiteImageFunctor( GenerateWhiteImage );
	idImageGeneratorFunctorGlobal grayImageFunctor( GenerateGrayImage );
	idImageGeneratorFunctorGlobal blackImageFunctor( GenerateBlackImage );
	idImageGeneratorFunctorGlobal flatNormalImageFunctor( GenerateFlatNormalImage );
	idImageGeneratorFunctorGlobal borderClampImageFunctor( GenerateBorderClampImage );
	idImageGeneratorFunctorGlobal rampImageFunctor( GenerateRampImage );
	idImageGeneratorFunctorGlobal alphaRampImageFunctor( GenerateAlphaRampImage );
	idImageGeneratorFunctorGlobal alphaNotchImageFunctor( GenerateAlphaNotchImage );
	idImageGeneratorFunctorGlobal normalCubeImageFunctor( GenerateNormalCubeImage );
	idImageGeneratorFunctorGlobal blackCubeImageFunctor( GenerateBlackCubeImage );
}

void idImageManager::Init() {
	if ( imageManagerInitialized ) {
		return;
	}

	memset( imageHashTable, 0, sizeof( imageHashTable ) );
	images.Clear();
	megaTextures.Clear();
	ddsSourceFileList.Clear();
	ddsDestFileList.Clear();
	ddsCodecList.Clear();
	ddsParamList.Clear();

	defaultImage = NULL;
	defaultMaterialImage = NULL;
	flatNormalMap = NULL;
	rampImage = NULL;
	alphaRampImage = NULL;
	alphaNotchImage = NULL;
	whiteImage = NULL;
	grayImage = NULL;
	blackImage = NULL;
	normalCubeMapImage = NULL;
	blackCubeMapImage = NULL;
	noFalloffImage = NULL;
	fogImage = NULL;
	fogEnterImage = NULL;
	cinematicImage = NULL;
	cinematicYImage = NULL;
	cinematicUImage = NULL;
	cinematicVImage = NULL;
	scratchImage = NULL;
	currentRenderImage = NULL;
	currentDepthImage = NULL;
	postProcessBuffer[ 0 ] = NULL;
	postProcessBuffer[ 1 ] = NULL;
	scratchCubeMapImage = NULL;
	scratchImage2 = NULL;
	noise = NULL;
	specularTableImage = NULL;
	specular2DTableImage = NULL;
	borderClampImage = NULL;
	memset( dither, 0, sizeof( dither ) );
	defaultDetailMaskImage = NULL;
	diffusionMask = NULL;

	insideLevelLoad = false;
	backgroundImageLoads = NULL;
	numActiveBackgroundImageLoads = 0;
	textureMinFilter = GL_LINEAR_MIPMAP_LINEAR;
	textureMaxFilter = GL_LINEAR;
	textureAnisotropy = 1.0f;
	textureLODBias = 0.0f;
	imageManagerInitialized = true;

	defaultImage = ImageFromFunction( "_default", defaultImageFunctor );
	defaultMaterialImage = ImageFromFunction( "_defaultMaterial", defaultMaterialImageFunctor );
	whiteImage = ImageFromFunction( "_white", whiteImageFunctor );
	grayImage = ImageFromFunction( "_gray", grayImageFunctor );
	blackImage = ImageFromFunction( "_black", blackImageFunctor );
	borderClampImage = ImageFromFunction( "_borderClamp", borderClampImageFunctor );
	flatNormalMap = ImageFromFunction( "_flat", flatNormalImageFunctor );
	rampImage = ImageFromFunction( "_ramp", rampImageFunctor );
	alphaRampImage = ImageFromFunction( "_alphaRamp", alphaRampImageFunctor );
	alphaNotchImage = ImageFromFunction( "_alphaNotch", alphaNotchImageFunctor );
	normalCubeMapImage = ImageFromFunction( "_normalCubeMap", normalCubeImageFunctor );
	blackCubeMapImage = ImageFromFunction( "_blackCubeMap", blackCubeImageFunctor );

	noFalloffImage = ImageFromFunction( "_noFalloff", whiteImageFunctor );
	fogImage = ImageFromFunction( "_fog", whiteImageFunctor );
	fogEnterImage = ImageFromFunction( "_fogEnter", whiteImageFunctor );
	specularTableImage = ImageFromFunction( "_specularTable", rampImageFunctor );
	specular2DTableImage = ImageFromFunction( "_specular2DTable", rampImageFunctor );
	defaultDetailMaskImage = ImageFromFunction( "_defaultDetailMask", whiteImageFunctor );
	diffusionMask = ImageFromFunction( "_diffusionMask", whiteImageFunctor );
	noise = ImageFromFunction( "_noise", grayImageFunctor );
	for ( int i = 0; i < 16; i++ ) {
		dither[ i ] = whiteImage;
	}

	cinematicImage = ImageFromParameters( "_cinematic", 16, 16, GL_RGBA8, TT_2D, TF_LINEAR, TR_CLAMP );
	cinematicYImage = ImageFromParameters( "_cinematicY", 16, 16, GL_LUMINANCE8, TT_2D, TF_LINEAR, TR_CLAMP );
	cinematicUImage = ImageFromParameters( "_cinematicU", 16, 16, GL_LUMINANCE8, TT_2D, TF_LINEAR, TR_CLAMP );
	cinematicVImage = ImageFromParameters( "_cinematicV", 16, 16, GL_LUMINANCE8, TT_2D, TF_LINEAR, TR_CLAMP );
	scratchImage = ImageFromParameters( "_scratch", 16, 16, GL_RGBA8, TT_2D, TF_LINEAR, TR_CLAMP );
	scratchImage2 = ImageFromParameters( "_scratch2", 16, 16, GL_RGB8, TT_2D, TF_LINEAR, TR_CLAMP );
	scratchCubeMapImage = ImageFromFunction( "_scratchCubeMap", normalCubeImageFunctor );
	currentRenderImage = ImageFromParameters( "_currentRender", 640, 480, GL_RGBA8, TT_RECT, TF_LINEAR, TR_CLAMP );
	currentDepthImage = ImageFromParameters( "_currentDepth", 640, 480, GL_DEPTH_COMPONENT24, TT_RECT, TF_NEAREST, TR_CLAMP );
	postProcessBuffer[ 0 ] = ImageFromParameters( "_postProcessBuffer_0", 160, 120, GL_RGBA8, TT_RECT, TF_LINEAR, TR_CLAMP );
	postProcessBuffer[ 1 ] = ImageFromParameters( "_postProcessBuffer_1", 160, 120, GL_RGBA8, TT_RECT, TF_LINEAR, TR_CLAMP );

	common->Printf( "idImageManager initialized with %d ETQW images\n", images.Num() );
}

void idImageManager::PreSys3DShutdown() {
}

void idImageManager::Shutdown() {
	for ( int i = 0; i < images.Num(); i++ ) {
		if ( images[ i ] != NULL ) {
			images[ i ]->Purge();
			delete images[ i ];
			images[ i ] = NULL;
		}
	}

	images.Clear();
	megaTextures.Clear();
	ddsSourceFileList.Clear();
	ddsDestFileList.Clear();
	ddsCodecList.Clear();
	ddsParamList.Clear();
	memset( imageHashTable, 0, sizeof( imageHashTable ) );
	backgroundImageLoads = NULL;
	numActiveBackgroundImageLoads = 0;
	insideLevelLoad = false;
	imageManagerInitialized = false;
}

idImage* idImageManager::GetImage( const char* name ) const {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage* image = images[ i ];
		if ( image != NULL && idStr::Icmp( image->imgName, name ) == 0 ) {
			return image;
		}
	}
	return NULL;
}

int idImageManager::SumOfUsedImages() {
	int total = 0;
	for ( int i = 0; i < images.Num(); i++ ) {
		const idImage* image = images[ i ];
		if ( image != NULL && image->frameUsed != 0 ) {
			total += image->uploadWidth * image->uploadHeight * Max( image->uploadDepth, 1 );
		}
	}
	return total;
}

void idImageManager::CheckCvars() {
}

void idImageManager::PurgeAllMegaTextures() {
	// MegaTexture.cpp owns destruction once that PDB unit is translated.
}

void idImageManager::PurgeAllImages() {
	for ( int i = 0; i < images.Num(); i++ ) {
		if ( images[ i ] != NULL ) {
			images[ i ]->Purge();
		}
	}
}

void idImageManager::ReloadAllImages() {
	for ( int i = 0; i < images.Num(); i++ ) {
		if ( images[ i ] != NULL ) {
			images[ i ]->Reload( true, true );
		}
	}
}

void idImageManager::BeginLevelLoad() {
	insideLevelLoad = true;
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage* image = images[ i ];
		if ( image != NULL && image->generatorFunction == NULL ) {
			image->levelLoadReferenced = false;
		}
	}
}

void idImageManager::EndLevelLoad() {
	insideLevelLoad = false;
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage* image = images[ i ];
		if ( image == NULL || image->generatorFunction != NULL ) {
			continue;
		}
		if ( !image->levelLoadReferenced && !image->referencedOutsideLevelLoad ) {
			image->Purge();
		} else if ( image->levelLoadReferenced && !image->IsLoaded() ) {
			image->ActuallyLoadImage( true );
		}
	}
}

void idImageManager::LevelStart() {
	for ( int i = 0; i < images.Num(); i++ ) {
		idImage* image = images[ i ];
		if ( image != NULL ) {
			image->smallestDistanceSeen = 0.0f;
			image->frameOfDistance = 0;
			image->distanceLod = false;
		}
	}
}

int idImageManager::LoadPendingImages( bool ) {
	return 0;
}

void idImageManager::GetPureServerChecksum( unsigned int& checksum ) {
	checksum = 0;
}
