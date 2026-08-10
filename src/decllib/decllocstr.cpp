// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "declLocStr.h"

#pragma hdrstop

size_t sdDeclLocStr::Size( void ) const {
	return sizeof( sdDeclLocStr ) + locText.Allocated();
}

const char* sdDeclLocStr::DefaultDefinition( void ) const {
	return "{\n}";
}

void sdDeclLocStr::FreeData( void ) {
	locText.Clear();
	numArgs = 0;
}

void sdDeclLocStr::Print( void ) const {
	common->Printf( "%ls with %i arguments", locText.c_str(), numArgs );
}

bool sdDeclLocStr::Parse( const char* text, const int textLength ) {
	idLexer src;
	idToken token;
	bool foundText = false;

	src.SetFlags( DECL_LEXER_FLAGS );
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SkipUntilString( "{", &token );

	locText.Clear();
	numArgs = 0;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "text" ) ) {
			if ( !src.ReadToken( &token ) ) {
				MakeDefault();
				src.Warning( "sdDeclLocStr::Parse: Unexpected end of file while parsing 'text' attribute" );
				return false;
			}
			locText = common->GetLanguageDict()->GetString( token.c_str() );
			foundText = true;
		} else if ( !token.Icmp( "arguments" ) ) {
			if ( !src.ReadToken( &token ) ) {
				MakeDefault();
				src.Warning( "sdDeclLocStr::Parse: Unexpected end of file while parsing 'arguments' attribute" );
				return false;
			}
			if ( token.type != TT_NUMBER ) {
				MakeDefault();
				src.Warning( "sdDeclLocStr::Parse: Expected number when parsing 'arguments' attribute" );
				return false;
			}
			numArgs = token.GetIntValue();
		} else {
			MakeDefault();
			src.Warning( "sdDeclLocStr::Parse: Unexpected token '%s'", token.c_str() );
			return false;
		}
	}

	if ( token != "}" ) {
		MakeDefault();
		src.Warning( "sdDeclLocStr::Parse: Unexpected end of file" );
		return false;
	}

	int openBracket = locText.Find( L'[' );
	int closeBracket = locText.Find( L']' );
	while ( openBracket != -1 && closeBracket != -1 ) {
		locText.EraseRange( openBracket, closeBracket - openBracket + 1 );
		openBracket = locText.Find( L'[' );
		closeBracket = locText.Find( L']' );
	}
	locText.Replace( L"&lbr", L"[" );
	locText.Replace( L"&rbr", L"]" );

	int foundArgs = 0;
	if ( locText.Find( L"%1" ) != -1 ) {
		do {
			foundArgs++;
		} while ( locText.Find( va( L"%%%i", foundArgs + 1 ) ) != -1 );
	}
	if ( foundArgs != numArgs ) {
		src.Warning( "sdDeclLocStr::Parse: Argument mismatch (expected %i but found %i)", numArgs, foundArgs );
		MakeDefault();
		return false;
	}

	if ( !foundText ) {
		locText = va( L"###%hs###", GetName() );
	}
	return true;
}

bool sdDeclLocStr::Format( idWStr& result, const idWStrList& inputs ) const {
	if ( GetState() == DS_DEFAULTED ) {
		result = va( L"###%hs###", GetName() );
		return false;
	}
	if ( inputs.Num() != numArgs ) {
		result = va( L"###%hs###", GetName() );
		common->Warning(
			"sdDeclLocStr::Format: '%s': invalid number of format strings (expected %i but found %i)",
			GetName(), numArgs, inputs.Num() );
		return false;
	}

	result = locText;
	for ( int i = 0; i < inputs.Num(); i++ ) {
		result.Replace( va( L"%%%i", i + 1 ), inputs[ i ].c_str() );
	}
	return true;
}
