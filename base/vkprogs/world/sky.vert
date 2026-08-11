#version 450

layout( push_constant ) uniform SkyPushConstants {
	mat4 unusedMatrix;
	vec4 fogColor;
	vec4 viewParameters;
	vec4 unused0;
	vec4 unused1;
} pc;

layout( location = 0 ) out vec2 fragmentTexCoord;

void main() {
	const vec2 positions[ 3 ] = vec2[](
		vec2( -1.0, -1.0 ),
		vec2(  3.0, -1.0 ),
		vec2( -1.0,  3.0 )
	);
	vec2 position = positions[ gl_VertexIndex ];
	fragmentTexCoord = position * 0.5 + 0.5;
	gl_Position = vec4( position, 0.999999, 1.0 );
}
