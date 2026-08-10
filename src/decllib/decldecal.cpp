// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "declDecal.h"
#include "declTypeHolder.h"

#pragma hdrstop

sdDeclDecal::sdDeclDecal( void ) :
	startColor( 1.0f, 1.0f, 1.0f, 1.0f ),
	endColor( 0.0f, 0.0f, 0.0f, 0.0f ),
	lifeTime( 10.0f ),
	minSize( 2.0f ),
	sizeDiff( 1.0f ),
	material( NULL ) {
}

const char* sdDeclDecal::DefaultDefinition( void ) const {
	return "{\tmaterial _default\tgridSize\t1,1\timage\t\t0,0,1,1}";
}

void sdDeclDecal::FreeData( void ) {
	startColor.Set( 1.0f, 1.0f, 1.0f, 1.0f );
	endColor.Zero();
	lifeTime = 10.0f;
	minSize = 2.0f;
	sizeDiff = 1.0f;
	material = NULL;
	images.Clear();
}

void sdDeclDecal::CacheFromDict( const idDict& dict ) {
	const idKeyValue* keyValue = NULL;
	while ( ( keyValue = dict.MatchPrefix( "decal", keyValue ) ) != NULL ) {
		if ( keyValue->GetValue().Length() != 0 ) {
			declHolder.declDecalType.LocalFind( keyValue->GetValue(), false );
		}
	}
}

bool sdDeclDecal::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;
	int gridWidth = 0;
	int gridHeight = 0;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );
	FreeData();

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "material" ) == 0 ) {
			if ( !src.ReadToken( &token ) ) {
				return false;
			}
			material = declHolder.FindMaterial( token, true );
		} else if ( token.Icmp( "lifeTime" ) == 0 ) {
			lifeTime = src.ParseFloat();
		} else if ( token.Icmp( "startColor" ) == 0 ) {
			if ( !src.Parse1DMatrix( 3, startColor.ToFloatPtr() ) ) {
				return false;
			}
		} else if ( token.Icmp( "endColor" ) == 0 ) {
			if ( !src.Parse1DMatrix( 3, endColor.ToFloatPtr() ) ) {
				return false;
			}
		} else if ( token.Icmp( "size" ) == 0 ) {
			minSize = src.ParseFloat();
			src.ExpectTokenString( "," );
			sizeDiff = src.ParseFloat() - minSize;
		} else if ( token.Icmp( "gridSize" ) == 0 ) {
			gridWidth = src.ParseInt();
			src.ExpectTokenString( "," );
			gridHeight = src.ParseInt();
		} else if ( token.Icmp( "image" ) == 0 ) {
			const int left = src.ParseInt();
			src.ExpectTokenString( "," );
			const int top = src.ParseInt();
			src.ExpectTokenString( "," );
			const int width = src.ParseInt();
			src.ExpectTokenString( "," );
			const int height = src.ParseInt();
			if ( gridWidth <= 0 || gridHeight <= 0 ) {
				src.Warning( "Decal Type: Found image before gridsize was specified" );
				MakeDefault();
				return false;
			}
			const idVec2 mins(
				static_cast< float >( left ) / gridWidth,
				static_cast< float >( top ) / gridHeight );
			const idVec2 maxs(
				static_cast< float >( left + width ) / gridWidth,
				static_cast< float >( top + height ) / gridHeight );
			images.Append( sdBounds2D( mins, maxs ) );
		} else {
			src.Warning( "Decal Type: bad token %s", token.c_str() );
			MakeDefault();
			return false;
		}
	}
	return false;
}
