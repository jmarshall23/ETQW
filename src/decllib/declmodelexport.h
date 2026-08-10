// Copyright (C) 2007 Id Software, Inc.
//


#ifndef __DECLMODELEXPORT_H__
#define __DECLMODELEXPORT_H__

#include "../framework/declManager.h"

class idLexer;
class idMaterial;

/*
===============================================================================

	sdDeclModelExport

	Per-surface settings used while converting models.  A surface can be
	selected either by its source material or by its numeric index.

===============================================================================
*/

class sdDeclModelExport : public idDecl {
public:
	enum exportMode_t {
		EM_ETQW = 0,
		EM_DOOM = 1
	};

	enum solidState_t {
		SS_SOLID = 0,
		SS_NONSOLID = 1,
		SS_MATERIAL_DEFAULT = 2
	};

	struct override_t {
		solidState_t		solidState;
		const idMaterial*	remapMaterial;
		const idMaterial*	material;
		int					surfaceIndex;
	};

						sdDeclModelExport( void );
	virtual				~sdDeclModelExport( void );

	virtual const char*	DefaultDefinition( void ) const;
	virtual bool			Parse( const char* text, const int textLength );
	virtual size_t		Size( void ) const;
	virtual void			FreeData( void );

	exportMode_t			GetExportMode( void ) const { return exportMode; }
	const idList< override_t >& GetOverrides( void ) const { return overrides; }

private:
	bool					ParseCompileMode( idLexer& src );
	bool					ParseSurfaceSetting( idLexer& src, override_t& surfaceOverride );

	idList< override_t >	overrides;
	exportMode_t			exportMode;
};

#endif /* !__DECLMODELEXPORT_H__ */
