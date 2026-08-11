#version 450

layout( set = 0, binding = 0 ) uniform sampler2D bumpTexture;
layout( set = 0, binding = 1 ) uniform sampler2D currentRenderTexture;

layout( push_constant ) uniform HeatHazePushConstants {
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
	vec2 mask = fragmentColor.rg;
	if ( min( mask.x, mask.y ) < 0.01 ) {
		discard;
	}
	vec4 packedNormal = texture( bumpTexture, fragmentTexCoord );
	// ETQW's non-DXN normal maps store X in alpha and Y in green.
	vec2 localNormal = vec2( packedNormal.a, packedNormal.g ) * 2.0 - 1.0;
	vec2 framebufferSize = vec2( textureSize( currentRenderTexture, 0 ) );
	vec2 screenTexCoord = gl_FragCoord.xy / framebufferSize;
	vec2 distortedTexCoord = clamp(
		screenTexCoord + localNormal * mask * 0.02, vec2( 0.0 ), vec2( 1.0 ) );
	outColor = vec4( texture( currentRenderTexture, distortedTexCoord ).rgb, 1.0 );
}
