#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec4 inColor;
layout( location = 2 ) in vec2 inPackedTexCoord;
layout( location = 3 ) in vec2 inPackedNormal;
layout( location = 4 ) in uint inNormalSign;

layout( push_constant ) uniform MegaPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 megaMaskParams;
	vec4 megaLevelParams;
	vec4 parameters;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out vec2 fragmentMaskCoord;
layout( location = 2 ) out vec4 fragmentColor;
layout( location = 3 ) out float fragmentLighting;

vec3 decodeOctahedron( vec2 encoded ) {
	vec3 value = vec3( encoded, 1.0 - abs( encoded.x ) - abs( encoded.y ) );
	if ( value.z < 0.0 ) {
		value.xy = ( 1.0 - abs( value.yx ) ) * sign( value.xy );
	}
	return normalize( value );
}

void main() {
	vec2 texCoord = inPackedTexCoord * pc.parameters.y;
	fragmentTexCoord = texCoord * pc.megaLevelParams.x;
	fragmentMaskCoord = vec2( 0.5 ) -
		( texCoord * pc.megaMaskParams.w + pc.megaMaskParams.xy );
	fragmentColor = pc.color * mix( vec4( 1.0 ), inColor, pc.parameters.x );
	float normalZ = sqrt( max( 0.0, 1.0 - dot( inPackedNormal, inPackedNormal ) ) );
	if ( inNormalSign == 0u ) {
		normalZ = -normalZ;
	}
	vec3 normal = normalize( vec3( inPackedNormal, normalZ ) );
	vec3 sunDirection = decodeOctahedron( pc.parameters.zw );
	fragmentLighting = 0.32 + 0.68 * max( dot( normal, sunDirection ), 0.0 );
	gl_Position = pc.modelViewProjection * vec4( inPosition, 1.0 );
}
