#version 450

layout( set = 0, binding = 0 ) uniform sampler2D skySideTexture;
layout( set = 0, binding = 1 ) uniform sampler2D skyTopTexture;

layout( push_constant ) uniform SkyboxPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 textureMatrixS;
	vec4 textureMatrixT;
	vec4 parameters;
} pc;

layout( location = 0 ) in vec2 fragmentTexCoord;
layout( location = 1 ) in float fragmentSideBlend;
layout( location = 0 ) out vec4 outColor;

void main() {
	// This is the retail skies/skybox composition.  ETQW's box-dome mesh uses
	// vertex red to feather between a square zenith texture and a four-face
	// horizontal strip; treating either image as a regular diffuse map exposes
	// the atlas and produces the broken sky seen in the Vulkan fallback.
	vec4 top = texture( skyTopTexture, fragmentTexCoord );
	// The source stage is clamp_x: horizontal atlas edges clamp while the
	// vertical coordinate repeats.  The generic Vulkan image sampler cannot
	// express that mixed legacy mode, so preserve its V wrapping explicitly.
	vec2 sideTexCoord = fragmentTexCoord + vec2( 0.75, 1.0 );
	sideTexCoord.y = fract( sideTexCoord.y );
	vec4 side = texture( skySideTexture, sideTexCoord );
	outColor = mix( top, side, fragmentSideBlend ) * pc.color;
}
