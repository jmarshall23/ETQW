#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec4 inColor;
layout( location = 2 ) in vec2 inPackedTexCoord;
layout( location = 8 ) in uvec4 inJointRows;
layout( location = 9 ) in vec4 inJointWeights;

layout( std140, set = 1, binding = 0 ) uniform JointPalette {
	vec4 rows[ 210 ];
} jointPalette;

layout( push_constant ) uniform TrivialPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 textureMatrixS;
	vec4 textureMatrixT;
	vec4 parameters;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out vec4 fragmentColor;

void blendedJointRows( out vec4 row0, out vec4 row1, out vec4 row2 ) {
	vec4 weights = inJointWeights;
	float total = dot( weights, vec4( 1.0 ) );
	weights = total > 0.0 ? weights / total : vec4( 1.0, 0.0, 0.0, 0.0 );
	row0 = jointPalette.rows[ inJointRows.x ] * weights.x +
		jointPalette.rows[ inJointRows.y ] * weights.y +
		jointPalette.rows[ inJointRows.z ] * weights.z +
		jointPalette.rows[ inJointRows.w ] * weights.w;
	row1 = jointPalette.rows[ inJointRows.x + 1u ] * weights.x +
		jointPalette.rows[ inJointRows.y + 1u ] * weights.y +
		jointPalette.rows[ inJointRows.z + 1u ] * weights.z +
		jointPalette.rows[ inJointRows.w + 1u ] * weights.w;
	row2 = jointPalette.rows[ inJointRows.x + 2u ] * weights.x +
		jointPalette.rows[ inJointRows.y + 2u ] * weights.y +
		jointPalette.rows[ inJointRows.z + 2u ] * weights.z +
		jointPalette.rows[ inJointRows.w + 2u ] * weights.w;
}

vec3 skinPosition( vec3 position ) {
	vec4 row0, row1, row2;
	blendedJointRows( row0, row1, row2 );
	vec4 localPosition = vec4( position, 1.0 );
	return vec3( dot( row0, localPosition ), dot( row1, localPosition ),
		dot( row2, localPosition ) );
}

void main() {
	vec2 texCoord = inPackedTexCoord * pc.parameters.y;
	fragmentTexCoord = vec2(
		dot( pc.textureMatrixS.xyz, vec3( texCoord, 1.0 ) ),
		dot( pc.textureMatrixT.xyz, vec3( texCoord, 1.0 ) )
	);
	fragmentColor = pc.color * mix( vec4( 1.0 ), inColor, pc.parameters.x );
	gl_Position = pc.modelViewProjection * vec4( skinPosition( inPosition ), 1.0 );
}
