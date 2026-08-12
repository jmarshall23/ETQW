#version 450

layout( set = 0, binding = 0 ) uniform sampler2D waterNormalTexture;
layout( set = 0, binding = 1 ) uniform samplerCube environmentTexture;
layout( set = 0, binding = 2 ) uniform sampler2D waterSpecularTexture;
layout( set = 0, binding = 3 ) uniform sampler2D waterNormalTexture2;
layout( set = 0, binding = 4 ) uniform sampler2D currentRenderTexture;

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
layout( location = 4 ) in vec3 fragmentPosition;
layout( location = 0 ) out vec4 outColor;

vec3 safeNormalize( vec3 value, vec3 fallbackValue ) {
	float lengthSquared = dot( value, value );
	return lengthSquared > 0.0000001 && !isnan( lengthSquared ) &&
		!isinf( lengthSquared ) ? value * inversesqrt( lengthSquared ) :
		fallbackValue;
}

vec3 decodeWaterNormal( vec4 packedNormal ) {
	vec2 tangentXY = clamp( vec2( packedNormal.a, packedNormal.g ),
		vec2( 0.0 ), vec2( 1.0 ) ) * 2.0 - 1.0;
	float tangentZ = sqrt( max( 0.0, 1.0 - dot( tangentXY, tangentXY ) ) );
	return safeNormalize( vec3( tangentXY, tangentZ ), vec3( 0.0, 0.0, 1.0 ) );
}

void main() {
	float frameLerp = clamp( pc.parameters.x, 0.0, 1.0 );
	vec3 primaryNormal = safeNormalize( mix(
		decodeWaterNormal( texture( waterNormalTexture, fragmentTexCoord ) ),
		decodeWaterNormal( texture( waterNormalTexture2, fragmentTexCoord ) ),
		frameLerp ), vec3( 0.0, 0.0, 1.0 ) );

	// A rotated, finer sample adds small crossing ripples without fighting the
	// authored frame animation.  Because the material scrolls fragmentTexCoord,
	// the rotated layer travels in a different direction automatically.
	vec2 detailCoord = vec2( -fragmentTexCoord.y, fragmentTexCoord.x ) * 1.37 +
		vec2( 0.19, 0.43 );
	vec3 detailNormal = safeNormalize( mix(
		decodeWaterNormal( texture( waterNormalTexture, detailCoord ) ),
		decodeWaterNormal( texture( waterNormalTexture2, detailCoord ) ),
		frameLerp ), vec3( 0.0, 0.0, 1.0 ) );
	// Keep the surface gently animated when a retail precompressed normal frame
	// is absent.  The material's scrolling texture matrix moves these crossing
	// waves, while their low amplitude preserves the authored normals when they
	// are available.
	vec2 waveCoord = fragmentTexCoord * 6.2831853;
	vec2 proceduralRipple = vec2(
		sin( waveCoord.x * 0.73 + waveCoord.y * 1.17 ) +
			sin( waveCoord.x * 1.91 - waveCoord.y * 0.46 ),
		cos( waveCoord.y * 0.81 - waveCoord.x * 1.29 ) +
			cos( waveCoord.y * 1.63 + waveCoord.x * 0.38 ) ) * 0.055;
	float bumpScale = clamp( pc.parameters.w, 0.2, 2.0 );
	vec3 tangentNormal = safeNormalize( vec3(
		( primaryNormal.xy + detailNormal.xy * 0.34 + proceduralRipple ) *
			bumpScale,
		max( 0.2, primaryNormal.z * detailNormal.z ) ),
		vec3( 0.0, 0.0, 1.0 ) );

	// Rebuild a stable orthonormal basis per fragment instead of trusting the
	// legacy bitangent.  Several Valley water triangles have parallel or zero
	// tangent data, which otherwise turns the complete color calculation NaN.
	vec3 geometricNormal = safeNormalize( fragmentNormal, vec3( 0.0, 0.0, 1.0 ) );
	vec3 tangentCandidate = fragmentTangent - geometricNormal *
		dot( fragmentTangent, geometricNormal );
	vec3 referenceAxis = abs( geometricNormal.z ) < 0.95 ?
		vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 );
	vec3 fallbackTangent = safeNormalize( cross( referenceAxis, geometricNormal ),
		vec3( 1.0, 0.0, 0.0 ) );
	vec3 surfaceTangent = safeNormalize( tangentCandidate, fallbackTangent );
	vec3 surfaceBitangent = safeNormalize(
		cross( geometricNormal, surfaceTangent ), vec3( 0.0, 1.0, 0.0 ) );
	vec3 surfaceNormal = safeNormalize(
		surfaceTangent * tangentNormal.x +
		surfaceBitangent * tangentNormal.y +
		geometricNormal * tangentNormal.z, geometricNormal );
	vec3 viewDirection = safeNormalize( pc.color.xyz - fragmentPosition,
		geometricNormal );
	if ( dot( surfaceNormal, viewDirection ) < 0.0 ) {
		surfaceNormal = -surfaceNormal;
	}

	float viewFacing = clamp( dot( surfaceNormal, viewDirection ), 0.0, 1.0 );
	float fresnelPower = clamp( pc.textureMatrixT.w, 1.0, 12.0 );
	float fresnel = 0.055 + 0.945 * pow( 1.0 - viewFacing, fresnelPower );
	vec3 reflectionDirection = reflect( -viewDirection, surfaceNormal );
	// Some legacy environment maps contain values well above display range (and
	// the fallback cube is white).  Compress them before tinting so reflections
	// add sky detail without turning the entire water sheet white.
	vec3 environmentSample = max(
		texture( environmentTexture, reflectionDirection ).rgb, vec3( 0.0 ) );
	vec3 environment = environmentSample / ( vec3( 1.0 ) + environmentSample );
	environment *= vec3( 0.30, 0.60, 0.68 );

	// The original water program is a refraction material: its fifth stage is
	// the opaque scene captured immediately before the refraction sort phase.
	// Distort in screen space with the animated tangent normal, keeping a
	// one-texel guard so filtering never samples beyond the captured image.
	vec2 framebufferSize = max( vec2( textureSize( currentRenderTexture, 0 ) ),
		vec2( 1.0 ) );
	vec2 texelSize = vec2( 1.0 ) / framebufferSize;
	vec2 screenTexCoord = gl_FragCoord.xy / framebufferSize;
	float distortionStrength = clamp( abs( pc.textureMatrixS.w ) * 0.48,
		0.0, 0.035 );
	vec2 refractedTexCoord = clamp( screenTexCoord +
		tangentNormal.xy * distortionStrength, texelSize,
		vec2( 1.0 ) - texelSize );
	vec3 refractedScene = max(
		texture( currentRenderTexture, refractedTexCoord ).rgb, vec3( 0.0 ) );
	// Gracefully compress HDR-ish scene values before tinting.  This also keeps
	// a pale riverbed from washing the surface out to featureless white.
	refractedScene /= vec3( 1.0 ) + max( refractedScene - vec3( 1.0 ),
		vec3( 0.0 ) );

	vec3 deepWater = vec3( 0.018, 0.14, 0.20 );
	vec3 lagoonWater = vec3( 0.055, 0.42, 0.47 );
	vec3 waterBody = mix( deepWater, lagoonWater,
		0.42 + 0.34 * clamp( surfaceNormal.z, 0.0, 1.0 ) );
	float lookupSpecular = texture( waterSpecularTexture,
		vec2( viewFacing, 0.5 ) ).r;
	vec3 sunDirection = normalize( vec3( 0.35, 0.45, 0.82 ) );
	vec3 halfDirection = safeNormalize( sunDirection + viewDirection,
		sunDirection );
	float glare = pow( max( dot( surfaceNormal, halfDirection ), 0.0 ), 84.0 ) *
		clamp( pc.parameters.z, 0.0, 1.5 ) * 0.10;
	float rippleCrest = smoothstep( 0.20, 0.72, length( tangentNormal.xy ) );
	vec3 mintCrest = vec3( 0.14, 0.66, 0.68 ) * rippleCrest * 0.075;
	float reflectionAmount = mix( 0.05, 0.22, clamp( fresnel, 0.0, 1.0 ) );
	vec3 tintedRefraction = mix( refractedScene, waterBody, 0.34 );
	vec3 color = mix( tintedRefraction, environment, reflectionAmount ) +
		mintCrest + vec3( 0.42, 0.88, 0.92 ) * glare *
		( 0.18 + 0.18 * lookupSpecular );
	// Preserve the readable teal silhouette even over Valley's pale riverbed.
	// Highlights may brighten the water, but never desaturate it to white.
	color = clamp( color, vec3( 0.0 ), vec3( 0.56, 0.78, 0.82 ) );
	if ( any( isnan( color ) ) || any( isinf( color ) ) ) {
		color = vec3( 0.045, 0.31, 0.38 );
	}
	float alpha = clamp( pc.color.w + 0.10 + fresnel * 0.08 +
		rippleCrest * 0.025, 0.86, 0.96 );
	outColor = vec4( color, alpha );
}
