// Copyright (C) 2007 Id Software, Inc.
//
// Render-program declaration implementation reconstructed under the exact
// declRenderProgram.obj source path recorded by the retail ETQW PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "declRenderProgram.h"
#include "DeclRenderProgram_opengl.h"
#include "RenderProgramParser.h"
#include "declRenderBinding.h"
#include "declTypeHolder.h"
#include "../renderer/RendererTypesImpl.h"

namespace {

int BlendSourceBits( const idToken& token ) {
	if ( token.Icmp( "GL_ONE" ) == 0 ) return 0x00;
	if ( token.Icmp( "GL_ZERO" ) == 0 ) return 0x01;
	if ( token.Icmp( "GL_DST_COLOR" ) == 0 ) return 0x03;
	if ( token.Icmp( "GL_ONE_MINUS_DST_COLOR" ) == 0 ) return 0x04;
	if ( token.Icmp( "GL_SRC_ALPHA" ) == 0 ) return 0x05;
	if ( token.Icmp( "GL_ONE_MINUS_SRC_ALPHA" ) == 0 ) return 0x06;
	if ( token.Icmp( "GL_DST_ALPHA" ) == 0 ) return 0x07;
	if ( token.Icmp( "GL_ONE_MINUS_DST_ALPHA" ) == 0 ) return 0x08;
	if ( token.Icmp( "GL_SRC_ALPHA_SATURATE" ) == 0 ) return 0x09;
	return -1;
}

int BlendDestinationBits( const idToken& token ) {
	if ( token.Icmp( "GL_ZERO" ) == 0 ) return 0x00;
	if ( token.Icmp( "GL_ONE" ) == 0 ) return 0x20;
	if ( token.Icmp( "GL_SRC_COLOR" ) == 0 ) return 0x30;
	if ( token.Icmp( "GL_ONE_MINUS_SRC_COLOR" ) == 0 ) return 0x40;
	if ( token.Icmp( "GL_SRC_ALPHA" ) == 0 ) return 0x50;
	if ( token.Icmp( "GL_ONE_MINUS_SRC_ALPHA" ) == 0 ) return 0x60;
	if ( token.Icmp( "GL_DST_ALPHA" ) == 0 ) return 0x70;
	if ( token.Icmp( "GL_ONE_MINUS_DST_ALPHA" ) == 0 ) return 0x80;
	return -1;
}

void StripProgramDelimiters( idStr& source ) {
	source.StripLeadingOnce( "{" );
	source.StripLeadingWhiteSpace();
	source.StripLeadingOnce( "<%" );
	source.StripTrailingOnce( "}" );
	source.StripTrailingWhiteSpace();
	source.StripTrailingOnce( "%>" );
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

void sdDeclRenderProgram::FreeData() {
	imposterBrightness = 1.0f;
	flags = 0;
	stateBits = 0;
	stateMask = 0;
	cullType = CT_FRONT_SIDED;
	requiredVertexAttribs = 0;
	numTextureBindings = 0;
	memset( textureBindings, 0, sizeof( textureBindings ) );
	machineSpec = 0;
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
	delete program;
	program = NULL;
}

bool sdDeclRenderProgram::ParseState( idLexer& src ) {
	idToken token;
	if ( !src.ReadToken( &token ) ) return false;
	if ( token.Icmp( "force" ) == 0 ) {
		stateMask = ~0x4000;
		if ( !src.ExpectTokenString( "{" ) ) return false;
	} else if ( token != "{" ) {
		src.Error( "expected '{' but found '%s'", token.c_str() );
		return false;
	}

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) break;
		if ( token.Icmp( "blend" ) == 0 ) {
			if ( !src.ReadTokenOnLine( &token ) ) return false;
			stateMask |= 0xFF;
			stateBits &= ~0xFF;
			if ( token.Icmp( "blend" ) == 0 ) stateBits |= 0x65;
			else if ( token.Icmp( "add" ) == 0 ) stateBits |= 0x20;
			else if ( token.Icmp( "filter" ) == 0 || token.Icmp( "modulate" ) == 0 ) stateBits |= 0x03;
			else if ( token.Icmp( "none" ) == 0 ) stateBits |= 0x00;
			else {
				const int sourceBits = BlendSourceBits( token );
				if ( sourceBits < 0 || !src.ExpectTokenString( "," ) || !src.ReadTokenOnLine( &token ) ) return false;
				const int destinationBits = BlendDestinationBits( token );
				if ( destinationBits < 0 ) return false;
				stateBits |= sourceBits | destinationBits;
			}
		} else if ( token.Icmp( "depthFunc" ) == 0 ) {
			if ( !src.ReadTokenOnLine( &token ) ) return false;
			stateMask |= 0xF0000;
			stateBits &= ~0xF0000;
			if ( token.Icmp( "equal" ) == 0 ) stateBits |= 0x20000;
			else if ( token.Icmp( "lequal" ) == 0 ) stateBits |= 0x40000;
			else if ( token.Icmp( "always" ) == 0 ) stateBits |= 0x10000;
			else if ( token.Icmp( "less" ) != 0 ) return false;
		} else if ( token.Icmp( "maskDepth" ) == 0 ) {
			stateBits |= 0x100; stateMask |= 0x100;
		} else if ( token.Icmp( "maskColor" ) == 0 ) {
			stateBits |= 0x1E00; stateMask |= 0x1E00;
		} else if ( token.Icmp( "maskRed" ) == 0 ) {
			stateBits |= 0x200; stateMask |= 0x200;
		} else if ( token.Icmp( "maskGreen" ) == 0 ) {
			stateBits |= 0x400; stateMask |= 0x400;
		} else if ( token.Icmp( "maskBlue" ) == 0 ) {
			stateBits |= 0x800; stateMask |= 0x800;
		} else if ( token.Icmp( "maskAlpha" ) == 0 ) {
			stateBits |= 0x1000; stateMask |= 0x1000;
		} else if ( token.Icmp( "line" ) == 0 ) {
			stateBits |= 0x2000; stateMask |= 0x2000;
		} else if ( token.Icmp( "backSided" ) == 0 ) {
			cullType = CT_BACK_SIDED;
		} else if ( token.Icmp( "twoSided" ) == 0 ) {
			cullType = CT_TWO_SIDED;
		} else {
			src.Warning( "sdDeclRenderProgram::ParseState : Unknown token %s", token.c_str() );
			return false;
		}
	}
	flags |= RP_DEFINESSTATE;
	if ( cullType != CT_FRONT_SIDED ) flags |= RP_DEFINESCULL;
	return true;
}

bool sdDeclRenderProgram::ParseProgram( idLexer& src, parseData_t& parseData ) {
	idToken token;
	if ( !src.ExpectAnyToken( &token ) ) return false;
	shaderType_t shaderType;
	if ( token.Icmp( "vertex" ) == 0 ) shaderType = ST_VERTEX_SHADER;
	else if ( token.Icmp( "fragment" ) == 0 ) shaderType = ST_FRAGMENT_SHADER;
	else {
		src.Warning( "sdDeclRenderProgram::ParseProgram : Unknown shader type %s", token.c_str() );
		return false;
	}

	if ( !src.ExpectAnyToken( &token ) ) return false;
	if ( token.Icmp( "reference" ) == 0 ) {
		if ( !src.ExpectAnyToken( &token ) ) return false;
		parseData.shaderReferences[ shaderType ] = token;
		return true;
	}

	idStr type = token;
	sdRenderShader* renderShader = SD_AllocRenderShader( type.c_str() );
	if ( renderShader == NULL ) {
		if ( type.Icmp( "arb" ) != 0 && type.Icmp( "cg" ) != 0 && type.Icmp( "glsl" ) != 0 ) {
			src.Warning( "sdDeclRenderProgram::ParseProgram : Unknown render program type %s", type.c_str() );
			return false;
		}
		return src.SkipBracedSection( true ) != 0;
	}
	renderShader->SetShaderType( shaderType );
	renderShader->ParseFlags( src );
	if ( program == NULL ) {
		program = renderShader->CreateProgram();
	}
	if ( program == NULL ) {
		delete renderShader;
		return false;
	}
	idStr source;
	const int shaderStartLine = src.GetLineNum();
	if ( !src.ParseBracedSectionExact( source, -1 ) ) {
		delete renderShader;
		return false;
	}
	StripProgramDelimiters( source );

	// Retail preserves shaders parsed before the OpenGL context exists.  They
	// are attached without compiling so declaration references and ownership
	// remain intact until the renderer reparses them with an active context.
	// Testing IsSupported first loses every ARB/Cg/GLSL shader because those
	// tests necessarily report false before GL initialization.
	if ( !renderSystem->IsOpenGLRunning() ) {
		if ( !program->AttachShader( renderShader ) ) {
			common->Warning(
				"sdDeclRenderProgram::ParseProgram : Failed to attach shader of type %d in '%s'",
				renderShader->GetShaderType(), GetName()
			);
			delete renderShader;
			return false;
		}
		return true;
	}

	if ( !renderShader->IsSupported() ) {
		delete renderShader;
		return true;
	}

	sdRenderProgramParser parser;
	if ( !parser.PreCompile( *this, *renderShader, source, renderShader->GetPreCompilerFlags(), shaderStartLine, GetFileName() ) ) {
		delete renderShader;
		return false;
	}
	renderShader->ProcessRenderBindings();
	if ( !renderShader->PreCompile( source, *this, shaderStartLine ) || !renderShader->Compile( source, *this ) ) {
		delete renderShader;
		return false;
	}
	if ( !program->AttachShader( renderShader ) ) {
		common->Warning(
			"sdDeclRenderProgram::ParseProgram : Failed to attach shader of type %d in '%s'",
			renderShader->GetShaderType(), GetName()
		);
		delete renderShader;
		return false;
	}
	return true;
}

bool sdDeclRenderProgram::ResolveReferences( parseData_t& parseData ) {
	for ( int i = 0; i < ST_NUM_SHADERS; ++i ) {
		if ( parseData.shaderReferences[ i ].IsEmpty() ) continue;
		const sdDeclRenderProgram* reference = declHolder.FindRenderProgram( parseData.shaderReferences[ i ].c_str(), true );
		if ( reference == NULL ) return false;
		declManager->AddDependency( this, reference );
		if ( reference->GetState() == DS_DEFAULTED ) {
			common->Warning(
				"sdDeclRenderProgram::ResolveReferences : Reference shader of type %d is defaulted in '%s'",
				i, GetName()
			);
			return false;
		}
		if ( reference == this || reference->program == NULL ) {
			common->Warning( "sdDeclRenderProgram::ResolveReferences : Circular dependency for reference in '%s'", GetName() );
			return false;
		}
		sdRenderShader* shader = reference->program->GetShader( static_cast< shaderType_t >( i ) );
		if ( shader == NULL ) {
			common->Warning( "sdDeclRenderProgram::ResolveReferences : Circular dependency for reference in '%s'", GetName() );
			return false;
		}
		if ( program == NULL ) program = shader->CreateProgram();
		if ( program == NULL || !program->AttachShader( shader ) ) {
			common->Warning(
				"sdDeclRenderProgram::ResolveReferences : Failed to attach shader of type %d in '%s'",
				shader->GetShaderType(), GetName()
			);
			return false;
		}
		shader->IncRef();

		// Texture bindings belong to the referenced shader, not merely to its
		// containing declaration.  A vertex reference to a program whose
		// fragment shader samples textures must not conflict with the local
		// fragment shader (the retail check is shader+0x138).
		if ( shader->GetNumTextureBindings() > 0 ) {
			if ( numTextureBindings > 0 ) {
				common->Warning( "sdDeclRenderProgram::ParseProgram : Reference and referee have conflicting texture bindings in '%s'", GetName() );
				return false;
			}
			numTextureBindings = reference->numTextureBindings;
			memcpy( textureBindings, reference->textureBindings, sizeof( textureBindings[ 0 ] ) * numTextureBindings );
		}
	}
	return true;
}

bool sdDeclRenderProgram::Parse( const char* text, const int textLength ) {
	FreeData();
	idLexer src;
	src.SetFlags( DECL_LEXER_FLAGS );
	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	if ( !src.SkipUntilString( "{" ) ) return false;

	parseData_t parseData;
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) break;
		if ( token.Icmp( "interaction" ) == 0 ) flags |= RP_INTERACTION;
		else if ( token.Icmp( "lowrangeuv" ) == 0 ) flags |= RP_LOWRANGEUV;
		else if ( token.Icmp( "state" ) == 0 ) {
			if ( !ParseState( src ) ) return false;
		} else if ( token.Icmp( "program" ) == 0 ) {
			if ( !ParseProgram( src, parseData ) ) return false;
		} else if ( token.Icmp( "hwSkinningVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.hwSkinningVersion = token;
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.hardSkinningVersion = token;
		} else if ( token.Icmp( "ambientVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.ambientVersion = token;
		} else if ( token.Icmp( "shadowMapVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.shadowMapVersion = token;
		} else if ( token.Icmp( "instanceVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.instanceVersion = token;
		} else if ( token.Icmp( "coverageVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.coverageVersion = token;
		} else if ( token.Icmp( "alphaToCoverageVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.alphaToCoverageVersion = token;
		} else if ( token.Icmp( "lodVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.lodVersion = token;
		} else if ( token.Icmp( "depthVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.depthVersion = token;
		} else if ( token.Icmp( "earlyCullVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.earlyCullVersion = token;
		} else if ( token.Icmp( "ambLitVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.ambLitVersion = token;
		} else if ( token.Icmp( "notLitVersion" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.notLitVersion = token;
		} else if ( token.Icmp( "fallBack" ) == 0 ) {
			if ( !src.ExpectAnyToken( &token ) ) return false; parseData.fallBackVersion = token;
		} else if ( token.Icmp( "machineSpec" ) == 0 ) {
			if ( !src.ExpectTokenType( TT_NUMBER, 0, &token ) ) return false; machineSpec = token.GetIntValue();
		} else if ( token.Icmp( "imposterBrightness" ) == 0 ) {
			if ( !src.ExpectTokenType( TT_NUMBER, 0, &token ) ) return false; imposterBrightness = token.GetFloatValue();
		} else {
			src.Warning( "sdDeclRenderProgram::Parse : Unknown token %s", token.c_str() );
			return false;
		}
	}

	struct altVersion_t {
		const idStr* name;
		const sdDeclRenderProgram** target;
		unsigned int bit;
	};
	altVersion_t versions[] = {
		{ &parseData.ambientVersion, &versionForAmbientLighting, RPAV_AMBIENTLIGHTING },
		{ &parseData.hwSkinningVersion, &versionForHWSkinning, RPAV_HWSKINNING },
		{ &parseData.hardSkinningVersion, &versionForHardSkinning, RPAV_HARDSKINNING },
		{ &parseData.instanceVersion, &versionForInstancing, RPAV_INSTANCING },
		{ &parseData.coverageVersion, &versionForCoverage, RPAV_COVERAGE },
		{ &parseData.lodVersion, &versionForLOD, RPAV_LOD },
		{ &parseData.alphaToCoverageVersion, &versionForAlphaToCoverage, RPAV_ALPHATOCOVERAGE },
		{ &parseData.depthVersion, &versionForDepth, RPAV_DEPTH },
		{ &parseData.earlyCullVersion, &versionForEarlyCull, RPAV_EARLYCULL },
		{ &parseData.ambLitVersion, &versionForAmbientLit, RPAV_AMBIENTLIT },
		{ &parseData.notLitVersion, &versionForNotLit, RPAV_NOTLIT }
	};
	for ( int i = 0; i < static_cast< int >( sizeof( versions ) / sizeof( versions[ 0 ] ) ); ++i ) {
		if ( versions[ i ].name->IsEmpty() ) continue;
		*versions[ i ].target = declHolder.FindRenderProgram( versions[ i ].name->c_str(), true );
		if ( *versions[ i ].target != NULL ) {
			declManager->AddDependency( this, *versions[ i ].target );
			altVersions |= versions[ i ].bit;
			if ( ( *versions[ i ].target )->GetState() == DS_DEFAULTED ) {
				common->Warning( "renderProg '%s' is defaulted", ( *versions[ i ].target )->GetName() );
			}
		}
	}
	if ( !parseData.fallBackVersion.IsEmpty() ) {
		versionForFallback = declHolder.FindRenderProgram( parseData.fallBackVersion.c_str(), true );
		if ( versionForFallback != NULL ) {
			declManager->AddDependency( this, versionForFallback );
			if ( versionForFallback->GetState() == DS_DEFAULTED ) {
				common->Warning( "renderProg '%s' is defaulted", versionForFallback->GetName() );
			}
		}
	}

	if ( !ResolveReferences( parseData ) ) return false;
	if ( glConfig.isInitialized ) {
		if ( program == NULL ) {
			common->Warning( "No program defined for '%s'", GetName() );
			return false;
		}
		if ( !program->Link( *this ) ) {
			common->Warning( "Link failure for '%s'", GetName() );
			return false;
		}
		for ( int shaderIndex = 0; shaderIndex < ST_NUM_SHADERS; ++shaderIndex ) {
			sdRenderShader* shader = program->GetShader( static_cast< shaderType_t >( shaderIndex ) );
			if ( shader == NULL ) continue;
			shader->AllocStateCache();
			for ( int i = 0; i < shader->GetNumVertexAttribBindings(); ++i ) requiredVertexAttribs |= 1 << shader->GetVertexAttribBinding( i )->GetAttribIndex();
		}
	}
	return true;
}

void sdDeclRenderProgram::List() const {
	common->Printf( "%s | %d | %s\n", GetName(), machineSpec, IsInteraction() ? "I" : "*" );
}

void sdDeclRenderProgram::Dot() const {
}

void sdDeclRenderProgram::Bind() const {
	if ( program != NULL ) program->Bind();
	else SD_UnbindRenderProgram();
}

void sdDeclRenderProgram::UpdateParameters() const {
	if ( program != NULL ) program->UpdateParameters();
}

void sdDeclRenderProgram::UpdateHWSkinningParameters( const idJointMat* joints, const int numJoints ) const {
	if ( program != NULL ) program->UpdateHWSkinningParameters( joints, numJoints );
}

void sdDeclRenderProgram::SetState( const int extraState, const cullType_t extraCull ) const {
	int combinedState = extraState;
	cullType_t combinedCull = extraCull;
	if ( ( flags & RP_DEFINESSTATE ) != 0 ) combinedState = stateBits | ( extraState & ~stateMask );
	if ( ( flags & RP_DEFINESCULL ) != 0 ) combinedCull = cullType;
	if ( program != NULL ) combinedState |= program->GetStateBits();
	renderSystem->SetGLState( combinedState );
	renderSystem->SetCull( combinedCull );
}
