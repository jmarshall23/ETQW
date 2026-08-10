// Copyright (C) 2007 Id Software, Inc.
//
// Sound shader declaration implementation reconstructed from the ETQW PDB
// and retail parser.  Sample allocation remains owned by the sound backend.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "SoundShader.h"
#include "../framework/DeclParseHelper.h"

void idSoundShader::Init() {
	desc = "<no description>";
	leadinVolume = 0.0f;
	errorDuringParse = false;
	onDemand = false;
	lowPriority = false;
	speakerMask = 0;
	altSound = NULL;
	numEntries = 0;
	numLeadins = 0;
	compressionMode = 3;
	memset( &parms, 0, sizeof( parms ) );
	memset( leadins, 0, sizeof( leadins ) );
	memset( entries, 0, sizeof( entries ) );
}

idSoundShader::idSoundShader() {
	Init();
}

idSoundShader::~idSoundShader() {
	FreeData();
}

size_t idSoundShader::Size() const {
	return sizeof( *this );
}

const char* idSoundShader::DefaultDefinition() const {
	return "{\n}";
}

void idSoundShader::FreeData() {
	numEntries = 0;
	numLeadins = 0;
	memset( leadins, 0, sizeof( leadins ) );
	memset( entries, 0, sizeof( entries ) );
}

bool idSoundShader::Parse( const char* text, const int textLength ) {
	idParser src;
	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	idToken token;
	if ( !src.SkipUntilString( "{", &token ) ) {
		return false;
	}
	errorDuringParse = false;
	return ParseShader( src ) && !errorDuringParse;
}

bool idSoundShader::ParseShader( idParser& src ) {
	idToken token;
	parms.minDistance = 0.0f;
	parms.maxDistance = 50.0f;
	parms.farDistance = 0.0f;
	parms.volume = 0.0f;
	parms.shakes = 0.0f;
	parms.soundShaderFlags = 0;
	parms.pitchShift = 1.0f;
	parms.soundClass = 0;
	parms.soundArea = 0;
	speakerMask = 0;
	altSound = NULL;
	numEntries = 0;
	numLeadins = 0;
	memset( leadins, 0, sizeof( leadins ) );
	memset( entries, 0, sizeof( entries ) );

	while ( src.ExpectAnyToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( !token.Icmp( "minSamples" ) ) {
			src.ParseInt();
		} else if ( !token.Icmp( "description" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				desc = token.c_str();
			}
		} else if ( !token.Icmp( "farDistance" ) ) {
			parms.farDistance = src.ParseFloat();
		} else if ( !token.Icmp( "minDistance" ) ) {
			parms.minDistance = src.ParseFloat();
		} else if ( !token.Icmp( "maxDistance" ) ) {
			parms.maxDistance = src.ParseFloat();
		} else if ( !token.Icmp( "shakes" ) ) {
			if ( src.ReadToken( &token ) ) {
				if ( token.type == TT_NUMBER ) {
					parms.shakes = token.GetFloatValue();
				} else {
					parms.shakes = 1.0f;
					src.UnreadToken( token );
				}
			}
		} else if ( !token.Icmp( "volume" ) ) {
			parms.volume = src.ParseFloat();
		} else if ( !token.Icmp( "leadinVolume" ) ) {
			leadinVolume = src.ParseFloat();
		} else if ( !token.Icmp( "pitchShift" ) ) {
			parms.pitchShift = src.ParseFloat();
		} else if ( !token.Icmp( "soundClass" ) ) {
			parms.soundClass = src.ParseInt();
			if ( parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES ) {
				src.Warning( "SoundClass out of range" );
				return false;
			}
		} else if ( !token.Icmp( "soundArea" ) ) {
			parms.soundArea = src.ParseInt();
		} else if ( !token.Icmp( "altSound" ) ) {
			if ( !src.ExpectAnyToken( &token ) ) {
				return false;
			}
			const qhandle_t soundType = declManager->GetDeclTypeHandle( declSoundShaderIdentifier );
			altSound = static_cast< const idSoundShader* >( declManager->FindType( soundType, token.c_str(), true ) );
		} else if ( !token.Icmp( "mask_left" ) ) {
			speakerMask |= BIT( 0 );
		} else if ( !token.Icmp( "mask_right" ) ) {
			speakerMask |= BIT( 1 );
		} else if ( !token.Icmp( "mask_center" ) ) {
			speakerMask |= BIT( 2 );
		} else if ( !token.Icmp( "mask_lfe" ) ) {
			speakerMask |= BIT( 3 );
		} else if ( !token.Icmp( "mask_backleft" ) ) {
			speakerMask |= BIT( 4 );
		} else if ( !token.Icmp( "mask_backright" ) ) {
			speakerMask |= BIT( 5 );
		} else if ( !token.Icmp( "mask_sideleft" ) ) {
			speakerMask |= BIT( 7 );
		} else if ( !token.Icmp( "mask_sideright" ) ) {
			speakerMask |= BIT( 6 );
		} else if ( !token.Icmp( "private" ) ) {
			parms.soundShaderFlags |= SSF_PRIVATE_SOUND;
		} else if ( !token.Icmp( "antiPrivate" ) ) {
			parms.soundShaderFlags |= SSF_ANTI_PRIVATE_SOUND;
		} else if ( !token.Icmp( "no_occlusion" ) ) {
			parms.soundShaderFlags |= SSF_NO_OCCLUSION;
		} else if ( !token.Icmp( "global" ) ) {
			parms.soundShaderFlags |= SSF_GLOBAL;
		} else if ( !token.Icmp( "omnidirectional" ) ) {
			parms.soundShaderFlags |= SSF_OMNIDIRECTIONAL;
		} else if ( !token.Icmp( "looping" ) ) {
			parms.soundShaderFlags |= SSF_LOOPING;
		} else if ( !token.Icmp( "playonce" ) ) {
			parms.soundShaderFlags |= SSF_PLAY_ONCE;
		} else if ( !token.Icmp( "unclamped" ) ) {
			parms.soundShaderFlags |= SSF_UNCLAMPED;
		} else if ( !token.Icmp( "no_flicker" ) ) {
			parms.soundShaderFlags |= SSF_NO_FLICKER;
		} else if ( !token.Icmp( "no_dups" ) ) {
			parms.soundShaderFlags |= SSF_NO_DUPS;
		} else if ( !token.Icmp( "randomize" ) ) {
			parms.soundShaderFlags |= SSF_RANDOMIZE;
		} else if ( !token.Icmp( "occlude_once" ) ) {
			parms.soundShaderFlags |= SSF_OCCLUDE_ONCE;
		} else if ( !token.Icmp( "onDemand" ) ) {
			onDemand = true;
		} else if ( !token.Icmp( "lowPriority" ) ) {
			lowPriority = true;
		} else if ( !token.Icmp( "leadin" ) ) {
			if ( !src.ExpectAnyToken( &token ) ) {
				return false;
			}
		} else if ( !token.Icmp( "ordered" ) || !token.Icmp( "plain" ) ) {
			// Accepted legacy keywords.
		} else if ( !token.Icmp( "compression" ) ) {
			if ( !src.ReadToken( &token ) ) {
				src.Warning( "Missing compression mode after compression" );
				return false;
			}
			if ( !token.Icmp( "wav" ) ) {
				compressionMode = 1;
			} else if ( !token.Icmp( "adpcm" ) ) {
				compressionMode = 2;
			} else if ( !token.Icmp( "ogg" ) ) {
				compressionMode = 3;
			} else if ( !token.Icmp( "theora" ) ) {
				compressionMode = 4;
			} else if ( !token.Icmp( "bink" ) ) {
				compressionMode = 5;
			} else {
				src.Warning( "Unknown compression type: \"%s\"", token.c_str() );
				return false;
			}
		} else if ( token.Find( ".wav", false ) == -1 &&
					token.Find( ".ogg", false ) == -1 &&
					token.Find( ".theora", false ) == -1 &&
					token.Find( ".bik", false ) == -1 ) {
			src.Warning( "unknown token '%s'", token.c_str() );
			return false;
		}
	}
	return false;
}

void idSoundShader::CacheFromDict( const idDict& dict ) {
	const qhandle_t soundType = declManager->GetDeclTypeHandle( declSoundShaderIdentifier );
	const idKeyValue* kv = NULL;
	while ( ( kv = dict.MatchPrefix( "snd", kv ) ) != NULL ) {
		if ( kv->GetValue().Length() != 0 ) {
			declManager->FindType( soundType, kv->GetValue().c_str(), false );
		}
	}
}

void idSoundShader::List() const {
	common->Printf( "%4i: %s\n", Index(), GetName() );
}

const char* idSoundShader::GetDescription() const { return desc.c_str(); }
float idSoundShader::GetMinDistance() const { return parms.minDistance; }
float idSoundShader::GetMaxDistance() const { return parms.maxDistance; }
int idSoundShader::GetTimeLength() const { return 0; }
const idSoundShader* idSoundShader::GetAltSound() const { return altSound; }
bool idSoundShader::HasDefaultSound() const { return false; }
const soundShaderParms_t* idSoundShader::GetParms() const { return &parms; }
int idSoundShader::GetNumSounds() const { return numLeadins + numEntries; }
const char* idSoundShader::GetSound( int ) const { return ""; }
bool idSoundShader::CheckShakesAndOgg() const { return false; }
bool idSoundShader::IsOGGCompressed() const { return compressionMode == 3; }
bool idSoundShader::RebuildTextSource() { return false; }
