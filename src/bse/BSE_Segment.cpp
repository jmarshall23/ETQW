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

static float SegmentRange( const idVec2 &range, idRandom &random ) {
	return range.x + random.RandomFloat() * ( range.y - range.x );
}

static bool SegmentCanSpawn( float requestedCount, int index, idRandom &random ) {
	if ( requestedCount >= static_cast<float>( index + 1 ) ) return true;
	if ( requestedCount <= static_cast<float>( index ) ) return false;
	return random.RandomFloat() < requestedCount - static_cast<float>( index );
}

static float SegmentAttenuation( const rvSegmentTemplate &segment, const rvBSEOwner &owner ) {
	if ( !segment.attenuateEmitter ) return 1.0f;
	float nearDistance = segment.attenuation.x;
	float farDistance = segment.attenuation.y;
	if ( nearDistance > farDistance ) {
		const float temporary = nearDistance;
		nearDistance = farDistance;
		farDistance = temporary;
	}
	const float distance = owner.viewOrigin.Length();
	float attenuation;
	if ( farDistance <= nearDistance ) {
		attenuation = distance <= nearDistance ? 1.0f : 0.0f;
	} else {
		attenuation = 1.0f - idMath::ClampFloat( 0.0f, 1.0f,
			( distance - nearDistance ) / ( farDistance - nearDistance ) );
	}
	return segment.inverseAttenuateEmitter ? 1.0f - attenuation : attenuation;
}

} // namespace

rvSegment::rvSegment() {
	segmentTemplate = NULL;
	segmentIndex = 0;
	seedBase = 0;
}

void rvSegment::Init( const rvSegmentTemplate *newTemplate, int index, int effectSeed ) {
	segmentTemplate = newTemplate;
	segmentIndex = index;
	seedBase = effectSeed ^ ( index * 7919 );
}

void rvSegment::Service( const rvBSEOwner &owner, idList<rvBSEParticle> &particles,
		int maxParticles, rvBSEStats *stats ) const {
	if ( segmentTemplate == NULL || !segmentTemplate->HasVisualParticle() ) return;
	const rvSegmentTemplate &segment = *segmentTemplate;
	if ( stats != NULL ) stats->segmentsServiced++;

	const int diversitySeed = idMath::Ftoi( owner.diversity * idRandom::MAX_RAND );
	idRandom segmentRandom( seedBase ^ diversitySeed );
	const float segmentStart = SegmentRange( segment.startTime, segmentRandom );
	if ( owner.time < segmentStart ) return;

	const float segmentDuration = Max( 0.0f, SegmentRange( segment.duration, segmentRandom ) );
	float requestedCount = Max( 0.0f, SegmentRange( segment.count, segmentRandom ) );
	const float densityCount = Max( 0.0f, SegmentRange( segment.density, segmentRandom ) );
	if ( densityCount > 0.0f ) requestedCount = Max( requestedCount, densityCount );
	requestedCount *= SegmentAttenuation( segment, owner );
	int candidateCount = idMath::Ceil( requestedCount );
	if ( segment.particleCap > 0.0f ) {
		const int cap = Max( 0, idMath::Ftoi( idMath::Ceil( segment.particleCap ) ) );
		if ( candidateCount > cap && stats != NULL ) stats->particlesCapped += candidateCount - cap;
		candidateCount = Min( candidateCount, cap );
	}
	candidateCount = idMath::ClampInt( 0, 8192, candidateCount );
	if ( candidateCount <= 0 ) return;

	const bool repeats = segment.constant || segment.looping;
	int firstCycle = 0;
	int lastCycle = 0;
	if ( repeats && segmentDuration > 0.002f ) {
		lastCycle = Max( 0, idMath::Ftoi( idMath::Floor( ( owner.time - segmentStart ) / segmentDuration ) ) );
		const int lifeCycles = idMath::Ceil( segment.particle.duration.y / segmentDuration ) + 1;
		firstCycle = Max( 0, lastCycle - idMath::ClampInt( 1, 64, lifeCycles ) );
	}

	for ( int cycle = firstCycle; cycle <= lastCycle; cycle++ ) {
		const float cycleStart = segmentStart + cycle * segmentDuration;
		for ( int particleIndex = 0; particleIndex < candidateCount; particleIndex++ ) {
			if ( particles.Num() >= maxParticles ) {
				if ( stats != NULL ) stats->particlesCapped++;
				return;
			}
			const int particleSeed = seedBase ^ diversitySeed ^ ( particleIndex * 19349663 ) ^ ( cycle * 83492791 );
			idRandom random( particleSeed );
			if ( !SegmentCanSpawn( requestedCount, particleIndex, random ) ) continue;

			float birthTime = cycleStart;
			if ( ( segment.type == SEG_EMITTER || segment.type == SEG_TRAIL ) && segmentDuration > 0.0f ) {
				birthTime += segmentDuration * static_cast<float>( particleIndex ) / Max( 1, candidateCount );
			}
			if ( !repeats && segmentDuration > 0.0f && birthTime > segmentStart + segmentDuration ) continue;
			if ( owner.stopTime > 0.0f && birthTime > owner.stopTime ) continue;

			const float life = Max( 0.002f, SegmentRange( segment.particle.duration, random ) );
			rvParticleSpawnInfo spawn;
			spawn.particleTemplate = &segment.particle;
			spawn.segmentTemplate = &segment;
			spawn.owner = &owner;
			spawn.segmentIndex = segmentIndex;
			spawn.particleIndex = particleIndex;
			spawn.seed = particleSeed;
			spawn.birthTime = birthTime;
			spawn.life = life;
			spawn.linearFraction = candidateCount > 1 ? static_cast<float>( particleIndex ) / ( candidateCount - 1 ) : 0.0f;
			rvBSEParticle evaluated;
			if ( rvParticle::Evaluate( spawn, evaluated ) ) {
				particles.Append( evaluated );
				if ( stats != NULL ) stats->particlesEvaluated++;
			}
		}
	}
}
