// Copyright (C) 2007 Id Software, Inc.
//


#include "precompiled.h"
#pragma hdrstop

#include "declmodelexport.h"
#include "declTypeHolder.h"

sdDeclModelExport::sdDeclModelExport( void ) {
}

sdDeclModelExport::~sdDeclModelExport( void ) {
	FreeData();
}

const char* sdDeclModelExport::DefaultDefinition( void ) const {
	return "{\n}\n";
}

void sdDeclModelExport::FreeData( void ) {
	exportMode = EM_DOOM;
	overrides.Clear();
}

size_t sdDeclModelExport::Size( void ) const {
	return sizeof( sdDeclModelExport );
}

bool sdDeclModelExport::ParseCompileMode( idLexer& src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return false;
	}

	if ( token.Icmp( "doom" ) == 0 ) {
		exportMode = EM_DOOM;
		return true;
	}
	if ( token.Icmp( "etqw" ) == 0 ) {
		exportMode = EM_ETQW;
		return true;
	}

	return false;
}

bool sdDeclModelExport::ParseSurfaceSetting( idLexer& src, override_t& surfaceOverride ) {
	surfaceOverride.solidState = SS_MATERIAL_DEFAULT;
	surfaceOverride.remapMaterial = NULL;

	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token.Icmp( "solid" ) == 0 ) {
			surfaceOverride.solidState = SS_SOLID;
			continue;
		}
		if ( token.Icmp( "nonsolid" ) == 0 ) {
			surfaceOverride.solidState = SS_NONSOLID;
			continue;
		}
		if ( token.Icmp( "remap" ) == 0 ) {
			if ( !src.ReadToken( &token ) ) {
				return false;
			}
			surfaceOverride.remapMaterial = declHolder.FindMaterial( token.c_str(), false );
			continue;
		}
		if ( token == "}" ) {
			return true;
		}
	}

	return false;
}

bool sdDeclModelExport::Parse( const char* text, const int textLength ) {
	idLexer src;
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SkipUntilString( "{" );

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token.Icmp( "mode" ) == 0 && !ParseCompileMode( src ) ) {
			return false;
		}

		const idMaterial* material = declHolder.FindMaterial( token.c_str(), false );
		if ( material != NULL ) {
			override_t& surfaceOverride = overrides.Alloc();
			surfaceOverride.surfaceIndex = -1;
			surfaceOverride.material = material;
			ParseSurfaceSetting( src, surfaceOverride );
		}

		if ( token.IsNumeric() ) {
			override_t& surfaceOverride = overrides.Alloc();
			surfaceOverride.surfaceIndex = token.GetIntValue();
			surfaceOverride.material = NULL;
			ParseSurfaceSetting( src, surfaceOverride );
		}

		if ( token == "}" ) {
			return true;
		}
	}

	return true;
}
