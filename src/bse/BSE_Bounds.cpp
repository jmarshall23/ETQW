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

namespace {

static float ParmMagnitude( const rvBSEParm &parm, int components ) {
	float magnitude = 0.0f;
	const rvBSEDomain *domains[2] = { &parm.start, &parm.end };
	for ( int d = 0; d < 2; d++ ) {
		const rvBSEDomain &domain = *domains[d];
		for ( int i = 0; i < domain.values.Num(); i++ ) {
			magnitude = Max( magnitude, idMath::Fabs( domain.values[i] ) );
		}
	}
	return magnitude * idMath::Sqrt( static_cast<float>( Max( 1, components ) ) );
}

} // namespace

void rvDeclEffect::CalculateBounds() {
	bounds.Clear();
	bounds.AddPoint( vec3_origin );
	float calculatedRadius = Max( 8.0f, size );
	for ( int i = 0; i < segments.Num(); i++ ) {
		const rvSegmentTemplate &segment = segments[i];
		if ( !segment.HasVisualParticle() ) continue;
		const rvParticleTemplate &particle = segment.particle;
		idBounds positionBounds = particle.parms[BSE_PARM_POSITION].start.GetBounds( 3 );
		positionBounds.AddBounds( particle.parms[BSE_PARM_POSITION].end.GetBounds( 3 ) );
		positionBounds.AddBounds( particle.parms[BSE_PARM_OFFSET].start.GetBounds( 3 ) );
		positionBounds.AddBounds( particle.parms[BSE_PARM_OFFSET].end.GetBounds( 3 ) );
		bounds.AddBounds( positionBounds );

		const float life = Max( particle.duration.x, particle.duration.y );
		const float velocity = ParmMagnitude( particle.parms[BSE_PARM_VELOCITY], 3 );
		const float acceleration = ParmMagnitude( particle.parms[BSE_PARM_ACCELERATION], 3 );
		const float gravity = Max( idMath::Fabs( particle.gravity.x ), idMath::Fabs( particle.gravity.y ) ) * 1066.0f;
		const float sizeRadius = ParmMagnitude( particle.parms[BSE_PARM_SIZE], 3 ) * Max( 1.0f, segment.scale );
		const float lengthRadius = ParmMagnitude( particle.parms[BSE_PARM_LENGTH], 3 );
		calculatedRadius = Max( calculatedRadius,
			positionBounds.GetRadius() + velocity * life + 0.5f * ( acceleration + gravity ) * life * life +
			sizeRadius + lengthRadius );
	}
	// Authored sizes are normally tighter; cap malformed analytical input while
	// retaining a conservative expansion for moving particles.
	calculatedRadius = idMath::ClampFloat( Max( 8.0f, size ), 131072.0f, calculatedRadius );
	bounds.AddPoint( idVec3( -calculatedRadius, -calculatedRadius, -calculatedRadius ) );
	bounds.AddPoint( idVec3( calculatedRadius, calculatedRadius, calculatedRadius ) );
}
