#version 450

layout( set = 0, binding = 0 ) uniform sampler2D guiTexture;

layout( location = 0 ) in vec2 fragmentTexCoord;
layout( location = 1 ) in vec4 fragmentColor;

layout( location = 0 ) out vec4 outColor;

void main() {
	outColor = texture( guiTexture, fragmentTexCoord ) * fragmentColor;
}
