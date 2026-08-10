// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../renderer/Image.h"
#include "declRenderBinding.h"

#pragma hdrstop

const char* sdDeclRenderBinding::DefaultDefinition( void ) const {
	return "{\n\tvector { 0 }\n}\n";
}

void sdDeclRenderBinding::FreeData( void ) {
	infrequent = -1;
}

void sdDeclRenderBinding::List( void ) const {
	common->Printf( "%s ", GetName() );
	switch ( type ) {
		case BT_VECTOR:
			common->Printf(
				"VECTOR cur: %f %f %f %f def: %f %f %f %f\n",
				data.vector[ 0 ], data.vector[ 1 ], data.vector[ 2 ], data.vector[ 3 ],
				defaults.vector[ 0 ], defaults.vector[ 1 ], defaults.vector[ 2 ], defaults.vector[ 3 ] );
			break;
		case BT_TEXTURE:
			common->Printf( "TEXTURE cur:'%p' def:'%p'\n", data.texture.image, defaults.texture.image );
			break;
		case BT_ATTRIB:
			common->Printf( "ATTRIB cur:%d def:%d\n", data.attrib, defaults.attrib );
			break;
		default:
			common->Printf( "UNKNOWN\n" );
			break;
	}
}

bool sdDeclRenderBinding::ParseVector( idParser& src ) {
	idToken token;
	if ( !src.ReadTokenOnLine( &token ) || token != "{" ) {
		return false;
	}

	int count = 0;
	while ( src.ReadTokenOnLine( &token ) ) {
		if ( token == "}" ) {
			break;
		}
		if ( count == 4 ) {
			src.Warning( "sdDeclRenderBinding::ParseVector : vector with more than 4 values" );
			return false;
		}

		bool negative = false;
		if ( token == "-" ) {
			negative = true;
			if ( !src.ReadTokenOnLine( &token ) || token.type != TT_NUMBER ) {
				src.Warning( "sdDeclRenderBinding::ParseVector : bad syntax for negative number" );
				return false;
			}
		}
		defaults.vector[ count++ ] = token.GetFloatValue() * ( negative ? -1.0f : 1.0f );
	}

	if ( count == 0 ) {
		src.Warning( "sdDeclRenderBinding::ParseVector : vector with 0 values" );
		return false;
	}
	if ( count == 1 ) {
		defaults.vector[ 1 ] = defaults.vector[ 0 ];
		defaults.vector[ 2 ] = defaults.vector[ 0 ];
		defaults.vector[ 3 ] = defaults.vector[ 0 ];
	} else if ( count == 2 ) {
		defaults.vector[ 2 ] = defaults.vector[ 0 ];
		defaults.vector[ 3 ] = defaults.vector[ 1 ];
	} else if ( count == 3 ) {
		defaults.vector[ 3 ] = defaults.vector[ 1 ];
	}
	return true;
}

bool sdDeclRenderBinding::ParseTexture( idParser& src ) {
	idToken token;
	if ( !src.ExpectTokenString( "{" ) || !src.ReadToken( &token ) ) {
		return false;
	}

	defaults.texture.defaultDepth = TD_DEFAULT;
	defaults.texture.defaultCubeMap = CF_2D;
	if ( token.Icmp( "diffuse" ) == 0 ) {
		defaults.texture.defaultDepth = TD_DIFFUSE;
	} else if ( token.Icmp( "specular" ) == 0 ) {
		defaults.texture.defaultDepth = TD_SPECULAR;
	} else if ( token.Icmp( "local" ) == 0 ) {
		defaults.texture.defaultDepth = TD_BUMP;
	} else if ( token.Icmp( "cubeMap" ) == 0 ) {
		defaults.texture.defaultCubeMap = CF_NATIVE;
	} else if ( token.Icmp( "cameraCubeMap" ) == 0 ) {
		defaults.texture.defaultCubeMap = CF_CAMERA;
	} else if ( token.Icmp( "halfSphereMap" ) == 0 ) {
		defaults.texture.defaultCubeMap = CF_HALFSPHERE;
	} else {
		src.UnreadToken( token );
	}

	imageParams_t imageParms;
	imageParms.td = defaults.texture.defaultDepth;
	imageParms.cubeMap = defaults.texture.defaultCubeMap;
	defaults.texture.image = idImageManager::ParseImage( src, imageParms );
	return src.ExpectTokenString( "}" );
}

bool sdDeclRenderBinding::ParseAttrib( idParser& src ) {
	idToken token;
	if ( !src.ReadTokenOnLine( &token ) || token.type != TT_NUMBER ) {
		src.Warning( "sdDeclRenderBinding::ParseAttrib : bad syntax for attribute index" );
		return false;
	}
	defaults.attrib = token.GetIntValue();
	return true;
}

bool sdDeclRenderBinding::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SkipUntilString( "{", &token );

	if ( !src.ExpectAnyToken( &token ) ) {
		return false;
	}
	if ( token.Icmp( "infrequent" ) == 0 ) {
		infrequent = 0;
		if ( !src.ExpectAnyToken( &token ) ) {
			return false;
		}
	}

	bool parsed = false;
	if ( token.Icmp( "vector" ) == 0 ) {
		type = BT_VECTOR;
		parsed = ParseVector( src );
	} else if ( token.Icmp( "texture" ) == 0 ) {
		type = BT_TEXTURE;
		parsed = ParseTexture( src );
	} else if ( token.Icmp( "attrib" ) == 0 ) {
		type = BT_ATTRIB;
		parsed = ParseAttrib( src );
	} else {
		src.Warning( "sdDeclRenderBinding::Parse : Unknown render binding type: %s", token.c_str() );
		return false;
	}

	if ( !parsed ) {
		return false;
	}
	memcpy( &data, &defaults, sizeof( data ) );
	return src.ExpectTokenString( "}" );
}

void sdDeclRenderBinding::Set( idCinematic* cinematic ) const {
	data.texture.image = reinterpret_cast< idImage* >( cinematic );
}
