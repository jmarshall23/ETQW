#version 460

layout( location = 0 ) in vec4 inPosition;
layout( location = 1 ) in vec2 inTexCoord;
layout( location = 2 ) in vec4 inColor;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out vec4 fragmentColor;

void main() {
	gl_Position = inPosition;
	fragmentTexCoord = inTexCoord;
	fragmentColor = inColor;
}
