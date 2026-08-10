// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#include "../framework/DeclParseHelper.h"
#include "declImposter.h"
#include "declTypeHolder.h"

#pragma hdrstop

void sdImposterSubImage::Write( idFile_Memory& file ) {
	file.WriteFloatString( "\tSubImage {\n" );
	file.WriteFloatString( "\t\tmin %f %f\n", rectMins.x, rectMins.y );
	file.WriteFloatString( "\t\tmax %f %f\n", rectMaxs.x, rectMaxs.y );
	for ( int i = 0; i < 4; i++ ) {
		file.WriteFloatString( "\t\ttexCoord %f %f\n", texCoords[ i ].x, texCoords[ i ].y );
	}
	file.WriteFloatString( "\t}\n" );
}

bool sdImposterSubImage::Read( idParser& src ) {
	idToken token;
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}

	int numTexCoords = 0;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( numTexCoords < 4 ) {
				src.Warning( "sdImposterSubImage: Not enough texCoords specified" );
				return false;
			}
			return true;
		}
		if ( token.Icmp( "min" ) == 0 ) {
			rectMins.x = src.ParseFloat();
			rectMins.y = src.ParseFloat();
		} else if ( token.Icmp( "max" ) == 0 ) {
			rectMaxs.x = src.ParseFloat();
			rectMaxs.y = src.ParseFloat();
		} else if ( token.Icmp( "texCoord" ) == 0 ) {
			if ( numTexCoords >= 4 ) {
				src.Warning( "sdImposterSubImage: To many texCoords specified" );
				return false;
			}
			texCoords[ numTexCoords ].x = src.ParseFloat();
			texCoords[ numTexCoords ].y = src.ParseFloat();
			numTexCoords++;
		} else {
			src.Warning( "sdImposterSubImage: bad token %s", token.c_str() );
			return false;
		}
	}
	return false;
}

sdDeclImposterGenerator::sdDeclImposterGenerator( void ) :
	vertexColor( false ),
	numAngles( 1 ),
	noBump( false ),
	startAngle( 0.0f ),
	screenScale( 1.0f ) {
	tileSize[ 0 ] = 128;
	tileSize[ 1 ] = 128;
}

const char* sdDeclImposterGenerator::DefaultDefinition( void ) const {
	return "\t\t   {\t\t   \t\t   }";
}

void sdDeclImposterGenerator::FreeData( void ) {
	sourceModel.Clear();
	outputTexture.Clear();
	vertexColor = false;
	numAngles = 1;
	tileSize[ 0 ] = tileSize[ 1 ] = 128;
	noBump = false;
	startAngle = 0.0f;
	screenScale = 1.0f;
}

bool sdDeclImposterGenerator::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );
	FreeData();

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "sourceModel" ) == 0 ) {
			src.ReadToken( &token );
			sourceModel = token;
		} else if ( token.Icmp( "outputTexture" ) == 0 ) {
			src.ReadToken( &token );
			outputTexture = token;
		} else if ( token.Icmp( "vertexColored" ) == 0 ) {
			vertexColor = true;
		} else if ( token.Icmp( "numAngles" ) == 0 ) {
			numAngles = src.ParseInt();
		} else if ( token.Icmp( "tileSize" ) == 0 ) {
			tileSize[ 0 ] = src.ParseInt();
			tileSize[ 1 ] = src.ParseInt();
		} else if ( token.Icmp( "noBump" ) == 0 ) {
			noBump = src.ParseBool();
		} else if ( token.Icmp( "startAngle" ) == 0 ) {
			startAngle = src.ParseFloat();
		} else if ( token.Icmp( "screenScale" ) == 0 ) {
			screenScale = src.ParseFloat();
		} else {
			src.Warning( "sdDeclImposterGenerator: bad token %s", token.c_str() );
			return false;
		}
	}
	return false;
}

sdDeclImposter::sdDeclImposter( void ) {
	info.material = NULL;
	info.origin = vec3_origin;
	info.scalex = 1.0f;
	info.scaley = 1.0f;
	info.screenScale = 1.0f;
	info.tileSize = 128;
	info.numAngles = 1;
}

const char* sdDeclImposter::DefaultDefinition( void ) const {
	return "{}";
}

void sdDeclImposter::FreeData( void ) {
	info.images.Clear();
	info.material = NULL;
	info.origin = vec3_origin;
	info.scalex = 1.0f;
	info.scaley = 1.0f;
	info.screenScale = 1.0f;
	info.tileSize = 128;
	info.numAngles = 1;
}

void sdDeclImposter::CacheFromDict( const idDict& dict ) {
	const idKeyValue* keyValue = NULL;
	while ( ( keyValue = dict.MatchPrefix( "imposter", keyValue ) ) != NULL ) {
		if ( keyValue->GetValue().Length() != 0 ) {
			declManager->MediaPrint( "Precaching imposter %s\n", keyValue->GetValue().c_str() );
			declHolder.FindImposter( keyValue->GetValue(), false );
		}
	}
}

bool sdDeclImposter::Parse( const char* text, const int textLength ) {
	idParser src;
	idToken token;

	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	src.SkipUntilString( "{", &token );
	FreeData();

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "material" ) == 0 ) {
			src.ReadToken( &token );
			info.material = declHolder.FindMaterial( token, true );
		} else if ( token.Icmp( "origin" ) == 0 ) {
			info.origin.x = src.ParseFloat();
			info.origin.y = src.ParseFloat();
			info.origin.z = src.ParseFloat();
		} else if ( token.Icmp( "scalex" ) == 0 ) {
			info.scalex = src.ParseFloat();
		} else if ( token.Icmp( "scaley" ) == 0 ) {
			info.scaley = src.ParseFloat();
		} else if ( token.Icmp( "screenScale" ) == 0 ) {
			info.screenScale = src.ParseFloat();
		} else if ( token.Icmp( "numAngles" ) == 0 ) {
			info.numAngles = src.ParseInt();
		} else if ( token.Icmp( "SubImage" ) == 0 ) {
			sdImposterSubImage image;
			if ( !image.Read( src ) ) {
				return false;
			}
			info.images.Append( image );
		} else {
			src.Warning( "sdDeclImposter: bad token %s", token.c_str() );
			return false;
		}
	}
	return false;
}

void sdDeclImposter::RebuildTextSource( void ) {
	idFile_Memory file( va( "imposter %s", GetName() ) );
	file.WriteFloatString( "imposter %s {\n", GetName() );
	if ( info.material != NULL ) {
		file.WriteFloatString( "\tmaterial \"%s\"\n", info.material->GetName() );
	}
	file.WriteFloatString( "\torigin %f %f %f\n", info.origin.x, info.origin.y, info.origin.z );
	file.WriteFloatString( "\tscalex %f\n", info.scalex );
	file.WriteFloatString( "\tscaley %f\n", info.scaley );
	file.WriteFloatString( "\tscreenScale %f\n", info.screenScale );
	file.WriteFloatString( "\tnumAngles %i\n", info.numAngles );
	for ( int i = 0; i < info.images.Num(); i++ ) {
		info.images[ i ].Write( file );
	}
	file.WriteFloatString( "}\n" );
	SetText( file.GetDataPtr() );
}

bool sdDeclImposter::Save( void ) {
	RebuildTextSource();
	return ReplaceSourceFileText();
}
