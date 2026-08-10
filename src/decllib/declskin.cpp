// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "../renderer/Material.h"
#include "declSkin.h"
#include "declTypeHolder.h"

#pragma hdrstop

size_t idDeclSkin::Size( void ) const {
	return sizeof( idDeclSkin ) + mappings.Allocated();
}

void idDeclSkin::FreeData( void ) {
	mappings.Clear();
}

const char* idDeclSkin::DefaultDefinition( void ) const {
	return "{\n\t\"*\"\t\"_default\"\n}";
}

void idDeclSkin::CacheFromDict( const idDict& dict ) {
	const char* skinName = dict.GetString( "skin" );
	if ( skinName[ 0 ] != '\0' ) {
		declHolder.FindSkin( skinName, false );
	}
}

bool idDeclSkin::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken from;
	idToken to;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &from );
	mappings.Clear();

	while ( src.ReadToken( &from ) ) {
		if ( from == "}" ) {
			return true;
		}
		if ( !src.ReadToken( &to ) ) {
			src.Warning( "Unexpected end of skin declaration" );
			return false;
		}
		if ( from.Icmp( "model" ) == 0 ) {
			continue;
		}

		skinMapping_t& mapping = mappings.Alloc();
		mapping.from = from == "*" ? NULL : declHolder.FindMaterial( from, false );
		mapping.to = declHolder.FindMaterial( to, true );
	}
	return false;
}

const idMaterial* idDeclSkin::RemapShaderBySkin( const idMaterial* shader ) const {
	if ( shader == NULL || !shader->IsDrawn() ) {
		return shader;
	}
	for ( int i = 0; i < mappings.Num(); i++ ) {
		if ( mappings[ i ].from == NULL || mappings[ i ].from == shader ) {
			return mappings[ i ].to;
		}
	}
	return shader;
}
