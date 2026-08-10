// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#pragma hdrstop

#include "SurfaceTypeMap.h"
#include "../decllib/DeclSurfaceType.h"
#include "../decllib/declTypeHolder.h"

extern idCVar com_purgeAll;

namespace {

const int SURFACE_TYPE_MAP_IDENT = ( 'I' ) | ( 'S' << 8 ) | ( 'T' << 16 ) | ( 'M' << 24 );
const int SURFACE_TYPE_MAP_VERSION = 2;

sdSurfaceTypeMapManager surfaceTypeMapManagerLocal;

}

sdSurfaceTypeMapManager* surfaceTypeMapManager = &surfaceTypeMapManagerLocal;

sdSurfaceTypeMap::sdSurfaceTypeMap( void ) :
	levelLoadReferenced( false ),
	referencedOutsideLevelLoad( false ),
	purged( true ),
	defaulted( false ),
	surfaceTypeMap( NULL ),
	colorMap( NULL ) {
	memset( &header, 0, sizeof( header ) );
}

sdSurfaceTypeMap::~sdSurfaceTypeMap( void ) {
	Purge();
}

void sdSurfaceTypeMap::Purge( void ) {
	if ( purged ) {
		return;
	}
	surfaceTypes.Clear();
	delete[] surfaceTypeMap;
	delete[] colorMap;
	surfaceTypeMap = NULL;
	colorMap = NULL;
	purged = true;
	defaulted = false;
}

void sdSurfaceTypeMap::Load( void ) {
	if ( defaulted ) {
		return;
	}
	Purge();
	defaulted = false;

	idStr fileName = name;
	fileName.SetFileExtension( ".stm" );
	idFile* file = fileSystem->OpenFileRead( fileName.c_str() );
	if ( file == NULL ) {
		common->Warning( "sdSurfaceTypeMap::Load : Failed to open %s", fileName.c_str() );
		defaulted = true;
		purged = false;
		return;
	}

	bool valid = file->ReadInt( header.ident ) == sizeof( int ) &&
		file->ReadInt( header.version ) == sizeof( int );
	if ( !valid || header.ident != SURFACE_TYPE_MAP_IDENT ) {
		common->Warning( "sdSurfaceTypeMap::Load : Unknown fileid on %s", fileName.c_str() );
		defaulted = true;
	} else if ( header.version != SURFACE_TYPE_MAP_VERSION && header.version != 1 ) {
		common->Warning( "sdSurfaceTypeMap::Load : Wrong version on %s (%i should be %i)",
			fileName.c_str(), header.version, SURFACE_TYPE_MAP_VERSION );
		defaulted = true;
	}

	if ( !defaulted ) {
		valid = file->ReadInt( header.width ) == sizeof( int ) &&
			file->ReadInt( header.height ) == sizeof( int ) &&
			file->ReadInt( header.numSurfaceTypes ) == sizeof( int ) &&
			file->ReadInt( header.surfaceTypesOffset ) == sizeof( int ) &&
			file->ReadInt( header.mapOffset ) == sizeof( int ) &&
			file->ReadInt( header.colorOffset ) == sizeof( int );

		const int numPixels = header.width * header.height;
		if ( !valid || header.width <= 0 || header.height <= 0 || header.numSurfaceTypes <= 0 ||
			numPixels <= 0 || header.surfaceTypesOffset < static_cast< int >( sizeof( header_s ) ) ||
			header.mapOffset < header.surfaceTypesOffset || header.colorOffset < header.mapOffset ) {
			common->Warning( "sdSurfaceTypeMap::Load : Invalid header in %s", fileName.c_str() );
			defaulted = true;
		}
	}

	if ( !defaulted ) {
		const int namesLength = header.mapOffset - header.surfaceTypesOffset;
		char* names = new char[ namesLength + 1 ];
		file->Seek( header.surfaceTypesOffset, FS_SEEK_SET );
		valid = file->Read( names, namesLength ) == namesLength;
		names[ namesLength ] = '\0';

		const char* cursor = names;
		const char* end = names + namesLength;
		for ( int i = 0; valid && i < header.numSurfaceTypes; ++i ) {
			if ( cursor >= end ) {
				valid = false;
				break;
			}
			const void* terminator = memchr( cursor, '\0', end - cursor );
			if ( terminator == NULL ) {
				valid = false;
				break;
			}
			const sdDeclSurfaceType* surfaceType = declHolder.FindSurfaceType( cursor, false );
			if ( surfaceType == NULL ) {
				common->Warning( "sdSurfaceTypeMap::Load : Unable to find surface type %s referenced from %s",
					cursor, fileName.c_str() );
				valid = false;
				break;
			}
			surfaceTypes.Append( surfaceType );
			cursor = static_cast< const char* >( terminator ) + 1;
		}
		delete[] names;

		if ( !valid ) {
			defaulted = true;
		}
	}

	if ( !defaulted ) {
		const int numPixels = header.width * header.height;
		surfaceTypeMap = new byte[ numPixels ];
		file->Seek( header.mapOffset, FS_SEEK_SET );
		if ( file->Read( surfaceTypeMap, numPixels ) != numPixels ) {
			defaulted = true;
		} else {
			bool invalidIndex = false;
			for ( int i = 0; i < numPixels; ++i ) {
				if ( surfaceTypeMap[ i ] >= header.numSurfaceTypes ) {
					surfaceTypeMap[ i ] = 0;
					invalidIndex = true;
				}
			}
			if ( invalidIndex ) {
				common->Warning( "sdSurfaceTypeMap::Load : Invalid surface type index in %s", fileName.c_str() );
			}
		}

		if ( !defaulted ) {
			colorMap = new byte[ numPixels * 3 ];
			if ( header.version == 1 ) {
				memset( colorMap, 255, numPixels * 3 );
			} else {
				file->Seek( header.colorOffset, FS_SEEK_SET );
				if ( file->Read( colorMap, numPixels * 3 ) != numPixels * 3 ) {
					defaulted = true;
				}
			}
		}
	}

	fileSystem->CloseFile( file );
	if ( defaulted ) {
		surfaceTypes.Clear();
		delete[] surfaceTypeMap;
		delete[] colorMap;
		surfaceTypeMap = NULL;
		colorMap = NULL;
	}
	purged = false;
}

const sdDeclSurfaceType* sdSurfaceTypeMap::GetSurfaceType( const idVec2& tc, idVec3* color ) const {
	if ( surfaceTypeMap == NULL ) {
		return NULL;
	}

	const float u = tc.x - idMath::Floor( tc.x );
	const float v = tc.y - idMath::Floor( tc.y );
	int x = static_cast< int >( header.width * u );
	int y = static_cast< int >( header.height * v );
	if ( x < 0 ) {
		x += header.width;
	}
	if ( y < 0 ) {
		y += header.height;
	}
	x = idMath::ClampInt( 0, header.width - 1, x );
	y = idMath::ClampInt( 0, header.height - 1, y );
	const int index = x + y * header.width;

	if ( color != NULL && colorMap != NULL ) {
		color->x = colorMap[ index * 3 + 2 ] * ( 1.0f / 255.0f );
		color->y = colorMap[ index * 3 + 1 ] * ( 1.0f / 255.0f );
		color->z = colorMap[ index * 3 + 0 ] * ( 1.0f / 255.0f );
	}

	const int surfaceIndex = surfaceTypeMap[ index ];
	return surfaceIndex < surfaceTypes.Num() ? surfaceTypes[ surfaceIndex ] : NULL;
}

sdSurfaceTypeMapManager::sdSurfaceTypeMapManager( void ) : insideLevelLoad( false ) {
}

sdSurfaceTypeMapManager::~sdSurfaceTypeMapManager( void ) {
	Shutdown();
}

sdSurfaceTypeMap* sdSurfaceTypeMapManager::FindSurfaceTypeMap( const char* name ) {
	sdSurfaceTypeMap** value = NULL;
	if ( !surfaceTypeMaps.Get( name, &value ) || value == NULL ) {
		return NULL;
	}
	return *value;
}

sdSurfaceTypeMap* sdSurfaceTypeMapManager::AllocSurfaceTypeMap( const char* name ) {
	sdSurfaceTypeMap* map = new sdSurfaceTypeMap;
	map->name = name;
	surfaceTypeMaps.Set( name, map );
	return map;
}

sdSurfaceTypeMap* sdSurfaceTypeMapManager::SurfaceTypeMapFromFile( const char* mapName, bool makeDefault ) {
	idStr canonicalName = mapName != NULL ? mapName : "";
	canonicalName.ToLower();
	canonicalName.BackSlashesToSlashes();

	sdSurfaceTypeMap* map = FindSurfaceTypeMap( canonicalName.c_str() );
	if ( map == NULL ) {
		if ( !makeDefault ) {
			idStr fileName = canonicalName;
			fileName.SetFileExtension( ".stm" );
			if ( fileSystem->ReadFile( fileName.c_str(), NULL ) == -1 ) {
				return NULL;
			}
		}
		map = AllocSurfaceTypeMap( canonicalName.c_str() );
	}

	map->levelLoadReferenced = true;
	if ( !insideLevelLoad ) {
		map->referencedOutsideLevelLoad = true;
		map->Load();
	}
	return map;
}

void sdSurfaceTypeMapManager::BeginLevelLoad( void ) {
	insideLevelLoad = true;
	for ( int i = 0; i < surfaceTypeMaps.Num(); ++i ) {
		sdSurfaceTypeMap* map = *surfaceTypeMaps.GetIndex( i );
		if ( com_purgeAll.GetBool() ) {
			map->Purge();
		}
		map->levelLoadReferenced = false;
	}
}

void sdSurfaceTypeMapManager::EndLevelLoad( void ) {
	insideLevelLoad = false;
	for ( int i = 0; i < surfaceTypeMaps.Num(); ++i ) {
		sdSurfaceTypeMap* map = *surfaceTypeMaps.GetIndex( i );
		if ( !map->levelLoadReferenced && !map->referencedOutsideLevelLoad ) {
			map->Purge();
		} else if ( map->levelLoadReferenced && map->purged ) {
			map->Load();
		}
	}
}

void sdSurfaceTypeMapManager::Shutdown( void ) {
	for ( int i = 0; i < surfaceTypeMaps.Num(); ++i ) {
		delete *surfaceTypeMaps.GetIndex( i );
	}
	surfaceTypeMaps.Clear();
}
