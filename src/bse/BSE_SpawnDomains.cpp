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

static float DomainValue( const idList<float> &values, int index, float fallback = 0.0f ) {
	if ( values.Num() <= 0 ) {
		return fallback;
	}
	if ( values.Num() == 1 ) {
		return values[0];
	}
	return values[idMath::ClampInt( 0, values.Num() - 1, index )];
}

static void DomainMinMax( const rvBSEDomain &domain, int component, int components, float &minimum, float &maximum ) {
	minimum = DomainValue( domain.values, component );
	maximum = DomainValue( domain.values, component + components, minimum );
	if ( minimum > maximum ) {
		const float temporary = minimum;
		minimum = maximum;
		maximum = temporary;
	}
}

} // namespace

rvBSEDomain::rvBSEDomain() {
	Clear();
}

void rvBSEDomain::Clear() {
	type = BSE_DOMAIN_NONE;
	values.Clear();
	modelName.Clear();
	relative = false;
	useEndOrigin = false;
	surface = false;
	cone = false;
	linearSpacing = false;
	attenuate = false;
	inverseAttenuate = false;
}

idVec4 rvBSEDomain::Sample( int components, idRandom &random, float linearFraction,
		bseModelSample_t modelSampler, idVec3 *normal ) const {
	idVec4 result = vec4_origin;
	components = idMath::ClampInt( 1, 4, components );
	if ( normal != NULL ) {
		normal->Zero();
	}

	if ( type == BSE_DOMAIN_MODEL && modelSampler != NULL && !modelName.IsEmpty() ) {
		idVec3 point;
		idVec3 sampledNormal;
		if ( modelSampler( modelName, random, point, sampledNormal ) ) {
			for ( int i = 0; i < 3; i++ ) {
				const float offset = DomainValue( values, i, 0.0f );
				const float scale = DomainValue( values, i + 3, 1.0f );
				result[i] = point[i] * scale + offset;
			}
			if ( normal != NULL ) {
				*normal = sampledNormal;
			}
			return result;
		}
	}

	if ( type == BSE_DOMAIN_NONE || values.Num() == 0 ) {
		return result;
	}
	if ( type == BSE_DOMAIN_POINT || values.Num() <= components ) {
		for ( int i = 0; i < components; i++ ) {
			result[i] = DomainValue( values, i );
		}
		return result;
	}

	const float sharedFraction = linearFraction >= 0.0f ?
		idMath::ClampFloat( 0.0f, 1.0f, linearFraction ) : random.RandomFloat();
	if ( type == BSE_DOMAIN_LINE || type == BSE_DOMAIN_CONE ) {
		for ( int i = 0; i < components; i++ ) {
			const float a = DomainValue( values, i );
			const float b = DomainValue( values, i + components, a );
			result[i] = a + sharedFraction * ( b - a );
		}
		return result;
	}

	if ( type == BSE_DOMAIN_BOX || type == BSE_DOMAIN_MODEL ) {
		int surfaceAxis = surface ? random.RandomInt( components ) : -1;
		for ( int i = 0; i < components; i++ ) {
			float minimum, maximum;
			DomainMinMax( *this, i, components, minimum, maximum );
			const float fraction = linearSpacing ? sharedFraction : random.RandomFloat();
			if ( i == surfaceAxis ) {
				result[i] = random.RandomInt( 2 ) ? minimum : maximum;
				if ( normal != NULL && i < 3 ) {
					( *normal )[i] = result[i] == maximum ? 1.0f : -1.0f;
				}
			} else {
				result[i] = minimum + fraction * ( maximum - minimum );
			}
		}
		return result;
	}

	if ( type == BSE_DOMAIN_SPHERE ) {
		idVec3 direction( random.CRandomFloat(), random.CRandomFloat(), random.CRandomFloat() );
		if ( direction.Normalize() == 0.0f ) {
			direction.Set( 1.0f, 0.0f, 0.0f );
		}
		const float radiusFraction = surface ? 1.0f : idMath::Pow( random.RandomFloat(), 1.0f / 3.0f );
		for ( int i = 0; i < components; i++ ) {
			float minimum, maximum;
			DomainMinMax( *this, i, components, minimum, maximum );
			const float center = ( minimum + maximum ) * 0.5f;
			const float radius = ( maximum - minimum ) * 0.5f;
			result[i] = center + direction[i < 3 ? i : 0] * radius * radiusFraction;
		}
		if ( normal != NULL ) {
			*normal = direction;
		}
		return result;
	}

	if ( type == BSE_DOMAIN_CYLINDER && components >= 3 ) {
		float minX, maxX, minY, maxY, minZ, maxZ;
		DomainMinMax( *this, 0, components, minX, maxX );
		DomainMinMax( *this, 1, components, minY, maxY );
		DomainMinMax( *this, 2, components, minZ, maxZ );
		const float angle = random.RandomFloat() * idMath::TWO_PI;
		const float radiusFraction = surface ? 1.0f : idMath::Sqrt( random.RandomFloat() );
		result.x = minX + ( linearSpacing ? sharedFraction : random.RandomFloat() ) * ( maxX - minX );
		result.y = ( minY + maxY ) * 0.5f + idMath::Cos( angle ) * ( maxY - minY ) * 0.5f * radiusFraction;
		result.z = ( minZ + maxZ ) * 0.5f + idMath::Sin( angle ) * ( maxZ - minZ ) * 0.5f * radiusFraction;
		if ( normal != NULL ) {
			normal->Set( 0.0f, idMath::Cos( angle ), idMath::Sin( angle ) );
		}
		return result;
	}

	if ( type == BSE_DOMAIN_SPIRAL ) {
		const float turns = idMath::Fabs( DomainValue( values, components * 2, 1.0f ) );
		const float angle = sharedFraction * Max( 1.0f, turns ) * idMath::TWO_PI;
		float minX, maxX, minY, maxY, minZ, maxZ;
		DomainMinMax( *this, 0, components, minX, maxX );
		DomainMinMax( *this, Min( 1, components - 1 ), components, minY, maxY );
		DomainMinMax( *this, Min( 2, components - 1 ), components, minZ, maxZ );
		result.x = minX + sharedFraction * ( maxX - minX );
		result.y = idMath::Cos( angle ) * ( minY + sharedFraction * ( maxY - minY ) );
		if ( components >= 3 ) {
			result.z = idMath::Sin( angle ) * ( minZ + sharedFraction * ( maxZ - minZ ) );
		}
		return result;
	}

	return result;
}

idBounds rvBSEDomain::GetBounds( int components ) const {
	idBounds result;
	result.Zero();
	if ( type == BSE_DOMAIN_MODEL ) {
		result.ExpandSelf( 8.0f );
		return result;
	}
	components = idMath::ClampInt( 1, 3, components );
	for ( int i = 0; i < components; i++ ) {
		float minimum, maximum;
		DomainMinMax( *this, i, components, minimum, maximum );
		result[0][i] = minimum;
		result[1][i] = maximum;
	}
	return result;
}
