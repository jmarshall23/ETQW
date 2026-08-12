#version 450

layout( set = 0, binding = 0 ) uniform sampler2D decalTexture;

layout( push_constant ) uniform DecalPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 textureMatrixS;
	vec4 textureMatrixT;
	vec4 parameters;
} pc;

layout( location = 0 ) in vec2 fragmentTexCoord;
layout( location = 1 ) in vec4 fragmentColor;
layout( location = 0 ) out vec4 outColor;

void main() {
	vec3 decal = texture( decalTexture, fragmentTexCoord ).rgb *
		fragmentColor.rgb;
	// ETQW filterCoverage/filterVertexColorCoverage fades a multiplicative
	// decal toward the neutral multiplier (white). Alpha is deliberately not
	// involved: the fixed-function blend is DST_COLOR, ZERO.
	float coverage = clamp( pc.parameters.z, 0.0, 1.0 );
	outColor = vec4( mix( vec3( 1.0 ), decal, coverage ), 1.0 );
}
