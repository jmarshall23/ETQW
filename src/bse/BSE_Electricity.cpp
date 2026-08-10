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

static idVec3 ElectricityEnd( const rvBSEOwner &owner, const rvBSEParticle &particle ) {
	return particle.useEndOrigin && owner.hasEndOrigin ? owner.endOrigin : particle.position + particle.length;
}

static void AppendBoltLine( const rvBSEParticle &source, const idVec3 &start, const idVec3 &end,
		float widthScale, idList<rvBSEParticle> &output ) {
	if ( ( end - start ).LengthSqr() < 0.0001f ) return;
	rvBSEParticle line = source;
	line.type = PTYPE_LINE;
	line.position = start;
	line.length = end - start;
	line.size *= widthScale;
	line.useEndOrigin = false;
	line.trailType = BSE_TRAIL_NONE;
	line.trailCount = 0;
	output.Append( line );
}

} // namespace

void BSE_CreateElectricity( const rvBSEOwner &owner, const rvBSEParticle &particle,
		idList<rvBSEParticle> &output ) {
	const idVec3 end = ElectricityEnd( owner, particle );
	const idVec3 delta = end - particle.position;
	if ( delta.LengthSqr() < 0.0001f ) return;

	const int subdivisions = idMath::ClampInt( 4, 32, 4 + idMath::Ftoi( delta.Length() / 32.0f ) );
	idList<idVec3> points;
	points.SetNum( subdivisions + 1 );
	const int timeSeed = particle.jitterRate > 0.0f ? idMath::Ftoi( particle.age * particle.jitterRate ) : 0;
	idRandom random( particle.seed ^ ( timeSeed * 2654435761u ) );
	points[0] = particle.position;
	points[subdivisions] = end;
	for ( int i = 1; i < subdivisions; i++ ) {
		const float fraction = static_cast<float>( i ) / subdivisions;
		float envelope = idMath::Sin( fraction * idMath::PI );
		if ( particle.jitterTable != NULL ) envelope *= particle.jitterTable->TableLookup( fraction );
		const idVec3 jitter( random.CRandomFloat() * particle.jitterSize.x,
			random.CRandomFloat() * particle.jitterSize.y,
			random.CRandomFloat() * particle.jitterSize.z );
		points[i] = particle.position + delta * fraction + jitter * envelope;
	}
	for ( int i = 0; i < subdivisions; i++ ) {
		AppendBoltLine( particle, points[i], points[i + 1], 1.0f, output );
	}

	for ( int fork = 0; fork < particle.numForks; fork++ ) {
		const int pointIndex = idMath::ClampInt( 1, subdivisions - 1, random.RandomInt( subdivisions ) );
		idVec3 forkOffset;
		for ( int axis = 0; axis < 3; axis++ ) {
			float minimum = particle.forkMins[axis];
			float maximum = particle.forkMaxs[axis];
			if ( minimum == 0.0f && maximum == 0.0f ) {
				minimum = -particle.jitterSize[axis] * 2.0f;
				maximum = particle.jitterSize[axis] * 2.0f;
			}
			if ( minimum > maximum ) {
				const float temporary = minimum;
				minimum = maximum;
				maximum = temporary;
			}
			forkOffset[axis] = minimum + random.RandomFloat() * ( maximum - minimum );
		}
		AppendBoltLine( particle, points[pointIndex], points[pointIndex] + forkOffset, 0.65f, output );
	}
}
