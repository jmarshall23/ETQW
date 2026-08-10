// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "declStuffType.h"
#include "declTypeHolder.h"

#pragma hdrstop

sdDeclStuffType::sdDeclStuffType( void ) :
	randomizeAngles( false ),
	lodType( NULL ) {
}

const char* sdDeclStuffType::DefaultDefinition( void ) const {
	return "{model _default}";
}

void sdDeclStuffType::FreeData( void ) {
	models.Clear();
	randomizeAngles = false;
	lodType = NULL;
}

bool sdDeclStuffType::Parse( const char* text, const int textLength ) {
	idLexer src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SkipUntilString( "{", &token );

	FreeData();
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( lodType != NULL && lodType->GetNumModels() != models.Num() ) {
				common->Warning( "Lod stuff declaration hasn't got the same number of models specified, ignored" );
				lodType = NULL;
			}
			return true;
		}

		if ( token.Icmp( "model" ) == 0 ) {
			if ( !src.ReadToken( &token ) ) {
				return false;
			}
			models.Append( token );
		} else if ( token.Icmp( "randomizeAngles" ) == 0 ) {
			randomizeAngles = true;
		} else if ( token.Icmp( "lod" ) == 0 ) {
			if ( !src.ReadToken( &token ) ) {
				return false;
			}
			lodType = declHolder.FindStuffType( token, true );
		} else {
			src.Warning( "Stuff Type: bad token %s", token.c_str() );
			MakeDefault();
			return false;
		}
	}
	return false;
}

bool sdDeclStuffType::RebuildTextSource( void ) {
	idFile_Memory file( va( "stuffType %s", GetName() ) );
	file.WriteFloatString( "stuffType %s {\n", GetName() );
	for ( int i = 0; i < models.Num(); i++ ) {
		file.WriteFloatString( "\tmodel \"%s\"\n", models[ i ].c_str() );
	}
	if ( randomizeAngles ) {
		file.WriteFloatString( "\trandomizeAngles\n" );
	}
	if ( lodType != NULL ) {
		file.WriteFloatString( "\tlod \"%s\"\n", lodType->GetName() );
	}
	file.WriteFloatString( "}\n" );
	SetText( file.GetDataPtr() );
	return true;
}
