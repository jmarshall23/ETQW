// Copyright (C) 2007 Id Software, Inc.
//
// ETQW effect declaration boundary.  The runtime BSE implementation is kept
// separate from the decl parser so effects can be registered and scanned
// before the renderer creates effect instances.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "BSEInterface.h"
#include "BSE_Envelope.h"
#include "BSE_SpawnDomains.h"
#include "BSE_Particle.h"
#include "BSE.h"

const char* rvDeclEffect::DefaultDefinition() const {
	return "{\n}\n";
}

bool rvDeclEffect::SetDefaultText() {
	SetText( va( "effect %s // IMPLICITLY GENERATED\n%s", GetName(), DefaultDefinition() ) );
	return true;
}

void rvDeclEffect::Init() {
	mFlags = 0;
	mMinDuration = 0.0f;
	mMaxDuration = 0.0f;
	mCutOffDistance = 0.0f;
	mSize = 0.0f;
	mSegmentTemplates.Clear();
	mPlayCount = 0;
	mLoopCount = 0;
}

void rvDeclEffect::FreeData() {
	mSegmentTemplates.Clear();
	mFlags = 0;
	mMinDuration = 0.0f;
	mMaxDuration = 0.0f;
	mCutOffDistance = 0.0f;
	mSize = 0.0f;
	mPlayCount = 0;
	mLoopCount = 0;
}

size_t rvDeclEffect::Size() const {
	return sizeof( *this ) + mSegmentTemplates.Allocated();
}

bool rvDeclEffect::Parse( const char* text, const int textLength ) {
	idLexer src;
	idToken token;

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	if ( !src.SkipUntilString( "{" ) ) {
		return false;
	}

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return !src.HadError();
		}
		if ( !token.Icmp( "size" ) ) {
			mSize = src.ParseFloat();
			continue;
		}
		if ( !token.Icmp( "cutOffDistance" ) ) {
			mCutOffDistance = src.ParseFloat();
			continue;
		}

		// Segment bodies are consumed here even while the renderer-neutral BSE
		// conversion is being joined to the retail SDK layout.  This preserves
		// declaration boundaries and lets every .effect file be indexed safely.
		if ( src.CheckTokenString( "{" ) ) {
			if ( !src.SkipBracedSection( false ) ) {
				return false;
			}
		}
	}

	return false;
}

void rvDeclEffect::CacheFromDict( const idDict& dict ) {
	const qhandle_t effectType = declManager->GetDeclTypeHandle( declEffectsIdentifier );
	const idKeyValue* kv = NULL;
	while ( ( kv = dict.MatchPrefix( "fx", kv ) ) != NULL ) {
		const char* effectName = kv->GetValue().c_str();
		if ( effectName[ 0 ] == '\0' ) {
			common->Warning( "rvDeclEffect::CacheFromDict: '%s' has an empty value", kv->GetKey().c_str() );
			continue;
		}
		declManager->FindType( effectType, effectName, true );
	}
}
