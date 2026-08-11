#version 450

layout( location = 0 ) in vec3 inPosition;
layout( location = 3 ) in vec2 inPackedNormal;
layout( location = 4 ) in uint inNormalSign;

layout( push_constant ) uniform AtmospherePushConstants {
	mat4 modelViewProjection;
	vec4 color;
	vec4 localViewOrigin;
	vec4 unused0;
	vec4 parameters;
} pc;

layout( location = 0 ) out float fragmentRim;

void main() {
	float normalZ = sqrt( max( 0.0, 1.0 - dot( inPackedNormal, inPackedNormal ) ) );
	if ( inNormalSign == 0u ) {
		normalZ = -normalZ;
	}
	vec3 normal = normalize( vec3( inPackedNormal, normalZ ) );
	vec3 viewDirection = normalize( pc.localViewOrigin.xyz - inPosition );
	float facing = clamp( abs( dot( normal, viewDirection ) ), 0.0, 1.0 );
	fragmentRim = pow( 1.0 - facing, 2.5 );
	gl_Position = pc.modelViewProjection * vec4( inPosition, 1.0 );
}
