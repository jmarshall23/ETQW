// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "DeclSurfaceType.h"

#pragma hdrstop

const char* sdDeclSurfaceType::DefaultDefinition( void ) const {
	return "{ type _default }";
}

void sdDeclSurfaceType::FreeData( void ) {
	type.Clear();
	properties.Clear();
}

bool sdDeclSurfaceType::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );

	type = GetName();
	properties.Clear();

	src.SkipUntilString( "{", &token );
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		if ( token.Icmp( "type" ) == 0 ) {
			if ( !src.ReadToken( &token ) ) {
				src.Warning( "sdDeclSurfaceType::Parse: missing type name" );
				return false;
			}
			type = token;
			continue;
		}

		if ( token.Icmp( "properties" ) == 0 ) {
			if ( !properties.Parse( src ) ) {
				src.Warning( "sdDeclSurfaceType::Parse: invalid properties block" );
				return false;
			}
			continue;
		}

		src.Warning( "sdDeclSurfaceType::Parse : Unknown token %s", token.c_str() );
		MakeDefault();
		return false;
	}

	src.Warning( "sdDeclSurfaceType::Parse: unexpected end of declaration" );
	return false;
}
