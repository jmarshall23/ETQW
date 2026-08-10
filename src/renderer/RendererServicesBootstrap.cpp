// Copyright (C) 2007 Id Software, Inc.
//
// Public renderer services reconstructed from the ETQW SDK/PDB boundary.
// Model and GPU image creation remain backend-neutral, while the image file
// helpers are functional so screenshots and filesystem image probes work.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Image.h"
#include "RenderSystem.h"
#include "ModelManager.h"

extern glconfig_t glConfig;

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

class idRenderModelManagerBootstrap : public idRenderModelManager {
public:
	virtual void Init() {}
	virtual void Shutdown() {}
	virtual void BeginLevelLoad() {}
	virtual void EndLevelLoad() {}
	virtual idRenderModel* AllocModel() { return NULL; }
	virtual void FreeModel( idRenderModel* ) {}
	virtual idRenderModel* FindModel( const char* ) { return NULL; }
	virtual idRenderModel* CheckModel( const char* ) { return NULL; }
	virtual idRenderModel* GetModel( const char* ) { return NULL; }
	virtual idRenderModel* DefaultModel() { return NULL; }
	virtual void AddModel( idRenderModel* ) {}
	virtual void RemoveModel( idRenderModel* ) {}
	virtual void ReloadModels( bool ) {}
	virtual void WritePrecacheCommands( idFile* ) {}
	virtual void FreeModelVertexCaches() {}
	virtual bool WriteSurfaceModel( const char*, idList< idSurface* >&, idStrList& ) { return false; }
	virtual bool WriteTriangleModelB( const char*, idRenderModel* ) { return false; }
	virtual bool WriteTriangleModel( const char*, idRenderModel* ) { return false; }
};

idRenderModelManagerBootstrap modelManagerBootstrap;
idImageManager imageManagerLocal;

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

idMegaTexture* idImageManager::MegaTextureFromFile( const char* ) { return NULL; }
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

idRenderModelManager* renderModelManager = &modelManagerBootstrap;
idImageManager* globalImages = &imageManagerLocal;

void LoadTGA( const char* name, byte** pic, int* width, int* height, unsigned* timestamp, bool markPaksReferenced ) {
	if ( pic != NULL ) {
		*pic = NULL;
	}
	if ( width != NULL ) {
		*width = 0;
	}
	if ( height != NULL ) {
		*height = 0;
	}
	if ( timestamp != NULL ) {
		*timestamp = 0;
	}
	if ( name == NULL || fileSystem == NULL ) {
		return;
	}

	void* fileBuffer = NULL;
	const int fileLength = fileSystem->ReadFile( name, &fileBuffer, timestamp, markPaksReferenced );
	if ( fileLength < 18 || fileBuffer == NULL ) {
		return;
	}

	const byte* source = static_cast< const byte* >( fileBuffer );
	const int idLength = source[ 0 ];
	const int colorMapType = source[ 1 ];
	const int imageType = source[ 2 ];
	const int imageWidth = source[ 12 ] | ( source[ 13 ] << 8 );
	const int imageHeight = source[ 14 ] | ( source[ 15 ] << 8 );
	const int bitsPerPixel = source[ 16 ];
	const int bytesPerPixel = bitsPerPixel / 8;
	const bool topOrigin = ( source[ 17 ] & 0x20 ) != 0;
	const int dataOffset = 18 + idLength;
	const int dataLength = imageWidth * imageHeight * bytesPerPixel;

	const bool validType = imageType == 2 || imageType == 3;
	const bool validDepth = ( imageType == 2 && ( bitsPerPixel == 24 || bitsPerPixel == 32 ) ) ||
		( imageType == 3 && bitsPerPixel == 8 );
	if ( colorMapType != 0 || !validType || !validDepth || imageWidth <= 0 || imageHeight <= 0 ||
		 dataOffset < 18 || dataLength < 0 || dataOffset + dataLength > fileLength ) {
		fileSystem->FreeFile( fileBuffer );
		return;
	}

	if ( width != NULL ) {
		*width = imageWidth;
	}
	if ( height != NULL ) {
		*height = imageHeight;
	}

	if ( pic != NULL ) {
		byte* output = static_cast< byte* >( Mem_Alloc( imageWidth * imageHeight * 4 ) );
		const byte* pixels = source + dataOffset;
		for ( int y = 0; y < imageHeight; y++ ) {
			const int sourceY = topOrigin ? y : imageHeight - 1 - y;
			const byte* sourceRow = pixels + sourceY * imageWidth * bytesPerPixel;
			byte* destination = output + y * imageWidth * 4;
			for ( int x = 0; x < imageWidth; x++, sourceRow += bytesPerPixel, destination += 4 ) {
				if ( imageType == 3 ) {
					destination[ 0 ] = sourceRow[ 0 ];
					destination[ 1 ] = sourceRow[ 0 ];
					destination[ 2 ] = sourceRow[ 0 ];
					destination[ 3 ] = 255;
				} else {
					destination[ 0 ] = sourceRow[ 2 ];
					destination[ 1 ] = sourceRow[ 1 ];
					destination[ 2 ] = sourceRow[ 0 ];
					destination[ 3 ] = bytesPerPixel == 4 ? sourceRow[ 3 ] : 255;
				}
			}
		}
		*pic = output;
	}

	fileSystem->FreeFile( fileBuffer );
}
