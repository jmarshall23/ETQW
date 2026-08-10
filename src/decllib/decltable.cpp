// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "declTable.h"

#pragma hdrstop

const char* idDeclTable::DefaultDefinition( void ) const {
	return "{ { 0 } }";
}

float idDeclTable::TableLookup( float index ) const {
	int domain = values.Num() - 1;
	if ( domain <= 1 ) {
		return 1.0f;
	}

	if ( isLinear ) {
		return idMath::ClampFloat( 0.0f, 1.0f, index );
	}

	if ( discontinuous ) {
		domain--;
	}

	int iIndex;
	float fraction;
	if ( clamp ) {
		index *= domain - 1;
		if ( index >= domain - 1 ) {
			return values[ domain - 1 ];
		}
		if ( index <= 0.0f ) {
			return values[ 0 ];
		}
		iIndex = idMath::Ftoi( index );
		fraction = index - iIndex;
	} else {
		index *= domain;
		if ( index < 0.0f ) {
			index += domain * idMath::Ceil( -index / domain );
		}
		const int integralIndex = idMath::FtoiFast( idMath::Floor( index ) );
		fraction = index - integralIndex;
		iIndex = integralIndex % domain;
	}

	if ( snap ) {
		return values[ iIndex ];
	}
	return values[ iIndex ] * ( 1.0f - fraction ) + values[ iIndex + 1 ] * fraction;
}

size_t idDeclTable::Size( void ) const {
	return sizeof( idDeclTable ) + values.Allocated();
}

void idDeclTable::FreeData( void ) {
	snap = false;
	clamp = false;
	discontinuous = false;
	isLinear = false;
	minValue = 0.0f;
	maxValue = 0.0f;
	values.Clear();
}

bool idDeclTable::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );

	snap = false;
	clamp = false;
	discontinuous = false;
	isLinear = false;
	minValue = idMath::INFINITY;
	maxValue = -idMath::INFINITY;
	values.Clear();

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}
		if ( token.Icmp( "snap" ) == 0 ) {
			snap = true;
			continue;
		}
		if ( token.Icmp( "clamp" ) == 0 ) {
			clamp = true;
			continue;
		}
		if ( token.Icmp( "discontinuous" ) == 0 ) {
			discontinuous = true;
			continue;
		}
		if ( token != "{" ) {
			src.Warning( "unknown token '%s'", token.c_str() );
			MakeDefault();
			return false;
		}

		for ( ;; ) {
			bool error = false;
			const float value = src.ParseFloat( &error );
			if ( error ) {
				MakeDefault();
				return false;
			}
			values.Append( value );
			minValue = Min( minValue, value );
			maxValue = Max( maxValue, value );

			if ( !src.ReadToken( &token ) ) {
				MakeDefault();
				return false;
			}
			if ( token == "}" ) {
				break;
			}
			if ( token != "," ) {
				src.Warning( "expected comma or brace" );
				MakeDefault();
				return false;
			}
		}
	}

	if ( values.Num() == 0 ) {
		MakeDefault();
		return false;
	}

	isLinear = !discontinuous && clamp && !snap && values.Num() == 2 &&
		idMath::Fabs( values[ 0 ] ) < 0.01f &&
		idMath::Fabs( values[ 1 ] - 1.0f ) < 0.01f;

	const float wrapValue = discontinuous ? values[ values.Num() - 1 ] : values[ 0 ];
	values.Append( wrapValue );
	return true;
}
