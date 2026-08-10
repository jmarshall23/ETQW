// Copyright (C) 2007 Id Software, Inc.
//


#ifndef __DECLSURFACETYPEMAP_H__
#define __DECLSURFACETYPEMAP_H__

#include "../framework/declManager.h"
#include "../idlib/bv/Bounds2D.h"
#include "../idlib/geometry/Winding2D.h"

class sdDeclSurfaceType;
class idLexer;

/*
===============================================================================

	sdDeclSurfaceTypeMap

	Maps normalized material coordinates to surface type declarations.  Areas
	are tested in reverse declaration order, allowing later regions to overlay
	earlier ones.

===============================================================================
*/

class sdDeclSurfaceTypeMap : public idDecl {
public:
	virtual					~sdDeclSurfaceTypeMap( void );

	virtual const char*		DefaultDefinition( void ) const;
	virtual bool			Parse( const char* text, const int textLength );
	virtual size_t			Size( void ) const { return sizeof( sdDeclSurfaceTypeMap ); }
	virtual void			FreeData( void );

	const sdDeclSurfaceType*	GetSurfaceType( const idVec2& tc, idVec3* color = NULL ) const;

private:
	class sdSurfaceTypeArea {
	public:
							sdSurfaceTypeArea( void );
		virtual				~sdSurfaceTypeArea( void ) {}

		virtual bool			Parse( idLexer& src ) = 0;
		virtual bool			ContainsPoint( const idVec2& point ) const;
		virtual void			AdjustCoordsForSize( const int width, const int height );

	protected:
		bool					ParseKey( const idToken& key, idLexer& src );

		sdBounds2D				bounds;
		const sdDeclSurfaceType*	surfaceTypeDecl;
		idVec3					surfaceColor;

		friend class sdDeclSurfaceTypeMap;
	};

	class sdSurfaceTypeAreaRect : public sdSurfaceTypeArea {
	public:
		virtual bool			Parse( idLexer& src );
	};

	class sdSurfaceTypeAreaWinding : public sdSurfaceTypeArea {
	public:
		virtual bool			Parse( idLexer& src );
		virtual bool			ContainsPoint( const idVec2& point ) const;
		virtual void			AdjustCoordsForSize( const int width, const int height );

	private:
		idWinding2D				winding;
	};

	idList< sdSurfaceTypeArea* >	surfaceTypeAreas;
};

#endif /* !__DECLSURFACETYPEMAP_H__ */
