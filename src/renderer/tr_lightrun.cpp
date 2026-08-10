// Copyright (C) 2007 Id Software, Inc.
//
// Runtime light projection derivation recovered from tr_lightrun.obj.  This
// operates on the public renderLight_t while idRenderLightLocal/viewLight_s
// are restored.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderWorld.h"
#include "Material.h"
#include "Image.h"
#include "tr_render.h"
#include "../decllib/declTypeHolder.h"

namespace {

void R_SetLightProject( idPlane lightProject[ 4 ], const idVec3& origin, const idVec3& target,
		const idVec3& rightVector, const idVec3& upVector, const idVec3& start, const idVec3& stop ) {
	idVec3 right = rightVector;
	idVec3 up = upVector;
	const float rightLength = Max( right.Normalize(), 1.0e-6f );
	const float upLength = Max( up.Normalize(), 1.0e-6f );
	idVec3 normal = up.Cross( right );
	normal.Normalize();

	float distance = target * normal;
	if ( distance < 0.0f ) {
		distance = -distance;
		normal = -normal;
	}
	right *= 0.5f * distance / rightLength;
	up *= -0.5f * distance / upLength;

	lightProject[ 2 ] = normal;
	lightProject[ 2 ][ 3 ] = -( origin * lightProject[ 2 ].Normal() );
	lightProject[ 0 ] = right;
	lightProject[ 0 ][ 3 ] = -( origin * lightProject[ 0 ].Normal() );
	lightProject[ 1 ] = up;
	lightProject[ 1 ][ 3 ] = -( origin * lightProject[ 1 ].Normal() );

	idVec4 targetGlobal;
	targetGlobal.ToVec3() = target + origin;
	targetGlobal[ 3 ] = 1.0f;
	float denominator = targetGlobal * lightProject[ 2 ].ToVec4();
	if ( idMath::Fabs( denominator ) < 1.0e-6f ) {
		denominator = 1.0f;
	}
	float offset = 0.5f - ( targetGlobal * lightProject[ 0 ].ToVec4() ) / denominator;
	lightProject[ 0 ].ToVec4() += offset * lightProject[ 2 ].ToVec4();
	offset = 0.5f - ( targetGlobal * lightProject[ 1 ].ToVec4() ) / denominator;
	lightProject[ 1 ].ToVec4() += offset * lightProject[ 2 ].ToVec4();

	normal = stop - start;
	distance = normal.Normalize();
	if ( distance <= 0.0f ) {
		distance = 1.0f;
	}
	lightProject[ 3 ] = normal * ( 1.0f / distance );
	const idVec3 startGlobal = start + origin;
	lightProject[ 3 ][ 3 ] = -( startGlobal * lightProject[ 3 ].Normal() );
}

idPlane LocalPlaneToGlobal( const idPlane& local, const renderLight_t& light ) {
	const idVec3 globalNormal = light.axis[ 0 ] * local[ 0 ] + light.axis[ 1 ] * local[ 1 ] + light.axis[ 2 ] * local[ 2 ];
	return idPlane( globalNormal.x, globalNormal.y, globalNormal.z, local[ 3 ] - globalNormal * light.origin );
}

}

void R_DeriveLightData( const renderLight_t& light, idPlane lightProject[ 4 ], idVec3& globalLightOrigin,
		const idMaterial*& lightMaterial, idImage*& falloffImage ) {
	lightMaterial = light.material;
	if ( lightMaterial == NULL ) {
		lightMaterial = declHolder.FindMaterial( light.flags.pointLight ? "lights/defaultPointLight" : "lights/defaultProjectedLight", true );
	}
	falloffImage = lightMaterial != NULL ? lightMaterial->LightFalloffImage() : NULL;
	if ( falloffImage == NULL ) {
		const idMaterial* defaultMaterial = declHolder.FindMaterial( light.flags.pointLight ? "lights/defaultPointLight" : "lights/defaultProjectedLight", true );
		falloffImage = defaultMaterial != NULL ? defaultMaterial->LightFalloffImage() : NULL;
	}

	if ( light.flags.pointLight ) {
		memset( lightProject, 0, sizeof( idPlane ) * 4 );
		lightProject[ 0 ][ 0 ] = 0.5f / Max( idMath::Fabs( light.lightRadius.x ), 1.0f );
		lightProject[ 1 ][ 1 ] = 0.5f / Max( idMath::Fabs( light.lightRadius.y ), 1.0f );
		lightProject[ 3 ][ 2 ] = 0.5f / Max( idMath::Fabs( light.lightRadius.z ), 1.0f );
		lightProject[ 0 ][ 3 ] = 0.5f;
		lightProject[ 1 ][ 3 ] = 0.5f;
		lightProject[ 2 ][ 3 ] = 1.0f;
		lightProject[ 3 ][ 3 ] = 0.5f;
	} else {
		R_SetLightProject( lightProject, vec3_origin, light.target, light.right, light.up, light.start, light.end );
	}
	for ( int i = 0; i < 4; ++i ) {
		lightProject[ i ] = LocalPlaneToGlobal( lightProject[ i ], light );
	}

	if ( light.flags.parallel ) {
		idVec3 direction = light.lightCenter;
		if ( direction.Normalize() == 0.0f ) {
			direction.Set( 0.0f, 0.0f, 1.0f );
		}
		globalLightOrigin = light.origin + direction * 100000.0f;
	} else {
		globalLightOrigin = light.origin + light.axis * light.lightCenter;
	}
}

void R_RenderLightFrustum( const renderLight_t& renderLight, idPlane lightFrustum[ 6 ] ) {
	idPlane lightProject[ 4 ];
	idVec3 globalLightOrigin;
	const idMaterial* lightMaterial;
	idImage* falloffImage;
	R_DeriveLightData( renderLight, lightProject, globalLightOrigin, lightMaterial, falloffImage );

	lightFrustum[ 0 ] = -lightProject[ 0 ];
	lightFrustum[ 1 ] = -lightProject[ 1 ];
	lightFrustum[ 2 ] = -( lightProject[ 2 ] - lightProject[ 0 ] );
	lightFrustum[ 3 ] = -( lightProject[ 2 ] - lightProject[ 1 ] );
	lightFrustum[ 4 ] = -lightProject[ 3 ];
	lightFrustum[ 5 ] = lightProject[ 3 ];
	lightFrustum[ 5 ][ 3 ] -= 1.0f;
	for ( int i = 0; i < 6; ++i ) {
		const float length = lightFrustum[ i ].Normalize();
		if ( length > 0.0f ) {
			lightFrustum[ i ][ 3 ] /= length;
		}
	}
}
