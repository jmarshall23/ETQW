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

static float ParticleRange( const idVec2 &range, idRandom &random ) {
	return range.x + random.RandomFloat() * ( range.y - range.x );
}

static int ComponentsForParm( int particleType, int parm ) {
	switch ( parm ) {
		case BSE_PARM_FADE:
		case BSE_PARM_WINDSTRENGTH:
			return 1;
		case BSE_PARM_SIZE: {
			// rvParticleTemplate::SetParameterCounts in ETQW assigns one width
			// component to line/electricity/linked particles, two to sprites and
			// oriented quads, and three to world-space volume/model types.
			switch ( particleType ) {
				case PTYPE_LINE:
				case PTYPE_ELECTRICITY:
				case PTYPE_LINKED:
				case PTYPE_ORIENTEDLINKED:
					return 1;
				case PTYPE_SPRITE:
				case PTYPE_ORIENTED:
					return 2;
				default:
					return 3;
			}
		}
		case BSE_PARM_ROTATE:
			switch ( particleType ) {
				case PTYPE_SPRITE:
				case PTYPE_DECAL:
					return 1;
				case PTYPE_LINE:
				case PTYPE_LIGHT:
				case PTYPE_ELECTRICITY:
				case PTYPE_LINKED:
				case PTYPE_ORIENTEDLINKED:
					return 0;
				default:
					return 3;
			}
		default:
			return 3;
	}
}

static idVec4 DefaultForParm( int parm ) {
	switch ( parm ) {
		case BSE_PARM_TINT: return idVec4( 1.0f, 1.0f, 1.0f, 1.0f );
		case BSE_PARM_FADE: return idVec4( 1.0f, 0.0f, 0.0f, 0.0f );
		case BSE_PARM_SIZE: return idVec4( 1.0f, 1.0f, 1.0f, 0.0f );
		case BSE_PARM_LENGTH: return idVec4( 1.0f, 0.0f, 0.0f, 0.0f );
		default: return vec4_origin;
	}
}

static void ApplyDomainAttenuation( const rvBSEDomain &domain, float fraction, idVec4 &value, int components ) {
	if ( !domain.attenuate && !domain.inverseAttenuate ) {
		return;
	}
	float scale = idMath::ClampFloat( 0.0f, 1.0f, fraction );
	if ( domain.attenuate && !domain.inverseAttenuate ) {
		scale = 1.0f - scale;
	}
	for ( int i = 0; i < components; i++ ) {
		value[i] *= scale;
	}
}

} // namespace

rvBSEParm::rvBSEParm() {
	Clear();
}

void rvBSEParm::Clear() {
	start.Clear();
	end.Clear();
	envelope.Clear();
	hasStart = false;
	hasEnd = false;
}

int rvBSEParm::Allocated() const {
	return start.Allocated() + end.Allocated() + envelope.Allocated();
}

idVec4 rvBSEParm::Evaluate( float fraction, int components, const idVec4 &defaultValue,
		idRandom &random, float linearFraction, bseModelSample_t modelSampler, idVec3 *normal ) const {
	if ( components <= 0 ) {
		if ( normal != NULL ) normal->Zero();
		return defaultValue;
	}
	idVec3 startNormal;
	idVec4 startValue = hasStart ? start.Sample( components, random, linearFraction, modelSampler, &startNormal ) : defaultValue;
	idVec4 endValue = hasEnd ? end.Sample( components, random, linearFraction, modelSampler, NULL ) : startValue;
	if ( hasEnd && end.relative ) {
		endValue += startValue;
	}
	ApplyDomainAttenuation( start, fraction, startValue, components );
	ApplyDomainAttenuation( end, fraction, endValue, components );
	if ( normal != NULL ) {
		*normal = startNormal;
	}

	idVec4 result = startValue;
	for ( int i = 0; i < components; i++ ) {
		const float envelopeFraction = envelope.Evaluate( fraction, i );
		result[i] = startValue[i] + envelopeFraction * ( endValue[i] - startValue[i] );
	}
	return result;
}

rvBSEAction::rvBSEAction() {
	Clear();
}

void rvBSEAction::Clear() {
	effects.Clear();
	bounce = 0.0f;
	physicsDistance = 0.0f;
	remove = false;
}

int rvBSEAction::Allocated() const {
	int total = effects.Allocated();
	for ( int i = 0; i < effects.Num(); i++ ) total += effects[i].Allocated();
	return total;
}

rvParticleTemplate::rvParticleTemplate() {
	Init();
}

void rvParticleTemplate::Init() {
	type = PTYPE_NONE;
	materialName.Clear();
	modelName.Clear();
	entityDefName.Clear();
	duration.Set( 1.0f, 1.0f );
	gravity.Zero();
	fadeIn = 0.0f;
	tiling = 1.0f;
	parentVelocity = 0.0f;
	windDeviationAngle = 0.0f;
	trailType = BSE_TRAIL_NONE;
	trailMaterialName.Clear();
	trailTime.Zero();
	trailCount.Zero();
	trailScale = 1.0f;
	trailRepeat = 1;
	numFrames = 1;
	numForks = 0;
	forkMins.Zero();
	forkMaxs.Zero();
	jitterRate = 0.0f;
	jitterSize.Zero();
	jitterTableName.Clear();
	jitterTable = NULL;
	blend = BSE_BLEND_DEFAULT;
	persist = false;
	generatedLine = false;
	generatedNormal = false;
	generatedOriginNormal = false;
	lineHit = false;
	flipNormal = false;
	useLightningAxis = false;
	shadows = false;
	specular = false;
	impact.Clear();
	timeout.Clear();
	for ( int i = 0; i < BSE_PARM_COUNT; i++ ) parms[i].Clear();
}

int rvParticleTemplate::Allocated() const {
	int total = materialName.Allocated() + modelName.Allocated() + entityDefName.Allocated() +
		trailMaterialName.Allocated() + jitterTableName.Allocated() + impact.Allocated() + timeout.Allocated();
	for ( int i = 0; i < BSE_PARM_COUNT; i++ ) total += parms[i].Allocated();
	return total;
}

bool rvParticle::Evaluate( const rvParticleSpawnInfo &spawn, rvBSEParticle &result ) {
	const rvParticleTemplate &particleTemplate = *spawn.particleTemplate;
	const rvSegmentTemplate &segmentTemplate = *spawn.segmentTemplate;
	const rvBSEOwner &owner = *spawn.owner;
	const float age = owner.time - spawn.birthTime;
	if ( age < 0.0f || ( !particleTemplate.persist && age > spawn.life ) ) {
		return false;
	}
	const float clampedAge = Min( age, spawn.life );
	const float fraction = idMath::ClampFloat( 0.0f, 1.0f, clampedAge / spawn.life );
	idRandom random( spawn.seed );

	idVec4 values[BSE_PARM_COUNT];
	idVec3 generatedNormal;
	for ( int parmIndex = 0; parmIndex < BSE_PARM_COUNT; parmIndex++ ) {
		values[parmIndex] = particleTemplate.parms[parmIndex].Evaluate( fraction,
			ComponentsForParm( particleTemplate.type, parmIndex ), DefaultForParm( parmIndex ), random,
			spawn.linearFraction, owner.modelSampler,
			parmIndex == BSE_PARM_POSITION ? &generatedNormal : NULL );
	}

	const rvBSEParm &fadeParm = particleTemplate.parms[BSE_PARM_FADE];
	if ( !fadeParm.envelope.name.IsEmpty() && !fadeParm.hasEnd ) {
		values[BSE_PARM_FADE].x *= 1.0f - fadeParm.envelope.Evaluate( fraction, 0 );
	}

	idVec3 startPosition = values[BSE_PARM_POSITION].ToVec3();
	if ( particleTemplate.parms[BSE_PARM_POSITION].start.useEndOrigin && owner.hasEndOrigin ) {
		startPosition = owner.endOrigin;
	}
	idVec3 direction = values[BSE_PARM_DIRECTION].ToVec3();
	if ( ( particleTemplate.generatedNormal || particleTemplate.generatedOriginNormal ) && generatedNormal.LengthSqr() > 0.0f ) {
		direction = particleTemplate.flipNormal ? -generatedNormal : generatedNormal;
	}
	idVec3 velocity = values[BSE_PARM_VELOCITY].ToVec3();
	if ( direction.LengthSqr() > 0.0f ) {
		velocity += direction;
	}
	const idVec3 acceleration = values[BSE_PARM_ACCELERATION].ToVec3();
	const idVec3 friction = values[BSE_PARM_FRICTION].ToVec3();
	const float gravityScale = ParticleRange( particleTemplate.gravity, random );
	const float windStrength = values[BSE_PARM_WINDSTRENGTH].x;

	idVec3 displacement = velocity * clampedAge + acceleration * ( 0.5f * clampedAge * clampedAge );
	displacement += owner.gravity * ( gravityScale * 0.5f * clampedAge * clampedAge );
	displacement += owner.wind * ( windStrength * clampedAge );
	for ( int axis = 0; axis < 3; axis++ ) {
		displacement[axis] /= 1.0f + idMath::Fabs( friction[axis] ) * clampedAge;
	}

	result.type = particleTemplate.type;
	result.segmentIndex = spawn.segmentIndex;
	result.particleTemplate = spawn.particleTemplate;
	result.materialName = particleTemplate.materialName;
	result.modelName = particleTemplate.modelName;
	result.entityDefName = particleTemplate.entityDefName;
	// ETQW evaluates the angle envelope in turns, converts it to radians, and
	// uses that orientation to rotate the offset envelope.  Particle visual
	// rotation is a separate envelope and must not be multiplied by age.
	const idVec3 angleTurns = values[BSE_PARM_ANGLE].ToVec3();
	const idMat3 offsetAxis = idAngles( angleTurns.x * 360.0f, angleTurns.y * 360.0f,
		angleTurns.z * 360.0f ).ToMat3();
	const idVec3 rotatedOffset = values[BSE_PARM_OFFSET].ToVec3() * offsetAxis;
	result.position = startPosition + displacement + rotatedOffset;
	result.velocity = velocity + acceleration * clampedAge + owner.gravity * ( gravityScale * clampedAge );
	result.length = values[BSE_PARM_LENGTH].ToVec3();
	result.angles = values[BSE_PARM_ROTATE].ToVec3();
	result.size.Set( idMath::Fabs( values[BSE_PARM_SIZE].x ), idMath::Fabs( values[BSE_PARM_SIZE].y ),
		idMath::Fabs( values[BSE_PARM_SIZE].z ) );
	result.size *= segmentTemplate.scale;
	result.color.Set( values[BSE_PARM_TINT].x * owner.color.x * owner.brightness,
		values[BSE_PARM_TINT].y * owner.color.y * owner.brightness,
		values[BSE_PARM_TINT].z * owner.color.z * owner.brightness,
		values[BSE_PARM_FADE].x * owner.color.w );
	if ( particleTemplate.fadeIn > 0.0f ) {
		result.color.w *= idMath::ClampFloat( 0.0f, 1.0f, clampedAge / particleTemplate.fadeIn );
	}
	result.jitterSize = particleTemplate.jitterSize;
	result.jitterRate = particleTemplate.jitterRate;
	result.jitterTable = particleTemplate.jitterTable;
	result.forkMins = particleTemplate.forkMins;
	result.forkMaxs = particleTemplate.forkMaxs;
	result.numForks = particleTemplate.numForks;
	result.seed = spawn.seed;
	result.age = clampedAge;
	result.life = spawn.life;
	const int frame = idMath::ClampInt( 0, particleTemplate.numFrames - 1,
		idMath::Ftoi( fraction * particleTemplate.numFrames ) );
	result.textureS0 = static_cast<float>( frame ) / particleTemplate.numFrames;
	result.textureS1 = static_cast<float>( frame + 1 ) / particleTemplate.numFrames;
	result.tiling = particleTemplate.tiling;
	result.trailType = particleTemplate.trailType;
	result.trailMaterialName = particleTemplate.trailMaterialName;
	result.trailTime = Max( 0.0f, ParticleRange( particleTemplate.trailTime, random ) );
	result.trailCount = Max( 0, idMath::Ftoi( ParticleRange( particleTemplate.trailCount, random ) ) );
	result.trailScale = particleTemplate.trailScale;
	result.useEndOrigin = particleTemplate.parms[BSE_PARM_LENGTH].start.useEndOrigin ||
		particleTemplate.parms[BSE_PARM_LENGTH].end.useEndOrigin;
	result.depthSort = segmentTemplate.depthSort;
	result.inverseDrawOrder = segmentTemplate.inverseDrawOrder;
	BSE_ResolveModelSafeParticle( result );
	return result.color.w > 0.0f;
}
