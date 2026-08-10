// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "declLocStr.h"

#pragma hdrstop

size_t sdDeclLocStr::Size( void ) const {
	return sizeof( sdDeclLocStr ) + locText.Allocated();
}

const char* sdDeclLocStr::DefaultDefinition( void ) const {
	return "{ \"###str_00000###\" }";
}

void sdDeclLocStr::FreeData( void ) {
	locText.Clear();
	numArgs = 0;
}

void sdDeclLocStr::Print( void ) const {
	common->Printf( "%ls\n", locText.c_str() );
}

bool sdDeclLocStr::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS | LEXFL_NOFATALERRORS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );

	locText.Clear();
	numArgs = 0;
	if ( !src.ReadToken( &token ) || token == "}" ) {
		return false;
	}

	wchar_t* wideText = new wchar_t[ token.Length() + 1 ];
	mbstowcs( wideText, token.c_str(), token.Length() + 1 );
	wideText[ token.Length() ] = L'\0';
	locText = wideText;
	delete[] wideText;
	for ( int i = 0; i < locText.Length(); i++ ) {
		if ( locText[ i ] != L'%' ) {
			continue;
		}
		if ( i + 1 < locText.Length() && locText[ i + 1 ] == L'%' ) {
			i++;
			continue;
		}
		numArgs++;
	}

	if ( !src.ReadToken( &token ) || token != "}" ) {
		src.Warning( "sdDeclLocStr::Parse: expected closing brace" );
		return false;
	}
	return true;
}

bool sdDeclLocStr::Format( idWStr& result, const idWStrList& inputs ) const {
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
