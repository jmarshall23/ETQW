// Copyright (C) 2007 Id Software, Inc.
//
// ETQW image file loading reconstructed in its original PDB source unit.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Image.h"

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

void R_LoadImage( const char* name, byte** pic, int* width, int* height,
	unsigned* timestamp, bool ) {
	if ( pic != NULL ) {
		*pic = NULL;
	}
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return;
	}
	idStr imageName = name;
	imageName.DefaultFileExtension( "tga" );
	LoadTGA( imageName.c_str(), pic, width, height, timestamp, true );
}

bool R_LoadCubeImages( const char* cubeName, cubeFiles_t cubeFiles,
	byte* pictures[ 6 ], int* size, unsigned* timestamp ) {
	if ( pictures == NULL || size == NULL || cubeName == NULL ) {
		return false;
	}
	for ( int face = 0; face < 6; ++face ) {
		pictures[ face ] = NULL;
	}
	*size = 0;
	if ( timestamp != NULL ) {
		*timestamp = 0;
	}
	static const char* nativeSuffixes[ 6 ] = {
		"_px.tga", "_nx.tga", "_py.tga", "_ny.tga", "_pz.tga", "_nz.tga"
	};
	static const char* cameraSuffixes[ 6 ] = {
		"_right.tga", "_left.tga", "_forward.tga", "_back.tga",
		"_up.tga", "_down.tga"
	};
	if ( cubeFiles != CF_NATIVE && cubeFiles != CF_CAMERA ) {
		return false;
	}
	const char** suffixes = cubeFiles == CF_NATIVE ? nativeSuffixes : cameraSuffixes;
	unsigned newestTimestamp = 0;
	for ( int face = 0; face < 6; ++face ) {
		idStr faceName = cubeName;
		faceName.StripFileExtension();
		faceName += suffixes[ face ];
		int width = 0;
		int height = 0;
		unsigned faceTimestamp = 0;
		LoadTGA( faceName.c_str(), &pictures[ face ], &width, &height,
			&faceTimestamp, true );
		if ( pictures[ face ] == NULL || width <= 0 || width != height ||
			( *size != 0 && width != *size ) ) {
			for ( int loadedFace = 0; loadedFace < 6; ++loadedFace ) {
				Mem_Free( pictures[ loadedFace ] );
				pictures[ loadedFace ] = NULL;
			}
			*size = 0;
			return false;
		}
		*size = width;
		newestTimestamp = Max( newestTimestamp, faceTimestamp );
	}
	if ( timestamp != NULL ) {
		*timestamp = newestTimestamp;
	}
	static int reportedCubeLoads = 0;
	if ( cvarSystem != NULL && cvarSystem->GetCVarBool( "r_vkDebugMaterials" ) &&
		reportedCubeLoads < 16 ) {
		common->Printf( "Loaded cube image '%s' (%dx%d) through the engine filesystem\n",
			cubeName, *size, *size );
		reportedCubeLoads++;
	}
	return true;
}
