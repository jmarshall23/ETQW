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
#include "RenderSystem.h"
#include "megatexture/MegaTexture.h"

extern glconfig_t glConfig;

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
	imageParams_t ditherParams;
	ditherParams.td = TD_HIGH_QUALITY;
	ditherParams.allowPicmip = false;
	for ( int i = 0; i < 16; i++ ) {
		dither[ i ] = ImageFromFile( va( "textures/common/dither%02i.tga", i ), ditherParams );
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
	for ( int i = 0; i < megaTextures.Num(); ++i ) {
		megaTextures[ i ]->Purge();
	}
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

namespace {

const char* imageFilterValues[] = {
	"GL_LINEAR_MIPMAP_NEAREST",
	"GL_LINEAR_MIPMAP_LINEAR",
	"GL_NEAREST",
	"GL_LINEAR",
	"GL_NEAREST_MIPMAP_NEAREST",
	"GL_NEAREST_MIPMAP_LINEAR",
	NULL
};

}

idCVar idImageManager::image_filter( "image_filter", "GL_LINEAR_MIPMAP_LINEAR", CVAR_RENDERER | CVAR_ARCHIVE,
	"changes texture filtering on mipmapped images", imageFilterValues );
idCVar idImageManager::image_anisotropy( "image_anisotropy", "1", CVAR_RENDERER | CVAR_ARCHIVE,
	"set the maximum texture anisotropy if available" );
idCVar idImageManager::image_lodbias( "image_lodbias", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"change lod bias on mipmapped images", -1.0f, 1.0f );
idCVar idImageManager::image_roundDown( "image_roundDown", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"round bad sizes down to nearest power of two" );
idCVar idImageManager::image_colorMipLevels( "image_colorMipLevels", "0", CVAR_RENDERER | CVAR_BOOL,
	"development aid to see texture mip usage" );
idCVar idImageManager::image_useCompression( "image_useCompression", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"0 = force everything to high quality" );
idCVar idImageManager::image_useAllFormats( "image_useAllFormats", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"allow alpha/intensity/luminance/luminance+alpha" );
idCVar idImageManager::image_useNormalCompression( "image_useNormalCompression", "2", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"2 = use rxgb compression for normal maps, 1 = use 256 color compression for normal maps if available" );
idCVar idImageManager::image_writeNormalTGA( "image_writeNormalTGA", "0", CVAR_RENDERER | CVAR_BOOL,
	"write .tgas of the final normal maps for debugging" );
idCVar idImageManager::image_writeNormalTGAPalletized( "image_writeNormalTGAPalletized", "0", CVAR_RENDERER | CVAR_BOOL,
	"write .tgas of the final palletized normal maps for debugging" );
idCVar idImageManager::image_writeTGA( "image_writeTGA", "0", CVAR_RENDERER | CVAR_BOOL,
	"write .tgas of the non normal maps for debugging" );
idCVar idImageManager::image_useOffLineCompression( "image_useOfflineCompression", "0", CVAR_RENDERER | CVAR_BOOL,
	"write a batch file for offline compression of DDS files" );
idCVar idImageManager::image_skipUpload( "image_skipUpload", "0", CVAR_RENDERER | CVAR_BOOL,
	"used during the build process, will skip uploads" );
idCVar idImageManager::image_useBackgroundLoads( "image_useBackgroundLoads", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL,
	"1 = enable background loading of images" );
idCVar idImageManager::image_showBackgroundLoads( "image_showBackgroundLoads", "0", CVAR_RENDERER | CVAR_BOOL,
	"1 = print number of outstanding background loads" );
idCVar idImageManager::image_ignoreHighQuality( "image_ignoreHighQuality", "0", CVAR_RENDERER | CVAR_ARCHIVE,
	"ignore high quality setting on materials" );
idCVar idImageManager::image_detailPower( "image_detailPower", "0.7", CVAR_RENDERER | CVAR_ARCHIVE,
	"Controls how fast the detail textures fade out (0 = normal mipmaps, 1 is falloff after the first level)", 0.0f, 1.0f );
idCVar idImageManager::image_picMipEnable( "image_picMipEnable", "1", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Enable picmip" );
idCVar idImageManager::image_picMip( "image_picMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );
idCVar idImageManager::image_editorPicMip( "image_editorPicMip", "1", CVAR_RENDERER | CVAR_INTEGER,
	"", -4.0f, 1.0f );
idCVar idImageManager::image_bumpPicMip( "image_bumpPicMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );
idCVar idImageManager::image_diffusePicMip( "image_diffusePicMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );
idCVar idImageManager::image_specularPicMip( "image_specularPicMip", "0", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"Uses a miplevel X steps down", -2.0f, 2.0f );

namespace {

idStr NormalizeImageName( const char* name ) {
	idStr normalized = name != NULL ? name : "";
	normalized.BackSlashesToSlashes();
	return normalized;
}

bool ParseImageProgramText( idParser& src, idStr& imageProgram ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return false;
	}

	imageProgram = token;
	if ( !src.PeekTokenString( "(" ) ) {
		return true;
	}

	int depth = 0;
	while ( src.ReadToken( &token ) ) {
		imageProgram += token;
		if ( token == "(" ) {
			++depth;
		} else if ( token == ")" ) {
			if ( --depth == 0 ) {
				return true;
			}
		}
	}
	return false;
}

}

idImage* idImageManager::ImageFromFile( const char* name, imageParams_t params ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return defaultImage;
	}

	const idStr normalized = NormalizeImageName( name );
	if ( !normalized.Icmp( "default" ) || !normalized.Icmp( "_default" ) ) {
		return defaultImage;
	}

	const int hash = normalized.FileNameHash( FILE_HASH_SIZE );
	for ( idImage* image = imageHashTable[ hash ]; image != NULL; image = image->hashNext ) {
		if ( idStr::Icmp( image->imgName, normalized ) != 0 ) {
			continue;
		}
		if ( image->filter != params.tf || image->repeat != params.trp || image->cubeFiles != params.cubeMap ) {
			continue;
		}
		image->levelLoadReferenced = true;
		if ( !insideLevelLoad ) {
			image->referencedOutsideLevelLoad = true;
			if ( !image->IsLoaded() && image->partialImage == NULL ) {
				image->ActuallyLoadImage( true );
			}
		}
		return image;
	}

	idImage* image = AllocImage( normalized );
	image->filter = params.tf;
	image->repeat = params.trp;
	image->depth = params.td;
	image->cubeFiles = params.cubeMap;
	image->mipmapState = params.mipState;
	image->allowDownSize = params.allowPicmip;
	image->picMipOfs = params.picmipofs;
	image->picMipMin = params.picMipMin;
	image->anisotropy = params.anisotropy;
	image->minLod = params.minLod;
	image->maxLod = params.maxLod;
	image->type = params.cubeMap == CF_2D ? TT_2D : TT_CUBIC;
	image->levelLoadReferenced = true;
	if ( !insideLevelLoad ) {
		image->referencedOutsideLevelLoad = true;
		image->ActuallyLoadImage( true );
	}
	return image;
}

idImage* idImageManager::ImageFromFunction( const char* name, const idImageGeneratorFunctorBase& generatorFunction ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		common->Error( "idImageManager::ImageFromFunction: NULL name" );
	}
	const idStr normalized = NormalizeImageName( name );
	const int hash = normalized.FileNameHash( FILE_HASH_SIZE );
	for ( idImage* image = imageHashTable[ hash ]; image != NULL; image = image->hashNext ) {
		if ( idStr::Icmp( image->imgName, normalized ) == 0 ) {
			if ( image->generatorFunction == NULL ) {
				image->generatorFunction = &generatorFunction;
			} else if ( image->generatorFunction != &generatorFunction ) {
				common->Warning( "reused image %s with mixed generators", normalized.c_str() );
			}
			return image;
		}
	}

	idImage* image = AllocImage( normalized );
	image->generatorFunction = &generatorFunction;
	image->referencedOutsideLevelLoad = true;
	image->levelLoadReferenced = true;
	image->ActuallyLoadImage( true );
	return image;
}

idImage* idImageManager::ImageFromParameters(
	const char* name,
	int width,
	int height,
	int internalFormat,
	textureType_t type,
	textureFilter_t filter,
	textureRepeat_t repeat
) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		common->Error( "idImageManager::ImageFromParameters: NULL name" );
	}
	const idStr normalized = NormalizeImageName( name );
	const int hash = normalized.FileNameHash( FILE_HASH_SIZE );
	for ( idImage* image = imageHashTable[ hash ]; image != NULL; image = image->hashNext ) {
		if ( idStr::Icmp( image->imgName, normalized ) != 0 ) {
			continue;
		}
		if ( image->uploadWidth != width || image->uploadHeight != height ||
			 image->internalFormat != internalFormat || image->type != type ||
			 image->filter != filter || image->repeat != repeat ) {
			image->FromParameters( width, height, internalFormat, type, filter, repeat );
		}
		return image;
	}
	idImage* image = AllocImage( normalized );
	image->referencedOutsideLevelLoad = true;
	image->levelLoadReferenced = true;
	image->FromParameters( width, height, internalFormat, type, filter, repeat );
	return image;
}

idMegaTexture* idImageManager::MegaTextureFromFile( const char* fileName ) {
	idStr canonical = fileName != NULL ? fileName : "";
	canonical.BackSlashesToSlashes();
	canonical.StripPath();
	canonical.StripFileExtension();
	if ( canonical.IsEmpty() ) {
		return NULL;
	}
	for ( int i = 0; i < megaTextures.Num(); ++i ) {
		if ( !canonical.Icmp( megaTextures[ i ]->GetName() ) ) {
			megaTextures[ i ]->Touch();
			megaTextures[ i ]->SetLevelLoadReferenced( insideLevelLoad );
			if ( !insideLevelLoad ) megaTextures[ i ]->SetReferencedOutsideLevelLoad( true );
			return megaTextures[ i ];
		}
	}
	idMegaTexture* megaTexture = new idMegaTexture;
	if ( !megaTexture->InitFromMegaFile( canonical.c_str() ) ) {
		delete megaTexture;
		return NULL;
	}
	megaTexture->SetLevelLoadReferenced( insideLevelLoad );
	megaTexture->SetReferencedOutsideLevelLoad( !insideLevelLoad );
	megaTextures.Append( megaTexture );
	return megaTexture;
}
idImage* idImageManager::ParseImage( idParser& src, const imageParams_t& defaultParms ) {
	imageParams_t parms = defaultParms;
	idToken token;
	while ( src.ReadTokenOnLine( &token ) ) {
		if ( token.Icmp( "bumpMap" ) == 0 ) {
			parms.td = TD_BUMP;
		} else if ( token.Icmp( "diffuseMap" ) == 0 ) {
			parms.td = TD_DIFFUSE;
		} else if ( token.Icmp( "specularMap" ) == 0 ) {
			parms.td = TD_SPECULAR;
		} else if ( token.Icmp( "cubeMap" ) == 0 ) {
			parms.cubeMap = CF_NATIVE;
		} else if ( token.Icmp( "cameraCubeMap" ) == 0 ) {
			parms.cubeMap = CF_CAMERA;
		} else if ( token.Icmp( "halfSphereMap" ) == 0 ) {
			parms.cubeMap = CF_HALFSPHERE;
		} else if ( token.Icmp( "nearest" ) == 0 ) {
			parms.tf = TF_NEAREST;
		} else if ( token.Icmp( "linear" ) == 0 ) {
			parms.tf = TF_LINEAR;
		} else if ( token.Icmp( "linearNearest" ) == 0 ) {
			parms.tf = TF_LINEARNEAREST;
		} else if ( token.Icmp( "waternormal" ) == 0 ) {
			parms.mipState.colorType = mipmapState_t::MT_WATER;
		} else if ( token.Icmp( "colormipmaps" ) == 0 ) {
			parms.mipState.colorType = mipmapState_t::MT_COLORLEVELS;
			if ( !src.Parse2DMatrix( 2, 4, parms.mipState.color ) ) {
				return NULL;
			}
		} else if ( token.Icmp( "mirror" ) == 0 ) {
			parms.trp = TR_MIRROR;
		} else if ( token.Icmp( "mirror_x" ) == 0 ) {
			parms.trp = TR_MIRROR_X;
		} else if ( token.Icmp( "mirror_y" ) == 0 ) {
			parms.trp = TR_MIRROR_Y;
		} else if ( token.Icmp( "clamp" ) == 0 ) {
			parms.trp = TR_CLAMP;
		} else if ( token.Icmp( "clamp_x" ) == 0 ) {
			parms.trp = TR_CLAMP_X;
		} else if ( token.Icmp( "clamp_y" ) == 0 ) {
			parms.trp = TR_CLAMP_Y;
		} else if ( token.Icmp( "noclamp" ) == 0 ) {
			parms.trp = TR_REPEAT;
		} else if ( token.Icmp( "zeroclamp" ) == 0 ) {
			parms.trp = TR_CLAMP_TO_ZERO;
		} else if ( token.Icmp( "alphazeroclamp" ) == 0 ) {
			parms.trp = TR_CLAMP_TO_ZERO_ALPHA;
		} else if ( token.Icmp( "forceHighQuality" ) == 0 ) {
			parms.td = TD_HIGH_QUALITY;
		} else if ( token.Icmp( "uncompressed" ) == 0 || token.Icmp( "highquality" ) == 0 ) {
			if ( !image_ignoreHighQuality.GetBool() ) {
				parms.td = TD_HIGH_QUALITY;
			}
		} else if ( token.Icmp( "nopicmip" ) == 0 ) {
			parms.allowPicmip = false;
		} else if ( token.Icmp( "picmip" ) == 0 ) {
			parms.allowPicmip = true;
			parms.picmipofs = src.ParseInt();
		} else if ( token.Icmp( "picmipmin" ) == 0 ) {
			parms.picMipMin = src.ParseInt();
		} else if ( token.Icmp( "anisotropy" ) == 0 ) {
			parms.anisotropy = src.ParseFloat();
		} else if ( token.Icmp( "minLod" ) == 0 ) {
			parms.minLod = src.ParseFloat();
		} else if ( token.Icmp( "maxLod" ) == 0 ) {
			parms.maxLod = src.ParseFloat();
		} else if ( token.Icmp( "partialLoad" ) == 0 ) {
			parms.partialLoad = true;
		} else {
			src.UnreadToken( token );
			break;
		}
	}

	idStr imageProgram;
	if ( !ParseImageProgramText( src, imageProgram ) ) {
		return NULL;
	}
	return globalImages != NULL ? globalImages->ImageFromFile( imageProgram, parms ) : NULL;
}

void idImageManager::BindNull() {
	if ( glConfig.isInitialized ) {
		glBindTexture( GL_TEXTURE_2D, 0 );
		if ( glConfig.cubeMapAvailable ) {
			glBindTexture( GL_TEXTURE_CUBE_MAP_ARB, 0 );
		}
		if ( glConfig.rectangleTextureAvailable ) {
			glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
		}
	}
}

void idImageManager::LoadImage(
	const char* fileName,
	byte** pic,
	int* width,
	int* height,
	unsigned* timestamp,
	bool
) {
	LoadTGA( fileName, pic, width, height, timestamp, true );
	if ( pic != NULL && *pic == NULL && fileName != NULL ) {
		idStr tgaName = fileName;
		tgaName.DefaultFileExtension( "tga" );
		if ( idStr::Icmp( tgaName, fileName ) != 0 ) {
			LoadTGA( tgaName, pic, width, height, timestamp, true );
		}
	}
}

void idImageManager::FreeImageBuffer( byte*& buffer ) {
	Mem_Free( buffer );
	buffer = NULL;
}

void idImageManager::WriteTGA( const char* fileName, const byte* data, int width, int height, int depth, bool swapBGR, bool flipVertical ) {
	byte* buffer = NULL;
	const int length = WriteTGABuffer( buffer, data, width, height, depth, swapBGR, flipVertical );
	if ( length > 0 && fileSystem != NULL ) {
		fileSystem->WriteFile( fileName, buffer, length );
	}
	Mem_Free( buffer );
}

int idImageManager::WriteTGABuffer( byte*& outBuffer, const byte* data, int width, int height, int depth, bool swapBGR, bool flipVertical ) {
	outBuffer = NULL;
	if ( data == NULL || width <= 0 || height <= 0 || ( depth != 3 && depth != 4 ) ) {
		return 0;
	}

	const int length = 18 + width * height * depth;
	byte* output = static_cast< byte* >( Mem_Alloc( length ) );
	memset( output, 0, 18 );
	output[ 2 ] = 2;
	output[ 12 ] = width & 0xff;
	output[ 13 ] = ( width >> 8 ) & 0xff;
	output[ 14 ] = height & 0xff;
	output[ 15 ] = ( height >> 8 ) & 0xff;
	output[ 16 ] = depth * 8;
	output[ 17 ] = static_cast< byte >( 0x20 | ( depth == 4 ? 8 : 0 ) );

	byte* destination = output + 18;
	for ( int y = 0; y < height; y++ ) {
		const int sourceY = flipVertical ? height - 1 - y : y;
		const byte* source = data + sourceY * width * depth;
		for ( int x = 0; x < width; x++, source += depth, destination += depth ) {
			if ( swapBGR ) {
				destination[ 0 ] = source[ 2 ];
				destination[ 1 ] = source[ 1 ];
				destination[ 2 ] = source[ 0 ];
			} else {
				memcpy( destination, source, 3 );
			}
			if ( depth == 4 ) {
				destination[ 3 ] = source[ 3 ];
			}
		}
	}
	outBuffer = output;
	return length;
}

void idImageManager::WriteBMP( const char*, const byte*, int, int, int ) {}
int idImageManager::WriteBMPBuffer( byte*& outBuffer, const byte*, int, int, int ) {
	outBuffer = NULL;
	return 0;
}
void idImageManager::WritePalTGA( const char*, const byte*, const byte*, int, int, bool ) {}

idImage* idImageManager::AllocImage( const char* name ) {
	if ( name == NULL || name[ 0 ] == '\0' ) {
		common->Error( "idImageManager::AllocImage: NULL name" );
	}
	if ( idStr::Length( name ) >= MAX_IMAGE_NAME ) {
		common->Error( "idImageManager::AllocImage: \"%s\" is too long", name );
	}

	idImage* image = new idImage;
	image->imgName = name;
	image->imageHash = image->imgName.FileNameHash( FILE_HASH_SIZE );
	image->hashNext = imageHashTable[ image->imageHash ];
	imageHashTable[ image->imageHash ] = image;
	images.Append( image );
	return image;
}

idImageManager imageManagerLocal;
idImageManager* globalImages = &imageManagerLocal;
