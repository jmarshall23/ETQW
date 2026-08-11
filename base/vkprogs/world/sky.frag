#version 450

layout( set = 0, binding = 0 ) uniform sampler2D skyGradient;

layout( push_constant ) uniform SkyPushConstants {
	mat4 unusedMatrix;
	vec4 fogColor;
	vec4 viewParameters;
	vec4 unused0;
	vec4 unused1;
} pc;

layout( location = 0 ) in vec2 fragmentTexCoord;
layout( location = 0 ) out vec4 outColor;

void main() {
	// The retail atmosphere images are authored as a horizon-to-zenith
	// gradient.  Keep the horizon fog-colored while retaining the authored
	// gradient over the rest of the sky.
	float height = clamp( fragmentTexCoord.y, 0.0, 1.0 );
	vec3 gradient = texture( skyGradient, vec2( 0.5, height ) ).rgb;
	float horizon = smoothstep( 0.0, 0.35, height );
	outColor = vec4( mix( pc.fogColor.rgb, gradient, horizon ), 1.0 );
}
