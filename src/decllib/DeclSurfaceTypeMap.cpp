// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#pragma hdrstop

#include "DeclSurfaceTypeMap.h"
#include "DeclSurfaceType.h"
#include "declTypeHolder.h"

const char* sdDeclSurfaceTypeMap::DefaultDefinition( void ) const {
	return "\t\t   {\t\t   }";
}

sdDeclSurfaceTypeMap::~sdDeclSurfaceTypeMap( void ) {
	FreeData();
}

sdDeclSurfaceTypeMap::sdSurfaceTypeArea::sdSurfaceTypeArea( void ) :
	surfaceTypeDecl( NULL ),
	surfaceColor( colorWhite.ToVec3() ) {
}

bool sdDeclSurfaceTypeMap::sdSurfaceTypeArea::ContainsPoint( const idVec2& point ) const {
	return bounds.ContainsPoint( point );
}

void sdDeclSurfaceTypeMap::sdSurfaceTypeArea::AdjustCoordsForSize( const int width, const int height ) {
	const float inverseWidth = 1.0f / static_cast< float >( width );
	const float inverseHeight = 1.0f / static_cast< float >( height );
	bounds[ 0 ].x *= inverseWidth;
	bounds[ 0 ].y *= inverseHeight;
	bounds[ 1 ].x *= inverseWidth;
	bounds[ 1 ].y *= inverseHeight;
}

bool sdDeclSurfaceTypeMap::sdSurfaceTypeArea::ParseKey( const idToken& key, idLexer& src ) {
	if ( key.Icmp( "surfaceType" ) == 0 ) {
		idToken token;
		if ( !src.ReadTokenOnLine( &token ) ) {
			src.Warning( "missing surface type name" );
			return false;
		}
		surfaceTypeDecl = declHolder.FindSurfaceType( token.c_str(), false );
		return true;
	}

	if ( key.Icmp( "surfaceColor" ) == 0 ) {
		return src.Parse1DMatrix( 3, surfaceColor.ToFloatPtr() ) != 0;
	}

	return false;
}

bool sdDeclSurfaceTypeMap::sdSurfaceTypeAreaRect::Parse( idLexer& src ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		if ( token.Icmp( "coords" ) == 0 ) {
			if ( !src.Parse1DMatrix( 2, bounds[ 0 ].ToFloatPtr() ) ||
				 !src.Parse1DMatrix( 2, bounds[ 1 ].ToFloatPtr() ) ) {
				return false;
			}
			continue;
		}

		if ( !ParseKey( token, src ) ) {
			src.Warning( "sdSurfaceTypeAreaRect::Parse : Unknown token %s", token.c_str() );
			return false;
		}
	}

	src.Warning( "Unexpected end of file" );
	return false;
}

bool sdDeclSurfaceTypeMap::sdSurfaceTypeAreaWinding::Parse( idLexer& src ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			winding.GetBounds( bounds );
			return true;
		}

		if ( token.Icmp( "coords" ) == 0 ) {
			const int numPoints = src.ParseInt();
			if ( numPoints < 3 || numPoints >= idWinding2D::MAX_POINTS ) {
				src.Warning( "sdSurfaceTypeAreaWinding::Parse : invalid number of points for winding %d", numPoints );
				return false;
			}
			if ( !src.ExpectTokenString( "{" ) ) {
				return false;
			}
			winding.Clear();
			for ( int i = 0; i < numPoints; ++i ) {
				idVec2 point;
				if ( !src.Parse1DMatrix( 2, point.ToFloatPtr() ) ) {
					return false;
				}
				winding.AddPoint( point );
			}
			if ( !src.ExpectTokenString( "}" ) ) {
				return false;
			}
			continue;
		}

		if ( !ParseKey( token, src ) ) {
			src.Warning( "sdSurfaceTypeAreaWinding::Parse : Unknown token %s", token.c_str() );
			return false;
		}
	}

	src.Warning( "Unexpected end of file" );
	return false;
}

bool sdDeclSurfaceTypeMap::sdSurfaceTypeAreaWinding::ContainsPoint( const idVec2& point ) const {
	return sdSurfaceTypeArea::ContainsPoint( point ) && winding.PointInside( point, 0.0f );
}

void sdDeclSurfaceTypeMap::sdSurfaceTypeAreaWinding::AdjustCoordsForSize( const int width, const int height ) {
	sdSurfaceTypeArea::AdjustCoordsForSize( width, height );
	const idVec2 scale( 1.0f / static_cast< float >( width ), 1.0f / static_cast< float >( height ) );
	winding.Scale( scale );
}

const sdDeclSurfaceType* sdDeclSurfaceTypeMap::GetSurfaceType( const idVec2& tc, idVec3* color ) const {
	for ( int i = surfaceTypeAreas.Num() - 1; i >= 0; --i ) {
		const sdSurfaceTypeArea* area = surfaceTypeAreas[ i ];
		if ( !area->ContainsPoint( tc ) ) {
			continue;
		}
		if ( color != NULL ) {
			*color = area->surfaceColor;
		}
		return area->surfaceTypeDecl;
	}
	return NULL;
}

bool sdDeclSurfaceTypeMap::Parse( const char* text, const int textLength ) {
	FreeData();
	surfaceTypeAreas.SetGranularity( 1 );

	idLexer src;
	src.SetFlags( DECL_LEXER_FLAGS );
	if ( !src.LoadMemory( text, textLength, GetFileName(), GetLineNum() ) ) {
		return false;
	}
	if ( !src.SkipUntilString( "{" ) ) {
		src.Warning( "missing opening brace" );
		return false;
	}

	int width = 1;
	int height = 1;
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			for ( int i = 0; i < surfaceTypeAreas.Num(); ++i ) {
				surfaceTypeAreas[ i ]->AdjustCoordsForSize( width, height );
			}
			return true;
		}

		if ( token.Icmp( "width" ) == 0 ) {
			width = src.ParseInt();
			if ( width <= 0 ) {
				src.Warning( "invalid surface type map width %d", width );
				return false;
			}
			continue;
		}
		if ( token.Icmp( "height" ) == 0 ) {
			height = src.ParseInt();
			if ( height <= 0 ) {
				src.Warning( "invalid surface type map height %d", height );
				return false;
			}
			continue;
		}

		sdSurfaceTypeArea* area = NULL;
		if ( token.Icmp( "rect" ) == 0 ) {
			area = new sdSurfaceTypeAreaRect;
		} else if ( token.Icmp( "winding" ) == 0 ) {
			area = new sdSurfaceTypeAreaWinding;
		} else {
			src.Warning( "sdDeclSurfaceTypeMap::Parse : Unknown token %s", token.c_str() );
			return false;
		}

		if ( !area->Parse( src ) ) {
			delete area;
			return false;
		}
		surfaceTypeAreas.Append( area );
	}

	src.Warning( "Unexpected end of file" );
	return false;
}

void sdDeclSurfaceTypeMap::FreeData( void ) {
	surfaceTypeAreas.DeleteContents( true );
}
