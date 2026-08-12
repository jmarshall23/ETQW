#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec4 inColor;
layout( location = 2 ) in vec2 inPackedTexCoord;

layout( push_constant ) uniform SkyboxPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 textureMatrixS;
	vec4 textureMatrixT;
	vec4 parameters;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;
layout( location = 1 ) out float fragmentSideBlend;

void main() {
	vec2 texCoord = inPackedTexCoord * pc.parameters.y;
	fragmentTexCoord = vec2(
		dot( pc.textureMatrixS.xyz, vec3( texCoord, 1.0 ) ),
		dot( pc.textureMatrixT.xyz, vec3( texCoord, 1.0 ) )
	);
	fragmentSideBlend = clamp( inColor.r, 0.0, 1.0 );
	gl_Position = pc.modelViewProjection * vec4( inPosition, 1.0 );
}
