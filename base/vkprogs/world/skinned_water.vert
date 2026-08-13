#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 2 ) in vec2 inPackedTexCoord;
layout( location = 3 ) in vec2 inPackedNormal;
layout( location = 4 ) in uint inNormalSign;
layout( location = 5 ) in vec2 inPackedTangent;
layout( location = 6 ) in uint inTangentSign;
layout( location = 7 ) in uint inBitangentSign;
layout( location = 8 ) in uvec4 inJointRows;
layout( location = 9 ) in vec4 inJointWeights;

layout( std140, set = 1, binding = 0 ) uniform JointPalette {
	vec4 rows[ 210 ];
} jointPalette;

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

void blendedJointRows( out vec4 row0, out vec4 row1, out vec4 row2 ) {
	vec4 weights = inJointWeights;
	float total = dot( weights, vec4( 1.0 ) );
	weights = total > 0.0 ? weights / total : vec4( 1.0, 0.0, 0.0, 0.0 );
	row0 = jointPalette.rows[ inJointRows.x ] * weights.x + jointPalette.rows[ inJointRows.y ] * weights.y + jointPalette.rows[ inJointRows.z ] * weights.z + jointPalette.rows[ inJointRows.w ] * weights.w;
	row1 = jointPalette.rows[ inJointRows.x + 1u ] * weights.x + jointPalette.rows[ inJointRows.y + 1u ] * weights.y + jointPalette.rows[ inJointRows.z + 1u ] * weights.z + jointPalette.rows[ inJointRows.w + 1u ] * weights.w;
	row2 = jointPalette.rows[ inJointRows.x + 2u ] * weights.x + jointPalette.rows[ inJointRows.y + 2u ] * weights.y + jointPalette.rows[ inJointRows.z + 2u ] * weights.z + jointPalette.rows[ inJointRows.w + 2u ] * weights.w;
}

vec3 skinValue( vec3 value, float translation, vec4 row0, vec4 row1, vec4 row2 ) {
	vec4 inputValue = vec4( value, translation );
	return vec3( dot( row0, inputValue ), dot( row1, inputValue ), dot( row2, inputValue ) );
}

vec3 unpackDirection( vec2 packedDirection, uint zSign ) {
	float z = sqrt( max( 0.0, 1.0 - dot( packedDirection, packedDirection ) ) );
	if ( zSign == 0u ) z = -z;
	return normalize( vec3( packedDirection, z ) );
}

void main() {
	vec4 row0, row1, row2;
	blendedJointRows( row0, row1, row2 );
	vec2 texCoord = inPackedTexCoord * pc.parameters.y;
	fragmentTexCoord = vec2( dot( pc.textureMatrixS.xyz, vec3( texCoord, 1.0 ) ), dot( pc.textureMatrixT.xyz, vec3( texCoord, 1.0 ) ) );
	fragmentNormal = normalize( skinValue( unpackDirection( inPackedNormal, inNormalSign ), 0.0, row0, row1, row2 ) );
	fragmentTangent = normalize( skinValue( unpackDirection( inPackedTangent, inTangentSign ), 0.0, row0, row1, row2 ) );
	fragmentBitangent = cross( fragmentNormal, fragmentTangent ) * ( float( inBitangentSign ) - 1.0 );
	fragmentPosition = skinValue( inPosition, 1.0, row0, row1, row2 );
	gl_Position = pc.modelViewProjection * vec4( fragmentPosition, 1.0 );
}
