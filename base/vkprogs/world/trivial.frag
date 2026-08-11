#version 450

layout( set = 0, binding = 0 ) uniform sampler2D colorTexture;

layout( push_constant ) uniform TrivialPushConstants {
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
	vec4 sampled = texture( colorTexture, fragmentTexCoord );
	if ( pc.parameters.w > 0.5 && sampled.a < pc.parameters.z ) {
		discard;
	}
	outColor = sampled * fragmentColor;
}
