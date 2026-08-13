// Private on-disk QuakeWars2 navmesh container shared by navbuild and runtime.

#ifndef __NAVIGATION_FILE_H__
#define __NAVIGATION_FILE_H__

#include "Navigation.h"

static const unsigned int NAV_FILE_MAGIC = ( 'Q' << 24 ) | ( 'W' << 16 ) | ( 'N' << 8 ) | 'V';
static const unsigned int NAV_FILE_VERSION = 1;

struct navFileHeader_t {
	unsigned int	magic;
	unsigned int	version;
	unsigned int	geometryCRC;
	unsigned int	profileCount;
};

struct navFileProfileHeader_t {
	navProfileSettings_t settings;
	float			orig[ 3 ];
	float			tileWidth;
	float			tileHeight;
	int				maxTiles;
	int				maxPolys;
	int				tileCount;
	int				linkCount;
};

struct navFileTileHeader_t {
	unsigned int	dataSize;
};

struct navFileLink_t {
	char			name[ 64 ];
	unsigned int	userId;
};

#endif // __NAVIGATION_FILE_H__
