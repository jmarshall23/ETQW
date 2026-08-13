#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 3 ) in vec2 inPackedNormal;
layout( location = 4 ) in uint inNormalSign;
layout( location = 8 ) in uvec4 inJointRows;
layout( location = 9 ) in vec4 inJointWeights;

layout( std140, set = 1, binding = 0 ) uniform JointPalette {
	vec4 rows[ 210 ];
} jointPalette;

layout( push_constant ) uniform AtmospherePushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 localViewOrigin;
	vec4 unused0;
	vec4 parameters;
} pc;

layout( location = 0 ) out float fragmentRim;

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

void main() {
	vec4 row0, row1, row2;
	blendedJointRows( row0, row1, row2 );
	float normalZ = sqrt( max( 0.0, 1.0 - dot( inPackedNormal, inPackedNormal ) ) );
	if ( inNormalSign == 0u ) normalZ = -normalZ;
	vec3 normal = normalize( skinValue( vec3( inPackedNormal, normalZ ), 0.0, row0, row1, row2 ) );
	vec3 position = skinValue( inPosition, 1.0, row0, row1, row2 );
	vec3 viewDirection = normalize( pc.localViewOrigin.xyz - position );
	float facing = clamp( abs( dot( normal, viewDirection ) ), 0.0, 1.0 );
	fragmentRim = pow( 1.0 - facing, 2.5 );
	gl_Position = pc.modelViewProjection * vec4( position, 1.0 );
}
