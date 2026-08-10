// Copyright (C) 2007 Id Software, Inc.
//


#ifndef __SURFACETYPEMAP_H__
#define __SURFACETYPEMAP_H__

class sdDeclSurfaceType;

class sdSurfaceTypeMap {
public:
	struct header_s {
		int		ident;
		int		version;
		int		width;
		int		height;
		int		numSurfaceTypes;
		int		surfaceTypesOffset;
		int		mapOffset;
		int		colorOffset;
	};

						sdSurfaceTypeMap( void );
						~sdSurfaceTypeMap( void );

	void				Load( void );
	void				Purge( void );
	const sdDeclSurfaceType*	GetSurfaceType( const idVec2& tc, idVec3* color = NULL ) const;

	const char*			GetName( void ) const { return name.c_str(); }
	bool				IsDefaulted( void ) const { return defaulted; }
	bool				IsPurged( void ) const { return purged; }

private:
	idStr				name;
	bool				levelLoadReferenced;
	bool				referencedOutsideLevelLoad;
	bool				purged;
	bool				defaulted;
	header_s			header;
	idList< const sdDeclSurfaceType* > surfaceTypes;
	byte*				surfaceTypeMap;
	byte*				colorMap;

	friend class sdSurfaceTypeMapManager;
};

class sdSurfaceTypeMapManager {
public:
						sdSurfaceTypeMapManager( void );
						~sdSurfaceTypeMapManager( void );

	void				BeginLevelLoad( void );
	void				EndLevelLoad( void );
	void				Shutdown( void );
	sdSurfaceTypeMap*	SurfaceTypeMapFromFile( const char* name, bool makeDefault = true );

private:
	sdSurfaceTypeMap*	FindSurfaceTypeMap( const char* name );
	sdSurfaceTypeMap*	AllocSurfaceTypeMap( const char* name );

	idHashMap< sdSurfaceTypeMap* > surfaceTypeMaps;
	bool				insideLevelLoad;
};

extern sdSurfaceTypeMapManager* surfaceTypeMapManager;

#endif /* !__SURFACETYPEMAP_H__ */
