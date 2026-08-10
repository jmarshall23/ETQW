// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "../renderer/Image.h"
#include "../renderer/ModelManager.h"
#include "declAtmosphere.h"
#include "declTypeHolder.h"

#pragma hdrstop

void sdPrecipitationParameters::Default() {
	material = declHolder.FindMaterial( "_default", true );
	model = NULL;
	effect = NULL;
	timeMin = 0.0f;
	timeMax = 500.0f;
	precipitationDistance = 1000.0f;

	switch ( preType ) {
		case PT_RAIN:
			maxParticles = 4000;
			heightMin = 50.0f;
			heightMax = 250.0f;
			weightMin = 1.5f;
			weightMax = 2.5f;
			windScale = 70.0f;
			gustWindScale = 100.0f;
			fallMin = 700.0f;
			fallMax = 900.0f;
			tumbleStrength = 0.0f;
			break;
		case PT_SNOW:
			maxParticles = 4000;
			heightMin = 3.0f;
			heightMax = 5.0f;
			weightMin = 1.5f;
			weightMax = 2.5f;
			windScale = 36.0f;
			gustWindScale = 40.0f;
			fallMin = 55.0f;
			fallMax = 105.0f;
			tumbleStrength = 24.0f;
			break;
		case PT_SPLASH:
			maxParticles = 4000;
			heightMin = 2.0f;
			heightMax = 3.0f;
			weightMin = 15.0f;
			weightMax = 20.0f;
			windScale = 0.0f;
			gustWindScale = 0.0f;
			fallMin = 5.0f;
			fallMax = 10.0f;
			tumbleStrength = 0.0f;
			break;
		case PT_MODELRAIN:
		case PT_MODELSNOW:
			maxParticles = 0;
			heightMin = heightMax = 0.0f;
			weightMin = weightMax = 0.0f;
			windScale = gustWindScale = 0.0f;
			fallMin = fallMax = 0.0f;
			tumbleStrength = 0.0f;
			model = renderModelManager != NULL
				? renderModelManager->FindModel(
					preType == PT_MODELRAIN ? "models/effects/rain.obj" : "models/effects/snow.obj" )
				: NULL;
			break;
		default:
			preType = PT_NONE;
			maxParticles = 0;
			heightMin = heightMax = 0.0f;
			weightMin = weightMax = 0.0f;
			windScale = gustWindScale = 0.0f;
			fallMin = fallMax = 0.0f;
			tumbleStrength = 0.0f;
			break;
	}
}

bool sdPrecipitationParameters::Parse( idParser& src ) {
	idToken token;
	Default();
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "type" ) == 0 ) {
			preType = static_cast< precipitationType_e >( src.ParseInt() );
		} else if ( token.Icmp( "maxParticles" ) == 0 ) {
			maxParticles = src.ParseInt();
		} else if ( token.Icmp( "heightMin" ) == 0 ) {
			heightMin = src.ParseFloat();
		} else if ( token.Icmp( "heightMax" ) == 0 ) {
			heightMax = src.ParseFloat();
		} else if ( token.Icmp( "weightMin" ) == 0 ) {
			weightMin = src.ParseFloat();
		} else if ( token.Icmp( "weightMax" ) == 0 ) {
			weightMax = src.ParseFloat();
		} else if ( token.Icmp( "timeMin" ) == 0 ) {
			timeMin = src.ParseFloat();
		} else if ( token.Icmp( "timeMax" ) == 0 ) {
			timeMax = src.ParseFloat();
		} else if ( token.Icmp( "windScale" ) == 0 ) {
			windScale = src.ParseFloat();
		} else if ( token.Icmp( "gustWindScale" ) == 0 ) {
			gustWindScale = src.ParseFloat();
		} else if ( token.Icmp( "fallMin" ) == 0 ) {
			fallMin = src.ParseFloat();
		} else if ( token.Icmp( "fallMax" ) == 0 ) {
			fallMax = src.ParseFloat();
		} else if ( token.Icmp( "tumbleStrength" ) == 0 ) {
			tumbleStrength = src.ParseFloat();
		} else if ( token.Icmp( "precipitationDistance" ) == 0 ) {
			precipitationDistance = src.ParseFloat();
		} else if ( token.Icmp( "material" ) == 0 ) {
			src.ReadToken( &token );
			material = declHolder.FindMaterial( token, false );
		} else if ( token.Icmp( "effect" ) == 0 ) {
			src.ReadToken( &token );
			effect = declHolder.FindEffect( token, false );
		} else if ( token.Icmp( "model" ) == 0 ) {
			src.ReadToken( &token );
			model = renderModelManager != NULL ? renderModelManager->FindModel( token ) : NULL;
		} else {
			src.Warning( "sdPrecipitationParameters::Parse : Unknown token: %s", token.c_str() );
			return false;
		}
	}
	return false;
}

void sdPrecipitationParameters::Save( idFile_Memory& file ) const {
	file.WriteFloatString( "\tprecipitation {\n" );
	file.WriteFloatString( "\t\ttype %i\n", preType );
	file.WriteFloatString( "\t\tmaxParticles %i\n", maxParticles );
	file.WriteFloatString( "\t\theightMin %f\n\t\theightMax %f\n", heightMin, heightMax );
	file.WriteFloatString( "\t\tweightMin %f\n\t\tweightMax %f\n", weightMin, weightMax );
	file.WriteFloatString( "\t\twindScale %f\n\t\tgustWindScale %f\n", windScale, gustWindScale );
	file.WriteFloatString( "\t\tfallMin %f\n\t\tfallMax %f\n", fallMin, fallMax );
	file.WriteFloatString( "\t\ttimeMin %f\n\t\ttimeMax %f\n", timeMin, timeMax );
	file.WriteFloatString( "\t\ttumbleStrength %f\n", tumbleStrength );
	file.WriteFloatString( "\t\tprecipitationDistance %f\n", precipitationDistance );
	file.WriteFloatString( "\t}\n" );
}

sdDeclAtmosphere::sdDeclAtmosphere() {
	FreeData();
}

const char* sdDeclAtmosphere::DefaultDefinition( void ) const {
	return "{}";
}

void sdDeclAtmosphere::FreeData() {
	modified = false;
	sunMaterial = declHolder.FindMaterial( "atmospheres/lights/default", true );
	sunDir.Zero();
	sunAzimuth = 0.0f;
	sunZenith = 0.0f;
	sunColor.Zero();
	sunHaloScale = 0.4f;
	sunHaloBias = 0.0f;

	sunSpriteMaterial = declHolder.FindMaterial( "atmospheres/sprites/sundisc", true );
	sunSpriteSize = 12600.0f;
	sunFlareMaterial = declHolder.FindMaterial( "atmospheres/sprites/sundisk_flare", true );
	sunFlareSize = 0.0f;
	sunFlareTime = 0.0f;
	enableSunFlareAziZen = false;
	sunFlareAzi = 0.0f;
	sunFlareZen = 0.0f;

	defaultPostProcessParms.tint.Set( 1.0f, 1.0f, 1.0f );
	defaultPostProcessParms.saturation = 1.0f;
	defaultPostProcessParms.contrast = 1.0f;
	defaultPostProcessParms.glareParms.Set( 0.84f, 1.0f, 0.0f, 1.0f );
	defaultPostProcessParms.glareBases.Set( 0.3f, 0.3f, 0.0f, 1.0f );
	postProcessParms = defaultPostProcessParms;

	fogDistHalf = 8000.0f;
	fogHeightHalf = 400.0f;
	fogHeightOffset = 1000.0f;
	fogColor.Zero();
	fogStart = 30000.0f;
	fogEnd = 40000.0f;

	atmosphereMaterial = declHolder.FindMaterial( "atmospheres/default", true );
	ambientCubeMap = declHolder.FindAmbientCubeMap( "_default", true );
	skyGradientImage = globalImages != NULL ? globalImages->normalCubeMapImage : NULL;
	farClip = 0.0f;
	isNight = false;
	drawAtmosphereLast = true;
	minSpecShadowColor.Set( 0.75f, 0.75f, 0.75f );

	windAngle = 0.0f;
	windAngleDev = 10.0f;
	windStrength = 100.0f;
	windStrengthDev = 20.0f;

	cloudLayers.Clear();
	numPrecipLayers = 0;
	for ( int i = 0; i < NUM_PRECIP_LAYERS; i++ ) {
		precipitation[ i ].preType = sdPrecipitationParameters::PT_NONE;
		precipitation[ i ].Default();
	}
}

void sdDeclAtmosphere::CacheFromDict( const idDict& dict ) {
	const idKeyValue* keyValue = NULL;
	while ( ( keyValue = dict.MatchPrefix( "atmosphere", keyValue ) ) != NULL ) {
		if ( keyValue->GetValue().Length() != 0 ) {
			declHolder.FindAtmosphere( keyValue->GetValue(), false );
		}
	}
}

bool sdDeclAtmosphere::ParsePostProcessParms( idParser& src ) {
	idToken token;
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			postProcessParms = defaultPostProcessParms;
			return true;
		}
		if ( token.Icmp( "tint" ) == 0 ) {
			src.Parse1DMatrix( 3, defaultPostProcessParms.tint.ToFloatPtr() );
		} else if ( token.Icmp( "saturation" ) == 0 ) {
			defaultPostProcessParms.saturation = src.ParseFloat();
		} else if ( token.Icmp( "contrast" ) == 0 ) {
			defaultPostProcessParms.contrast = src.ParseFloat();
		} else if ( token.Icmp( "glareParms" ) == 0 ) {
			src.Parse1DMatrix( 4, defaultPostProcessParms.glareParms.ToFloatPtr() );
		} else if ( token.Icmp( "glareBases" ) == 0 ) {
			src.Parse1DMatrix( 4, defaultPostProcessParms.glareBases.ToFloatPtr() );
		} else {
			src.Warning( "sdDeclAtmosphere::ParsePostProcessParms : Unknown token: %s", token.c_str() );
			return false;
		}
	}
	return false;
}

bool sdDeclAtmosphere::ParseCloudLayer( idParser& src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return false;
	}
	sdCloudLayer layer;
	layer.material = declHolder.FindMaterial( token, true );
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( cloudLayers.Num() >= MAX_CLOUD_LAYERS ) {
				src.Warning( "Too many cloud layers; maximum is %i", MAX_CLOUD_LAYERS );
				return false;
			}
			cloudLayers.Append( layer );
			return true;
		}
		if ( token.Icmp( "style" ) == 0 ) {
			src.ReadToken( &token );
			if ( token.Icmp( "old" ) == 0 ) {
				layer.style = 0;
			} else if ( token.Icmp( "skybox" ) == 0 ) {
				layer.style = 1;
			} else {
				src.Warning( "sdDeclAtmosphere::ParseCloudLayer : Unknown style: %s", token.c_str() );
				return false;
			}
		} else if ( token.Icmp( "parms" ) == 0 ) {
			const int count = idMath::ClampInt( 0, NUM_CLOUD_LAYER_PARAMETERS, src.ParseInt() );
			if ( count != 0 && !src.Parse1DMatrix( count, layer.parms ) ) {
				return false;
			}
		} else {
			src.Warning( "sdDeclAtmosphere::ParseCloudLayer : Unknown token: %s", token.c_str() );
			return false;
		}
	}
	return false;
}

bool sdDeclAtmosphere::ParsePrecipitationLayer( idParser& src ) {
	if ( numPrecipLayers >= NUM_PRECIP_LAYERS ) {
		src.Warning( "Too many precipitation layers" );
		return false;
	}
	if ( !precipitation[ numPrecipLayers ].Parse( src ) ) {
		return false;
	}
	numPrecipLayers++;
	return true;
}

bool sdDeclAtmosphere::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );
	FreeData();

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( ambientCubeMap != NULL ) {
				const_cast< sdDeclAmbientCubeMap* >( ambientCubeMap )->SetSunParameters( sunDir, sunColor );
			}
			modified = false;
			return true;
		}

		if ( token.Icmp( "sunMaterial" ) == 0 ) {
			src.ReadToken( &token );
			sunMaterial = declHolder.FindMaterial( token, true );
		} else if ( token.Icmp( "sunDir" ) == 0 || token.Icmp( "sunDirection" ) == 0 ) {
			src.Parse1DMatrix( 3, sunDir.ToFloatPtr() );
		} else if ( token.Icmp( "sunAzimuth" ) == 0 ) {
			sunAzimuth = src.ParseFloat();
			UpdateSunDirFromAziZen();
		} else if ( token.Icmp( "sunZenith" ) == 0 ) {
			sunZenith = src.ParseFloat();
			UpdateSunDirFromAziZen();
		} else if ( token.Icmp( "sunColor" ) == 0 ) {
			src.Parse1DMatrix( 3, sunColor.ToFloatPtr() );
		} else if ( token.Icmp( "sunHaloScale" ) == 0 ) {
			sunHaloScale = src.ParseFloat();
		} else if ( token.Icmp( "sunHaloBias" ) == 0 ) {
			sunHaloBias = src.ParseFloat();
		} else if ( token.Icmp( "sunSpriteMaterial" ) == 0 ) {
			src.ReadToken( &token );
			sunSpriteMaterial = declHolder.FindMaterial( token, true );
		} else if ( token.Icmp( "sunSpriteSize" ) == 0 ) {
			sunSpriteSize = src.ParseFloat();
		} else if ( token.Icmp( "sunFlareMaterial" ) == 0 ) {
			src.ReadToken( &token );
			sunFlareMaterial = declHolder.FindMaterial( token, true );
		} else if ( token.Icmp( "sunFlareSize" ) == 0 ) {
			sunFlareSize = src.ParseFloat();
		} else if ( token.Icmp( "sunFlareTime" ) == 0 ) {
			sunFlareTime = src.ParseFloat();
		} else if ( token.Icmp( "enableSunFlareAziZen" ) == 0 ) {
			enableSunFlareAziZen = src.ParseBool();
		} else if ( token.Icmp( "sunFlareAzi" ) == 0 ) {
			sunFlareAzi = src.ParseFloat();
		} else if ( token.Icmp( "sunFlareZen" ) == 0 ) {
			sunFlareZen = src.ParseFloat();
		} else if ( token.Icmp( "postProcessParms" ) == 0 || token.Icmp( "postProcess" ) == 0 ) {
			if ( !ParsePostProcessParms( src ) ) {
				return false;
			}
		} else if ( token.Icmp( "fogDistHalf" ) == 0 ) {
			fogDistHalf = src.ParseFloat();
		} else if ( token.Icmp( "fogHeightHalf" ) == 0 ) {
			fogHeightHalf = src.ParseFloat();
		} else if ( token.Icmp( "fogHeightOffset" ) == 0 ) {
			fogHeightOffset = src.ParseFloat();
		} else if ( token.Icmp( "fogColor" ) == 0 ) {
			src.Parse1DMatrix( 3, fogColor.ToFloatPtr() );
		} else if ( token.Icmp( "fogStart" ) == 0 ) {
			fogStart = src.ParseFloat();
		} else if ( token.Icmp( "fogEnd" ) == 0 ) {
			fogEnd = src.ParseFloat();
		} else if ( token.Icmp( "atmosphereMaterial" ) == 0 ) {
			src.ReadToken( &token );
			atmosphereMaterial = declHolder.FindMaterial( token, true );
		} else if ( token.Icmp( "ambientCubeMap" ) == 0 ) {
			src.ReadToken( &token );
			ambientCubeMap = declHolder.FindAmbientCubeMap( token, true );
		} else if ( token.Icmp( "skyGradientImage" ) == 0 ) {
			src.ReadToken( &token );
			SetSkyGradientImage( token );
		} else if ( token.Icmp( "farClip" ) == 0 ) {
			farClip = src.ParseFloat();
		} else if ( token.Icmp( "isNight" ) == 0 ) {
			isNight = src.ParseBool();
		} else if ( token.Icmp( "drawAtmosphereLast" ) == 0 ) {
			drawAtmosphereLast = src.ParseBool();
		} else if ( token.Icmp( "minSpecShadowColor" ) == 0 ) {
			src.Parse1DMatrix( 3, minSpecShadowColor.ToFloatPtr() );
		} else if ( token.Icmp( "cloudLayer" ) == 0 ) {
			if ( !ParseCloudLayer( src ) ) {
				return false;
			}
		} else if ( token.Icmp( "precipitationLayer" ) == 0 || token.Icmp( "precipitation" ) == 0 ) {
			if ( !ParsePrecipitationLayer( src ) ) {
				return false;
			}
		} else if ( token.Icmp( "windAngle" ) == 0 ) {
			windAngle = src.ParseFloat();
		} else if ( token.Icmp( "windAngleDev" ) == 0 ) {
			windAngleDev = src.ParseFloat();
		} else if ( token.Icmp( "windStrength" ) == 0 ) {
			windStrength = src.ParseFloat();
		} else if ( token.Icmp( "windStrengthDev" ) == 0 ) {
			windStrengthDev = src.ParseFloat();
		} else {
			src.Warning( "sdDeclAtmosphere::Parse : Unknown token: %s", token.c_str() );
			return false;
		}
	}
	return false;
}

bool sdDeclAtmosphere::SetSkyGradientImage( const char* imageName ) {
	if ( globalImages == NULL || imageName == NULL || imageName[ 0 ] == '\0' ) {
		skyGradientImage = NULL;
		return false;
	}
	imageParams_t parms;
	skyGradientImage = globalImages->ImageFromFile( imageName, parms );
	modified = true;
	return skyGradientImage != NULL;
}

void sdDeclAtmosphere::UpdateSunDirFromAziZen() {
	sunDir.x = idMath::Cos( DEG2RAD( sunAzimuth ) ) * idMath::Sin( DEG2RAD( sunZenith ) );
	sunDir.y = idMath::Sin( DEG2RAD( sunAzimuth ) ) * idMath::Sin( DEG2RAD( sunZenith ) );
	sunDir.z = idMath::Cos( DEG2RAD( sunZenith ) );
}

void sdDeclAtmosphere::RebuildTextSource( idFile_Memory& file ) const {
	file.WriteFloatString( "atmosphere %s {\n", GetName() );
	file.WriteFloatString( "\tsunDir ( %f %f %f )\n", sunDir.x, sunDir.y, sunDir.z );
	file.WriteFloatString( "\tsunColor ( %f %f %f )\n", sunColor.x, sunColor.y, sunColor.z );
	file.WriteFloatString( "\tfogDistHalf %f\n\tfogHeightHalf %f\n\tfogHeightOffset %f\n",
		fogDistHalf, fogHeightHalf, fogHeightOffset );
	file.WriteFloatString( "\tfogColor ( %f %f %f )\n", fogColor.x, fogColor.y, fogColor.z );
	file.WriteFloatString( "\tfogStart %f\n\tfogEnd %f\n", fogStart, fogEnd );
	file.WriteFloatString( "\tfarClip %f\n", farClip );
	file.WriteFloatString( "\tisNight %i\n\tdrawAtmosphereLast %i\n",
		isNight ? 1 : 0, drawAtmosphereLast ? 1 : 0 );
	file.WriteFloatString( "\twindAngle %f\n\twindAngleDev %f\n", windAngle, windAngleDev );
	file.WriteFloatString( "\twindStrength %f\n\twindStrengthDev %f\n", windStrength, windStrengthDev );
	for ( int i = 0; i < numPrecipLayers; i++ ) {
		precipitation[ i ].Save( file );
	}
	file.WriteFloatString( "}\n" );
}

void sdDeclAtmosphere::Save( idFile_Memory& file ) const {
	RebuildTextSource( file );
}

void sdDeclAtmosphere::Save() {
	idFile_Memory file( va( "atmosphere %s", GetName() ) );
	RebuildTextSource( file );
	SetText( file.GetDataPtr() );
	ReplaceSourceFileText();
	modified = false;
}
