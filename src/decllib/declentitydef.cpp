// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "declEntityDef.h"
#include "declTypeHolder.h"

#pragma hdrstop

size_t idDeclEntityDef::Size( void ) const {
	return sizeof( idDeclEntityDef ) + dict.Allocated();
}

void idDeclEntityDef::FreeData( void ) {
	dict.Clear();
}

const char* idDeclEntityDef::DefaultDefinition( void ) const {
	return "{\n\t\"DEFAULTED\"\t\"1\"\n}";
}

void idDeclEntityDef::Print( void ) const {
	dict.Print();
}

void idDeclEntityDef::CacheFromDict( const idDict& source ) {
	const idKeyValue* keyValue = NULL;
	while ( ( keyValue = source.MatchPrefix( "def", keyValue ) ) != NULL ) {
		if ( keyValue->GetValue().Length() != 0 ) {
			declHolder.FindEntityDef( keyValue->GetValue(), false );
		}
	}
}

bool idDeclEntityDef::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken key;
	idToken value;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &key );
	dict.Clear();

	while ( src.ReadToken( &key ) ) {
		if ( key == "}" ) {
			break;
		}
		if ( key.type != TT_STRING ) {
			src.Warning( "Expected quoted string, but found '%s'", key.c_str() );
			MakeDefault();
			return false;
		}
		if ( !src.ReadToken( &value ) ) {
			src.Warning( "Unexpected end of file" );
			MakeDefault();
			return false;
		}
		if ( dict.FindKey( key ) != NULL ) {
			src.Warning( "'%s' already defined", key.c_str() );
		}
		dict.Set( key, value );
	}

	dict.Set( "classname", GetName() );

	idList< const idDeclEntityDef* > inherited;
	for ( ;; ) {
		const idKeyValue* keyValue = dict.MatchPrefix( "inherit", NULL );
		if ( keyValue == NULL ) {
			break;
		}
		const idDeclEntityDef* parent = declHolder.FindEntityDef( keyValue->GetValue(), false );
		if ( parent == NULL ) {
			src.Warning( "Unknown entityDef '%s' inherited by '%s'",
				keyValue->GetValue().c_str(), GetName() );
		} else {
			inherited.Append( parent );
		}
		dict.Delete( keyValue->GetKey() );
	}

	for ( int i = 0; i < inherited.Num(); i++ ) {
		dict.SetDefaults( &inherited[ i ]->dict );
	}
	declManager->CacheFromDict( dict );
	return true;
}
