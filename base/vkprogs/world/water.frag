#version 450

layout( set = 0, binding = 0 ) uniform sampler2D waterNormalTexture;
layout( set = 0, binding = 1 ) uniform samplerCube environmentTexture;
layout( set = 0, binding = 2 ) uniform sampler2D waterSpecularTexture;

layout( push_constant ) uniform WaterPushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 textureMatrixS;
	vec4 textureMatrixT;
	vec4 parameters;
} pc;

layout( location = 0 ) in vec2 fragmentTexCoord;
layout( location = 1 ) in vec3 fragmentNormal;
layout( location = 2 ) in vec3 fragmentTangent;
layout( location = 3 ) in vec3 fragmentBitangent;
layout( location = 0 ) out vec4 outColor;

void main() {
	vec4 packedNormal = texture( waterNormalTexture, fragmentTexCoord );
	vec2 tangentXY = vec2( packedNormal.a, packedNormal.g ) * 2.0 - 1.0;
	float tangentZ = sqrt( max( 0.0, 1.0 - dot( tangentXY, tangentXY ) ) );
	vec3 tangentNormal = normalize( vec3( tangentXY, tangentZ ) );
	vec3 environmentDirection = normalize(
		fragmentTangent * tangentNormal.x +
		fragmentBitangent * tangentNormal.y +
		fragmentNormal * abs( tangentNormal.z ) );
	vec3 environment = texture( environmentTexture, environmentDirection ).rgb;
	float specular = texture( waterSpecularTexture,
		vec2( clamp( 1.0 - abs( environmentDirection.z ), 0.0, 1.0 ), 0.5 ) ).r;
	vec3 waterTint = mix( vec3( 0.035, 0.09, 0.11 ), environment, 0.72 );
	outColor = vec4( waterTint + specular * 0.16, 0.76 );
}
