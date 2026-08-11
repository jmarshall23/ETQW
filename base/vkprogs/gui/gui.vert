#version 450

layout( location = 0 ) in vec2 inPosition;
layout( location = 1 ) in vec2 inTexCoord;

layout( push_constant ) uniform GuiPushConstants {
	vec4 transform;
	vec4 color;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out vec4 fragmentColor;

void main() {
	gl_Position = vec4( inPosition * pc.transform.xy + pc.transform.zw, 0.0, 1.0 );
	fragmentTexCoord = inTexCoord;
	fragmentColor = pc.color;
}
