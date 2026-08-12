#version 450

layout( set = 0, binding = 0 ) uniform sampler2D levelTexture;

layout( push_constant ) uniform MegaPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 megaMaskParams;
	vec4 megaLevelParams;
	vec4 parameters;
} pc;

layout( location = 0 ) in vec2 fragmentTexCoord;
layout( location = 1 ) in vec2 fragmentMaskCoord;
layout( location = 2 ) in vec4 fragmentColor;
layout( location = 3 ) in float fragmentLighting;
layout( location = 0 ) out vec4 outColor;

void main() {
	vec4 sampled = texture( levelTexture, fragmentTexCoord );
	float weight = pc.megaLevelParams.y;
	if ( pc.megaLevelParams.z > 0.5 ) {
		float edgeDistance = max( abs( fragmentMaskCoord.x ),
			abs( fragmentMaskCoord.y ) );
		weight *= clamp( -32.0 * edgeDistance + 16.0, 0.0, 1.0 );
	}
	outColor = vec4( sampled.rgb * fragmentColor.rgb, weight );
}
