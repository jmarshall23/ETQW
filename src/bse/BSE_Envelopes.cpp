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

rvBSEEnvelope::rvBSEEnvelope() {
	Clear();
}

void rvBSEEnvelope::Clear() {
	name.Clear();
	table = NULL;
	count.Set( 1.0f, 1.0f, 1.0f, 1.0f );
	offset.Zero();
	rate.Zero();
	hasCount = false;
	hasOffset = false;
	hasRate = false;
}

float BSE_EvaluateNamedEnvelope( const char *name, const idDeclTable *table, float fraction ) {
	fraction = idMath::ClampFloat( 0.0f, 1.0f, fraction );
	if ( table != NULL ) {
		return table->TableLookup( fraction );
	}
	if ( name == NULL || !name[0] || !idStr::Icmp( name, "linear" ) ) {
		return fraction;
	}

	idStr curveName = name;
	curveName.ToLower();
	if ( curveName.Find( "inverse" ) >= 0 || curveName.Find( "1minusx" ) >= 0 ) {
		fraction = 1.0f - fraction;
	}
	if ( curveName.Find( "sin" ) >= 0 ) {
		return idMath::Sin( fraction * idMath::HALF_PI );
	}
	if ( curveName.Find( "fastin" ) >= 0 || curveName.Find( "convex" ) >= 0 ) {
		return 1.0f - ( 1.0f - fraction ) * ( 1.0f - fraction );
	}
	if ( curveName.Find( "slowin" ) >= 0 || curveName.Find( "concave" ) >= 0 || curveName.Find( "x2" ) >= 0 ) {
		return fraction * fraction;
	}
	if ( curveName.Find( "smooth" ) >= 0 ) {
		return fraction * fraction * ( 3.0f - 2.0f * fraction );
	}
	return fraction;
}

float rvBSEEnvelope::Evaluate( float fraction, int component ) const {
	component = idMath::ClampInt( 0, 3, component );
	float phase = fraction;
	if ( hasRate ) {
		phase *= Max( 0.0f, rate[component] );
	} else if ( hasCount ) {
		phase *= Max( 0.0f, count[component] );
	}
	if ( hasOffset ) {
		phase += offset[component];
	}

	// Counts/rates above one repeat the envelope.  Preserve the exact terminal
	// sample so a whole-number count still reaches its authored endpoint.
	if ( phase > 1.0f ) {
		const float whole = idMath::Floor( phase );
		phase -= whole;
		if ( fraction >= 1.0f && phase <= 0.00001f ) {
			phase = 1.0f;
		}
	}
	return BSE_EvaluateNamedEnvelope( name, table, phase );
}
