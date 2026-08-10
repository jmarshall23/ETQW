/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "BSE.h"

idVec2 BSE_ParseRange( idLexer &src, float defaultValue ) {
	bool error = false;
	const float first = src.ParseFloat( &error );
	if ( error ) {
		return idVec2( defaultValue, defaultValue );
	}
	if ( src.CheckTokenString( "," ) ) {
		return idVec2( first, src.ParseFloat() );
	}
	return idVec2( first, first );
}

idVec3 BSE_ParseVector( idLexer &src, int count ) {
	idVec3 result( 0.0f, 0.0f, 0.0f );
	for ( int i = 0; i < count && i < 3; i++ ) {
		result[i] = src.ParseFloat();
		if ( i + 1 < count ) {
			src.CheckTokenString( "," );
		}
	}
	return result;
}

idStr BSE_ParseString( idLexer &src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return "";
	}
	token.StripQuotes();
	return token.c_str();
}

void BSE_SkipUnknown( idLexer &src, const idToken &token ) {
	if ( token == "{" ) {
		src.SkipBracedSection( false );
		return;
	}
	idToken value;
	while ( src.ReadTokenOnLine( &value ) ) {
		if ( value == "{" ) {
			src.SkipBracedSection( false );
			return;
		}
	}
}

int BSE_SegmentTypeForToken( const idToken &token ) {
	if ( !token.Icmp( "effect" ) ) return SEG_EFFECT;
	if ( !token.Icmp( "emitter" ) ) return SEG_EMITTER;
	if ( !token.Icmp( "spawner" ) ) return SEG_SPAWNER;
	if ( !token.Icmp( "trail" ) ) return SEG_TRAIL;
	if ( !token.Icmp( "sound" ) ) return SEG_SOUND;
	if ( !token.Icmp( "decal" ) ) return SEG_DECAL;
	if ( !token.Icmp( "light" ) ) return SEG_LIGHT;
	if ( !token.Icmp( "delay" ) ) return SEG_DELAY;
	if ( !token.Icmp( "shake" ) ) return SEG_SHAKE;
	if ( !token.Icmp( "tunnel" ) ) return SEG_TUNNEL;
	return SEG_NONE;
}

int BSE_ParticleTypeForToken( const idToken &token ) {
	if ( !token.Icmp( "sprite" ) ) return PTYPE_SPRITE;
	if ( !token.Icmp( "line" ) ) return PTYPE_LINE;
	if ( !token.Icmp( "oriented" ) ) return PTYPE_ORIENTED;
	if ( !token.Icmp( "decal" ) ) return PTYPE_DECAL;
	if ( !token.Icmp( "model" ) ) return PTYPE_MODEL;
	if ( !token.Icmp( "light" ) ) return PTYPE_LIGHT;
	if ( !token.Icmp( "electricity" ) ) return PTYPE_ELECTRICITY;
	if ( !token.Icmp( "linked" ) ) return PTYPE_LINKED;
	if ( !token.Icmp( "orientedlinked" ) ) return PTYPE_ORIENTEDLINKED;
	if ( !token.Icmp( "debris" ) ) return PTYPE_DEBRIS;
	return PTYPE_NONE;
}

int BSE_ParmForToken( const idToken &token ) {
	if ( !token.Icmp( "position" ) ) return BSE_PARM_POSITION;
	if ( !token.Icmp( "direction" ) ) return BSE_PARM_DIRECTION;
	if ( !token.Icmp( "velocity" ) ) return BSE_PARM_VELOCITY;
	if ( !token.Icmp( "acceleration" ) ) return BSE_PARM_ACCELERATION;
	if ( !token.Icmp( "friction" ) ) return BSE_PARM_FRICTION;
	if ( !token.Icmp( "tint" ) ) return BSE_PARM_TINT;
	if ( !token.Icmp( "fade" ) ) return BSE_PARM_FADE;
	if ( !token.Icmp( "size" ) ) return BSE_PARM_SIZE;
	if ( !token.Icmp( "rotate" ) ) return BSE_PARM_ROTATE;
	if ( !token.Icmp( "angle" ) ) return BSE_PARM_ANGLE;
	if ( !token.Icmp( "offset" ) ) return BSE_PARM_OFFSET;
	if ( !token.Icmp( "length" ) ) return BSE_PARM_LENGTH;
	if ( !token.Icmp( "windStrength" ) ) return BSE_PARM_WINDSTRENGTH;
	return -1;
}

bseDomainType_t BSE_DomainTypeForToken( const idToken &token ) {
	if ( !token.Icmp( "point" ) ) return BSE_DOMAIN_POINT;
	if ( !token.Icmp( "line" ) ) return BSE_DOMAIN_LINE;
	if ( !token.Icmp( "box" ) ) return BSE_DOMAIN_BOX;
	if ( !token.Icmp( "sphere" ) ) return BSE_DOMAIN_SPHERE;
	if ( !token.Icmp( "cylinder" ) ) return BSE_DOMAIN_CYLINDER;
	if ( !token.Icmp( "cone" ) ) return BSE_DOMAIN_CONE;
	if ( !token.Icmp( "spiral" ) ) return BSE_DOMAIN_SPIRAL;
	if ( !token.Icmp( "model" ) ) return BSE_DOMAIN_MODEL;
	return BSE_DOMAIN_NONE;
}

rvSegmentTemplate::rvSegmentTemplate() {
	Init();
}

void rvSegmentTemplate::Init() {
	name.Clear();
	type = SEG_NONE;
	startTime.Zero();
	duration.Zero();
	count.Set( 1.0f, 1.0f );
	density.Zero();
	attenuation.Zero();
	soundVolume.Zero();
	frequencyShift.Zero();
	particleCap = 0.0f;
	scale = 1.0f;
	detail = 1.0f;
	decalAxis = 0;
	soundShader.Clear();
	soundChannel.Clear();
	spawnEffects.Clear();
	locked = false;
	looping = false;
	constant = false;
	calculateDuration = false;
	depthSort = false;
	inverseDrawOrder = false;
	useMaterialColor = false;
	orientateIdentity = false;
	attenuateEmitter = false;
	inverseAttenuateEmitter = false;
	particle.Init();
}

int rvSegmentTemplate::Allocated() const {
	int total = name.Allocated() + soundShader.Allocated() + soundChannel.Allocated() +
		spawnEffects.Allocated() + particle.Allocated();
	for ( int i = 0; i < spawnEffects.Num(); i++ ) {
		total += spawnEffects[i].Allocated();
	}
	return total;
}

bool rvSegmentTemplate::HasVisualParticle() const {
	return particle.type > PTYPE_NONE && particle.type < PTYPE_COUNT;
}

bool rvDeclEffect::ParseSegment( idLexer &src, int segmentType ) {
	rvSegmentTemplate segment;
	segment.type = segmentType;

	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return false;
	}
	if ( token != "{" ) {
		token.StripQuotes();
		segment.name = token.c_str();
		if ( !src.ExpectTokenString( "{" ) ) {
			return false;
		}
	} else {
		segment.name = va( "unnamed%d", segments.Num() );
	}

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			segments.Append( segment );
			return true;
		}
		if ( !token.Icmp( "count" ) || !token.Icmp( "rate" ) ) {
			segment.count = BSE_ParseRange( src, 1.0f );
		} else if ( !token.Icmp( "density" ) ) {
			segment.density = BSE_ParseRange( src );
		} else if ( !token.Icmp( "particleCap" ) ) {
			segment.particleCap = src.ParseFloat();
		} else if ( !token.Icmp( "start" ) ) {
			segment.startTime = BSE_ParseRange( src );
		} else if ( !token.Icmp( "duration" ) ) {
			segment.duration = BSE_ParseRange( src );
		} else if ( !token.Icmp( "detail" ) ) {
			segment.detail = src.ParseFloat();
		} else if ( !token.Icmp( "soundShader" ) ) {
			segment.soundShader = BSE_ParseString( src );
		} else if ( !token.Icmp( "volume" ) ) {
			segment.soundVolume = BSE_ParseRange( src );
		} else if ( !token.Icmp( "freqShift" ) || !token.Icmp( "freqshift" ) ) {
			segment.frequencyShift = BSE_ParseRange( src );
		} else if ( !token.Icmp( "effect" ) ) {
			segment.spawnEffects.Append( BSE_ParseString( src ) );
		} else if ( !token.Icmp( "channel" ) ) {
			segment.soundChannel = BSE_ParseString( src );
		} else if ( !token.Icmp( "scale" ) ) {
			segment.scale = src.ParseFloat();
		} else if ( !token.Icmp( "attenuation" ) ) {
			segment.attenuation = BSE_ParseRange( src );
		} else if ( !token.Icmp( "decalAxis" ) ) {
			segment.decalAxis = src.ParseInt();
		} else if ( !token.Icmp( "attenuateEmitter" ) ) {
			segment.attenuateEmitter = true;
		} else if ( !token.Icmp( "inverseAttenuateEmitter" ) ) {
			segment.attenuateEmitter = true;
			segment.inverseAttenuateEmitter = true;
		} else if ( !token.Icmp( "locked" ) ) {
			segment.locked = true;
		} else if ( !token.Icmp( "looping" ) ) {
			segment.looping = true;
		} else if ( !token.Icmp( "constant" ) ) {
			segment.constant = true;
		} else if ( !token.Icmp( "calcDuration" ) ) {
			segment.calculateDuration = true;
		} else if ( !token.Icmp( "depthSort" ) || !token.Icmp( "depthsort" ) ) {
			segment.depthSort = true;
		} else if ( !token.Icmp( "inverseDrawOrder" ) ) {
			segment.inverseDrawOrder = true;
		} else if ( !token.Icmp( "useMaterialColor" ) ) {
			segment.useMaterialColor = true;
		} else if ( !token.Icmp( "orientateIdentity" ) ) {
			segment.orientateIdentity = true;
		} else {
			const int particleType = BSE_ParticleTypeForToken( token );
			if ( particleType != PTYPE_NONE ) {
				if ( !ParseParticle( src, segment.particle, particleType ) ) {
					return false;
				}
			} else {
				BSE_SkipUnknown( src, token );
			}
		}
	}
	return false;
}
