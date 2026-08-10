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

