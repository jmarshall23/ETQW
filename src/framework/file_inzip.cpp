// Copyright (C) 2007 Id Software, Inc.
//


#include "precompiled.h"
#pragma hdrstop

#include "minizip/unzip.h"
#include "minizip/unzip_internal.h"

idFile_InZip::idFile_InZip( void ) {
	name = "invalid";
	zipFilePos = 0;
	fileSize = 0;
	z = NULL;
}

idFile_InZip::~idFile_InZip( void ) {
	unzCloseCurrentFile( z );
	unzClose( z );
}

int idFile_InZip::Read( void* buffer, int len ) {
	const int read = unzReadCurrentFile( z, buffer, len );
	fileSystem->AddToReadCount( read );
	return read;
}

int idFile_InZip::Write( const void* buffer, int len ) {
	common->FatalError( "idFile_InZip::Write: cannot write to the zipped file %s", name.c_str() );
	return 0;
}

void idFile_InZip::ForceFlush( void ) {
	common->FatalError( "idFile_InZip::ForceFlush: cannot flush the zipped file %s", name.c_str() );
}

void idFile_InZip::Flush( void ) {
	common->FatalError( "idFile_InZip::Flush: cannot flush the zipped file %s", name.c_str() );
}

int idFile_InZip::Tell( void ) {
	return unztell( z );
}

int idFile_InZip::Length( void ) const {
	return fileSize;
}

unsigned int idFile_InZip::Timestamp( void ) {
	return 0;
}

#define ZIP_SEEK_BUF_SIZE ( 1 << 15 )

int idFile_InZip::Seek( long offset, fsOrigin_t origin ) {
	int seekLength;

	switch ( origin ) {
		case FS_SEEK_END:
			seekLength = fileSize - offset;
			break;
		case FS_SEEK_SET:
			seekLength = offset;
			break;
		case FS_SEEK_CUR:
			seekLength = offset;
			goto skipBytes;
		default:
			common->FatalError( "idFile_InZip::Seek: bad origin for %s\n", name.c_str() );
			return -1;
	}

	// Minizip cannot seek within a compressed member. Reopen the member at
	// its recorded directory position, then discard bytes up to the target.
	unzSetOffset( z, zipFilePos );
	unzOpenCurrentFile( z );
	if ( seekLength <= 0 ) {
		return 0;
	}

skipBytes:
	char* buffer = static_cast< char* >( _alloca16( ZIP_SEEK_BUF_SIZE ) );
	int totalRead = 0;
	while ( totalRead < seekLength - ZIP_SEEK_BUF_SIZE ) {
		const int read = unzReadCurrentFile( z, buffer, ZIP_SEEK_BUF_SIZE );
		if ( read < ZIP_SEEK_BUF_SIZE ) {
			return -1;
		}
		totalRead += ZIP_SEEK_BUF_SIZE;
	}

	totalRead += unzReadCurrentFile( z, buffer, seekLength - totalRead );
	return totalRead == seekLength ? 0 : -1;
}
