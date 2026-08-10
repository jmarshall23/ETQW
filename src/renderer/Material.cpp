// Copyright (C) 2007 Id Software, Inc.
//
// ETQW material declaration boundary reconstructed from the Microsoft PDB and
// quakewars-hexrays/renderer/Material.cpp.  This unit intentionally implements
// the declaration/runtime state first; the recovered render-program expression
// compiler is being restored separately.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Material.h"
#include "Image.h"
#include "SurfaceTypeMap.h"
#include "../decllib/DeclSurfaceTypeMap.h"
#include "../decllib/declTypeHolder.h"

namespace {

struct recoveredMaterialStage_t {
	idImage*					image;
	const sdDeclRenderBinding*	renderBinding;
	const sdDeclRenderProgram*	renderProgram;

	recoveredMaterialStage_t() :
		image( NULL ),
		renderBinding( NULL ),
		renderProgram( NULL ) {
	}
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
	if ( !token.Icmp( "decal" ) ) {
		return SS_DECAL;
	}
	if ( !token.Icmp( "gui" ) ) {
		return SS_GUI;
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
	if ( !token.Icmp( "nearest" ) ) {
		return SS_NEAREST;
	}
	if ( !token.Icmp( "postProcess" ) ) {
		return SS_POST_PROCESS;
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

sdDeclRenderProgram::sdDeclRenderProgram() {
	program = NULL;
	FreeData();
}

sdDeclRenderProgram::~sdDeclRenderProgram() {
	FreeData();
}

const char* sdDeclRenderProgram::DefaultDefinition() const {
	return "{\n"
		"\tstate force {\n"
		"\t\tdepthFunc less\n"
		"\t}\n"
		"\tprogram vertex arb {\n"
		"\t\tOPTION ARB_position_invariant;\n"
		"\t}\n"
		"\tprogram fragment arb {\n"
		"\t\tPARAM colorOrange = { 1.0, 0.5, 0, 1 };\n"
		"\t\tMOV result.color, colorOrange;\n"
		"\t}\n"
		"}\n";
}

bool sdDeclRenderProgram::Parse( const char* text, const int textLength ) {
	FreeData();
	if ( text == NULL || textLength <= 0 ) {
		return false;
	}

	// Shader compilation is restored separately from declaration discovery.
	// Keep the declaration strict enough to reject truncated source while
	// preserving the flags queried by the material/backend paths.
	int depth = 0;
	bool sawOpenBrace = false;
	bool inString = false;
	bool inLineComment = false;
	bool inBlockComment = false;
	for ( int i = 0; i < textLength; ++i ) {
		const char c = text[ i ];
		const char next = i + 1 < textLength ? text[ i + 1 ] : '\0';
		if ( inLineComment ) {
			if ( c == '\n' ) {
				inLineComment = false;
			}
			continue;
		}
		if ( inBlockComment ) {
			if ( c == '*' && next == '/' ) {
				inBlockComment = false;
				++i;
			}
			continue;
		}
		if ( !inString && c == '/' && next == '/' ) {
			inLineComment = true;
			++i;
			continue;
		}
		if ( !inString && c == '/' && next == '*' ) {
			inBlockComment = true;
			++i;
			continue;
		}
		if ( c == '"' && ( i == 0 || text[ i - 1 ] != '\\' ) ) {
			inString = !inString;
			continue;
		}
		if ( inString ) {
			continue;
		}
		if ( c == '{' ) {
			sawOpenBrace = true;
			++depth;
		} else if ( c == '}' ) {
			if ( --depth < 0 ) {
				return false;
			}
		}
	}
	if ( !sawOpenBrace || depth != 0 || inString || inBlockComment ) {
		return false;
	}

	idParser src;
	src.SetFlags( DECL_LEXER_FLAGS );
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	idToken token;
	if ( src.SkipUntilString( "{", &token ) ) {
		int tokenDepth = 1;
		while ( tokenDepth > 0 && src.ReadToken( &token ) ) {
			if ( token == "{" ) {
				++tokenDepth;
				continue;
			}
			if ( token == "}" ) {
				--tokenDepth;
				continue;
			}
			if ( tokenDepth != 1 ) {
				continue;
			}
			if ( token.Icmp( "interaction" ) == 0 ) {
				flags |= RP_INTERACTION;
			} else if ( token.Icmp( "lowrangeuv" ) == 0 ) {
				flags |= RP_LOWRANGEUV;
			} else if ( token.Icmp( "machineSpec" ) == 0 && src.ReadToken( &token ) ) {
				machineSpec = token.GetIntValue();
			} else if ( token.Icmp( "imposterBrightness" ) == 0 && src.ReadToken( &token ) ) {
				imposterBrightness = token.GetFloatValue();
			}
		}
	}

	return true;
}

void sdDeclRenderProgram::FreeData() {
	flags = 0;
	program = NULL;
	numTextureBindings = 0;
	memset( textureBindings, 0, sizeof( textureBindings ) );
	stateBits = 0;
	stateMask = 0;
	cullType = CT_FRONT_SIDED;
	requiredVertexAttribs = 0;
	machineSpec = 0;
	imposterBrightness = 1.0f;
	versionForAmbientLighting = NULL;
	versionForHWSkinning = NULL;
	versionForHardSkinning = NULL;
	versionForInstancing = NULL;
	versionForCoverage = NULL;
	versionForLOD = NULL;
	versionForFallback = NULL;
	versionForAlphaToCoverage = NULL;
	versionForDepth = NULL;
	versionForEarlyCull = NULL;
	versionForAmbientLit = NULL;
	versionForNotLit = NULL;
	altVersions = 0;
}

void sdDeclRenderProgram::List() const {
	common->Printf( "%s | %d | %s\n", GetName(), machineSpec, IsInteraction() ? "I" : "*" );
}

void sdDeclRenderProgram::Dot() const {
}

void sdDeclRenderProgram::Bind() const {
}

void sdDeclRenderProgram::UpdateParameters() const {
}

void sdDeclRenderProgram::UpdateHWSkinningParameters( const idJointMat*, const int ) const {
}

void sdDeclRenderProgram::SetState( const int, const cullType_t ) const {
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
			if ( !token.Icmp( "surfaceType" ) ) {
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
	const viewDef_s*,
	idSoundEmitter*,
	int numManualLights
) const {
	if ( registers == NULL ) {
		return;
	}
	if ( expressionRegisters != NULL && numRegisters > 0 ) {
		memcpy( registers, expressionRegisters, numRegisters * sizeof( float ) );
	} else if ( numRegisters > 0 ) {
		memset( registers, 0, numRegisters * sizeof( float ) );
	}
	if ( shaderParms != NULL ) {
		memcpy( registers + EXP_REG_PARM0, shaderParms, MAX_ENTITY_SHADER_PARMS * sizeof( float ) );
	}
	registers[ EXP_REG_NUMLIGHTS ] = static_cast< float >( numManualLights );

	for ( int i = 0; i < numOps; i++ ) {
		const expOp_t& op = ops[ i ];
		switch ( op.opType ) {
			case OP_TYPE_ADD:		registers[ op.c ] = registers[ op.a ] + registers[ op.b ]; break;
			case OP_TYPE_SUBTRACT:	registers[ op.c ] = registers[ op.a ] - registers[ op.b ]; break;
			case OP_TYPE_MULTIPLY:	registers[ op.c ] = registers[ op.a ] * registers[ op.b ]; break;
			case OP_TYPE_DIVIDE:		registers[ op.c ] = registers[ op.b ] != 0.0f ? registers[ op.a ] / registers[ op.b ] : 0.0f; break;
			case OP_TYPE_MOD: {
				const int divisor = static_cast< int >( registers[ op.b ] );
				registers[ op.c ] = divisor != 0 ? static_cast< int >( registers[ op.a ] ) % divisor : 0.0f;
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
			case OP_TYPE_LOAD:		registers[ op.c ] = registers[ op.a ]; break;
			default:				registers[ op.c ] = 0.0f; break;
		}
	}
}

const float* idMaterial::ConstantRegisters(
	const float[ MAX_ENTITY_SHADER_PARMS ],
	const viewDef_s*
) const {
	return constantRegisters;
}

void idMaterial::SetRenderBindings( const materialStage_t*, const float*, float ) {
	// Restored with the render-binding/program backend.
}

void idMaterial::PurgePartialLoadableImages() {
}

void idMaterial::LoadPartialLoadableImages( bool ) {
}

bool idMaterial::IsFinishedPartialLoading() const {
	return true;
}
