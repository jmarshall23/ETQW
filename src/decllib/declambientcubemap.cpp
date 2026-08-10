// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "declAmbientCubeMap.h"
#include "declTypeHolder.h"

#pragma hdrstop

float sdDeclAmbientCubeMap::cubeMapDataFloat[ 6 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4 ];
byte sdDeclAmbientCubeMap::cubeMapDataByte[ 6 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4 ];
byte sdDeclAmbientCubeMap::gradientMapData[ GRADIENT_SIZE * 4 ];

float* sdDeclAmbientCubeMap::cubeMapFloat[ 6 ] = {
	cubeMapDataFloat + 0 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataFloat + 1 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataFloat + 2 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataFloat + 3 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataFloat + 4 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataFloat + 5 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4
};

byte* sdDeclAmbientCubeMap::cubeMapByte[ 6 ] = {
	cubeMapDataByte + 0 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataByte + 1 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataByte + 2 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataByte + 3 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataByte + 4 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4,
	cubeMapDataByte + 5 * BAKEDLIGHT_SIZE * BAKEDLIGHT_SIZE * 4
};

sdDeclAmbientCubeMap::sdDeclAmbientCubeMap() :
	indoors( false ),
	brightness( 1.0f ),
	ambientCubeMap( NULL ),
	lightCubeMap( NULL ),
	specularCubeMap( NULL ),
	environmentCubeMap( NULL ),
	gradientMap( NULL ) {
	ambientCubeMapImageFunctor.Init( this, &sdDeclAmbientCubeMap::AmbientCubeMapImage );
	lightCubeMapImageFunctor.Init( this, &sdDeclAmbientCubeMap::LightCubeMapImage );
	specularCubeMapImageFunctor.Init( this, &sdDeclAmbientCubeMap::SpecularCubeMapImage );
	gradientMapImageFunctor.Init( this, &sdDeclAmbientCubeMap::GradientMapImage );
	FreeData();
}

const char* sdDeclAmbientCubeMap::DefaultDefinition( void ) const {
	return "{}";
}

void sdDeclAmbientCubeMap::FreeData() {
	ambientLights.Clear();
	indoors = false;
	envMap.Clear();
	ambientColor.Set( 0.5f, 0.5f, 0.5f );
	highLightColor.Set( 0.8f, 0.8f, 0.8f );
	minSpecAmbientColor.Set( 0.6f, 0.6f, 0.6f );
	minSpecShadowColor.Set( 0.5f, 0.5f, 0.5f );
	brightness = 1.0f;
	sunDirection.Zero();
	sunColor.Zero();
	avgAmbientColor.Zero();
	ambientCubeMap = NULL;
	lightCubeMap = NULL;
	specularCubeMap = NULL;
	environmentCubeMap = NULL;
	gradientMap = NULL;
}

void sdDeclAmbientCubeMap::CacheFromDict( const idDict& dict ) {
	const idKeyValue* keyValue = NULL;
	while ( ( keyValue = dict.MatchPrefix( "ambientCubeMap", keyValue ) ) != NULL ) {
		if ( keyValue->GetValue().Length() != 0 ) {
			declManager->MediaPrint( "Precaching ambient cube map %s\n", keyValue->GetValue().c_str() );
			declHolder.FindAmbientCubeMap( keyValue->GetValue(), false );
		}
	}
}

bool sdDeclAmbientCubeMap::ParseAmbientLight( idParser* src ) {
	ambientLight_t light;
	light.dir.Set( 1.0f, 0.0f, 0.0f );
	light.color.Set( 1.0f, 1.0f, 1.0f );
	light.specular = true;
	light.ambient = true;

	idToken token;
	if ( !src->ExpectTokenString( "{" ) ) {
		return false;
	}
	while ( src->ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( light.name.IsEmpty() ) {
				light.name = va( "Light%i", ambientLights.Num() );
			}
			ambientLights.Append( light );
			return true;
		}
		if ( token.Icmp( "color" ) == 0 ) {
			light.color.x = src->ParseFloat();
			light.color.y = src->ParseFloat();
			light.color.z = src->ParseFloat();
		} else if ( token.Icmp( "direction" ) == 0 ) {
			light.dir.x = src->ParseFloat();
			light.dir.y = src->ParseFloat();
			light.dir.z = src->ParseFloat();
			light.dir.Normalize();
		} else if ( token.Icmp( "brightness" ) == 0 ) {
			const float lightBrightness = src->ParseFloat();
			light.color *= lightBrightness;
		} else if ( token.Icmp( "ambient" ) == 0 ) {
			light.ambient = src->ParseBool();
		} else if ( token.Icmp( "specular" ) == 0 ) {
			light.specular = src->ParseBool();
		} else if ( token.Icmp( "name" ) == 0 ) {
			if ( !src->ReadToken( &token ) ) {
				return false;
			}
			light.name = token;
		} else {
			src->Warning( "sdDeclAmbientCubeMap::ParseAmbientLight: Unknown token %s", token.c_str() );
			return false;
		}
	}
	return false;
}

bool sdDeclAmbientCubeMap::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );
	FreeData();

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			GenerateImages();
			return true;
		}
		if ( token.Icmp( "ambientLight" ) == 0 ) {
			if ( !ParseAmbientLight( &src ) ) {
				return false;
			}
		} else if ( token.Icmp( "indoors" ) == 0 ) {
			indoors = true;
		} else if ( token.Icmp( "envMap" ) == 0 ) {
			if ( !src.ReadToken( &token ) ) {
				return false;
			}
			envMap = token;
		} else if ( token.Icmp( "ambientColor" ) == 0 ) {
			ambientColor.x = src.ParseFloat();
			ambientColor.y = src.ParseFloat();
			ambientColor.z = src.ParseFloat();
		} else if ( token.Icmp( "minSpecAmbientColor" ) == 0 ) {
			minSpecAmbientColor.x = src.ParseFloat();
			minSpecAmbientColor.y = src.ParseFloat();
			minSpecAmbientColor.z = src.ParseFloat();
		} else if ( token.Icmp( "minSpecShadowColor" ) == 0 ) {
			minSpecShadowColor.x = src.ParseFloat();
			minSpecShadowColor.y = src.ParseFloat();
			minSpecShadowColor.z = src.ParseFloat();
		} else if ( token.Icmp( "brightness" ) == 0 ) {
			brightness = src.ParseFloat();
		} else if ( token.Icmp( "highLightColor" ) == 0 ) {
			highLightColor.x = src.ParseFloat();
			highLightColor.y = src.ParseFloat();
			highLightColor.z = src.ParseFloat();
		} else {
			src.Warning( "sdDeclAmbientCubeMap::Parse : Unknown token: %s", token.c_str() );
			return false;
		}
	}
	return false;
}

void sdDeclAmbientCubeMap::SetSunParameters( const idVec3& direction, const idVec3& color ) {
	if ( indoors ) {
		common->Warning( "sdDeclAmbientCubeMap::SetSunParameters : called on indoors ambient cube map '%s'", GetName() );
		return;
	}
	sunDirection = direction;
	sunColor = color;
	GenerateImages();
}

void sdDeclAmbientCubeMap::GenerateImages() {
	idVec3 total = ambientColor;
	for ( int i = 0; i < ambientLights.Num(); i++ ) {
		if ( ambientLights[ i ].ambient ) {
			total += ambientLights[ i ].color;
		}
	}
	avgAmbientColor.Set( total.x, total.y, total.z, 1.0f );

	if ( globalImages == NULL ) {
		return;
	}
	ambientCubeMap = globalImages->ImageFromFunction(
		va( "_ambientCubeMap_%s", GetName() ), ambientCubeMapImageFunctor );
	lightCubeMap = globalImages->ImageFromFunction(
		va( "_lightCubeMap_%s", GetName() ), lightCubeMapImageFunctor );
	specularCubeMap = globalImages->ImageFromFunction(
		va( "_specularCubeMap_%s", GetName() ), specularCubeMapImageFunctor );
	gradientMap = globalImages->ImageFromFunction(
		va( "_gradientMap_%s", GetName() ), gradientMapImageFunctor );
	if ( !envMap.IsEmpty() ) {
		imageParams_t parms;
		parms.cubeMap = CF_NATIVE;
		environmentCubeMap = globalImages->ImageFromFile( envMap, parms );
	}
}

bool sdDeclAmbientCubeMap::RebuildTextSource() {
	idFile_Memory file( va( "ambientCubeMap %s", GetName() ) );
	file.WriteFloatString( "ambientCubeMap %s {\n", GetName() );
	if ( indoors ) {
		file.WriteFloatString( "\tindoors\n" );
	}
	if ( !envMap.IsEmpty() ) {
		file.WriteFloatString( "\tenvMap \"%s\"\n", envMap.c_str() );
	}
	file.WriteFloatString( "\tambientColor %f %f %f\n", ambientColor.x, ambientColor.y, ambientColor.z );
	file.WriteFloatString( "\thighLightColor %f %f %f\n", highLightColor.x, highLightColor.y, highLightColor.z );
	file.WriteFloatString( "\tminSpecAmbientColor %f %f %f\n",
		minSpecAmbientColor.x, minSpecAmbientColor.y, minSpecAmbientColor.z );
	file.WriteFloatString( "\tminSpecShadowColor %f %f %f\n",
		minSpecShadowColor.x, minSpecShadowColor.y, minSpecShadowColor.z );
	file.WriteFloatString( "\tbrightness %f\n", brightness );
	for ( int i = 0; i < ambientLights.Num(); i++ ) {
		const ambientLight_t& light = ambientLights[ i ];
		file.WriteFloatString( "\tambientLight {\n" );
		file.WriteFloatString( "\t\tname \"%s\"\n", light.name.c_str() );
		file.WriteFloatString( "\t\tdirection %f %f %f\n", light.dir.x, light.dir.y, light.dir.z );
		file.WriteFloatString( "\t\tcolor %f %f %f\n", light.color.x, light.color.y, light.color.z );
		file.WriteFloatString( "\t\tambient %i\n", light.ambient ? 1 : 0 );
		file.WriteFloatString( "\t\tspecular %i\n", light.specular ? 1 : 0 );
		file.WriteFloatString( "\t}\n" );
	}
	file.WriteFloatString( "}\n" );
	SetText( file.GetDataPtr() );
	return true;
}

bool sdDeclAmbientCubeMap::Save() {
	if ( !RebuildTextSource() ) {
		return false;
	}
	return ReplaceSourceFileText();
}

void sdDeclAmbientCubeMap::ClearCubeMap( float* cubeMap[ 6 ], const int faceSize ) {
	for ( int face = 0; face < 6; face++ ) {
		memset( cubeMap[ face ], 0, faceSize * faceSize * 4 * sizeof( float ) );
	}
}

void sdDeclAmbientCubeMap::ScaleCubeMapColor( float* cubeMap[ 6 ], const int faceSize, const float scale ) {
	for ( int face = 0; face < 6; face++ ) {
		for ( int i = 0; i < faceSize * faceSize * 3; i++ ) {
			cubeMap[ face ][ i ] *= scale;
		}
	}
}

void sdDeclAmbientCubeMap::CubeMapFtob( float* source[ 6 ], byte* destination[ 6 ], const int faceSize ) {
	for ( int face = 0; face < 6; face++ ) {
		for ( int i = 0; i < faceSize * faceSize * 4; i++ ) {
			destination[ face ][ i ] = static_cast< byte >(
				idMath::ClampInt( 0, 255, idMath::Ftoi( source[ face ][ i ] * 255.0f ) ) );
		}
	}
}

void sdDeclAmbientCubeMap::BakeLight( float* cubeMap[ 6 ], const int faceSize,
		const idVec3& lightDir, const idVec3& lightColor ) {
	BakeLight( cubeMap, faceSize, lightDir, lightColor, 1.0f );
}

void sdDeclAmbientCubeMap::BakeLight( float* cubeMap[ 6 ], const int faceSize,
		const idVec3& lightDir, const idVec3& lightColor, const float power ) {
	const float contribution = Max( 0.0f, lightDir.z ) * power;
	for ( int face = 0; face < 6; face++ ) {
		for ( int pixel = 0; pixel < faceSize * faceSize; pixel++ ) {
			float* color = cubeMap[ face ] + pixel * 4;
			color[ 0 ] += lightColor.x * contribution;
			color[ 1 ] += lightColor.y * contribution;
			color[ 2 ] += lightColor.z * contribution;
			color[ 3 ] = 1.0f;
		}
	}
}

void sdDeclAmbientCubeMap::BakeGradientMap( byte* pic, const int size,
		const idVec3& ambient, const idVec3& highlight ) {
	for ( int i = 0; i < size; i++ ) {
		const float fraction = size > 1 ? static_cast< float >( i ) / ( size - 1 ) : 0.0f;
		for ( int channel = 0; channel < 3; channel++ ) {
			const float value = ambient[ channel ] * ( 1.0f - fraction ) + highlight[ channel ] * fraction;
			pic[ i * 4 + channel ] = static_cast< byte >(
				idMath::ClampInt( 0, 255, idMath::Ftoi( value * 255.0f ) ) );
		}
		pic[ i * 4 + 3 ] = 255;
	}
}

void sdDeclAmbientCubeMap::UploadCubeMap( idImage*, const byte* [ 6 ], const int ) {
}

void sdDeclAmbientCubeMap::AmbientCubeMapImage( idImage* ) {
}

void sdDeclAmbientCubeMap::LightCubeMapImage( idImage* ) {
}

void sdDeclAmbientCubeMap::SpecularCubeMapImage( idImage* ) {
}

void sdDeclAmbientCubeMap::GradientMapImage( idImage* ) {
}
