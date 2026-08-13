#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec4 inColor;
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

vec3 rotateToWorld( vec3 value, vec4 idQuaternion ) {
	vec3 quaternionVector = -idQuaternion.xyz;
	vec3 temporary = 2.0 * cross( quaternionVector, value );
	return value + idQuaternion.w * temporary + cross( quaternionVector, temporary );
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
	fragmentColor = pc.color * mix( vec4( 1.0 ), inColor, pc.parameters.x );
	vec3 normal = normalize( skinValue( unpackDirection( inPackedNormal, inNormalSign ), 0.0, row0, row1, row2 ) );
	vec3 tangent = normalize( skinValue( unpackDirection( inPackedTangent, inTangentSign ), 0.0, row0, row1, row2 ) );
	vec3 bitangent = normalize( cross( normal, tangent ) ) * ( float( inBitangentSign ) - 1.0 );
	vec2 quaternionXY = unpackSnorm2x16( pc.packedRotationXY );
	vec2 quaternionZW = unpackSnorm2x16( pc.packedRotationZW );
	vec4 modelRotation = normalize( vec4( quaternionXY, quaternionZW ) );
	fragmentTangent = normalize( rotateToWorld( tangent, modelRotation ) );
	fragmentBitangent = normalize( rotateToWorld( bitangent, modelRotation ) );
	fragmentNormal = normalize( rotateToWorld( normal, modelRotation ) );
	vec3 worldSunDirection = normalize( vec3( 0.35, 0.45, 0.82 ) );
	fragmentSunDirection = normalize( vec3( dot( worldSunDirection, fragmentTangent ), dot( worldSunDirection, fragmentBitangent ), dot( worldSunDirection, fragmentNormal ) ) );
	vec3 position = skinValue( inPosition, 1.0, row0, row1, row2 );
	gl_Position = pc.modelViewProjection * vec4( position, 1.0 );
}
