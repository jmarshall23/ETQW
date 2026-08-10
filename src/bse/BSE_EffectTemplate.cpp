/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "BSE.h"

rvDeclEffect::rvDeclEffect() {
	size = 8.0f;
	cutOffDistance = 0.0f;
	minDuration = 0.0f;
	maxDuration = 0.0f;
	bounds.Zero();
	bounds.ExpandSelf( size );
}

rvDeclEffect::~rvDeclEffect() {
	FreeData();
}

size_t rvDeclEffect::Size() const {
	int allocated = segments.Allocated();
	for ( int i = 0; i < segments.Num(); i++ ) {
		allocated += segments[i].Allocated();
	}
	return sizeof( *this ) + allocated;
}

const char *rvDeclEffect::DefaultDefinition() const {
	return "{ size 8 }";
}

void rvDeclEffect::FreeData() {
	segments.Clear();
	size = 8.0f;
	cutOffDistance = 0.0f;
	minDuration = 0.0f;
	maxDuration = 0.0f;
	bounds.Zero();
	bounds.ExpandSelf( size );
}

bool rvDeclEffect::Parse( const char *text, const int textLength ) {
	FreeData();
	idLexer src;
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	if ( !src.SkipUntilString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			Finish();
			return !src.HadError();
		}
		if ( !token.Icmp( "size" ) ) {
			size = Max( 0.0f, src.ParseFloat() );
			continue;
		}
		if ( !token.Icmp( "cutOffDistance" ) ) {
			cutOffDistance = Max( 0.0f, src.ParseFloat() );
			continue;
		}

		const int segmentType = BSE_SegmentTypeForToken( token );
		if ( segmentType != SEG_NONE ) {
			if ( !ParseSegment( src, segmentType ) ) {
				return false;
			}
		} else {
			BSE_SkipUnknown( src, token );
		}
	}
	return false;
}

void rvDeclEffect::Finish() {
	minDuration = 0.0f;
	maxDuration = 0.0f;
	for ( int i = 0; i < segments.Num(); i++ ) {
		const rvSegmentTemplate &segment = segments[i];
		float segmentMin = segment.startTime.x + segment.duration.x;
		float segmentMax = segment.startTime.y + segment.duration.y;
		if ( segment.HasVisualParticle() ) {
			segmentMin += segment.particle.duration.x;
			segmentMax += segment.particle.duration.y;
		}
		if ( segment.constant || segment.looping ) {
			segmentMax = 300.0f;
		}
		minDuration = Max( minDuration, segmentMin );
		maxDuration = Max( maxDuration, segmentMax );
	}
	CalculateBounds();
}
