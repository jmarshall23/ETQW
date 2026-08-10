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

namespace {

static bool IsDomainKeyword( const idToken &token ) {
	return token == "}" || !token.Icmp( "surface" ) || !token.Icmp( "useEndOrigin" ) ||
		!token.Icmp( "cone" ) || !token.Icmp( "relative" ) || !token.Icmp( "linearSpacing" ) ||
		!token.Icmp( "attenuate" ) || !token.Icmp( "inverseAttenuate" ) ||
		!token.Icmp( "rate" ) || !token.Icmp( "count" ) || !token.Icmp( "offset" );
}

static void FillModifier( const idList<float> &values, idVec4 &result, float fallback ) {
	result.Set( fallback, fallback, fallback, fallback );
	for ( int i = 0; i < 4 && i < values.Num(); i++ ) {
		result[i] = values[i];
	}
	if ( values.Num() > 0 ) {
		for ( int i = values.Num(); i < 4; i++ ) {
			result[i] = values[values.Num() - 1];
		}
	}
}

static void ParseNumberList( idLexer &src, idList<float> &values ) {
	idToken token;
	bool negative = false;
	while ( src.ReadToken( &token ) ) {
		if ( token == "," || token == "(" || token == ")" ) {
			continue;
		}
		if ( token == "-" || token == "+" ) {
			negative = token == "-";
			continue;
		}
		if ( token.IsNumeric() ) {
			const float value = static_cast<float>( atof( token.c_str() ) );
			values.Append( negative ? -value : value );
			negative = false;
			continue;
		}
		src.UnreadToken( &token );
		break;
	}
}

static void ParseDomain( idLexer &src, rvBSEParm &parm, rvBSEDomain &domain, bool motion ) {
	domain.Clear();
	idToken token;
	if ( !src.ExpectTokenString( "{" ) || !src.ReadToken( &token ) ) {
		return;
	}

	if ( !token.Icmp( "envelope" ) ) {
		if ( src.ReadToken( &token ) ) {
			token.StripQuotes();
			parm.envelope.name = token.c_str();
			if ( token.Icmp( "linear" ) ) {
				parm.envelope.table = static_cast<const idDeclTable *>(
					declManager->FindType( DECL_TABLE, token.c_str(), false ) );
			}
		}
	} else {
		domain.type = BSE_DomainTypeForToken( token );
		if ( domain.type == BSE_DOMAIN_NONE ) {
			domain.type = BSE_DOMAIN_POINT;
			src.UnreadToken( &token );
		}
	}

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}
		if ( token == "," || token == "(" || token == ")" ) {
			continue;
		}
		if ( !token.Icmp( "surface" ) ) {
			domain.surface = true;
		} else if ( !token.Icmp( "useEndOrigin" ) ) {
			domain.useEndOrigin = true;
		} else if ( !token.Icmp( "cone" ) ) {
			domain.cone = true;
		} else if ( !token.Icmp( "relative" ) ) {
			domain.relative = true;
		} else if ( !token.Icmp( "linearSpacing" ) ) {
			domain.linearSpacing = true;
		} else if ( !token.Icmp( "attenuate" ) ) {
			domain.attenuate = true;
		} else if ( !token.Icmp( "inverseAttenuate" ) ) {
			domain.inverseAttenuate = true;
		} else if ( !token.Icmp( "count" ) || !token.Icmp( "rate" ) || !token.Icmp( "offset" ) ) {
			const bool isCount = !token.Icmp( "count" );
			const bool isRate = !token.Icmp( "rate" );
			idList<float> modifier;
			ParseNumberList( src, modifier );
			if ( isCount ) {
				FillModifier( modifier, parm.envelope.count, 1.0f );
				parm.envelope.hasCount = true;
			} else if ( isRate ) {
				FillModifier( modifier, parm.envelope.rate, 0.0f );
				parm.envelope.hasRate = true;
			} else {
				FillModifier( modifier, parm.envelope.offset, 0.0f );
				parm.envelope.hasOffset = true;
			}
		} else if ( token == "-" || token == "+" || token.IsNumeric() ) {
			src.UnreadToken( &token );
			ParseNumberList( src, domain.values );
		} else if ( domain.type == BSE_DOMAIN_MODEL && domain.modelName.IsEmpty() ) {
			token.StripQuotes();
			domain.modelName = token.c_str();
		} else if ( motion && IsDomainKeyword( token ) ) {
			// Already covered above; retained to document the SDK grammar.
		} else {
			// Domain extensions unknown to this runtime are metadata-only flags.
		}
	}
}

static bseBlendType_t ParseBlend( idLexer &src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return BSE_BLEND_DEFAULT;
	}
	if ( !token.Icmp( "add" ) ) return BSE_BLEND_ADD;
	if ( !token.Icmp( "alpha" ) || !token.Icmp( "blend" ) ) return BSE_BLEND_ALPHA;
	if ( !token.Icmp( "premultiplied" ) ) return BSE_BLEND_PREMULTIPLIED;
	return BSE_BLEND_DEFAULT;
}

} // namespace

bool rvDeclEffect::ParseDomainGroup( idLexer &src, rvParticleTemplate &particle, int group ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		const int parmIndex = BSE_ParmForToken( token );
		if ( parmIndex < 0 ) {
			BSE_SkipUnknown( src, token );
			continue;
		}
		rvBSEParm &parm = particle.parms[parmIndex];
		if ( group == 0 ) {
			ParseDomain( src, parm, parm.start, false );
			parm.hasStart = true;
		} else if ( group == 2 ) {
			ParseDomain( src, parm, parm.end, false );
			parm.hasEnd = true;
		} else {
			rvBSEDomain motionDomain;
			ParseDomain( src, parm, motionDomain, true );
		}
	}
	return false;
}

bool rvDeclEffect::ParseAction( idLexer &src, rvBSEAction &action ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( !token.Icmp( "effect" ) ) {
			action.effects.Append( BSE_ParseString( src ) );
		} else if ( !token.Icmp( "remove" ) ) {
			action.remove = true;
		} else if ( !token.Icmp( "bounce" ) ) {
			action.bounce = src.ParseFloat();
		} else if ( !token.Icmp( "physicsDistance" ) ) {
			action.physicsDistance = src.ParseFloat();
		} else {
			BSE_SkipUnknown( src, token );
		}
	}
	return false;
}

bool rvDeclEffect::ParseParticle( idLexer &src, rvParticleTemplate &particle, int particleType ) {
	particle.Init();
	particle.type = particleType;
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( !token.Icmp( "start" ) ) {
			if ( !ParseDomainGroup( src, particle, 0 ) ) return false;
		} else if ( !token.Icmp( "motion" ) ) {
			if ( !ParseDomainGroup( src, particle, 1 ) ) return false;
		} else if ( !token.Icmp( "end" ) ) {
			if ( !ParseDomainGroup( src, particle, 2 ) ) return false;
		} else if ( !token.Icmp( "duration" ) ) {
			particle.duration = BSE_ParseRange( src, 1.0f );
			particle.duration.x = idMath::ClampFloat( 0.002f, 300.0f, particle.duration.x );
			particle.duration.y = idMath::ClampFloat( 0.002f, 300.0f, particle.duration.y );
		} else if ( !token.Icmp( "gravity" ) ) {
			particle.gravity = BSE_ParseRange( src );
		} else if ( !token.Icmp( "material" ) ) {
			particle.materialName = BSE_ParseString( src );
		} else if ( !token.Icmp( "model" ) ) {
			particle.modelName = BSE_ParseString( src );
		} else if ( !token.Icmp( "entityDef" ) ) {
			particle.entityDefName = BSE_ParseString( src );
		} else if ( !token.Icmp( "parentvelocity" ) ) {
			// This is a flag in the shipped grammar.  The owner velocity is not
			// available to a render-model instance, but retaining the bit avoids
			// consuming the following particle keyword.
			particle.parentVelocity = 1.0f;
		} else if ( !token.Icmp( "windDeviationAngle" ) ) {
			particle.windDeviationAngle = src.ParseFloat();
		} else if ( !token.Icmp( "trailType" ) ) {
			idStr trail = BSE_ParseString( src );
			particle.trailType = !trail.Icmp( "burn" ) ? BSE_TRAIL_BURN : BSE_TRAIL_MOTION;
		} else if ( !token.Icmp( "trailMaterial" ) ) {
			particle.trailMaterialName = BSE_ParseString( src );
		} else if ( !token.Icmp( "trailTime" ) ) {
			particle.trailTime = BSE_ParseRange( src );
		} else if ( !token.Icmp( "trailCount" ) ) {
			particle.trailCount = BSE_ParseRange( src );
		} else if ( !token.Icmp( "trailScale" ) ) {
			particle.trailScale = src.ParseFloat();
		} else if ( !token.Icmp( "trailRepeat" ) ) {
			particle.trailRepeat = Max( 1, src.ParseInt() );
		} else if ( !token.Icmp( "tiling" ) ) {
			particle.tiling = src.ParseFloat();
		} else if ( !token.Icmp( "fork" ) ) {
			particle.numForks = idMath::ClampInt( 0, 16, src.ParseInt() );
		} else if ( !token.Icmp( "forkMins" ) ) {
			particle.forkMins = BSE_ParseVector( src, 3 );
		} else if ( !token.Icmp( "forkMaxs" ) ) {
			particle.forkMaxs = BSE_ParseVector( src, 3 );
		} else if ( !token.Icmp( "jitterSize" ) ) {
			particle.jitterSize = BSE_ParseVector( src, 3 );
		} else if ( !token.Icmp( "jitterRate" ) ) {
			particle.jitterRate = src.ParseFloat();
		} else if ( !token.Icmp( "jitterTable" ) ) {
			particle.jitterTableName = BSE_ParseString( src );
			particle.jitterTable = static_cast<const idDeclTable *>(
				declManager->FindType( DECL_TABLE, particle.jitterTableName, false ) );
		} else if ( !token.Icmp( "blend" ) ) {
			particle.blend = ParseBlend( src );
		} else if ( !token.Icmp( "numFrames" ) ) {
			particle.numFrames = Max( 1, src.ParseInt() );
		} else if ( !token.Icmp( "impact" ) ) {
			if ( !ParseAction( src, particle.impact ) ) return false;
		} else if ( !token.Icmp( "timeout" ) ) {
			if ( !ParseAction( src, particle.timeout ) ) return false;
		} else if ( !token.Icmp( "fadeIn" ) ) {
			particle.fadeIn = 0.1f;
		} else if ( !token.Icmp( "persist" ) ) {
			particle.persist = true;
		} else if ( !token.Icmp( "generatedLine" ) ) {
			particle.generatedLine = true;
		} else if ( !token.Icmp( "generatedNormal" ) ) {
			particle.generatedNormal = true;
		} else if ( !token.Icmp( "generatedOriginNormal" ) ) {
			particle.generatedOriginNormal = true;
		} else if ( !token.Icmp( "lineHit" ) ) {
			particle.lineHit = true;
		} else if ( !token.Icmp( "flipNormal" ) ) {
			particle.flipNormal = true;
		} else if ( !token.Icmp( "useLightningAxis" ) ) {
			particle.useLightningAxis = true;
		} else if ( !token.Icmp( "shadows" ) ) {
			particle.shadows = true;
		} else if ( !token.Icmp( "specular" ) ) {
			particle.specular = true;
		} else {
			BSE_SkipUnknown( src, token );
		}
	}
	return false;
}
