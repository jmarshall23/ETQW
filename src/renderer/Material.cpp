
// Copyright (C) 2007 Id Software, Inc.
//
// ETQW material declaration boundary reconstructed from the Microsoft PDB and
// quakewars-hexrays/renderer/Material.cpp.  This unit intentionally implements
// the declaration/runtime state first; the recovered render-program expression
// compiler is being restored separately.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Material.h"
#include "draw_local.h"
#include "Image.h"
#include "renderbindings.h"
#include "tr_render.h"
#include "megatexture/MegaTexture.h"
#include "SurfaceTypeMap.h"
#include "../decllib/DeclSurfaceTypeMap.h"
#include "../decllib/declRenderBinding.h"
#include "../decllib/declTable.h"
#include "../decllib/declTypeHolder.h"
#include "../decllib/declAtmosphere.h"
#include "../framework/DeclParseHelper.h"
#include "../sound/SoundEmitter.h"

extern idCVar r_lightScale;
extern idCVar r_shaderQuality;
extern idCVar r_useConstantMaterials;

namespace {

const int MAX_EXPRESSION_REGISTERS = 512;
const int MAX_EXPRESSION_OPS = 512;

const int NUM_ATMOSPHERE_EXPRESSIONS = 18;

// R_SetupExpressionMemory updates this once for each submitted view.  Material
// OP_LOAD expressions then see one coherent atmosphere snapshot, as in ETQW,
// instead of advancing randf or rebuilding wind/sun data for every surface.
float atmosphereExpressionMemory[ NUM_ATMOSPHERE_EXPRESSIONS ] = {
	0.0f, 1.0f,                         // halo bias, halo scale
	0.0f, 0.0f, 1.0f,                  // sun direction
	1.0f, 1.0f, 1.0f,                  // sun colour
	1.0f, 1.0f, 1.0f,                  // desaturated sun colour
	0.0f,                               // rotated sun azimuth
	1.0f, 0.0f,                         // wind direction
	1.0f, 0.0f, 0.0f, 0.5f            // light scale, shader quality, night, rand
};

bool atmosphereExpressionMemoryInitialized = false;

void BuildAtmosphereExpressionMemory( const viewDef_s* viewDef, float result[ NUM_ATMOSPHERE_EXPRESSIONS ] ) {
	memcpy( result, atmosphereExpressionMemory, sizeof( atmosphereExpressionMemory ) );

	const sdDeclAtmosphere* atmosphere = viewDef != NULL ? viewDef->atmosphere : NULL;
	if ( atmosphere != NULL ) {
		result[ 0 ] = atmosphere->GetSunHaloBias();
		result[ 1 ] = atmosphere->GetSunHaloScale();

		const idVec3& sunDirection = atmosphere->GetSunDirection();
		result[ 2 ] = sunDirection.x;
		result[ 3 ] = sunDirection.y;
		result[ 4 ] = sunDirection.z;

		const idVec3& sunColor = atmosphere->GetSunColor();
		result[ 5 ] = sunColor.x;
		result[ 6 ] = sunColor.y;
		result[ 7 ] = sunColor.z;
		const float desaturatedSun = ( sunColor.x + sunColor.y + sunColor.z ) * 0.33f * 0.8f;
		result[ 8 ] = sunColor.x * 0.2f + desaturatedSun;
		result[ 9 ] = sunColor.y * 0.2f + desaturatedSun;
		result[ 10 ] = sunColor.z * 0.2f + desaturatedSun;

		float sunAzimuth = 360.0f - atmosphere->GetSunAzimuth() + 90.0f;
		sunAzimuth -= idMath::Floor( sunAzimuth / 360.0f ) * 360.0f;
		result[ 11 ] = sunAzimuth;

		const float windRadians = atmosphere->GetWindAngle() * idMath::M_DEG2RAD;
		result[ 12 ] = idMath::Cos( windRadians );
		result[ 13 ] = idMath::Sin( windRadians );
		result[ 16 ] = atmosphere->IsNight() ? 1.0f : 0.0f;
	}

	result[ 14 ] = r_lightScale.GetFloat();
	result[ 15 ] = static_cast< float >( r_shaderQuality.GetInteger() );
	result[ 17 ] = idRandom::StaticRandom().RandomFloat();
}

}

void R_SetupExpressionMemory( const viewDef_s* viewDef ) {
	float updated[ NUM_ATMOSPHERE_EXPRESSIONS ];
	BuildAtmosphereExpressionMemory( viewDef, updated );

	// randf and the wind vector are dynamic registers.  All other entries can
	// participate in a precomputed material, so invalidate those registers when
	// the atmosphere or either renderer scale changes.
	bool constantsChanged = !atmosphereExpressionMemoryInitialized;
	for ( int i = 0; !constantsChanged && i < NUM_ATMOSPHERE_EXPRESSIONS; ++i ) {
		if ( i == 12 || i == 13 || i == 17 ) {
			continue;
		}
		constantsChanged = updated[ i ] != atmosphereExpressionMemory[ i ];
	}
	if ( constantsChanged ) {
		++idMaterial::currentAtmosphereFrame;
	}
	memcpy( atmosphereExpressionMemory, updated, sizeof( atmosphereExpressionMemory ) );
	atmosphereExpressionMemoryInitialized = true;
}

// PDB type: mtrParsingData_s, sizeof 0x8604 in the original Win32 build.
// This data intentionally stays on the stack so declaration parsing remains
// re-entrant, exactly as it did in the retail renderer.
struct mtrParsingData_s {
	bool			registerIsTemporary[ 512 ];
	float			materialRegisters[ 512 ];
	expOp_t			shaderOps[ 512 ];
	materialStage_t	parseStages[ MAX_SHADER_STAGES ];
	bool			registersAreConstant;
	bool			registersUseTime;
	bool			forceOverlays;
};

static_assert( sizeof( mtrParsingData_s ) == 0x8604, "mtrParsingData_s must match the ETQW PDB layout" );

namespace {

// Recovered verbatim from the ETQW 1.5 executable.  These flags are consumed
// by both the collision-model loader and game code, so silently ignoring one
// of these material keywords changes the meaning of the compiled .cmb data.
struct infoParm_t {
	const char*	name;
	bool		clearSolid;
	int			surfaceFlags;
	int			contents;
};

static const infoParm_t infoParms[] = {
	{ "solid",             false, 0,                     CONTENTS_SOLID },
	{ "water",             true,  0,                     CONTENTS_WATER },
	{ "playerclip",        false, 0,                     CONTENTS_PLAYERCLIP },
	{ "vehicleclip",       false, 0,                     CONTENTS_VEHICLECLIP },
	{ "moveableclip",      false, 0,                     CONTENTS_MOVEABLECLIP },
	{ "rendermodelclip",   false, 0,                     CONTENTS_RENDERMODEL },
	{ "ikclip",            false, 0,                     CONTENTS_IKCLIP },
	{ "trigger",           false, 0,                     CONTENTS_TRIGGER },
	{ "nonsolid",          true,  SURF_NONSOLID,         0 },
	{ "nullNormal",        false, SURF_NULLNORMAL,       0 },
	{ "projectileclip",    false, 0,                     CONTENTS_PROJECTILE },
	{ "explosionclip",     false, 0,                     CONTENTS_EXPLOSIONSOLID },
	{ "aassolidplayer",    false, 0,                     CONTENTS_AAS_SOLID_PLAYER },
	{ "aassolidvehicle",   false, 0,                     CONTENTS_AAS_SOLID_VEHICLE },
	{ "aasclusterportal",  false, 0,                     CONTENTS_AAS_CLUSTER_PORTAL },
	{ "aasobstacle",       false, 0,                     CONTENTS_AAS_OBSTACLE },
	{ "areaportal",        true,  0,                     CONTENTS_AREAPORTAL },
	{ "qer_nocarve",       true,  0,                     CONTENTS_NOCSG },
	{ "occluder",          true,  0,                     CONTENTS_OCCLUDER },
	{ "discrete",          true,  SURF_DISCRETE,         0 },
	{ "noFragment",        false, SURF_NOFRAGMENT,       0 },
	{ "slick",             false, SURF_SLICK,            0 },
	{ "collision",         false, SURF_COLLISION,        0 },
	{ "shadowcollision",   false, SURF_SHADOWCOLLISION,  0 },
	{ "allcontent",        false, 0,                     -1 },
	{ "noimpact",          false, SURF_NOIMPACT,         0 },
	{ "nodamage",          false, SURF_NODAMAGE,         0 },
	{ "ladder",            false, SURF_LADDER,           0 },
	{ "nosteps",           false, SURF_NOSTEPS,          0 },
	{ "noplant",           false, SURF_NOPLANT,          0 },
	{ "noareas",           false, SURF_NOAREAS,          0 },
	{ "walkerclip",        false, 0,                     CONTENTS_WALKERCLIP },
	{ "forcefieldclip",    false, 0,                     CONTENTS_FORCEFIELD },
	{ "crosshairclip",     false, 0,                     CONTENTS_CROSSHAIRSOLID },
	{ "flyerhiveclip",     false, 0,                     CONTENTS_FLYERHIVECLIP }
};

struct portalParm_t {
	const char*	name;
	int			flags;
};

static const portalParm_t portalParms[] = {
	{ "vis",             BIT( PORTAL_VIS ) },
	{ "outside",         BIT( PORTAL_OUTSIDE ) },
	{ "blockAmbient",    BIT( PORTAL_BLOCKAMBIENT ) },
	{ "audio",           BIT( PORTAL_AUDIO ) },
	{ "playzone",        BIT( PORTAL_PLAYZONE ) },
	{ "occlusionQuery",  BIT( PORTAL_OCCTEST ) }
};

float SortForName( idToken& token ) {
	if ( !token.Icmp( "subview" ) ) {
		return SS_SUBVIEW;
	}
	if ( !token.Icmp( "opaqueFirst" ) ) {
		return SS_OPAQUEFIRST;
	}
	if ( !token.Icmp( "opaque" ) ) {
		return SS_OPAQUE;
	}
	if ( !token.Icmp( "opaqueNearer" ) ) {
		return SS_OPAQUENEARER;
	}
	if ( !token.Icmp( "opaqueNearest" ) ) {
		return SS_OPAQUENEAREST;
	}
	if ( !token.Icmp( "decal" ) ) {
		return SS_DECAL;
	}
	if ( !token.Icmp( "gui" ) ) {
		return SS_GUI;
	}
	if ( !token.Icmp( "refractable" ) ) {
		return SS_REFRACTABLE;
	}
	if ( !token.Icmp( "refraction" ) ) {
		return SS_REFRACTION;
	}
	if ( !token.Icmp( "farPreAtmos" ) ) {
		return SS_FAR_PRE_ATMOS;
	}
	if ( !token.Icmp( "mediumPreAtmos" ) ) {
		return SS_MEDIUM_PRE_ATMOS;
	}
	if ( !token.Icmp( "closePreAtmos" ) ) {
		return SS_CLOSE_PRE_ATMOS;
	}
	if ( !token.Icmp( "atmosphere" ) ) {
		return SS_ATMOSPHERE;
	}
	if ( !token.Icmp( "far" ) ) {
		return SS_FAR;
	}
	if ( !token.Icmp( "medium" ) ) {
		return SS_MEDIUM;
	}
	if ( !token.Icmp( "close" ) ) {
		return SS_CLOSE;
	}
	if ( !token.Icmp( "almostNearest" ) ) {
		return SS_ALMOST_NEAREST;
	}
	if ( !token.Icmp( "nearest" ) ) {
		return SS_NEAREST;
	}
	if ( !token.Icmp( "postProcess" ) ) {
		return SS_POST_PROCESS;
	}
	if ( !token.Icmp( "last" ) ) {
		return SS_LAST;
	}
	return token.GetFloatValue();
}

idImage* LoadMaterialImage( const char* name ) {
	if ( globalImages == NULL || name == NULL || name[ 0 ] == '\0' ) {
		return NULL;
	}

	imageParams_t params;
	params.allowPicmip = false;
	return globalImages->ImageFromFile( name, params );
}

}

int idMaterial::currentAtmosphereFrame = 0;

void idMaterial::CommonInit() {
	desc = "<none>";
	renderBump.Clear();
	lightFalloffImage = NULL;
	entityGui = 0;
	noFog = false;
	spectrum = 0;
	polygonOffset = 0.0f;
	contentFlags = CONTENTS_SOLID;
	surfaceFlags = 0;
	portalFlags = 0;
	materialFlags = 0;
	surfaceTypeDecl = NULL;
	surfaceTypeMapDecl = NULL;
	surfaceTypeMap = NULL;
	surfaceColor = colorWhite.ToVec3();
	decalInfo.stayTime = 10000;
	decalInfo.fadeTime = 4000;
	decalInfo.start = colorWhite;
	decalInfo.end = colorBlack;
	gpuSpec = 0;
	sort = SS_BAD;
	deform = DFRM_NONE;
	memset( deformRegisters, 0, sizeof( deformRegisters ) );
	deformDecl = NULL;
	coverage = MC_BAD;
	cullType = CT_FRONT_SIDED;
	shouldCreateBackSides = false;
	fogLight = false;
	blendLight = false;
#if SD_SUPPORT_UNSMOOTHEDTANGENTS
	unsmoothedTangents = false;
#endif
	hasSubview = false;
	allowOverlays = true;
	numOps = 0;
	ops = NULL;
	numRegisters = 0;
	expressionRegisters = NULL;
	constantRegisters = NULL;
	lastFloatTime = -1.0f;
	atmosphereFrame = -1;
	timeBasedRegisters = false;
	numStages = 0;
	numAmbientStages = 0;
	stages = NULL;
	editorImageName.Clear();
	editorImage = NULL;
	editorAlpha = 1.0f;
	doLodDistance = false;
	subviewInfo.boxExpand = -1.0f;
	subviewInfo.farPlane = -1.0f;
	subviewInfo.backgroundImage = NULL;
	breakpointFlags = 0;
	slopTexCoordMod = 1.0f;
	backSideMaterial = NULL;
	pd = NULL;
}

idMaterial::idMaterial() :
	surfaceArea( 0.0f ) {
	CommonInit();
}

idMaterial::~idMaterial() {
	FreeData();
}

void idMaterial::FreeData() {
	if ( stages != NULL ) {
		for ( int i = 0; i < numStages; i++ ) {
			Mem_Free( stages[ i ].vectors );
			Mem_Free( stages[ i ].textures );
			Mem_Free( stages[ i ].textureMatrices );
			stages[ i ].vectors = NULL;
			stages[ i ].textures = NULL;
			stages[ i ].textureMatrices = NULL;
		}
		Mem_Free( stages );
		stages = NULL;
	}
	Mem_Free( expressionRegisters );
	expressionRegisters = NULL;
	if ( constantRegisters != NULL ) {
		Mem_FreeAligned( constantRegisters );
		constantRegisters = NULL;
	}
	Mem_Free( ops );
	ops = NULL;
	numStages = 0;
	numAmbientStages = 0;
	numRegisters = 0;
	numOps = 0;
	timeBasedRegisters = false;
}

size_t idMaterial::Size() const {
	return sizeof( *this );
}

const char* idMaterial::DefaultDefinition() const {
	return "{\n\t{\n\t\tblend\tblend\n\t\tmap\t\t_defaultMaterial\n\t}\n}";
}

bool idMaterial::SetDefaultText() {
	const char* name = GetName();
	if ( name == NULL || name[ 0 ] == '\0' ) {
		return false;
	}

	char generated[ 2048 ];
	idStr::snPrintf(
		generated,
		sizeof( generated ),
		"material %s { // IMPLICITLY GENERATED\n"
		"\t{\n"
		"\t\tblend blend\n"
		"\t\tcolored\n"
		"\t\tmap clamp \"%s\"\n"
		"\t}\n"
		"}\n",
		name,
		name
	);
	SetText( generated );
	return true;
}

bool idMaterial::CheckSurfaceParm( idToken* token ) {
	for ( int i = 0; i < static_cast< int >( sizeof( infoParms ) / sizeof( infoParms[ 0 ] ) ); ++i ) {
		if ( token->Icmp( infoParms[ i ].name ) != 0 ) {
			continue;
		}

		surfaceFlags |= infoParms[ i ].surfaceFlags;
		contentFlags |= infoParms[ i ].contents;
		if ( infoParms[ i ].clearSolid ) {
			contentFlags &= ~CONTENTS_SOLID;
		}
		return true;
	}
	return false;
}

bool idMaterial::CheckPortalParm( idToken* token ) {
	for ( int i = 0; i < static_cast< int >( sizeof( portalParms ) / sizeof( portalParms[ 0 ] ) ); ++i ) {
		if ( token->Icmp( portalParms[ i ].name ) == 0 ) {
			portalFlags |= portalParms[ i ].flags;
			return true;
		}
	}
	return false;
}

bool idMaterial::MatchToken( idParser& src, const char* match ) {
	if ( !src.ExpectTokenString( match ) ) {
		materialFlags |= MF_DEFAULTED;
		return false;
	}
	return true;
}

void idMaterial::ParseSort( idParser& src ) {
	idToken token;
	if ( !src.ReadTokenOnLine( &token ) ) {
		src.Warning( "missing sort parameter" );
		materialFlags |= MF_DEFAULTED;
		return;
	}
	sort = SortForName( token );
}

int idMaterial::GetExpressionConstant( float f ) {
	int i;
	for ( i = EXP_REG_NUM_PREDEFINED; i < numRegisters; ++i ) {
		if ( !pd->registerIsTemporary[ i ] && pd->materialRegisters[ i ] == f ) {
			return i;
		}
	}
	if ( numRegisters == MAX_EXPRESSION_REGISTERS ) {
		common->Warning( "GetExpressionConstant: material '%s' hit MAX_EXPRESSION_REGISTERS", GetName() );
		materialFlags |= MF_DEFAULTED;
		return 0;
	}
	pd->registerIsTemporary[ i ] = false;
	pd->materialRegisters[ i ] = f;
	++numRegisters;
	return i;
}

int idMaterial::GetExpressionTemporary() {
	if ( numRegisters == MAX_EXPRESSION_REGISTERS ) {
		common->Warning( "GetExpressionTemporary: material '%s' hit MAX_EXPRESSION_REGISTERS", GetName() );
		materialFlags |= MF_DEFAULTED;
		return 0;
	}
	pd->registerIsTemporary[ numRegisters ] = true;
	return numRegisters++;
}

expOp_t* idMaterial::GetExpressionOp() {
	if ( numOps == MAX_EXPRESSION_OPS ) {
		common->Warning( "GetExpressionOp: material '%s' hit MAX_EXPRESSION_OPS", GetName() );
		materialFlags |= MF_DEFAULTED;
		return &pd->shaderOps[ 0 ];
	}
	return &pd->shaderOps[ numOps++ ];
}

int idMaterial::EmitOp( int a, int b, expOpType_t opType ) {
	if ( opType == OP_TYPE_ADD ) {
		if ( !pd->registerIsTemporary[ a ] && pd->materialRegisters[ a ] == 0.0f ) {
			return b;
		}
		if ( !pd->registerIsTemporary[ b ] && pd->materialRegisters[ b ] == 0.0f ) {
			return a;
		}
		if ( !pd->registerIsTemporary[ a ] && !pd->registerIsTemporary[ b ] ) {
			return GetExpressionConstant( pd->materialRegisters[ a ] + pd->materialRegisters[ b ] );
		}
	} else if ( opType == OP_TYPE_MULTIPLY ) {
		if ( !pd->registerIsTemporary[ a ] && pd->materialRegisters[ a ] == 1.0f ) {
			return b;
		}
		if ( !pd->registerIsTemporary[ a ] && pd->materialRegisters[ a ] == 0.0f ) {
			return a;
		}
		if ( !pd->registerIsTemporary[ b ] && pd->materialRegisters[ b ] == 1.0f ) {
			return a;
		}
		if ( !pd->registerIsTemporary[ b ] && pd->materialRegisters[ b ] == 0.0f ) {
			return b;
		}
		if ( !pd->registerIsTemporary[ a ] && !pd->registerIsTemporary[ b ] ) {
			return GetExpressionConstant( pd->materialRegisters[ a ] * pd->materialRegisters[ b ] );
		}
	}

	expOp_t* op = GetExpressionOp();
	op->opType = opType;
	op->a = a;
	op->b = b;
	op->c = GetExpressionTemporary();
	return op->c;
}

int idMaterial::ParseEmitOp( idParser& src, int a, expOpType_t opType, int priority ) {
	return EmitOp( a, ParseExpressionPriority( src, priority ), opType );
}

int idMaterial::ParseTerm( idParser& src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		materialFlags |= MF_DEFAULTED;
		return 0;
	}

	if ( token == "(" ) {
		const int result = ParseExpressionPriority( src, 4 );
		MatchToken( src, ")" );
		return result;
	}
	if ( !token.Icmp( "time" ) ) {
		pd->registersUseTime = true;
		return EXP_REG_TIME;
	}
	if ( !token.Icmpn( "parm", 4 ) && token.Length() > 4 ) {
		const int index = atoi( token.c_str() + 4 );
		if ( index >= 0 && index < MAX_ENTITY_SHADER_PARMS ) {
			pd->registersAreConstant = false;
			return EXP_REG_PARM0 + index;
		}
	}
	if ( !token.Icmpn( "global", 6 ) && token.Length() > 6 ) {
		const int index = atoi( token.c_str() + 6 );
		if ( index >= 0 && index < 8 ) {
			pd->registersAreConstant = false;
			return EXP_REG_GLOBAL0 + index;
		}
	}
	if ( !token.Icmp( "numLights" ) ) {
		pd->registersAreConstant = false;
		return EXP_REG_NUMLIGHTS;
	}
	if ( !token.Icmp( "fragmentPrograms" ) ) {
		return GetExpressionConstant( 1.0f );
	}
	if ( !token.Icmp( "sound" ) ) {
		pd->registersAreConstant = false;
		expOp_t* op = GetExpressionOp();
		op->opType = OP_TYPE_SOUND;
		op->a = op->b = 0;
		op->c = GetExpressionTemporary();
		return op->c;
	}

	static const char* atmosphereNames[] = {
		"halobias", "haloscale", "sun_x", "sun_y", "sun_z", "sun_r", "sun_g", "sun_b",
		"desat_sun_r", "desat_sun_g", "desat_sun_b", "sun_azimuth", "wind_x", "wind_y",
		"lightscale", "shaderQuality", "nighttime", "randf"
	};
	for ( int i = 0; i < static_cast< int >( sizeof( atmosphereNames ) / sizeof( atmosphereNames[ 0 ] ) ); ++i ) {
		if ( !token.Icmp( atmosphereNames[ i ] ) ) {
			if ( i == 12 || i == 13 || i == 17 ) {
				pd->registersAreConstant = false;
			}
			expOp_t* op = GetExpressionOp();
			op->opType = OP_TYPE_LOAD;
			op->a = i;
			op->b = 0;
			op->c = GetExpressionTemporary();
			return op->c;
		}
	}

	if ( token == "-" ) {
		if ( !src.ReadToken( &token ) || ( token.type != TT_NUMBER && token != "." ) ) {
			src.Warning( "Bad negative number '%s'", token.c_str() );
			materialFlags |= MF_DEFAULTED;
			return 0;
		}
		return GetExpressionConstant( -token.GetFloatValue() );
	}
	if ( token.type == TT_NUMBER || token == "." ) {
		return GetExpressionConstant( token.GetFloatValue() );
	}

	const idDeclTable* table = declHolder.FindTable( token.c_str(), false );
	if ( table == NULL ) {
		src.Warning( "Bad term '%s'", token.c_str() );
		materialFlags |= MF_DEFAULTED;
		return 0;
	}
	MatchToken( src, "[" );
	const int index = ParseExpressionPriority( src, 4 );
	MatchToken( src, "]" );
	expOp_t* op = GetExpressionOp();
	op->opType = OP_TYPE_TABLE;
	op->a = table->Index();
	op->b = index;
	op->c = GetExpressionTemporary();
	return op->c;
}

int idMaterial::ParseExpressionPriority( idParser& src, int priority ) {
	if ( priority == 0 ) {
		return ParseTerm( src );
	}
	int a = ParseExpressionPriority( src, priority - 1 );
	if ( materialFlags & MF_DEFAULTED ) {
		return 0;
	}
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return a;
	}
	if ( priority == 1 ) {
		if ( token == "*" ) return ParseEmitOp( src, a, OP_TYPE_MULTIPLY, priority );
		if ( token == "/" ) return ParseEmitOp( src, a, OP_TYPE_DIVIDE, priority );
		if ( token == "%" ) return ParseEmitOp( src, a, OP_TYPE_MOD, priority );
	} else if ( priority == 2 ) {
		if ( token == "+" ) return ParseEmitOp( src, a, OP_TYPE_ADD, priority );
		if ( token == "-" ) return ParseEmitOp( src, a, OP_TYPE_SUBTRACT, priority );
	} else if ( priority == 3 ) {
		if ( token == ">" ) return ParseEmitOp( src, a, OP_TYPE_GT, priority );
		if ( token == ">=" ) return ParseEmitOp( src, a, OP_TYPE_GE, priority );
		if ( token == "<" ) return ParseEmitOp( src, a, OP_TYPE_LT, priority );
		if ( token == "<=" ) return ParseEmitOp( src, a, OP_TYPE_LE, priority );
		if ( token == "==" ) return ParseEmitOp( src, a, OP_TYPE_EQ, priority );
		if ( token == "!=" ) return ParseEmitOp( src, a, OP_TYPE_NE, priority );
	} else if ( priority == 4 ) {
		if ( token == "&&" ) return ParseEmitOp( src, a, OP_TYPE_AND, priority );
		if ( token == "||" ) return ParseEmitOp( src, a, OP_TYPE_OR, priority );
	}
	src.UnreadToken( token );
	return a;
}

int idMaterial::ParseExpression( idParser& src ) {
	return ParseExpressionPriority( src, 4 );
}

void idMaterial::ClearStage( materialStage_t* stage ) {
	memset( stage, 0, sizeof( *stage ) );
	stage->conditionRegister = GetExpressionConstant( 1.0f );
	stage->specularPowerRegister = GetExpressionConstant( -1.0f );
	stage->cullType = CT_INVALID;
	stage->destinationBuffer = -1;
}

int idMaterial::NameToSrcBlendMode( const idStr& name ) {
	if ( !name.Icmp( "GL_ONE" ) ) return 0x0;
	if ( !name.Icmp( "GL_ZERO" ) ) return 0x1;
	if ( !name.Icmp( "GL_DST_COLOR" ) ) return 0x3;
	if ( !name.Icmp( "GL_ONE_MINUS_DST_COLOR" ) ) return 0x4;
	if ( !name.Icmp( "GL_SRC_ALPHA" ) ) return 0x5;
	if ( !name.Icmp( "GL_ONE_MINUS_SRC_ALPHA" ) ) return 0x6;
	if ( !name.Icmp( "GL_DST_ALPHA" ) ) return 0x7;
	if ( !name.Icmp( "GL_ONE_MINUS_DST_ALPHA" ) ) return 0x8;
	if ( !name.Icmp( "GL_SRC_ALPHA_SATURATE" ) ) return 0x9;
	common->Warning( "unknown blend mode '%s' in material '%s'", name.c_str(), GetName() );
	materialFlags |= MF_DEFAULTED;
	return 0;
}

int idMaterial::NameToDstBlendMode( const idStr& name ) {
	if ( !name.Icmp( "GL_ZERO" ) ) return 0x00;
	if ( !name.Icmp( "GL_ONE" ) ) return 0x20;
	if ( !name.Icmp( "GL_SRC_COLOR" ) ) return 0x30;
	if ( !name.Icmp( "GL_ONE_MINUS_SRC_COLOR" ) ) return 0x40;
	if ( !name.Icmp( "GL_SRC_ALPHA" ) ) return 0x50;
	if ( !name.Icmp( "GL_ONE_MINUS_SRC_ALPHA" ) ) return 0x60;
	if ( !name.Icmp( "GL_DST_ALPHA" ) ) return 0x70;
	if ( !name.Icmp( "GL_ONE_MINUS_DST_ALPHA" ) ) return 0x80;
	common->Warning( "unknown blend mode '%s' in material '%s'", name.c_str(), GetName() );
	materialFlags |= MF_DEFAULTED;
	return 0x20;
}

void idMaterial::ParseBlend( idParser& src, materialStage_t* stage ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return;
	}
	stage->drawStateBits &= ~0xFF;
	if ( !token.Icmp( "blend" ) ) {
		stage->drawStateBits |= 0x65;
	} else if ( !token.Icmp( "add" ) ) {
		stage->drawStateBits |= 0x20;
	} else if ( !token.Icmp( "filter" ) || !token.Icmp( "modulate" ) ) {
		stage->drawStateBits |= 0x03;
	} else if ( !token.Icmp( "none" ) ) {
		stage->drawStateBits |= 0x21;
	} else if ( !token.Icmp( "screen" ) ) {
		stage->drawStateBits |= 0x24;
	} else if ( !token.Icmp( "lighten" ) ) {
		stage->drawStateBits |= 0x08000020;
	} else if ( !token.Icmp( "darken" ) ) {
		stage->drawStateBits |= 0x04000020;
	} else {
		const int srcMode = NameToSrcBlendMode( token );
		MatchToken( src, "," );
		if ( src.ReadToken( &token ) ) {
			stage->drawStateBits |= srcMode | NameToDstBlendMode( token );
		}
	}
}

void idMaterial::ParseFillMode( idParser& src, materialStage_t* stage ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return;
	}
	if ( !token.Icmp( "lines" ) ) {
		stage->drawStateBits |= 0x2000;
		if ( src.ReadTokenOnLine( &token ) && token.type == TT_NUMBER ) {
			stage->lineWidth = token.GetFloatValue();
		} else {
			stage->lineWidth = 1.0f;
			if ( token.Length() != 0 ) src.UnreadToken( token );
		}
	} else if ( token.Icmp( "fill" ) ) {
		src.Warning( "unknown fill mode '%s'", token.c_str() );
	}
}

void idMaterial::ParseCullFace( idParser& src, materialStage_t* stage ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		return;
	}
	if ( !token.Icmp( "front" ) ) stage->cullType = CT_BACK_SIDED;
	else if ( !token.Icmp( "back" ) ) stage->cullType = CT_FRONT_SIDED;
	else if ( !token.Icmp( "none" ) ) stage->cullType = CT_TWO_SIDED;
	else src.Warning( "unknown cull face mode '%s'", token.c_str() );
}

void idMaterial::MultiplyTextureMatrix( stageTextureMatrix_t* stm, int registers[ 2 ][ 3 ], const sdDeclRenderBinding* matrix_s, const sdDeclRenderBinding* matrix_t ) {
	if ( stm->renderBinding_s == NULL ) {
		memcpy( stm->matrix, registers, sizeof( stm->matrix ) );
		stm->renderBinding_s = matrix_s;
		stm->renderBinding_t = matrix_t;
		return;
	}
	const int old[ 2 ][ 3 ] = {
		{ stm->matrix[ 0 ][ 0 ], stm->matrix[ 0 ][ 1 ], stm->matrix[ 0 ][ 2 ] },
		{ stm->matrix[ 1 ][ 0 ], stm->matrix[ 1 ][ 1 ], stm->matrix[ 1 ][ 2 ] }
	};
	for ( int row = 0; row < 2; ++row ) {
		stm->matrix[ row ][ 0 ] = EmitOp( EmitOp( old[ row ][ 0 ], registers[ 0 ][ 0 ], OP_TYPE_MULTIPLY ), EmitOp( old[ row ][ 1 ], registers[ 1 ][ 0 ], OP_TYPE_MULTIPLY ), OP_TYPE_ADD );
		stm->matrix[ row ][ 1 ] = EmitOp( EmitOp( old[ row ][ 0 ], registers[ 0 ][ 1 ], OP_TYPE_MULTIPLY ), EmitOp( old[ row ][ 1 ], registers[ 1 ][ 1 ], OP_TYPE_MULTIPLY ), OP_TYPE_ADD );
		stm->matrix[ row ][ 2 ] = EmitOp( EmitOp( EmitOp( old[ row ][ 0 ], registers[ 0 ][ 2 ], OP_TYPE_MULTIPLY ), EmitOp( old[ row ][ 1 ], registers[ 1 ][ 2 ], OP_TYPE_MULTIPLY ), OP_TYPE_ADD ), old[ row ][ 2 ], OP_TYPE_ADD );
	}
}

bool idMaterial::ParseTextureMatrixKey( idToken& key, idParser& src, stageTextureMatrix_t& stm, const sdDeclRenderBinding* matrix_s, const sdDeclRenderBinding* matrix_t ) {
	int matrix[ 2 ][ 3 ];
	if ( !key.Icmp( "matrix" ) ) {
		for ( int row = 0; row < 2; ++row ) {
			for ( int column = 0; column < 3; ++column ) {
				matrix[ row ][ column ] = ParseExpression( src );
				if ( row != 1 || column != 2 ) MatchToken( src, "," );
			}
		}
	} else if ( !key.Icmp( "scroll" ) || !key.Icmp( "translate" ) ) {
		matrix[ 0 ][ 0 ] = GetExpressionConstant( 1.0f );
		matrix[ 0 ][ 1 ] = GetExpressionConstant( 0.0f );
		matrix[ 0 ][ 2 ] = ParseExpression( src );
		MatchToken( src, "," );
		matrix[ 1 ][ 0 ] = GetExpressionConstant( 0.0f );
		matrix[ 1 ][ 1 ] = GetExpressionConstant( 1.0f );
		matrix[ 1 ][ 2 ] = ParseExpression( src );
	} else if ( !key.Icmp( "scale" ) ) {
		matrix[ 0 ][ 0 ] = ParseExpression( src );
		matrix[ 0 ][ 1 ] = GetExpressionConstant( 0.0f );
		matrix[ 0 ][ 2 ] = GetExpressionConstant( 0.0f );
		MatchToken( src, "," );
		matrix[ 1 ][ 0 ] = GetExpressionConstant( 0.0f );
		matrix[ 1 ][ 1 ] = ParseExpression( src );
		matrix[ 1 ][ 2 ] = GetExpressionConstant( 0.0f );
	} else if ( !key.Icmp( "centerScale" ) ) {
		const int x = ParseExpression( src );
		MatchToken( src, "," );
		const int y = ParseExpression( src );
		matrix[ 0 ][ 0 ] = x;
		matrix[ 0 ][ 1 ] = GetExpressionConstant( 0.0f );
		matrix[ 0 ][ 2 ] = EmitOp( GetExpressionConstant( 0.5f ), EmitOp( GetExpressionConstant( 0.5f ), x, OP_TYPE_MULTIPLY ), OP_TYPE_SUBTRACT );
		matrix[ 1 ][ 0 ] = GetExpressionConstant( 0.0f );
		matrix[ 1 ][ 1 ] = y;
		matrix[ 1 ][ 2 ] = EmitOp( GetExpressionConstant( 0.5f ), EmitOp( GetExpressionConstant( 0.5f ), y, OP_TYPE_MULTIPLY ), OP_TYPE_SUBTRACT );
	} else if ( !key.Icmp( "shear" ) ) {
		const int x = ParseExpression( src );
		MatchToken( src, "," );
		const int y = ParseExpression( src );
		matrix[ 0 ][ 0 ] = GetExpressionConstant( 1.0f );
		matrix[ 0 ][ 1 ] = x;
		matrix[ 0 ][ 2 ] = EmitOp( GetExpressionConstant( -0.5f ), x, OP_TYPE_MULTIPLY );
		matrix[ 1 ][ 0 ] = y;
		matrix[ 1 ][ 1 ] = GetExpressionConstant( 1.0f );
		matrix[ 1 ][ 2 ] = EmitOp( GetExpressionConstant( -0.5f ), y, OP_TYPE_MULTIPLY );
	} else if ( !key.Icmp( "rotate" ) ) {
		const idDeclTable* sinTable = declHolder.FindTable( "sinTable", false );
		const idDeclTable* cosTable = declHolder.FindTable( "cosTable", false );
		if ( sinTable == NULL || cosTable == NULL ) {
			materialFlags |= MF_DEFAULTED;
			return true;
		}
		const int angle = ParseExpression( src );
		expOp_t* sinOp = GetExpressionOp();
		sinOp->opType = OP_TYPE_TABLE; sinOp->a = sinTable->Index(); sinOp->b = angle; sinOp->c = GetExpressionTemporary();
		expOp_t* cosOp = GetExpressionOp();
		cosOp->opType = OP_TYPE_TABLE; cosOp->a = cosTable->Index(); cosOp->b = angle; cosOp->c = GetExpressionTemporary();
		matrix[ 0 ][ 0 ] = cosOp->c;
		matrix[ 0 ][ 1 ] = EmitOp( GetExpressionConstant( 0.0f ), sinOp->c, OP_TYPE_SUBTRACT );
		matrix[ 0 ][ 2 ] = EmitOp( EmitOp( EmitOp( GetExpressionConstant( -0.5f ), cosOp->c, OP_TYPE_MULTIPLY ), EmitOp( GetExpressionConstant( 0.5f ), sinOp->c, OP_TYPE_MULTIPLY ), OP_TYPE_ADD ), GetExpressionConstant( 0.5f ), OP_TYPE_ADD );
		matrix[ 1 ][ 0 ] = sinOp->c;
		matrix[ 1 ][ 1 ] = cosOp->c;
		matrix[ 1 ][ 2 ] = EmitOp( EmitOp( EmitOp( GetExpressionConstant( -0.5f ), sinOp->c, OP_TYPE_MULTIPLY ), EmitOp( GetExpressionConstant( -0.5f ), cosOp->c, OP_TYPE_MULTIPLY ), OP_TYPE_ADD ), GetExpressionConstant( 0.5f ), OP_TYPE_ADD );
	} else {
		return false;
	}
	MultiplyTextureMatrix( &stm, matrix, matrix_s, matrix_t );
	return true;
}

void idMaterial::CompleteStage( materialStage_t*, stageParseData_t& spd, const sdDeclRenderBinding** defaults, const int numDefaults ) {
	for ( int i = 0; i < numDefaults; ++i ) {
		const sdDeclRenderBinding* binding = defaults[ i ];
		if ( binding == NULL ) {
			continue;
		}
		if ( binding->GetBindingType() == sdDeclRenderBinding::BT_TEXTURE ) {
			int j;
			for ( j = 0; j < spd.numTextures; ++j ) {
				if ( spd.textures[ j ].renderBinding == binding ) break;
			}
			if ( j == spd.numTextures ) {
				if ( spd.numTextures == MAX_STAGE_TEXTURES ) {
					common->Warning( "material '%s': MAX_STAGE_TEXTURES hit", GetName() );
					materialFlags |= MF_DEFAULTED;
					return;
				}
				spd.textures[ spd.numTextures ].renderBinding = binding;
				spd.textures[ spd.numTextures++ ].image = binding->GetDefaultImage();
			}
		} else if ( binding->GetBindingType() == sdDeclRenderBinding::BT_VECTOR ) {
			int j;
			for ( j = 0; j < spd.numVectors; ++j ) {
				if ( spd.vectors[ j ].renderBinding == binding ) break;
			}
			if ( j != spd.numVectors ) continue;
			for ( j = 0; j < spd.numTextureMatrices; ++j ) {
				if ( spd.textureMatrices[ j ].renderBinding_s == binding || spd.textureMatrices[ j ].renderBinding_t == binding ) break;
			}
			if ( j != spd.numTextureMatrices ) continue;
			if ( spd.numVectors == MAX_STAGE_VECTORS ) {
				common->Warning( "material '%s': MAX_STAGE_VECTORS hit", GetName() );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			stageVector_t& vector = spd.vectors[ spd.numVectors++ ];
			vector.renderBinding = binding;
			const float* values = binding->GetDefaultVector();
			for ( int component = 0; component < 4; ++component ) {
				vector.registers[ component ] = GetExpressionConstant( values[ component ] );
			}
		}
	}
}

void idMaterial::CompleteInterationStage( materialStage_t* stage, stageParseData_t& spd ) {
	if ( stage->megaTexture != NULL ) {
		const sdDeclRenderBinding* defaults[] = { rbinds->diffuseColor };
		CompleteStage( stage, spd, defaults, 1 );
		return;
	}
	const sdDeclRenderBinding* defaults[] = {
		rbinds->diffuseMap, rbinds->bumpMap, rbinds->specularMap,
		rbinds->diffuseColor, rbinds->specularColor,
		rbinds->diffuseMatrix_s, rbinds->diffuseMatrix_t,
		rbinds->bumpMatrix_s, rbinds->bumpMatrix_t,
		rbinds->specularMatrix_s, rbinds->specularMatrix_t
	};
	CompleteStage( stage, spd, defaults, sizeof( defaults ) / sizeof( defaults[ 0 ] ) );
	for ( int i = 0; i < spd.numTextures; ++i ) {
		const sdDeclRenderBinding* binding = spd.textures[ i ].renderBinding;
		if ( binding == rbinds->diffuseDetailMap || binding == rbinds->bumpDetailMap || binding == rbinds->specDetailMap ) {
			const sdDeclRenderBinding* detailDefaults[] = { rbinds->diffuseDetailMap, rbinds->bumpDetailMap, rbinds->specDetailMap };
			CompleteStage( stage, spd, detailDefaults, 3 );
			break;
		}
	}
}

void idMaterial::FinishStage( materialStage_t* stage, stageParseData_t& spd ) {
	++numStages;
	stage->numVectors = spd.numVectors;
	if ( stage->numVectors != 0 ) {
		stage->vectors = static_cast< stageVector_t* >( Mem_Alloc( sizeof( stageVector_t ) * stage->numVectors ) );
		memcpy( stage->vectors, spd.vectors, sizeof( stageVector_t ) * stage->numVectors );
	}
	stage->numTextures = spd.numTextures;
	if ( stage->numTextures != 0 ) {
		stage->textures = static_cast< stageTexture_t* >( Mem_Alloc( sizeof( stageTexture_t ) * stage->numTextures ) );
		memcpy( stage->textures, spd.textures, sizeof( stageTexture_t ) * stage->numTextures );
	}
	stage->numTextureMatrices = spd.numTextureMatrices;
	if ( stage->numTextureMatrices != 0 ) {
		stage->textureMatrices = static_cast< stageTextureMatrix_t* >( Mem_Alloc( sizeof( stageTextureMatrix_t ) * stage->numTextureMatrices ) );
		memcpy( stage->textureMatrices, spd.textureMatrices, sizeof( stageTextureMatrix_t ) * stage->numTextureMatrices );
	}
	for ( int i = 0; i < stage->numVectors; ++i ) {
		if ( stage->vectors[ i ].renderBinding == rbinds->diffuseColor ) {
			stage->colorVector = &stage->vectors[ i ];
		}
		declManager->AddDependency( this, stage->vectors[ i ].renderBinding );
	}
	for ( int i = 0; i < stage->numTextureMatrices; ++i ) {
		if ( stage->textureMatrices[ i ].renderBinding_s == rbinds->diffuseMatrix_s ) {
			stage->diffuseTextureMatrix = &stage->textureMatrices[ i ];
		}
		declManager->AddDependency( this, stage->textureMatrices[ i ].renderBinding_s );
		declManager->AddDependency( this, stage->textureMatrices[ i ].renderBinding_t );
	}
	for ( int i = 0; i < stage->numTextures; ++i ) {
		declManager->AddDependency( this, stage->textures[ i ].renderBinding );
	}
	if ( stage->renderProgram != NULL ) {
		declManager->AddDependency( this, stage->renderProgram );
	}
}

void idMaterial::ParseStage( idParser& src ) {
	if ( numStages == MAX_SHADER_STAGES ) {
		common->Warning( "material '%s' exceeded %i stages", GetName(), MAX_SHADER_STAGES );
		materialFlags |= MF_DEFAULTED;
		return;
	}
	materialStage_t* stage = &pd->parseStages[ numStages ];
	ClearStage( stage );
	stageParseData_t spd;
	stageTextureMatrix_t globalMatrix;
	memset( &globalMatrix, 0, sizeof( globalMatrix ) );
	idStr megaTextureName;
	stageVector_t globalColor;
	globalColor.renderBinding = NULL;
	for ( int i = 0; i < 4; ++i ) globalColor.registers[ i ] = GetExpressionConstant( 1.0f );

	if ( src.CheckTokenString( "if" ) ) {
		if ( src.CheckTokenString( "cvar" ) ) {
			bool enabled = true;
			if ( !ParseConstantCVarExpression( src, enabled ) ) {
				materialFlags |= MF_DEFAULTED;
				return;
			}
			if ( !enabled ) {
				src.SkipBracedSection( false );
				return;
			}
		} else {
			stage->conditionRegister = ParseExpression( src );
		}
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( !megaTextureName.IsEmpty() ) {
				stage->megaTexture = globalImages->MegaTextureFromFile( megaTextureName.c_str() );
				if ( stage->megaTexture == NULL ) {
					src.Warning( "Unable to load megaTexture '%s'", megaTextureName.c_str() );
				} else {
					// The backend uses this ETQW material flag to include MegaTexture
					// surfaces in its ambient interaction pass.
					materialFlags |= MF_HASMEGA;
				}
			}
			if ( globalMatrix.renderBinding_s != NULL ) {
				if ( spd.numTextureMatrices == MAX_STAGE_TEXTUREMATRICES ) {
					materialFlags |= MF_DEFAULTED;
					return;
				}
				spd.textureMatrices[ spd.numTextureMatrices++ ] = globalMatrix;
			}
			if ( globalColor.renderBinding != NULL ) {
				if ( spd.numVectors == MAX_STAGE_VECTORS ) {
					materialFlags |= MF_DEFAULTED;
					return;
				}
				spd.vectors[ spd.numVectors++ ] = globalColor;
			}

			bool hasInteractionTexture = false;
			bool hasDetailTexture = false;
			for ( int i = 0; i < spd.numTextures; ++i ) {
				const sdDeclRenderBinding* binding = spd.textures[ i ].renderBinding;
				hasInteractionTexture |= binding == rbinds->diffuseMap || binding == rbinds->bumpMap || binding == rbinds->specularMap;
				hasDetailTexture |= binding == rbinds->diffuseDetailMap || binding == rbinds->bumpDetailMap || binding == rbinds->specDetailMap;
			}
			if ( stage->renderProgram == NULL ) {
				if ( fogLight ) stage->renderProgram = rbinds->fogLightProgram;
				else if ( blendLight ) stage->renderProgram = rbinds->blendLightProgram;
				else if ( hasInteractionTexture ) {
					if ( stage->hasAlphaTest ) stage->renderProgram = hasDetailTexture ? rbinds->interactionBasicDetailAlphatestProgram : rbinds->interactionBasicAlphatestProgram;
					else stage->renderProgram = hasDetailTexture ? rbinds->interactionBasicDetailProgram : rbinds->interactionBasicProgram;
				} else stage->renderProgram = rbinds->trivialWithTextureMatrixProgram;
			}
			if ( stage->renderProgram != NULL && stage->renderProgram->IsInteraction() ) {
				CompleteInterationStage( stage, spd );
			} else if ( stage->renderProgram != NULL ) {
				const int count = stage->renderProgram->GetNumTextureBindings();
				const sdDeclRenderBinding* defaults[ MAX_STAGE_TEXTURES ];
				for ( int i = 0; i < count; ++i ) defaults[ i ] = stage->renderProgram->GetTextureBinding( i );
				CompleteStage( stage, spd, defaults, count );
			}
			FinishStage( stage, spd );
			return;
		}

		if ( !token.Icmp( "blend" ) ) {
			ParseBlend( src, stage );
		} else if ( !token.Icmp( "blendFunc" ) ) {
			if ( src.ReadToken( &token ) ) {
				if ( !token.Icmp( "subtract" ) ) stage->drawStateBits |= 0x01000000;
				else if ( !token.Icmp( "reverseSubtract" ) ) stage->drawStateBits |= 0x02000000;
				else if ( !token.Icmp( "min" ) ) stage->drawStateBits |= 0x04000000;
				else if ( !token.Icmp( "max" ) ) stage->drawStateBits |= 0x08000000;
			}
		} else if ( !token.Icmp( "depthFunc" ) ) {
			if ( src.ReadToken( &token ) ) {
				stage->drawStateBits &= ~0x70000;
				if ( !token.Icmp( "equal" ) ) stage->drawStateBits |= 0x20000;
				else if ( !token.Icmp( "lequal" ) ) stage->drawStateBits |= 0x40000;
				else if ( !token.Icmp( "always" ) ) stage->drawStateBits |= 0x10000;
				stage->hasExplicitDepthFunc = true;
			}
		} else if ( !token.Icmp( "fillMode" ) ) {
			ParseFillMode( src, stage );
		} else if ( !token.Icmp( "cullFace" ) ) {
			ParseCullFace( src, stage );
		} else if ( !token.Icmp( "updateCurrentRender" ) ) {
			stage->updateCurrentRender = true;
			materialFlags |= MF_UPDATECURRENTRENDER;
		} else if ( !token.Icmp( "vertexColor" ) ) {
			stage->vertexColor = SVC_MODULATE;
		} else if ( !token.Icmp( "vertexAlpha" ) ) {
			stage->vertexColor = SVC_MODULATE_ALPHA;
		} else if ( !token.Icmp( "inverseVertexColor" ) ) {
			stage->vertexColor = SVC_INVERSE_MODULATE;
		} else if ( !token.Icmp( "privatePolygonOffset" ) ) {
			stage->privatePolygonOffset = 1.0f;
			idToken value;
			if ( src.ReadTokenOnLine( &value ) ) stage->privatePolygonOffset = value.GetFloatValue();
		} else if ( ParseTextureMatrixKey( token, src, globalMatrix, rbinds->diffuseMatrix_s, rbinds->diffuseMatrix_t ) ) {
			continue;
		} else if ( !token.Icmp( "alphaToCoverage" ) ) stage->drawStateBits |= 0x4000;
		else if ( !token.Icmp( "maskRed" ) ) stage->drawStateBits |= 0x200;
		else if ( !token.Icmp( "maskGreen" ) ) stage->drawStateBits |= 0x400;
		else if ( !token.Icmp( "maskBlue" ) ) stage->drawStateBits |= 0x800;
		else if ( !token.Icmp( "maskAlpha" ) ) stage->drawStateBits |= 0x1000;
		else if ( !token.Icmp( "maskColor" ) ) stage->drawStateBits |= 0xE00;
		else if ( !token.Icmp( "maskDepth" ) ) { stage->drawStateBits |= 0x100; stage->hasExplicitDepthMask = true; }
		else if ( !token.Icmp( "writeDepth" ) ) stage->hasExplicitDepthMask = true;
		else if ( !token.Icmp( "alphaTest" ) ) { stage->hasAlphaTest = true; stage->alphaTestRegister = ParseExpression( src ); coverage = MC_PERFORATED; }
		else if ( !token.Icmp( "specularPower" ) ) stage->specularPowerRegister = ParseExpression( src );
		else if ( !token.Icmp( "colored" ) ) {
			globalColor.renderBinding = rbinds->diffuseColor;
			for ( int i = 0; i < 4; ++i ) globalColor.registers[ i ] = EXP_REG_PARM0 + i;
			pd->registersAreConstant = false;
		} else if ( !token.Icmp( "color" ) ) {
			globalColor.renderBinding = rbinds->diffuseColor;
			for ( int i = 0; i < 4; ++i ) { globalColor.registers[ i ] = ParseExpression( src ); if ( i != 3 ) MatchToken( src, "," ); }
		} else if ( !token.Icmp( "red" ) || !token.Icmp( "green" ) || !token.Icmp( "blue" ) || !token.Icmp( "alpha" ) ) {
			globalColor.renderBinding = rbinds->diffuseColor;
			const int component = !token.Icmp( "red" ) ? 0 : !token.Icmp( "green" ) ? 1 : !token.Icmp( "blue" ) ? 2 : 3;
			globalColor.registers[ component ] = ParseExpression( src );
		} else if ( !token.Icmp( "rgb" ) || !token.Icmp( "rgba" ) ) {
			globalColor.renderBinding = rbinds->diffuseColor;
			const int value = ParseExpression( src );
			globalColor.registers[ 0 ] = globalColor.registers[ 1 ] = globalColor.registers[ 2 ] = value;
			if ( !token.Icmp( "rgba" ) ) globalColor.registers[ 3 ] = value;
		} else if ( !token.Icmp( "program" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				stage->renderProgram = declHolder.FindRenderProgram( token.c_str(), false );
				if ( stage->renderProgram == NULL ) {
					src.Warning( "Unable to find render program '%s'", token.c_str() );
					materialFlags |= MF_DEFAULTED;
					return;
				}
			}
		} else if ( !token.Icmp( "megaTexture" ) ) {
			if ( !src.ReadTokenOnLine( &token ) ) {
				src.Warning( "Expected filename after megaTexture" );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			megaTextureName = token;
		} else if ( !token.Icmp( "textureMatrix" ) ) {
			if ( spd.numTextureMatrices == MAX_STAGE_TEXTUREMATRICES ) {
				src.Warning( "MAX_STAGE_TEXTUREMATRICES hit" );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			if ( !src.ReadTokenOnLine( &token ) ) {
				src.Warning( "textureMatrix definition without a binding name" );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			const sdDeclRenderBinding* matrixS = declHolder.FindRenderBinding( va( "%s_s", token.c_str() ), false );
			const sdDeclRenderBinding* matrixT = declHolder.FindRenderBinding( va( "%s_t", token.c_str() ), false );
			if ( matrixS == NULL || matrixT == NULL ) {
				src.Warning( "Missing render binding for textureMatrix" );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			if ( matrixS->GetBindingType() != sdDeclRenderBinding::BT_VECTOR || matrixT->GetBindingType() != sdDeclRenderBinding::BT_VECTOR ) {
				src.Warning( "Bad render binding type for textureMatrix" );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			if ( !src.ExpectTokenString( "{" ) ) {
				materialFlags |= MF_DEFAULTED;
				return;
			}
			stageTextureMatrix_t& matrix = spd.textureMatrices[ spd.numTextureMatrices ];
			memset( &matrix, 0, sizeof( matrix ) );
			while ( src.ReadToken( &token ) && token != "}" ) {
				if ( !ParseTextureMatrixKey( token, src, matrix, matrixS, matrixT ) ) {
					src.Warning( "Unknown token '%s'", token.c_str() );
					materialFlags |= MF_DEFAULTED;
					return;
				}
			}
			if ( matrix.renderBinding_t == NULL ) {
				src.Warning( "textureMatrix definition without parameters" );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			++spd.numTextureMatrices;
		} else if ( !token.Icmp( "destinationBuffer" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) stage->destinationBuffer = token.GetIntValue();
		} else if ( !token.Icmp( "breakpoint" ) ) {
			stage->breakpoint = true;
		} else {
			const sdDeclRenderBinding* binding = declHolder.FindRenderBinding( token.c_str(), false );
			if ( binding == NULL ) {
				src.Warning( "Unknown token '%s'", token.c_str() );
				materialFlags |= MF_DEFAULTED;
				return;
			}
			if ( binding->GetBindingType() == sdDeclRenderBinding::BT_TEXTURE ) {
				if ( spd.numTextures == MAX_STAGE_TEXTURES ) { materialFlags |= MF_DEFAULTED; return; }
				imageParams_t parms;
				parms.td = binding->GetTextureDepth();
				parms.cubeMap = binding->GetCubeMap();
				idImage* image = idImageManager::ParseImage( src, parms );
				if ( image == NULL ) { materialFlags |= MF_DEFAULTED; return; }
				spd.textures[ spd.numTextures ].image = image;
				spd.textures[ spd.numTextures++ ].renderBinding = binding;
			} else if ( binding->GetBindingType() == sdDeclRenderBinding::BT_VECTOR ) {
				if ( spd.numVectors == MAX_STAGE_VECTORS ) { materialFlags |= MF_DEFAULTED; return; }
				stageVector_t& vector = spd.vectors[ spd.numVectors++ ];
				vector.renderBinding = binding;
				vector.registers[ 0 ] = ParseExpression( src );
				int count = 1;
				while ( count < 4 && src.CheckTokenString( "," ) ) vector.registers[ count++ ] = ParseExpression( src );
				if ( count == 1 ) vector.registers[ 1 ] = vector.registers[ 2 ] = vector.registers[ 3 ] = vector.registers[ 0 ];
				else {
					while ( count < 3 ) vector.registers[ count++ ] = GetExpressionConstant( 0.0f );
					if ( count < 4 ) vector.registers[ count ] = GetExpressionConstant( 1.0f );
				}
			} else {
				src.Warning( "Unsupported render binding type '%s'", token.c_str() );
				materialFlags |= MF_DEFAULTED;
				return;
			}
		}
		if ( materialFlags & MF_DEFAULTED ) return;
	}
	materialFlags |= MF_DEFAULTED;
}

bool idMaterial::ParseConstantCVarExpression( idParser& src, bool& result ) {
	idToken token;
	bool negate = src.CheckTokenString( "!" ) != 0;
	const bool parenthesized = src.CheckTokenString( "(" ) != 0;
	if ( !src.ReadToken( &token ) ) return false;
	const idStr variableName = token;
	if ( !src.ReadToken( &token ) ) {
		result = cvarSystem->GetCVarBool( variableName.c_str() );
		result = negate ? !result : result;
		return true;
	}
	if ( parenthesized && token == ")" ) {
		result = cvarSystem->GetCVarBool( variableName.c_str() );
		result = negate ? !result : result;
		return true;
	}
	if ( !parenthesized ) {
		src.UnreadToken( token );
		result = cvarSystem->GetCVarBool( variableName.c_str() );
		result = negate ? !result : result;
		return true;
	}
	const idStr comparison = token;
	if ( !src.ReadToken( &token ) ) return false;
	const float lhs = cvarSystem->GetCVarFloat( variableName.c_str() );
	const float rhs = token.GetFloatValue();
	if ( comparison == "==" ) result = lhs == rhs;
	else if ( comparison == "!=" ) result = lhs != rhs;
	else if ( comparison == ">" ) result = lhs > rhs;
	else if ( comparison == ">=" ) result = lhs >= rhs;
	else if ( comparison == "<" ) result = lhs < rhs;
	else if ( comparison == "<=" ) result = lhs <= rhs;
	else return false;
	if ( !src.ExpectTokenString( ")" ) ) return false;
	if ( negate ) result = !result;
	return true;
}

void idMaterial::ParseDecalInfo( idParser& src ) {
	decalInfo.stayTime = static_cast< int >( src.ParseFloat() * 1000.0f );
	decalInfo.fadeTime = static_cast< int >( src.ParseFloat() * 1000.0f );
	src.Parse1DMatrix( 4, decalInfo.start.ToFloatPtr() );
	src.Parse1DMatrix( 4, decalInfo.end.ToFloatPtr() );
}

void idMaterial::ParseDeform( idParser& src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) return;
	if ( !token.Icmp( "sprite" ) ) { deform = DFRM_SPRITE; cullType = CT_TWO_SIDED; materialFlags |= MF_NOSHADOWS; }
	else if ( !token.Icmp( "tube" ) ) { deform = DFRM_TUBE; cullType = CT_TWO_SIDED; materialFlags |= MF_NOSHADOWS; }
	else if ( !token.Icmp( "flare" ) || !token.Icmp( "flarevcol" ) ) { deform = !token.Icmp( "flare" ) ? DFRM_FLARE : DFRM_FLARE_VCOL; cullType = CT_TWO_SIDED; deformRegisters[ 0 ] = ParseExpression( src ); materialFlags |= MF_NOSHADOWS; }
	else if ( !token.Icmp( "expand" ) ) { deform = DFRM_EXPAND; deformRegisters[ 0 ] = ParseExpression( src ); }
	else if ( !token.Icmp( "move" ) ) { deform = DFRM_MOVE; deformRegisters[ 0 ] = ParseExpression( src ); }
	else if ( !token.Icmp( "eyeBall" ) ) deform = DFRM_EYEBALL;
	else if ( !token.Icmp( "clusterTransform" ) ) materialFlags |= MF_CLUSTERTRANSFORM;
	else src.SkipRestOfLine();
}

void idMaterial::ParseMaterial( idParser& src ) {
	idImage* globalDiffuseMap = NULL;
	idImage* globalBumpMap = NULL;
	idImage* globalSpecularMap = NULL;
	numOps = 0;
	numRegisters = EXP_REG_NUM_PREDEFINED;
	for ( int i = 0; i < numRegisters; ++i ) pd->registerIsTemporary[ i ] = true;
	numStages = 0;
	doLodDistance = false;

	idToken token;
	while ( !( materialFlags & MF_DEFAULTED ) && src.ReadToken( &token ) ) {
		if ( token == "}" ) break;
		if ( token == "{" ) {
			ParseStage( src );
			continue;
		}
		if ( CheckSurfaceParm( &token ) ) continue;

		if ( !token.Icmp( "qer_editorimage" ) || !token.Icmp( "editorImage" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) editorImageName = token;
			src.SkipRestOfLine();
		} else if ( !token.Icmp( "description" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) desc = token;
		} else if ( !token.Icmp( "portal" ) ) {
			if ( src.ReadTokenOnLine( &token ) && !CheckPortalParm( &token ) ) {
				src.Warning( "bad portal flag '%s' in material '%s'", token.c_str(), GetName() );
				materialFlags |= MF_DEFAULTED;
			}
		} else if ( !token.Icmp( "surfaceType" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				surfaceTypeDecl = declHolder.FindSurfaceType( token.c_str(), false );
				if ( surfaceTypeDecl == NULL ) materialFlags |= MF_DEFAULTED;
			}
		} else if ( !token.Icmp( "surfaceColor" ) ) {
			if ( !src.Parse1DMatrix( 3, surfaceColor.ToFloatPtr() ) ) materialFlags |= MF_DEFAULTED;
		} else if ( !token.Icmp( "massive" ) ) {
			materialFlags |= MF_ADVERT;
			surfaceFlags |= SURF_DISCRETE;
		} else if ( !token.Icmp( "surfaceTypeMap" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				surfaceTypeMapDecl = declHolder.FindSurfaceTypeMap( token.c_str(), false );
				if ( surfaceTypeMapDecl == NULL ) surfaceTypeMap = surfaceTypeMapManager->SurfaceTypeMapFromFile( token.c_str(), false );
				if ( surfaceTypeMapDecl == NULL && surfaceTypeMap == NULL ) materialFlags |= MF_DEFAULTED;
			}
		} else if ( !token.Icmp( "polygonOffset" ) ) {
			materialFlags |= MF_POLYGONOFFSET;
			polygonOffset = 0.1f;
			idToken value;
			if ( src.ReadTokenOnLine( &value ) ) polygonOffset = value.GetFloatValue();
		} else if ( !token.Icmp( "noShadows" ) ) materialFlags |= MF_NOSHADOWS;
		else if ( !token.Icmp( "shadowMapped" ) ) materialFlags |= MF_SHADOWMAPPED | MF_NOSHADOWS;
		else if ( !token.Icmp( "noSelfShadow" ) ) materialFlags |= MF_NOSELFSHADOW;
		else if ( !token.Icmp( "noPortalFog" ) ) materialFlags |= MF_NOPORTALFOG;
		else if ( !token.Icmp( "forceShadows" ) ) materialFlags |= MF_FORCESHADOWS;
		else if ( !token.Icmp( "onlyAtmosphereInteraction" ) ) materialFlags |= MF_ONLYATMOSPHEREINTERACTION;
		else if ( !token.Icmp( "noAtmosphereInteraction" ) ) materialFlags |= MF_NOATMOSPHEREINTERACTION;
		else if ( !token.Icmp( "noAmbient" ) || !token.Icmp( "noAtmosphere" ) ) materialFlags |= MF_NOAMBIENT;
		else if ( !token.Icmp( "forceAtmosphere" ) ) materialFlags |= MF_FORCEATMOSPHERE;
		else if ( !token.Icmp( "forceTangents" ) ) materialFlags |= MF_FORCETANGENTS;
		else if ( !token.Icmp( "receivesLightingOnBackSides" ) ) materialFlags |= MF_RECEIVESLIGHTINGONBACKSIDES;
		else if ( !token.Icmp( "lowrangeuvs" ) ) materialFlags |= MF_LOWRANGEUVCOMPRESS;
		else if ( !token.Icmp( "noOverlays" ) ) allowOverlays = false;
		else if ( !token.Icmp( "forceOverlays" ) ) pd->forceOverlays = true;
		else if ( !token.Icmp( "translucent" ) ) coverage = MC_TRANSLUCENT;
		else if ( !token.Icmp( "noImplicitStages" ) ) materialFlags |= MF_NOIMPLICITSTAGES;
		else if ( !token.Icmp( "forceOpaque" ) ) coverage = MC_OPAQUE;
		else if ( !token.Icmp( "twoSided" ) ) { cullType = CT_TWO_SIDED; materialFlags |= MF_NOSHADOWS; }
		else if ( !token.Icmp( "backSided" ) ) { cullType = CT_BACK_SIDED; materialFlags |= MF_NOSHADOWS; }
		else if ( !token.Icmp( "backSide" ) ) {
			if ( src.ReadTokenOnLine( &token ) ) backSideMaterial = declHolder.FindMaterial( token.c_str(), false );
		} else if ( !token.Icmp( "flipBacksidedNormals" ) ) materialFlags |= MF_FLIPBACKSIDENORMALS;
		else if ( !token.Icmp( "updateCurrentRender" ) ) materialFlags |= MF_UPDATECURRENTRENDER;
		else if ( !token.Icmp( "shadowsCastOnlyFromStaticObjects" ) ) materialFlags |= MF_SHADOWSCASTONLYFROMSTATICOBJECTS;
		else if ( !token.Icmp( "fogLight" ) ) {
			fogLight = true;
			sort = SS_OPAQUE;
			idToken modifier;
			if ( src.ReadTokenOnLine( &modifier ) && !modifier.Icmp( "refractable" ) ) {
				sort = LS_REFRACTABLE;
			}
		}
		else if ( !token.Icmp( "blendLight" ) ) {
			blendLight = true;
			sort = SS_OPAQUE;
			idToken modifier;
			if ( src.ReadTokenOnLine( &modifier ) && !modifier.Icmp( "refractable" ) ) {
				sort = LS_REFRACTABLE;
			}
		}
		else if ( !token.Icmp( "ambientOcclusionLight" ) ) { blendLight = true; sort = SS_SUBVIEW; }
		else if ( !token.Icmp( "mirror" ) ) { coverage = MC_OPAQUE; sort = SS_SUBVIEW; }
		else if ( !token.Icmp( "noFog" ) ) noFog = true;
		else if ( !token.Icmp( "unsmoothedTangents" ) ) {
#if SD_SUPPORT_UNSMOOTHEDTANGENTS
			unsmoothedTangents = true;
#endif
		} else if ( !token.Icmp( "lightFalloffImage" ) ) {
			imageParams_t parms;
			parms.allowPicmip = false;
			lightFalloffImage = idImageManager::ParseImage( src, parms );
		} else if ( !token.Icmp( "guisurf" ) || !token.Icmp( "gui" ) ) {
			entityGui = 1;
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( !token.Icmp( "entity2" ) ) entityGui = 2;
				else if ( !token.Icmp( "entity3" ) ) entityGui = 3;
			}
		} else if ( !token.Icmp( "sort" ) ) ParseSort( src );
		else if ( !token.Icmp( "gpuSpec" ) ) { if ( src.ReadTokenOnLine( &token ) ) gpuSpec = token.GetIntValue(); }
		else if ( !token.Icmp( "spectrum" ) ) { if ( src.ReadTokenOnLine( &token ) ) spectrum = token.GetIntValue(); }
		else if ( !token.Icmp( "deform" ) ) ParseDeform( src );
		else if ( !token.Icmp( "decalInfo" ) ) ParseDecalInfo( src );
		else if ( !token.Icmp( "renderbump" ) ) src.ParseRestOfLine( renderBump );
		else if ( !token.Icmp( "diffusemap" ) ) {
			imageParams_t parms; parms.td = TD_DIFFUSE; globalDiffuseMap = idImageManager::ParseImage( src, parms );
			if ( globalDiffuseMap == NULL ) materialFlags |= MF_DEFAULTED;
		} else if ( !token.Icmp( "bumpmap" ) ) {
			imageParams_t parms; parms.td = TD_BUMP; globalBumpMap = idImageManager::ParseImage( src, parms );
			if ( globalBumpMap == NULL ) materialFlags |= MF_DEFAULTED;
		} else if ( !token.Icmp( "specularmap" ) ) {
			imageParams_t parms; parms.td = TD_SPECULAR; globalSpecularMap = idImageManager::ParseImage( src, parms );
			if ( globalSpecularMap == NULL ) materialFlags |= MF_DEFAULTED;
		} else if ( !token.Icmp( "imageLod" ) ) doLodDistance = true;
		else if ( !token.Icmp( "fullScreenPostProcess" ) ) materialFlags |= MF_FULLSCREENPOSTPROCESS;
		else if ( !token.Icmp( "noHardwareSkinning" ) ) materialFlags |= MF_NOHWSKINNING;
		else if ( !token.Icmp( "noSurfaceMerge" ) ) materialFlags |= MF_NOSURFACEMERGE;
		else if ( !token.Icmp( "vertexPositionOnly" ) ) materialFlags |= MF_VERTEXPOSITIONONLY;
		else if ( !token.Icmp( "forceSourceNormals" ) ) materialFlags |= MF_FORCESOURCENORMALS;
		else if ( !token.Icmp( "bakedInAtmosLightCol" ) ) materialFlags |= MF_BAKEDINATMOSLIGHTCOL;
		else if ( !token.Icmp( "translucentInteraction" ) ) materialFlags |= MF_TRANSLUCENTINTERACTION;
		else if ( !token.Icmp( "staticOccluder" ) ) materialFlags |= MF_OCCLUSION_OCCLUDE;
		else if ( !token.Icmp( "occlusionQuery" ) ) materialFlags |= MF_OCCLUSION_QUERY;
		else if ( !token.Icmp( "slopTexCoordMod" ) ) { if ( src.ReadTokenOnLine( &token ) ) slopTexCoordMod = token.GetFloatValue(); }
		else if ( !token.Icmp( "breakpoint_depthfill" ) ) breakpointFlags |= BP_DEPTHFILL;
		else if ( !token.Icmp( "breakpoint_ambient" ) ) breakpointFlags |= BP_AMBIENT;
		else if ( !token.Icmp( "breakpoint_atmosphere" ) ) breakpointFlags |= BP_ATMOSPHERE;
		else if ( !token.Icmp( "breakpoint_interaction" ) ) breakpointFlags |= BP_INTERACTION;
		else if ( !token.Icmp( "breakpoint_shadowbuffer" ) ) breakpointFlags |= BP_SHADOWBUFFER;
		else if ( !token.Icmp( "parmName" ) || !token.Icmp( "subviewInfo" ) ) src.SkipRestOfLine();
		else {
			src.Warning( "unknown general material parameter '%s'", token.c_str() );
			materialFlags |= MF_DEFAULTED;
		}
	}

	if ( !( materialFlags & MF_DEFAULTED ) && ( globalDiffuseMap != NULL || globalBumpMap != NULL || globalSpecularMap != NULL ) ) {
		if ( numStages == MAX_SHADER_STAGES ) { materialFlags |= MF_DEFAULTED; return; }
		materialStage_t* stage = &pd->parseStages[ numStages ];
		ClearStage( stage );
		stage->renderProgram = rbinds->interactionBasicProgram;
		stageParseData_t spd;
		if ( globalDiffuseMap != NULL ) { spd.textures[ spd.numTextures ].image = globalDiffuseMap; spd.textures[ spd.numTextures++ ].renderBinding = rbinds->diffuseMap; }
		if ( globalBumpMap != NULL ) { spd.textures[ spd.numTextures ].image = globalBumpMap; spd.textures[ spd.numTextures++ ].renderBinding = rbinds->bumpMap; }
		if ( globalSpecularMap != NULL ) { spd.textures[ spd.numTextures ].image = globalSpecularMap; spd.textures[ spd.numTextures++ ].renderBinding = rbinds->specularMap; }
		CompleteInterationStage( stage, spd );
		FinishStage( stage, spd );
	}
	if ( cullType == CT_TWO_SIDED ) {
		for ( int i = 0; i < numStages; ++i ) {
			if ( pd->parseStages[ i ].renderProgram != NULL && pd->parseStages[ i ].renderProgram->IsInteraction() ) {
				cullType = CT_FRONT_SIDED;
				shouldCreateBackSides = true;
				break;
			}
		}
	}
	for ( int i = 0; i < numStages; ++i ) {
		if ( pd->parseStages[ i ].cullType == CT_INVALID ) pd->parseStages[ i ].cullType = cullType;
	}
}

bool idMaterial::AddImplicitStages() {
	if ( materialFlags & MF_NOIMPLICITSTAGES ) return true;
	if ( coverage != MC_OPAQUE && coverage != MC_PERFORATED ) return true;
	if ( numStages == 0 ) return true;
	if ( numStages == MAX_SHADER_STAGES ) {
		materialFlags |= MF_DEFAULTED;
		return false;
	}

	materialStage_t* depthStage = &pd->parseStages[ numStages ];
	ClearStage( depthStage );
	depthStage->depthStage = true;
	stageParseData_t spd;
	if ( coverage == MC_PERFORATED ) {
		depthStage->hasAlphaTest = true;
		depthStage->renderProgram = rbinds->depthAlphaProgram;
		materialStage_t* alphaStage = NULL;
		for ( int i = 0; i < numStages; ++i ) {
			if ( pd->parseStages[ i ].hasAlphaTest ) { alphaStage = &pd->parseStages[ i ]; break; }
		}
		if ( alphaStage == NULL ) {
			common->Warning( "material '%s' is perforated without an alpha test stage", GetName() );
			materialFlags |= MF_DEFAULTED;
			return false;
		}
		depthStage->alphaTestRegister = alphaStage->alphaTestRegister;
		int textureIndex;
		for ( textureIndex = 0; textureIndex < alphaStage->numTextures; ++textureIndex ) {
			const sdDeclRenderBinding* binding = alphaStage->textures[ textureIndex ].renderBinding;
			if ( binding == rbinds->diffuseMap || binding == rbinds->map ) break;
		}
		if ( textureIndex == alphaStage->numTextures ) {
			common->Warning( "material '%s' has an alpha test stage with missing 'diffuseMap' or 'map'", GetName() );
			materialFlags |= MF_DEFAULTED;
			return false;
		}
		spd.textures[ spd.numTextures ].image = alphaStage->textures[ textureIndex ].image;
		spd.textures[ spd.numTextures++ ].renderBinding = rbinds->diffuseMap;
		for ( int i = 0; i < alphaStage->numTextureMatrices; ++i ) {
			if ( alphaStage->textureMatrices[ i ].renderBinding_s == rbinds->diffuseMatrix_s ) {
				spd.textureMatrices[ spd.numTextureMatrices++ ] = alphaStage->textureMatrices[ i ];
				break;
			}
		}
	} else {
		depthStage->renderProgram = rbinds->depthOnlyProgram;
	}
	const sdDeclRenderBinding* defaults[] = { rbinds->diffuseMatrix_s, rbinds->diffuseMatrix_t };
	CompleteStage( depthStage, spd, defaults, 2 );
	FinishStage( depthStage, spd );
	return true;
}

void idMaterial::CheckForConstantRegisters() {
	if ( !pd->registersAreConstant || numRegisters == 0 ) return;
	constantRegisters = static_cast< float* >( Mem_AllocAligned( sizeof( float ) * numRegisters, ALIGN_16 ) );
	memset( constantRegisters, 0, sizeof( float ) * numRegisters );
	timeBasedRegisters = pd->registersUseTime;
	float shaderParms[ MAX_ENTITY_SHADER_PARMS ];
	memset( shaderParms, 0, sizeof( shaderParms ) );
	EvaluateRegisters( constantRegisters, shaderParms, NULL, NULL, 0 );
}

#if 0 // superseded below by the PDB-layout material parser

bool idMaterial::Parse( const char* text, const int textLength ) {
	const float oldSurfaceArea = surfaceArea;
	FreeData();
	CommonInit();
	surfaceArea = oldSurfaceArea;

	if ( text == NULL || textLength <= 0 ) {
		materialFlags |= MF_DEFAULTED;
		return false;
	}

	idParser src;
	src.SetFlags(
		LEXFL_NOSTRINGCONCAT |
		LEXFL_ALLOWPATHNAMES |
		LEXFL_ALLOWMULTICHARLITERALS |
		LEXFL_ALLOWBACKSLASHSTRINGCONCAT
	);
	src.LoadMemory( text, textLength, GetName() != NULL ? GetName() : "<material>" );

	idToken token;
	if ( !src.SkipUntilString( "{", &token ) ) {
		materialFlags |= MF_DEFAULTED;
		return false;
	}

	idList< recoveredMaterialStage_t > recoveredStages;
	int depth = 1;
	int currentStage = -1;
	bool malformed = false;
	while ( depth > 0 && src.ReadToken( &token ) ) {
		if ( token == "{" ) {
			depth++;
			if ( depth == 2 ) {
				currentStage = recoveredStages.Append( recoveredMaterialStage_t() );
			}
			continue;
		}
		if ( token == "}" ) {
			if ( depth == 2 ) {
				currentStage = -1;
			}
			depth--;
			continue;
		}

		if ( depth == 1 ) {
			if ( CheckSurfaceParm( &token ) ) {
				continue;
			} else if ( !token.Icmp( "portal" ) ) {
				if ( src.ReadTokenOnLine( &token ) && !CheckPortalParm( &token ) ) {
					common->Warning( "unknown portal parameter '%s' in '%s'", token.c_str(), GetName() );
				}
			} else if ( !token.Icmp( "massive" ) ) {
				// Marks the model surface consumed by sdAdEntity.  The original
				// parser also keeps advert surfaces from being merged by dmap.
				materialFlags |= MF_ADVERT;
				surfaceFlags |= SURF_DISCRETE;
			} else if ( !token.Icmp( "surfaceType" ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
					surfaceTypeDecl = declHolder.FindSurfaceType( token.c_str(), false );
					if ( surfaceTypeDecl == NULL ) {
						common->Warning( "missing surface type '%s' in '%s'", token.c_str(), GetName() );
						materialFlags |= MF_DEFAULTED;
					}
				}
			} else if ( !token.Icmp( "surfaceColor" ) ) {
				if ( !src.Parse1DMatrix( 3, surfaceColor.ToFloatPtr() ) ) {
					common->Warning( "bad surfaceColor parameter in '%s'", GetName() );
					materialFlags |= MF_DEFAULTED;
				}
			} else if ( !token.Icmp( "surfaceTypeMap" ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
					surfaceTypeMapDecl = declHolder.FindSurfaceTypeMap( token.c_str(), false );
					if ( surfaceTypeMapDecl == NULL ) {
						surfaceTypeMap = surfaceTypeMapManager->SurfaceTypeMapFromFile( token.c_str(), false );
					}
					if ( surfaceTypeMapDecl == NULL && surfaceTypeMap == NULL ) {
						common->Warning( "missing surface type map '%s' in '%s'", token.c_str(), GetName() );
						materialFlags |= MF_DEFAULTED;
					}
				}
			} else if ( !token.Icmp( "description" ) ) {
				if ( src.ReadToken( &token ) ) {
					desc = token;
				}
			} else if ( !token.Icmp( "qer_editorimage" ) || !token.Icmp( "editorImage" ) ) {
				if ( src.ReadToken( &token ) ) {
					editorImageName = token;
				}
			} else if ( !token.Icmp( "renderbump" ) ) {
				if ( src.ReadToken( &token ) ) {
					renderBump = token;
				}
			} else if ( !token.Icmp( "sort" ) ) {
				if ( src.ReadToken( &token ) ) {
					sort = SortForName( token );
				}
			} else if ( !token.Icmp( "cull" ) ) {
				if ( src.ReadToken( &token ) ) {
					if ( !token.Icmp( "none" ) || !token.Icmp( "twoSided" ) || !token.Icmp( "disable" ) ) {
						cullType = CT_TWO_SIDED;
					} else if ( !token.Icmp( "back" ) || !token.Icmp( "backSided" ) ) {
						cullType = CT_BACK_SIDED;
					} else {
						cullType = CT_FRONT_SIDED;
					}
				}
			} else if ( !token.Icmp( "translucent" ) ) {
				coverage = MC_TRANSLUCENT;
			} else if ( !token.Icmp( "forceOpaque" ) ) {
				coverage = MC_OPAQUE;
			} else if ( !token.Icmp( "noFog" ) ) {
				noFog = true;
			} else if ( !token.Icmp( "noShadows" ) ) {
				materialFlags |= MF_NOSHADOWS;
			} else if ( !token.Icmp( "forceShadows" ) ) {
				materialFlags |= MF_FORCESHADOWS;
			} else if ( !token.Icmp( "noSelfShadow" ) ) {
				materialFlags |= MF_NOSELFSHADOW;
			} else if ( !token.Icmp( "polygonOffset" ) ) {
				materialFlags |= MF_POLYGONOFFSET;
				polygonOffset = 1.0f;
			} else if ( !token.Icmp( "noOverlays" ) ) {
				allowOverlays = false;
			} else if ( !token.Icmp( "gui" ) ) {
				entityGui = 1;
			} else if ( !token.Icmp( "fogLight" ) ) {
				fogLight = true;
			} else if ( !token.Icmp( "blendLight" ) ) {
				blendLight = true;
			}
		} else if ( depth == 2 && currentStage >= 0 ) {
			if ( !token.Icmp( "map" ) ||
				 !token.Icmp( "diffuseMap" ) ||
				 !token.Icmp( "bumpMap" ) ||
				 !token.Icmp( "specularMap" ) ) {
				imageParams_t parms;
				if ( !token.Icmp( "diffuseMap" ) ) {
					parms.td = TD_DIFFUSE;
				} else if ( !token.Icmp( "bumpMap" ) ) {
					parms.td = TD_BUMP;
				} else if ( !token.Icmp( "specularMap" ) ) {
					parms.td = TD_SPECULAR;
				}
				recoveredStages[ currentStage ].image = idImageManager::ParseImage( src, parms );
			} else if ( !token.Icmp( "cinematicY" ) ) {
				imageParams_t parms;
				parms.allowPicmip = false;
				recoveredStages[ currentStage ].image = idImageManager::ParseImage( src, parms );
				recoveredStages[ currentStage ].renderBinding = declHolder.FindRenderBinding( "cinematicY", false );
			} else if ( !token.Icmp( "program" ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
					recoveredStages[ currentStage ].renderProgram = declHolder.FindRenderProgram( token.c_str(), false );
				}
			} else if ( !token.Icmp( "alphaTest" ) ) {
				coverage = MC_PERFORATED;
			} else if ( !token.Icmp( "blend" ) ) {
				coverage = MC_TRANSLUCENT;
			}
		}
	}

	if ( depth != 0 ) {
		malformed = true;
	}

	numStages = recoveredStages.Num();
	numAmbientStages = numStages;
	if ( numStages > 0 ) {
		stages = static_cast< materialStage_t* >( Mem_ClearedAlloc( sizeof( materialStage_t ) * numStages ) );
		for ( int i = 0; i < numStages; i++ ) {
			materialStage_t& stage = stages[ i ];
			stage.conditionRegister = 0;
			stage.cullType = cullType;
			stage.destinationBuffer = -1;
			stage.renderProgram = recoveredStages[ i ].renderProgram;
			if ( recoveredStages[ i ].image != NULL ) {
				stage.numTextures = 1;
				stage.textures = static_cast< stageTexture_t* >( Mem_ClearedAlloc( sizeof( stageTexture_t ) ) );
				stage.textures[ 0 ].image = recoveredStages[ i ].image;
				stage.textures[ 0 ].renderBinding = recoveredStages[ i ].renderBinding;
			}
		}
	}

	numRegisters = EXP_REG_NUM_PREDEFINED;
	expressionRegisters = static_cast< float* >( Mem_ClearedAlloc( sizeof( float ) * numRegisters ) );
	if ( coverage == MC_BAD ) {
		coverage = numStages > 0 ? MC_OPAQUE : MC_TRANSLUCENT;
	}
	if ( sort == SS_BAD ) {
		sort = coverage == MC_TRANSLUCENT ? SS_MEDIUM : SS_OPAQUE;
	}
	hasSubview = sort == SS_SUBVIEW;
	if ( coverage == MC_TRANSLUCENT ) {
		editorAlpha = 0.5f;
	} else {
		contentFlags |= CONTENTS_OPAQUE;
	}

	if ( malformed ) {
		materialFlags |= MF_DEFAULTED;
	}
	return !malformed;
}
#endif

bool idMaterial::Parse( const char* text, const int textLength ) {
	const float oldSurfaceArea = surfaceArea;
	FreeData();
	CommonInit();
	surfaceArea = oldSurfaceArea;
	if ( text == NULL || textLength <= 0 ) {
		materialFlags |= MF_DEFAULTED;
		return false;
	}

	idParser src;
	src.SetFlags( DECL_LEXER_FLAGS );
	sdDeclParseHelper declHelper( this, text, textLength, src );
	idToken openingBrace;
	if ( !src.SkipUntilString( "{", &openingBrace ) ) {
		materialFlags |= MF_DEFAULTED;
		return false;
	}

	mtrParsingData_s parsingData;
	memset( &parsingData, 0, sizeof( parsingData ) );
	parsingData.registersAreConstant = true;
	pd = &parsingData;
	ParseMaterial( src );

	numAmbientStages = 0;
	for ( int i = 0; i < numStages; ++i ) {
		const sdDeclRenderProgram* program = pd->parseStages[ i ].renderProgram;
		if ( program == NULL || !program->IsInteraction() ) ++numAmbientStages;
	}
	hasSubview = sort == SS_SUBVIEW;
	if ( coverage == MC_BAD ) {
		if ( numStages == 0 ) coverage = MC_TRANSLUCENT;
		else if ( numStages != numAmbientStages ) coverage = MC_OPAQUE;
		else {
			const int blendBits = pd->parseStages[ 0 ].drawStateBits & 0xFF;
			const int srcBlend = blendBits & 0x0F;
			coverage = ( ( blendBits & 0xF0 ) != 0 || srcBlend == 3 || srcBlend == 4 || srcBlend == 7 || srcBlend == 8 ) ? MC_TRANSLUCENT : MC_OPAQUE;
		}
	}
	if ( coverage == MC_TRANSLUCENT ) {
		materialFlags |= MF_NOSHADOWS;
		editorAlpha = 0.5f;
	} else {
		contentFlags |= CONTENTS_OPAQUE;
		editorAlpha = 1.0f;
	}
	if ( sort == SS_BAD ) {
		if ( materialFlags & MF_POLYGONOFFSET ) sort = SS_DECAL;
		else if ( coverage == MC_TRANSLUCENT ) sort = SS_MEDIUM;
		else if ( coverage == MC_PERFORATED ) sort = SS_OPAQUENEARER;
		else sort = SS_OPAQUE;
	}
	for ( int i = 0; i < numStages; ++i ) {
		materialStage_t& stage = pd->parseStages[ i ];
		if ( sort < SS_POST_PROCESS ) {
			if ( coverage != MC_TRANSLUCENT && !stage.hasExplicitDepthFunc ) stage.drawStateBits |= coverage == MC_OPAQUE ? 0x40000 : 0x20000;
			if ( !stage.hasExplicitDepthMask ) stage.drawStateBits |= 0x100;
		}
	}
	if ( pd->forceOverlays ) allowOverlays = true;
	else if ( numStages == 0 || coverage != MC_OPAQUE || ( surfaceFlags & SURF_NOIMPACT ) != 0 ) allowOverlays = false;

	if ( !( materialFlags & MF_DEFAULTED ) ) AddImplicitStages();
	if ( materialFlags & MF_DEFAULTED ) {
		for ( int i = 0; i < numStages; ++i ) {
			Mem_Free( pd->parseStages[ i ].vectors );
			Mem_Free( pd->parseStages[ i ].textures );
			Mem_Free( pd->parseStages[ i ].textureMatrices );
		}
		pd = NULL;
		numStages = 0;
		return false;
	}

	if ( numStages != 0 ) {
		stages = static_cast< materialStage_t* >( Mem_Alloc( sizeof( materialStage_t ) * numStages ) );
		memcpy( stages, pd->parseStages, sizeof( materialStage_t ) * numStages );
	}
	if ( numOps != 0 ) {
		ops = static_cast< expOp_t* >( Mem_Alloc( sizeof( expOp_t ) * numOps ) );
		memcpy( ops, pd->shaderOps, sizeof( expOp_t ) * numOps );
	}
	if ( numRegisters != 0 ) {
		expressionRegisters = static_cast< float* >( Mem_Alloc( sizeof( float ) * numRegisters ) );
		memcpy( expressionRegisters, pd->materialRegisters, sizeof( float ) * numRegisters );
	}
	CheckForConstantRegisters();
	pd = NULL;
	return true;
}

void idMaterial::Print() const {
	common->Printf(
		"material %s: %d stage%s, %d register%s, sort %.2f\n",
		GetName(),
		numStages,
		numStages == 1 ? "" : "s",
		numRegisters,
		numRegisters == 1 ? "" : "s",
		sort
	);
}

void idMaterial::CacheFromDict( const idDict& dict ) {
	const idKeyValue* value = NULL;
	while ( ( value = dict.MatchPrefix( "mtr", value ) ) != NULL ) {
		if ( value->GetValue().Length() != 0 ) {
			declHolder.FindMaterial( value->GetValue(), false );
		}
	}
}

idImage* idMaterial::GetEditorImage() const {
	if ( editorImage != NULL ) {
		return editorImage;
	}
	if ( !editorImageName.IsEmpty() ) {
		editorImage = LoadMaterialImage( editorImageName );
	}
	if ( editorImage == NULL && stages != NULL ) {
		for ( int i = 0; i < numStages && editorImage == NULL; i++ ) {
			for ( int j = 0; j < stages[ i ].numTextures; j++ ) {
				editorImage = stages[ i ].textures[ j ].image;
				if ( editorImage != NULL ) {
					break;
				}
			}
		}
	}
	if ( editorImage == NULL && globalImages != NULL ) {
		editorImage = globalImages->defaultImage;
	}
	return editorImage;
}

int idMaterial::GetImageWidth() const {
	idImage* image = GetEditorImage();
	return image != NULL ? image->uploadWidth : 0;
}

int idMaterial::GetImageHeight() const {
	idImage* image = GetEditorImage();
	return image != NULL ? image->uploadHeight : 0;
}

void idMaterial::ReloadImages( bool force ) const {
	for ( int i = 0; i < numStages; i++ ) {
		for ( int j = 0; j < stages[ i ].numTextures; j++ ) {
			idImage* image = stages[ i ].textures[ j ].image;
			if ( image != NULL ) {
				image->Reload( false, force );
			}
		}
	}
}

void idMaterial::SetLodDistance( float distance ) const {
	if ( !doLodDistance ) {
		return;
	}
	for ( int i = 0; i < numStages; i++ ) {
		for ( int j = 0; j < stages[ i ].numTextures; j++ ) {
			idImage* image = stages[ i ].textures[ j ].image;
			if ( image != NULL ) {
				image->smallestDistanceSeen = Min( image->smallestDistanceSeen, distance );
			}
		}
	}
}

void idMaterial::EvaluateRegisters(
	float* registers,
	const float shaderParms[ MAX_ENTITY_SHADER_PARMS ],
	const viewDef_s* view,
	idSoundEmitter* soundEmitter,
	int numManualLights
) const {
	if ( registers == NULL ) {
		return;
	}
	if ( numRegisters <= 0 ) {
		return;
	}

	memset( registers, 0, Min( numRegisters, static_cast< int >( EXP_REG_NUM_PREDEFINED ) ) * sizeof( float ) );
	if ( expressionRegisters != NULL && numRegisters > EXP_REG_NUM_PREDEFINED ) {
		memcpy( registers + EXP_REG_NUM_PREDEFINED, expressionRegisters + EXP_REG_NUM_PREDEFINED,
			( numRegisters - EXP_REG_NUM_PREDEFINED ) * sizeof( float ) );
	}
	registers[ EXP_REG_TIME ] = view != NULL ? view->floatTime : Sys_Milliseconds() * 0.001f;
	if ( shaderParms != NULL ) {
		memcpy( registers + EXP_REG_PARM0, shaderParms, MAX_ENTITY_SHADER_PARMS * sizeof( float ) );
	}
	if ( view != NULL ) {
		memcpy( registers + EXP_REG_GLOBAL0, view->renderView.shaderParms, MAX_GLOBAL_SHADER_PARMS * sizeof( float ) );
	}
	registers[ EXP_REG_NUMLIGHTS ] = static_cast< float >( numManualLights );

	for ( int i = 0; i < numOps; i++ ) {
		const expOp_t& op = ops[ i ];
		switch ( op.opType ) {
			case OP_TYPE_ADD:		registers[ op.c ] = registers[ op.a ] + registers[ op.b ]; break;
			case OP_TYPE_SUBTRACT:	registers[ op.c ] = registers[ op.a ] - registers[ op.b ]; break;
			case OP_TYPE_MULTIPLY:	registers[ op.c ] = registers[ op.a ] * registers[ op.b ]; break;
			case OP_TYPE_DIVIDE:		registers[ op.c ] = registers[ op.a ] / registers[ op.b ]; break;
			case OP_TYPE_MOD: {
				int divisor = static_cast< int >( registers[ op.b ] );
				if ( divisor == 0 ) {
					divisor = 1;
				}
				registers[ op.c ] = static_cast< float >( static_cast< int >( registers[ op.a ] ) % divisor );
				break;
			}
			case OP_TYPE_GT:			registers[ op.c ] = registers[ op.a ] > registers[ op.b ]; break;
			case OP_TYPE_GE:			registers[ op.c ] = registers[ op.a ] >= registers[ op.b ]; break;
			case OP_TYPE_LT:			registers[ op.c ] = registers[ op.a ] < registers[ op.b ]; break;
			case OP_TYPE_LE:			registers[ op.c ] = registers[ op.a ] <= registers[ op.b ]; break;
			case OP_TYPE_EQ:			registers[ op.c ] = registers[ op.a ] == registers[ op.b ]; break;
			case OP_TYPE_NE:			registers[ op.c ] = registers[ op.a ] != registers[ op.b ]; break;
			case OP_TYPE_AND:		registers[ op.c ] = registers[ op.a ] != 0.0f && registers[ op.b ] != 0.0f; break;
			case OP_TYPE_OR:			registers[ op.c ] = registers[ op.a ] != 0.0f || registers[ op.b ] != 0.0f; break;
			case OP_TYPE_TABLE: {
				const idDeclTable* table = declHolder.FindTableByIndex( op.a, true );
				registers[ op.c ] = table != NULL ? table->TableLookup( registers[ op.b ] ) : 0.0f;
				break;
			}
			case OP_TYPE_SOUND:		registers[ op.c ] = soundEmitter != NULL ? soundEmitter->CurrentAmplitude() : 0.0f; break;
			case OP_TYPE_LOAD:		registers[ op.c ] = op.a >= 0 && op.a < NUM_ATMOSPHERE_EXPRESSIONS ? atmosphereExpressionMemory[ op.a ] : 0.0f; break;
			default:				registers[ op.c ] = 0.0f; break;
		}
	}
}

const float* idMaterial::ConstantRegisters(
	const float shaderParms[ MAX_ENTITY_SHADER_PARMS ],
	const viewDef_s* view
) const {
	if ( !r_useConstantMaterials.GetBool() ) {
		return NULL;
	}
	if ( constantRegisters != NULL && view != NULL &&
		( atmosphereFrame != currentAtmosphereFrame ||
		( timeBasedRegisters && idMath::Fabs( lastFloatTime - view->floatTime ) > ( 1.0f / 60.0f ) ) ) ) {
		EvaluateRegisters( constantRegisters, shaderParms, view, NULL, 0 );
		lastFloatTime = view->floatTime;
		atmosphereFrame = currentAtmosphereFrame;
	}
	return constantRegisters;
}

void idMaterial::SetTextureMatrix( const stageTextureMatrix_t* textureMatrix, const float* materialRegisters, idVec4 matrix[ 2 ] ) {
	if ( textureMatrix == NULL ) {
		matrix[ 0 ].Set( 1.0f, 0.0f, 0.0f, 0.0f );
		matrix[ 1 ].Set( 0.0f, 1.0f, 0.0f, 0.0f );
		return;
	}
	matrix[ 0 ].Set( materialRegisters[ textureMatrix->matrix[ 0 ][ 0 ] ], materialRegisters[ textureMatrix->matrix[ 0 ][ 1 ] ], 0.0f, materialRegisters[ textureMatrix->matrix[ 0 ][ 2 ] ] );
	matrix[ 1 ].Set( materialRegisters[ textureMatrix->matrix[ 1 ][ 0 ] ], materialRegisters[ textureMatrix->matrix[ 1 ][ 1 ] ], 0.0f, materialRegisters[ textureMatrix->matrix[ 1 ][ 2 ] ] );
	if ( matrix[ 0 ].w < -40.0f || matrix[ 0 ].w > 40.0f ) matrix[ 0 ].w -= static_cast< int >( matrix[ 0 ].w );
	if ( matrix[ 1 ].w < -40.0f || matrix[ 1 ].w > 40.0f ) matrix[ 1 ].w -= static_cast< int >( matrix[ 1 ].w );
}

void idMaterial::SetRenderBindings( const materialStage_t* stage, const float* materialRegisters, float texCoordScale ) {
	if ( stage == NULL || materialRegisters == NULL || rbinds == NULL ) return;
	switch ( stage->vertexColor ) {
		case SVC_IGNORE: rbinds->colorModulate->Set( 0.0f ); rbinds->colorAdd->Set( 1.0f ); break;
		case SVC_MODULATE: rbinds->colorModulate->Set( 1.0f ); rbinds->colorAdd->Set( 0.0f ); break;
		case SVC_MODULATE_ALPHA: rbinds->colorModulate->Set( 0.0f, 0.0f, 0.0f, 1.0f ); rbinds->colorAdd->Set( 1.0f, 1.0f, 1.0f, 0.0f ); break;
		case SVC_INVERSE_MODULATE: rbinds->colorModulate->Set( -1.0f ); rbinds->colorAdd->Set( 1.0f ); break;
	}
	for ( int i = 0; i < stage->numVectors; ++i ) {
		const stageVector_t& vector = stage->vectors[ i ];
		vector.renderBinding->Set( materialRegisters[ vector.registers[ 0 ] ], materialRegisters[ vector.registers[ 1 ] ], materialRegisters[ vector.registers[ 2 ] ], materialRegisters[ vector.registers[ 3 ] ] );
	}
	for ( int i = 0; i < stage->numTextures; ++i ) stage->textures[ i ].renderBinding->Set( stage->textures[ i ].image );
	for ( int i = 0; i < stage->numTextureMatrices; ++i ) {
		idVec4 matrix[ 2 ];
		SetTextureMatrix( &stage->textureMatrices[ i ], materialRegisters, matrix );
		matrix[ 0 ].x *= texCoordScale; matrix[ 0 ].y *= texCoordScale; matrix[ 0 ].z *= texCoordScale;
		matrix[ 1 ].x *= texCoordScale; matrix[ 1 ].y *= texCoordScale; matrix[ 1 ].z *= texCoordScale;
		stage->textureMatrices[ i ].renderBinding_s->Set( matrix[ 0 ] );
		stage->textureMatrices[ i ].renderBinding_t->Set( matrix[ 1 ] );
	}
	if ( stage->numTextureMatrices == 0 ) {
		rbinds->diffuseMatrix_s->Set( texCoordScale, 0.0f, 0.0f, 0.0f );
		rbinds->diffuseMatrix_t->Set( 0.0f, texCoordScale, 0.0f, 0.0f );
	}
	if ( stage->megaTexture != NULL ) {
		stage->megaTexture->UpdateMapping( RB_GetDrawWorld() );
		const renderView_t* view = RB_GetDrawView();
		if ( view != NULL ) {
			stage->megaTexture->UpdateForViewOrigin( view->vieworg, view->time );
		}
	}
	if ( stage->imgSequence != NULL ) stage->imgSequence->UpdateBindings();
	if ( stage->hasAlphaTest ) rbinds->alphaThresh->Set( materialRegisters[ stage->alphaTestRegister ] );
}

void idMaterial::PurgePartialLoadableImages() {
}

void idMaterial::LoadPartialLoadableImages( bool ) {
}

bool idMaterial::IsFinishedPartialLoading() const {
	return true;
}
