#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 2 ) in vec2 inPackedTexCoord;
layout( location = 3 ) in vec2 inPackedNormal;
layout( location = 4 ) in uint inNormalSign;
layout( location = 5 ) in vec2 inPackedTangent;
layout( location = 6 ) in uint inTangentSign;
layout( location = 7 ) in uint inBitangentSign;

layout( push_constant ) uniform WaterPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 textureMatrixS;
	vec4 textureMatrixT;
	vec4 parameters;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out vec3 fragmentNormal;
layout( location = 2 ) out vec3 fragmentTangent;
layout( location = 3 ) out vec3 fragmentBitangent;
layout( location = 4 ) out vec3 fragmentPosition;

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
	fragmentNormal = unpackDirection( inPackedNormal, inNormalSign );
	fragmentTangent = unpackDirection( inPackedTangent, inTangentSign );
	// Do not normalize here: old static water meshes can contain a degenerate
	// tangent, and normalize( 0 ) poisons every interpolated fragment with NaNs.
	fragmentBitangent = cross( fragmentNormal, fragmentTangent ) *
		( float( inBitangentSign ) - 1.0 );
	fragmentPosition = inPosition;
	gl_Position = pc.modelViewProjection * vec4( inPosition, 1.0 );
}
