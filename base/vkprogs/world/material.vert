#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec4 inColor;
layout( location = 2 ) in vec2 inPackedTexCoord;
layout( location = 3 ) in vec2 inPackedNormal;
layout( location = 4 ) in uint inNormalSign;
layout( location = 5 ) in vec2 inPackedTangent;
layout( location = 6 ) in uint inTangentSign;
layout( location = 7 ) in uint inBitangentSign;

layout( push_constant ) uniform MaterialPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec3 textureMatrixS;
	uint packedRotationXY;
	vec3 textureMatrixT;
	uint packedRotationZW;
	vec4 parameters;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out vec4 fragmentColor;
layout( location = 2 ) out vec3 fragmentSunDirection;
layout( location = 3 ) out vec3 fragmentTangent;
layout( location = 4 ) out vec3 fragmentBitangent;
layout( location = 5 ) out vec3 fragmentNormal;

vec3 rotateToWorld( vec3 value, vec4 idQuaternion ) {
	// idQuat transforms a vector as inverse(q) * v * q, the conjugate of
	// GLSL's conventional quaternion-vector formula.
	vec3 quaternionVector = -idQuaternion.xyz;
	vec3 temporary = 2.0 * cross( quaternionVector, value );
	return value + idQuaternion.w * temporary +
		cross( quaternionVector, temporary );
}

vec3 unpackDirection( vec2 packedDirection, uint zSign ) {
	float z = sqrt( max( 0.0, 1.0 - dot( packedDirection, packedDirection ) ) );
	if ( zSign == 0u ) {
		z = -z;
	}
	return normalize( vec3( packedDirection, z ) );
}

void main() {
	vec2 texCoord = inPackedTexCoord * pc.parameters.y;
	fragmentTexCoord = vec2(
		dot( pc.textureMatrixS.xyz, vec3( texCoord, 1.0 ) ),
		dot( pc.textureMatrixT.xyz, vec3( texCoord, 1.0 ) )
	);
	fragmentColor = pc.color * mix( vec4( 1.0 ), inColor, pc.parameters.x );
	vec3 normal = unpackDirection( inPackedNormal, inNormalSign );
	vec3 tangent = unpackDirection( inPackedTangent, inTangentSign );
	vec3 bitangent = normalize( cross( normal, tangent ) ) *
		( float( inBitangentSign ) - 1.0 );
	vec2 quaternionXY = unpackSnorm2x16( pc.packedRotationXY );
	vec2 quaternionZW = unpackSnorm2x16( pc.packedRotationZW );
	vec4 modelRotation = normalize( vec4( quaternionXY, quaternionZW ) );
	fragmentTangent = normalize( rotateToWorld( tangent, modelRotation ) );
	fragmentBitangent = normalize( rotateToWorld( bitangent, modelRotation ) );
	fragmentNormal = normalize( rotateToWorld( normal, modelRotation ) );
	vec3 worldSunDirection = normalize( vec3( 0.35, 0.45, 0.82 ) );
	fragmentSunDirection = normalize( vec3(
		dot( worldSunDirection, fragmentTangent ),
		dot( worldSunDirection, fragmentBitangent ),
		dot( worldSunDirection, fragmentNormal )
	) );
	gl_Position = pc.modelViewProjection * vec4( inPosition, 1.0 );
}
