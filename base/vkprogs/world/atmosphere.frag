#version 450

layout( push_constant ) uniform AtmospherePushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 localViewOrigin;
	vec4 unused0;
	vec4 parameters;
} pc;

layout( location = 0 ) in float fragmentRim;
layout( location = 0 ) out vec4 outColor;

void main() {
	float intensity = fragmentRim * pc.color.a * 0.65;
	outColor = vec4( pc.color.rgb * intensity, intensity );
}
