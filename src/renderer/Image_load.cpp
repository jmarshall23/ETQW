// Copyright (C) 2007 Id Software, Inc.
//
// ETQW image upload and binding implementation reconstructed from the
// Microsoft PDB and quakewars-hexrays/renderer/Image_load.cpp.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Image.h"
#include "RenderSystem.h"
#include "tr_render.h"

extern glconfig_t glConfig;

namespace {

static const byte mipBlendColors[ 16 ][ 4 ] = {
	{ 0, 0, 0, 0 },
	{ 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 },
	{ 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 },
	{ 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 },
	{ 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 },
	{ 255, 0, 0, 128 }, { 0, 255, 0, 128 }, { 0, 0, 255, 128 }
};

void ApplyMipmapState( byte* pixels, int width, int height, int level, mipmapState_t& state ) {
	if ( pixels == NULL || width <= 0 || height <= 0 || state.colorType == mipmapState_t::MT_NONE ) return;
	if ( state.colorType == mipmapState_t::MT_DEFAULT ) {
		if ( level == 0 ) return;
		byte color[ 4 ];
		byte amount[ 4 ];
		for ( int channel = 0; channel < 4; ++channel ) {
			color[ channel ] = static_cast< byte >( idMath::ClampInt( 0, 255, idMath::Ftoi( state.color[ channel ] * 255.0f ) ) );
			amount[ channel ] = static_cast< byte >( idMath::ClampInt( 0, 255, idMath::Ftoi( state.blend[ channel ] * 255.0f ) ) );
		}
		R_BlendOverTexture( pixels, width * height, color, amount );
	} else if ( state.colorType == mipmapState_t::MT_WATER ) {
		const float alphaScale = Max( 0.0f, ( 5.0f - static_cast< float >( level ) ) * 0.25f );
		R_SetAlphaChannel( pixels, width * height, static_cast< byte >( level == 0 ? 50 : idMath::ClampInt( 0, 255, idMath::Ftoi( alphaScale * 255.0f ) ) ) );
	} else if ( state.colorType == mipmapState_t::MT_COLORLEVELS ) {
		const int colorLevel = idMath::ClampInt( 0, 15, level );
		R_BlendOverTexture( pixels, width * height, mipBlendColors[ colorLevel ], mipBlendColors[ colorLevel ][ 3 ] );
	}
}

GLenum TextureTargetForType( textureType_t type ) {
	switch ( type ) {
		case TT_2D:
			return GL_TEXTURE_2D;
		case TT_3D:
			return GL_TEXTURE_3D;
		case TT_CUBIC:
			return GL_TEXTURE_CUBE_MAP_ARB;
		case TT_RECT:
			return GL_TEXTURE_RECTANGLE_ARB;
		default:
			return 0;
	}
}

int NextPowerOfTwo( int value ) {
	int result = 1;
	while ( result < value ) {
		result <<= 1;
	}
	return result;
}

byte* ResampleRGBA( const byte* source, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	byte* result = static_cast< byte* >( Mem_Alloc( targetWidth * targetHeight * 4 ) );
	for ( int y = 0; y < targetHeight; y++ ) {
		const int sourceY = Min( sourceHeight - 1, y * sourceHeight / targetHeight );
		for ( int x = 0; x < targetWidth; x++ ) {
			const int sourceX = Min( sourceWidth - 1, x * sourceWidth / targetWidth );
			memcpy(
				result + ( y * targetWidth + x ) * 4,
				source + ( sourceY * sourceWidth + sourceX ) * 4,
				4
			);
		}
	}
	return result;
}

byte* MipMapRGBA( const byte* source, int width, int height ) {
	const int targetWidth = Max( 1, width >> 1 );
	const int targetHeight = Max( 1, height >> 1 );
	byte* result = static_cast< byte* >( Mem_Alloc( targetWidth * targetHeight * 4 ) );

	for ( int y = 0; y < targetHeight; y++ ) {
		for ( int x = 0; x < targetWidth; x++ ) {
			const int x0 = Min( width - 1, x * 2 );
			const int x1 = Min( width - 1, x0 + 1 );
			const int y0 = Min( height - 1, y * 2 );
			const int y1 = Min( height - 1, y0 + 1 );
			const byte* samples[ 4 ] = {
				source + ( y0 * width + x0 ) * 4,
				source + ( y0 * width + x1 ) * 4,
				source + ( y1 * width + x0 ) * 4,
				source + ( y1 * width + x1 ) * 4
			};
			byte* destination = result + ( y * targetWidth + x ) * 4;
			for ( int channel = 0; channel < 4; channel++ ) {
				destination[ channel ] = static_cast< byte >(
					( samples[ 0 ][ channel ] + samples[ 1 ][ channel ] +
					  samples[ 2 ][ channel ] + samples[ 3 ][ channel ] + 2 ) >> 2
				);
			}
		}
	}
	return result;
}

void ApplyZeroClampBorder( byte* pixels, int width, int height, bool zeroAlpha ) {
	if ( pixels == NULL || width <= 0 || height <= 0 ) {
		return;
	}
	const byte border[ 4 ] = {
		static_cast< byte >( zeroAlpha ? 255 : 0 ),
		static_cast< byte >( zeroAlpha ? 255 : 0 ),
		static_cast< byte >( zeroAlpha ? 255 : 0 ),
		static_cast< byte >( zeroAlpha ? 0 : 255 )
	};
	for ( int x = 0; x < width; x++ ) {
		memcpy( pixels + x * 4, border, 4 );
		memcpy( pixels + ( ( height - 1 ) * width + x ) * 4, border, 4 );
	}
	for ( int y = 0; y < height; y++ ) {
		memcpy( pixels + ( y * width ) * 4, border, 4 );
		memcpy( pixels + ( y * width + width - 1 ) * 4, border, 4 );
	}
}

typedef void ( APIENTRY * generateMipmapProc_t )( GLenum target );
typedef void ( APIENTRY * texImage3DProc_t )(
	GLenum target,
	GLint level,
	GLint internalFormat,
	GLsizei width,
	GLsizei height,
	GLsizei depth,
	GLint border,
	GLenum format,
	GLenum type,
	const GLvoid* pixels
);
typedef void ( APIENTRY * activeTextureProc_t )( GLenum texture );

template< typename T >
T ResolveGLProc( const char* name ) {
#if defined( _WIN32 )
	return reinterpret_cast< T >( wglGetProcAddress( name ) );
#else
	return NULL;
#endif
}

}

bool idImage::IsLoaded() const {
	return texnum != TEXTURE_NOT_LOADED;
}

int idImage::BitsForInternalFormat( int format ) const {
	switch ( format ) {
		case 1:
		case GL_ALPHA8:
		case GL_INTENSITY8:
		case GL_LUMINANCE8:
			return 8;
		case 2:
		case GL_LUMINANCE8_ALPHA8:
		case GL_RGBA4:
		case GL_RGB5:
			return 16;
		case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			return 4;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			return 8;
		case 3:
		case 4:
		case GL_RGB8:
		case GL_RGBA8:
		case GL_DEPTH_COMPONENT24:
			return 32;
		default:
			common->Warning( "idImage::BitsForInternalFormat: unknown format 0x%x", format );
			return 32;
	}
}

GLenum idImage::SelectInternalFormat(
	const byte**,
	int,
	int,
	int,
	textureDepth_t minimumDepth
) const {
	if ( minimumDepth == TD_HIGH_QUALITY || minimumDepth == TD_BUMP ) {
		return GL_RGBA8;
	}
	return GL_RGBA8;
}

void idImage::SetImageFilterAndRepeat() const {
	const GLenum target = TextureTargetForType( type );
	if ( target == 0 || !IsLoaded() ) {
		return;
	}

	GLint minFilter = GL_LINEAR;
	GLint magFilter = GL_LINEAR;
	switch ( filter ) {
		case TF_LINEAR:
			break;
		case TF_NEAREST:
			minFilter = GL_NEAREST;
			magFilter = GL_NEAREST;
			break;
		case TF_LINEARNEAREST:
			minFilter = GL_NEAREST;
			break;
		case TF_DEFAULT:
			minFilter = globalImages != NULL ? globalImages->textureMinFilter : GL_LINEAR_MIPMAP_LINEAR;
			magFilter = globalImages != NULL ? globalImages->textureMaxFilter : GL_LINEAR;
			break;
		default:
			break;
	}
	if ( type == TT_RECT ) {
		minFilter = minFilter == GL_NEAREST ? GL_NEAREST : GL_LINEAR;
	}
	glTexParameteri( target, GL_TEXTURE_MIN_FILTER, minFilter );
	glTexParameteri( target, GL_TEXTURE_MAG_FILTER, magFilter );

	if ( target == GL_TEXTURE_2D ) {
		glTexParameteri( target, GL_TEXTURE_BASE_LEVEL, static_cast< GLint >( minLod ) );
		glTexParameteri( target, GL_TEXTURE_MAX_LEVEL, static_cast< GLint >( maxLod ) );
	}
	if ( glConfig.anisotropicAvailable ) {
		float value = anisotropy;
		if ( value < 1.0f ) {
			value = filter == TF_DEFAULT && globalImages != NULL ? globalImages->textureAnisotropy : 1.0f;
		}
		value = idMath::ClampFloat( 1.0f, Max( 1.0f, glConfig.maxTextureAnisotropy ), value );
		glTexParameterf( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, value );
	}
	if ( glConfig.textureLODBiasAvailable && globalImages != NULL ) {
		glTexParameterf( target, GL_TEXTURE_LOD_BIAS_EXT, globalImages->textureLODBias );
	}

	GLint wrapS = GL_REPEAT;
	GLint wrapT = GL_REPEAT;
	switch ( repeat ) {
		case TR_REPEAT:
			break;
		case TR_CLAMP_X:
			wrapS = GL_CLAMP_TO_EDGE;
			break;
		case TR_CLAMP_Y:
			wrapT = GL_CLAMP_TO_EDGE;
			break;
		case TR_MIRROR:
			wrapS = wrapT = GL_MIRRORED_REPEAT_ARB;
			break;
		case TR_MIRROR_X:
			wrapS = GL_MIRRORED_REPEAT_ARB;
			break;
		case TR_MIRROR_Y:
			wrapT = GL_MIRRORED_REPEAT_ARB;
			break;
		case TR_CLAMP_TO_BORDER:
			wrapS = wrapT = GL_CLAMP_TO_BORDER;
			break;
		case TR_CLAMP:
		case TR_CLAMP_TO_ZERO:
		case TR_CLAMP_TO_ZERO_ALPHA:
		default:
			wrapS = wrapT = GL_CLAMP_TO_EDGE;
			break;
	}
	glTexParameteri( target, GL_TEXTURE_WRAP_S, wrapS );
	glTexParameteri( target, GL_TEXTURE_WRAP_T, wrapT );
	if ( type == TT_3D ) {
		glTexParameteri( target, GL_TEXTURE_WRAP_R, repeat == TR_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE );
	}
}

void idImage::GetDownsize( int& scaledWidth, int& scaledHeight ) const {
	if ( allowDownSize ) {
		for ( int i = 0; i < picMipOfs; i++ ) {
			scaledWidth = Max( 1, scaledWidth >> 1 );
			scaledHeight = Max( 1, scaledHeight >> 1 );
		}
	}
	const int maximum = Max( glConfig.maxTextureSize, 1 );
	while ( scaledWidth > maximum || scaledHeight > maximum ) {
		scaledWidth = Max( 1, scaledWidth >> 1 );
		scaledHeight = Max( 1, scaledHeight >> 1 );
	}
}

void idImage::Purge() {
	if ( IsLoaded() && glConfig.isInitialized ) {
		glDeleteTextures( 1, &texnum );
	}
	texnum = TEXTURE_NOT_LOADED;
	defaulted = false;
	backgroundLoadInProgress = false;
	bglNext = NULL;
}

void idImage::Bind() {
	if ( !IsLoaded() ) {
		if ( partialImage != NULL ) {
			partialImage->Bind();
			return;
		}
		ActuallyLoadImage( true );
	}
	if ( !IsLoaded() ) {
		return;
	}
	frameUsed++;
	bindCount++;
	glBindTexture( TextureTargetForType( type ), texnum );
}

void idImage::BindFragment() {
	if ( !IsLoaded() ) {
		if ( partialImage != NULL ) {
			partialImage->BindFragment();
			return;
		}
		ActuallyLoadImage( true );
	}
	if ( !IsLoaded() ) {
		return;
	}
	frameUsed++;
	bindCount++;
	glBindTexture( TextureTargetForType( type ), texnum );
}

void idImage::BindFragment( const int imageUnit ) {
	// BindFragment and GL_SelectTexture share one active-unit cache in retail.
	// Going directly through glActiveTextureARB here leaves the renderer cache
	// stale and makes a later cached texture-unit selection bind to the wrong
	// sampler.
	GL_SelectTexture( imageUnit );
	BindFragment();
}

void idImage::SetMipmapLevel( byte* pixels, int width, int height, int level, mipmapState_t& state ) {
	if ( !IsLoaded() || type != TT_2D ) {
		return;
	}
	ApplyMipmapState( pixels, width, height, level, state );
	glTexImage2D(
		GL_TEXTURE_2D,
		level,
		internalFormat,
		width,
		height,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		pixels
	);
}

void idImage::GenerateImage(
	const byte* pic,
	int width,
	int height,
	textureFilter_t filterParm,
	bool allowDownSizeParm,
	textureRepeat_t repeatParm,
	textureDepth_t depthParm,
	mipmapState_t mipmapStateParm
) {
	mipmapState = mipmapStateParm;
	GenerateImageEx(
		pic,
		width,
		height,
		filterParm,
		allowDownSizeParm,
		repeatParm,
		depthParm,
		0,
		-1
	);
}

void idImage::GenerateImageEx(
	const byte* pic,
	int width,
	int height,
	textureFilter_t filterParm,
	bool allowDownSizeParm,
	textureRepeat_t repeatParm,
	textureDepth_t depthParm,
	int internalFormatParm,
	int requestedMipLevels
) {
	Purge();
	filter = filterParm;
	allowDownSize = allowDownSizeParm;
	repeat = repeatParm;
	depth = depthParm;
	type = TT_2D;
	sourceWidth = width;
	sourceHeight = height;
	uploadDepth = 1;
	defaulted = false;

	if ( pic == NULL || width <= 0 || height <= 0 ) {
		return;
	}

	int scaledWidth = glConfig.textureNonPowerOfTwoAvailable ? width : NextPowerOfTwo( width );
	int scaledHeight = glConfig.textureNonPowerOfTwoAvailable ? height : NextPowerOfTwo( height );
	GetDownsize( scaledWidth, scaledHeight );
	uploadWidth = scaledWidth;
	uploadHeight = scaledHeight;
	internalFormat = internalFormatParm != 0
		? internalFormatParm
		: SelectInternalFormat( &pic, 1, width, height, depthParm );

	if ( !glConfig.isInitialized ) {
		return;
	}

	byte* levelPixels = ResampleRGBA( pic, width, height, scaledWidth, scaledHeight );
	if ( repeat == TR_CLAMP_TO_ZERO || repeat == TR_CLAMP_TO_ZERO_ALPHA ) {
		ApplyZeroClampBorder( levelPixels, scaledWidth, scaledHeight, repeat == TR_CLAMP_TO_ZERO_ALPHA );
	}

	glGenTextures( 1, &texnum );
	glBindTexture( GL_TEXTURE_2D, texnum );

	int level = 0;
	int levelWidth = scaledWidth;
	int levelHeight = scaledHeight;
	const int maximumMipLevels = requestedMipLevels < 0 ? MAX_TEXTURE_LEVELS : Max( requestedMipLevels, 1 );
	for ( ;; ) {
		ApplyMipmapState( levelPixels, levelWidth, levelHeight, level, mipmapState );
		glTexImage2D(
			GL_TEXTURE_2D,
			level,
			internalFormat,
			levelWidth,
			levelHeight,
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			levelPixels
		);
		if ( ( levelWidth == 1 && levelHeight == 1 ) || level + 1 >= maximumMipLevels ) {
			break;
		}
		byte* nextLevel = MipMapRGBA( levelPixels, levelWidth, levelHeight );
		Mem_Free( levelPixels );
		levelPixels = nextLevel;
		levelWidth = Max( 1, levelWidth >> 1 );
		levelHeight = Max( 1, levelHeight >> 1 );
		level++;
	}
	numMipLevels = level + 1;
	Mem_Free( levelPixels );
	SetImageFilterAndRepeat();
}

void idImage::Generate3DImage(
	const byte* pic,
	int width,
	int height,
	int imageDepth,
	textureFilter_t filterParm,
	bool allowDownSizeParm,
	textureRepeat_t repeatParm,
	textureDepth_t depthParm
) {
	Purge();
	filter = filterParm;
	allowDownSize = allowDownSizeParm;
	repeat = repeatParm;
	depth = depthParm;
	type = TT_3D;
	sourceWidth = uploadWidth = width;
	sourceHeight = uploadHeight = height;
	uploadDepth = imageDepth;
	internalFormat = GL_RGBA8;
	numMipLevels = 1;

	if ( !glConfig.isInitialized || !glConfig.texture3DAvailable || pic == NULL ) {
		return;
	}
	static texImage3DProc_t texImage3D = ResolveGLProc< texImage3DProc_t >( "glTexImage3D" );
	if ( texImage3D == NULL ) {
		texImage3D = ResolveGLProc< texImage3DProc_t >( "glTexImage3DEXT" );
	}
	if ( texImage3D == NULL ) {
		return;
	}
	glGenTextures( 1, &texnum );
	glBindTexture( GL_TEXTURE_3D, texnum );
	texImage3D(
		GL_TEXTURE_3D,
		0,
		internalFormat,
		width,
		height,
		imageDepth,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		pic
	);
	SetImageFilterAndRepeat();
}

void idImage::GenerateCubeImage(
	const byte* pic[ 6 ],
	int size,
	textureFilter_t filterParm,
	bool allowDownSizeParm,
	textureDepth_t depthParm
) {
	Purge();
	filter = filterParm;
	allowDownSize = allowDownSizeParm;
	repeat = TR_CLAMP;
	depth = depthParm;
	type = TT_CUBIC;
	sourceWidth = sourceHeight = uploadWidth = uploadHeight = size;
	uploadDepth = 1;
	internalFormat = GL_RGBA8;
	numMipLevels = 1;

	if ( !glConfig.isInitialized || !glConfig.cubeMapAvailable || pic == NULL || size <= 0 ) {
		return;
	}
	glGenTextures( 1, &texnum );
	glBindTexture( GL_TEXTURE_CUBE_MAP_ARB, texnum );
	for ( int face = 0; face < 6; face++ ) {
		glTexImage2D(
			GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + face,
			0,
			internalFormat,
			size,
			size,
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			pic[ face ]
		);
	}
	SetImageFilterAndRepeat();
	GenerateMipmaps();
}

void idImage::GenerateMipmaps() {
	if ( !IsLoaded() || type == TT_RECT ) {
		return;
	}
	static generateMipmapProc_t generateMipmap = ResolveGLProc< generateMipmapProc_t >( "glGenerateMipmapEXT" );
	if ( generateMipmap == NULL ) {
		generateMipmap = ResolveGLProc< generateMipmapProc_t >( "glGenerateMipmap" );
	}
	if ( generateMipmap != NULL ) {
		BindFragment();
		generateMipmap( TextureTargetForType( type ) );
	}
}

void idImage::MakeDefault() {
	byte data[ 16 * 16 * 4 ];
	for ( int y = 0; y < 16; y++ ) {
		for ( int x = 0; x < 16; x++ ) {
			const bool border = x == 0 || y == 0 || x == 15 || y == 15;
			byte* pixel = data + ( y * 16 + x ) * 4;
			pixel[ 0 ] = border ? 255 : 0;
			pixel[ 1 ] = 0;
			pixel[ 2 ] = border ? 255 : 0;
			pixel[ 3 ] = 255;
		}
	}
	GenerateImage( data, 16, 16, TF_DEFAULT, false, TR_REPEAT, TD_DEFAULT );
	defaulted = true;
}

void idImage::FromParameters(
	int width,
	int height,
	int internalFormatParm,
	textureType_t typeParm,
	textureFilter_t filterParm,
	textureRepeat_t repeatParm
) {
	Purge();
	filter = filterParm;
	repeat = typeParm == TT_CUBIC ? TR_CLAMP : repeatParm;
	depth = TD_HIGH_QUALITY;
	type = typeParm;
	sourceWidth = uploadWidth = width;
	sourceHeight = uploadHeight = height;
	uploadDepth = 1;
	internalFormat = internalFormatParm;
	fromParams = true;
	numMipLevels = 1;

	if ( !glConfig.isInitialized ) {
		return;
	}
	const GLenum target = TextureTargetForType( type );
	if ( target == 0 || type == TT_3D ) {
		common->Warning( "idImage::FromParameters: unsupported texture type %d for '%s'", type, imgName.c_str() );
		return;
	}
	glGenTextures( 1, &texnum );
	glBindTexture( target, texnum );
	if ( type == TT_CUBIC ) {
		for ( int face = 0; face < 6; face++ ) {
			glTexImage2D(
				GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + face,
				0,
				internalFormat,
				width,
				height,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				NULL
			);
		}
	} else {
		const bool depthFormat =
			internalFormat == GL_DEPTH_COMPONENT ||
			internalFormat == GL_DEPTH_COMPONENT16 ||
			internalFormat == GL_DEPTH_COMPONENT24 ||
			internalFormat == GL_DEPTH_COMPONENT32;
		glTexImage2D(
			target,
			0,
			internalFormat,
			width,
			height,
			0,
			depthFormat ? GL_DEPTH_COMPONENT : GL_RGBA,
			GL_UNSIGNED_BYTE,
			NULL
		);
	}
	SetImageFilterAndRepeat();
}

void idImage::Reload( bool, bool force ) {
	if ( !force && generatorFunction == NULL && timestamp == 0 ) {
		return;
	}
	Purge();
	ActuallyLoadImage( true );
}

bool idImage::CanBePartialLoaded() {
	return false;
}

void idImage::WritePrecompressedImage() {
}

bool idImage::CheckPrecompressedImage( bool ) {
	if ( !glConfig.isInitialized || !glConfig.textureCompressionAvailable ||
		 qglCompressedTexImage2DARB == NULL || fileSystem == NULL ) {
		return false;
	}
	if ( fileSystem->PerformingCopyFiles() ) {
		return false;
	}
	if ( depth == TD_BUMP && idImageManager::image_useNormalCompression.GetInteger() != 2 ) {
		return false;
	}

	char filename[ MAX_IMAGE_NAME ];
	ImageProgramStringToCompressedFileName( imgName.c_str(), filename );

	void* fileBuffer = NULL;
	unsigned precompressedTimestamp = 0;
	const int length = fileSystem->ReadFile( filename, &fileBuffer, &precompressedTimestamp, true );
	if ( length < static_cast< int >( sizeof( ddsFileHeader_t ) + 4 ) || fileBuffer == NULL ) {
		return false;
	}

	const byte* bytes = static_cast< const byte* >( fileBuffer );
	const unsigned long magic = LittleLong( *reinterpret_cast< const unsigned long* >( bytes ) );
	const ddsFileHeader_t* header = reinterpret_cast< const ddsFileHeader_t* >( bytes + 4 );
	const unsigned long pixelFlags = LittleLong( header->ddspf.dwFlags );
	if ( magic != DDS_MAKEFOURCC( 'D', 'D', 'S', ' ' ) ) {
		common->Warning( "CheckPrecompressedImage( %s ): magic != 'DDS '", imgName.c_str() );
		fileSystem->FreeFile( fileBuffer );
		return false;
	}
	if ( ( pixelFlags & DDSF_ID_INDEXCOLOR ) && !glConfig.sharedTexturePaletteAvailable ) {
		fileSystem->FreeFile( fileBuffer );
		return false;
	}

	timestamp = precompressedTimestamp;
	UploadPrecompressedImage( static_cast< byte* >( fileBuffer ), length );
	fileSystem->FreeFile( fileBuffer );
	return IsLoaded();
}

void idImage::UploadPrecompressedImage( byte* data, int len ) {
	if ( data == NULL || len < static_cast< int >( sizeof( ddsFileHeader_t ) + 4 ) ) {
		return;
	}

	ddsFileHeader_t* header = reinterpret_cast< ddsFileHeader_t* >( data + 4 );
	header->dwSize = LittleLong( header->dwSize );
	header->dwFlags = LittleLong( header->dwFlags );
	header->dwHeight = LittleLong( header->dwHeight );
	header->dwWidth = LittleLong( header->dwWidth );
	header->dwPitchOrLinearSize = LittleLong( header->dwPitchOrLinearSize );
	header->dwDepth = LittleLong( header->dwDepth );
	header->dwMipMapCount = LittleLong( header->dwMipMapCount );
	header->dwCaps1 = LittleLong( header->dwCaps1 );
	header->dwCaps2 = LittleLong( header->dwCaps2 );
	header->ddspf.dwSize = LittleLong( header->ddspf.dwSize );
	header->ddspf.dwFlags = LittleLong( header->ddspf.dwFlags );
	header->ddspf.dwFourCC = LittleLong( header->ddspf.dwFourCC );
	header->ddspf.dwRGBBitCount = LittleLong( header->ddspf.dwRGBBitCount );
	header->ddspf.dwRBitMask = LittleLong( header->ddspf.dwRBitMask );
	header->ddspf.dwGBitMask = LittleLong( header->ddspf.dwGBitMask );
	header->ddspf.dwBBitMask = LittleLong( header->ddspf.dwBBitMask );
	header->ddspf.dwABitMask = LittleLong( header->ddspf.dwABitMask );

	if ( header->dwSize != sizeof( ddsFileHeader_t ) ||
		 header->ddspf.dwSize != sizeof( ddsFilePixelFormat_t ) ||
		 header->dwWidth == 0 || header->dwHeight == 0 ) {
		common->Warning( "Invalid DDS header for image '%s'", imgName.c_str() );
		return;
	}

	GLenum externalFormat = 0;
	bool compressed = false;
	if ( header->ddspf.dwFlags & DDSF_FOURCC ) {
		compressed = true;
		switch ( header->ddspf.dwFourCC ) {
			case DDS_MAKEFOURCC( 'D', 'X', 'T', '1' ):
				internalFormat = ( header->ddspf.dwFlags & DDSF_ALPHAPIXELS )
					? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
					: GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
				break;
			case DDS_MAKEFOURCC( 'D', 'X', 'T', '3' ):
				internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
				break;
			case DDS_MAKEFOURCC( 'D', 'X', 'T', '5' ):
			case DDS_MAKEFOURCC( 'R', 'X', 'G', 'B' ):
				internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				break;
			case DDS_MAKEFOURCC( 'A', 'T', 'I', '1' ):
				isMonochrome = true;
				internalFormat = GL_COMPRESSED_LUMINANCE_LATC1_EXT;
				break;
			case DDS_MAKEFOURCC( 'A', 'T', 'I', '2' ):
				isMonochrome = true;
				internalFormat = glConfig.textureCompression3DCAvailable
					? GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI
					: GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT;
				break;
			default:
				common->Warning( "Invalid compressed DDS format 0x%x for image '%s'",
					header->ddspf.dwFourCC, imgName.c_str() );
				return;
		}
	} else if ( ( header->ddspf.dwFlags & DDSF_RGBA ) && header->ddspf.dwRGBBitCount == 32 ) {
		externalFormat = GL_BGRA_EXT;
		internalFormat = GL_RGBA8;
	} else if ( ( header->ddspf.dwFlags & DDSF_RGB ) && header->ddspf.dwRGBBitCount == 24 ) {
		externalFormat = GL_BGR_EXT;
		internalFormat = GL_RGB8;
	} else if ( header->ddspf.dwRGBBitCount == 8 ) {
		externalFormat = GL_ALPHA;
		internalFormat = GL_ALPHA8;
	} else {
		common->Warning( "Invalid uncompressed DDS format for image '%s'", imgName.c_str() );
		return;
	}

	const int originalWidth = static_cast< int >( header->dwWidth );
	const int originalHeight = static_cast< int >( header->dwHeight );
	sourceWidth = originalWidth;
	sourceHeight = originalHeight;
	uploadWidth = originalWidth;
	uploadHeight = originalHeight;
	uploadDepth = 1;
	type = TT_2D;
	precompressedFile = true;
	defaulted = false;
	if ( header->dwFlags & DDSF_ID_MONOCHROME ) {
		isMonochrome = true;
	}
	GetDownsize( uploadWidth, uploadHeight );

	glGenTextures( 1, &texnum );
	if ( texnum == TEXTURE_NOT_LOADED ) {
		precompressedFile = false;
		return;
	}
	glBindTexture( GL_TEXTURE_2D, texnum );

	const int fileMipCount = ( header->dwFlags & DDSF_MIPMAPCOUNT )
		? Max( 1, static_cast< int >( header->dwMipMapCount ) )
		: 1;
	int mipWidth = originalWidth;
	int mipHeight = originalHeight;
	int skippedMips = 0;
	int uploadedMips = 0;
	const byte* imageData = data + sizeof( ddsFileHeader_t ) + 4;
	const byte* const fileEnd = data + len;

	for ( int mip = 0; mip < fileMipCount; mip++ ) {
		int mipSize;
		if ( compressed ) {
			const bool eightByteBlock = internalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
				internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
				internalFormat == GL_COMPRESSED_LUMINANCE_LATC1_EXT;
			mipSize = ( ( mipWidth + 3 ) / 4 ) * ( ( mipHeight + 3 ) / 4 ) *
				( eightByteBlock ? 8 : 16 );
		} else {
			mipSize = mipWidth * mipHeight * static_cast< int >( header->ddspf.dwRGBBitCount / 8 );
		}
		if ( mipSize <= 0 || imageData + mipSize > fileEnd ) {
			common->Warning( "Truncated DDS mip data for image '%s'", imgName.c_str() );
			break;
		}

		if ( mipWidth > uploadWidth || mipHeight > uploadHeight ) {
			skippedMips++;
		} else if ( compressed ) {
			qglCompressedTexImage2DARB(
				GL_TEXTURE_2D, mip - skippedMips, internalFormat,
				mipWidth, mipHeight, 0, mipSize, imageData );
			uploadedMips++;
		} else {
			glTexImage2D(
				GL_TEXTURE_2D, mip - skippedMips, internalFormat,
				mipWidth, mipHeight, 0, externalFormat, GL_UNSIGNED_BYTE, imageData );
			uploadedMips++;
		}

		imageData += mipSize;
		mipWidth = Max( 1, mipWidth >> 1 );
		mipHeight = Max( 1, mipHeight >> 1 );
	}

	if ( uploadedMips == 0 ) {
		glDeleteTextures( 1, &texnum );
		texnum = TEXTURE_NOT_LOADED;
		precompressedFile = false;
		return;
	}
	numMipLevels = uploadedMips;
	SetImageFilterAndRepeat();
}

bool idImage::StartBackgroundImageLoad() {
	return false;
}

void idImage::ActuallyLoadImage( bool checkForPrecompressed ) {
	if ( IsLoaded() || !glConfig.isInitialized ) {
		return;
	}
	if ( generatorFunction != NULL ) {
		( *generatorFunction )( this );
		return;
	}
	if ( fromParams ) {
		FromParameters( uploadWidth, uploadHeight, internalFormat, type, filter, repeat );
		return;
	}
	if ( cubeFiles == CF_2D && depth != TD_HIGH_QUALITY && checkForPrecompressed &&
		 CheckPrecompressedImage( true ) ) {
		return;
	}

	byte* pixels = NULL;
	int width = 0;
	int height = 0;
	unsigned loadedTimestamp = 0;
	if ( globalImages != NULL ) {
		globalImages->LoadImage( imgName, &pixels, &width, &height, &loadedTimestamp, true );
	}
	if ( pixels == NULL ) {
		MakeDefault();
		return;
	}
	timestamp = loadedTimestamp;
	GenerateImage(
		pixels,
		width,
		height,
		filter,
		allowDownSize,
		repeat,
		depth,
		mipmapState
	);
	precompressedFile = false;
	if ( globalImages != NULL ) {
		globalImages->FreeImageBuffer( pixels );
	} else {
		Mem_Free( pixels );
	}
}

void idImage::UploadCompressedNormalMap( int width, int height, const byte* rgba, int mipLevel ) {
	glTexImage2D(
		GL_TEXTURE_2D,
		mipLevel,
		GL_RGBA8,
		width,
		height,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		rgba
	);
}

void idImage::ImageProgramStringToCompressedFileName( const char* imageProgram, char* fileName ) const {
	if ( fileName == NULL ) {
		return;
	}
	const char* program = imageProgram != NULL ? imageProgram : "";
	if ( idStr::CheckExtension( program, "dds" ) ) {
		idStr::Copynz( fileName, program, MAX_IMAGE_NAME );
		return;
	}

	idStr::Copynz( fileName, "generated/dds/", MAX_IMAGE_NAME );
	char* output = fileName + strlen( fileName );
	char* const outputEnd = fileName + MAX_IMAGE_NAME - 1;
	const char* component = program;
	int directoryDepth = 0;
	for ( const char* scan = program;
		  *scan != '\0' && scan - program < 128 && directoryDepth < 4;
		  scan++ ) {
		if ( *scan != '/' && *scan != '\\' && *scan != '(' ) {
			continue;
		}
		while ( component < scan && *component == ' ' ) {
			component++;
		}
		for ( const char* source = component; source < scan && output < outputEnd; source++ ) {
			if ( *source == ')' || *source == ',' ) {
				continue;
			}
			*output++ = idStr::IsBadFilenameChar( *source ) ? '_' : *source;
		}
		if ( output < outputEnd ) {
			*output++ = '/';
		}
		component = scan + 1;
		directoryDepth++;
	}
	*output = '\0';

	const unsigned long checksum = MD5_BlockChecksum( program, static_cast< int >( strlen( program ) ) );
	idStr::Append( fileName, MAX_IMAGE_NAME,
		va( "%lub%d.dds", checksum, static_cast< int >( strlen( program ) ) ) );
}

int idImage::NumLevelsForImageSize( int width, int height ) {
	int levels = 1;
	while ( width > 1 || height > 1 ) {
		width = Max( 1, width >> 1 );
		height = Max( 1, height >> 1 );
		levels++;
	}
	return levels;
}

void idImage::CopyFramebuffer( int x, int y, int width, int height, bool ) {
	if ( width <= 0 || height <= 0 || !glConfig.isInitialized ) {
		return;
	}
	if ( !IsLoaded() || type != TT_RECT || uploadWidth != width || uploadHeight != height ) {
		FromParameters( width, height, GL_RGBA8, TT_RECT, TF_LINEAR, TR_CLAMP );
	}
	glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texnum );
	glCopyTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, x, y, width, height );
}

void idImage::CopyFramebufferCube( int x, int y, int width, int height, int faceNum ) {
	if ( faceNum < 0 || faceNum >= 6 || !glConfig.isInitialized ) {
		return;
	}
	if ( !IsLoaded() || type != TT_CUBIC || uploadWidth != width || uploadHeight != height ) {
		FromParameters( width, height, GL_RGBA8, TT_CUBIC, TF_LINEAR, TR_CLAMP );
	}
	glBindTexture( GL_TEXTURE_CUBE_MAP_ARB, texnum );
	glCopyTexSubImage2D(
		GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + faceNum,
		0,
		0,
		0,
		x,
		y,
		width,
		height
	);
}

void idImage::CopyDepthbuffer( int x, int y, int width, int height ) {
	if ( width <= 0 || height <= 0 || !glConfig.isInitialized ) {
		return;
	}
	if ( !IsLoaded() || type != TT_RECT || uploadWidth != width || uploadHeight != height ) {
		FromParameters( width, height, GL_DEPTH_COMPONENT24, TT_RECT, TF_NEAREST, TR_CLAMP );
	}
	glBindTexture( GL_TEXTURE_RECTANGLE_ARB, texnum );
	glCopyTexSubImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, x, y, width, height );
}

void idImage::UploadScratch( const byte* pic, int width, int height ) {
	if ( pic == NULL || width <= 0 || height <= 0 ) {
		return;
	}

	// Retail overloads this entry point for six vertically packed RGBA cube
	// faces as well as ordinary 2D scratch images.
	if ( height == width * 6 ) {
		const int faceHeight = height / 6;
		const bool allocate = !IsLoaded() || type != TT_CUBIC || uploadWidth != width || uploadHeight != faceHeight;
		if ( !IsLoaded() || type != TT_CUBIC ) {
			FromParameters( width, faceHeight, GL_RGB8, TT_CUBIC, TF_LINEAR, TR_CLAMP );
		}
		glBindTexture( GL_TEXTURE_CUBE_MAP_ARB, texnum );
		for ( int face = 0; face < 6; ++face ) {
			const byte* facePixels = pic + face * width * faceHeight * 4;
			if ( allocate ) {
				glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + face, 0, GL_RGB8,
					width, faceHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, facePixels );
			} else {
				glTexSubImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + face, 0, 0, 0,
					width, faceHeight, GL_RGBA, GL_UNSIGNED_BYTE, facePixels );
			}
		}
		type = TT_CUBIC;
		sourceWidth = uploadWidth = width;
		sourceHeight = uploadHeight = faceHeight;
		repeat = TR_CLAMP;
		glTexParameteri( GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_CUBE_MAP_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		return;
	}

	const bool luminance = internalFormat == GL_LUMINANCE8;
	const GLenum sourceFormat = luminance ? GL_LUMINANCE : GL_RGBA;
	const int scratchInternalFormat = luminance ? GL_LUMINANCE8 : GL_RGBA8;
	const bool allocate = !IsLoaded() || type != TT_2D || uploadWidth != width || uploadHeight != height;
	if ( !IsLoaded() || type != TT_2D ) {
		FromParameters( width, height, scratchInternalFormat, TT_2D, TF_LINEAR, TR_REPEAT );
	}
	glBindTexture( GL_TEXTURE_2D, texnum );
	if ( allocate ) {
		glTexImage2D( GL_TEXTURE_2D, 0, scratchInternalFormat, width, height, 0, sourceFormat, GL_UNSIGNED_BYTE, pic );
	} else {
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, sourceFormat, GL_UNSIGNED_BYTE, pic );
	}
	type = TT_2D;
	sourceWidth = uploadWidth = width;
	sourceHeight = uploadHeight = height;
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
}

void idImage::CopyFromImage( idImage* image ) {
	if ( image == NULL ) {
		return;
	}
	byte* pixels = NULL;
	int width = 0;
	int height = 0;
	image->Download( &pixels, &width, &height );
	if ( pixels != NULL ) {
		GenerateImage( pixels, width, height, image->filter, false, image->repeat, image->depth );
		Mem_Free( pixels );
	}
}

void idImage::CopyFromImageCube( idImage* image ) {
	if ( image == NULL || image->type != TT_CUBIC || !image->IsLoaded() ) {
		return;
	}
	FromParameters(
		image->uploadWidth,
		image->uploadHeight,
		image->internalFormat,
		TT_CUBIC,
		image->filter,
		image->repeat
	);
}

void idImage::Download( byte** pixels, int* width, int* height ) {
	if ( pixels != NULL ) {
		*pixels = NULL;
	}
	if ( width != NULL ) {
		*width = uploadWidth;
	}
	if ( height != NULL ) {
		*height = uploadHeight;
	}
	if ( pixels == NULL || !IsLoaded() || type != TT_2D ) {
		return;
	}
	*pixels = static_cast< byte* >( Mem_Alloc( uploadWidth * uploadHeight * 4 ) );
	glBindTexture( GL_TEXTURE_2D, texnum );
	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, *pixels );
}

void idImage::SetClassification( int tag ) {
	classification = tag;
}

int idImage::StorageSize() const {
	if ( !IsLoaded() ) {
		return 0;
	}
	const int depthValue = Max( uploadDepth, 1 );
	const int bits = BitsForInternalFormat( internalFormat );
	int size = uploadWidth * uploadHeight * depthValue * bits / 8;
	if ( numMipLevels > 1 ) {
		size = size * 4 / 3;
	}
	if ( type == TT_CUBIC ) {
		size *= 6;
	}
	return size;
}

void idImage::Print( bool csv ) const {
	if ( csv ) {
		common->Printf(
			"\"%s\",%d,%d,%d,0x%x,%d\n",
			imgName.c_str(),
			uploadWidth,
			uploadHeight,
			uploadDepth,
			internalFormat,
			StorageSize()
		);
		return;
	}
	common->Printf(
		"%4d %4d %s %s\n",
		uploadWidth,
		uploadHeight,
		IsLoaded() ? "loaded  " : "unloaded",
		imgName.c_str()
	);
}

void idImage::SetLodDistance( float distance ) {
	if ( frameOfDistance == 0 || distance < smallestDistanceSeen ) {
		smallestDistanceSeen = distance;
	}
	frameOfDistance++;
}
