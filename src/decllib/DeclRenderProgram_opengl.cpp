// Copyright (C) 2007 Id Software, Inc.
//
// OpenGL render-program runtime.  The class layouts, method ownership and
// ARB/GLSL binding rules are recovered from DeclRenderProgram_opengl.obj.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "DeclRenderProgram_opengl.h"
#include "declRenderProgram.h"
#include "declRenderBinding.h"
#include "declTypeHolder.h"
#include "../renderer/RendererTypesImpl.h"
#include "../renderer/RenderSystem.h"
#include "../libs/qglLib/qgl.h"
#include "../libs/qglLib/qcg.h"

#include <GL/gl.h>
#include <malloc.h>

const unsigned int sdRenderProgramARB::shaderTypes[ ST_NUM_SHADERS ] = {
	GL_VERTEX_PROGRAM_ARB,
	GL_FRAGMENT_PROGRAM_ARB
};

const unsigned int sdRenderProgramGLSL::shaderTypes[ ST_NUM_SHADERS ] = {
	GL_VERTEX_SHADER_ARB,
	GL_FRAGMENT_SHADER_ARB
};

extern idCVar r_32ByteVtx;

idCVar r_dumpShaders( "r_dumpShaders", "0", CVAR_FLOAT, "Dump compiled and preprocessed shaders to text files" );
idCVar r_stateCache( "r_stateCache", "1", CVAR_FLOAT, "check state before upload to drive" );
idCVar r_useARBPositionInvariant( "r_useARBPositionInvariant", "0", CVAR_FLOAT, "don't replace ARBPositionInvariant" );

namespace {

void ReplaceBinding( idStr& source, const sdDeclRenderBinding* binding, const char* replacement ) {
	idStr marker = "$";
	marker.Append( binding->GetName() );
	marker.Append( "$" );
	source.Replace( marker.c_str(), replacement );
}

int PackedVertexPreamblePosition( const idStr& source ) {
	int lastOption = -1;
	for ( int search = 0; search < source.Length(); ) {
		const int found = idStr::FindText( source.c_str(), "OPTION", true, search, source.Length() );
		if ( found < 0 ) break;
		lastOption = found;
		search = found + 6;
	}
	int position = lastOption >= 0 ? lastOption : 0;
	while ( position < source.Length() && source[ position ] != '\n' ) ++position;
	return position;
}

void ReplacePositionInvariant( idStr& source ) {
	const int marker = idStr::FindText( source.c_str(), "ARB_position_invariant", true, 0, source.Length() );
	if ( marker < 0 ) return;

	int optionStart = marker;
	while ( optionStart > 0 && idStr::Cmpn( source.c_str() + optionStart, "OPTION", 6 ) != 0 ) {
		--optionStart;
	}
	if ( idStr::Cmpn( source.c_str() + optionStart, "OPTION", 6 ) != 0 ) return;

	int optionEnd = marker;
	while ( optionEnd < source.Length() && source[ optionEnd ] != ';' ) ++optionEnd;
	if ( optionEnd >= source.Length() ) return;

	idStr option;
	source.Mid( optionStart, optionEnd - optionStart + 1, option );
	source.ReplaceFirst( option.c_str(),
		"PARAM  __mvp[4]={state.matrix.mvp};\n"
		"DP4 result.position.x, __mvp[0], vertex.position;\n"
		"DP4 result.position.y, __mvp[1], vertex.position;\n"
		"DP4 result.position.z, __mvp[2], vertex.position;\n"
		"DP4 result.position.w, __mvp[3], vertex.position;\n" );
}

GLenum BlendSourceForBits( int stateBits ) {
	switch ( stateBits & 0x0F ) {
		case 0x01: return GL_ZERO;
		case 0x03: return GL_DST_COLOR;
		case 0x04: return GL_ONE_MINUS_DST_COLOR;
		case 0x05: return GL_SRC_ALPHA;
		case 0x06: return GL_ONE_MINUS_SRC_ALPHA;
		case 0x07: return GL_DST_ALPHA;
		case 0x08: return GL_ONE_MINUS_DST_ALPHA;
		case 0x09: return GL_SRC_ALPHA_SATURATE;
		default: return GL_ONE;
	}
}

GLenum BlendDestinationForBits( int stateBits ) {
	switch ( stateBits & 0xF0 ) {
		case 0x20: return GL_ONE;
		case 0x30: return GL_SRC_COLOR;
		case 0x40: return GL_ONE_MINUS_SRC_COLOR;
		case 0x50: return GL_SRC_ALPHA;
		case 0x60: return GL_ONE_MINUS_SRC_ALPHA;
		case 0x70: return GL_DST_ALPHA;
		case 0x80: return GL_ONE_MINUS_DST_ALPHA;
		default: return GL_ZERO;
	}
}

void ApplyState( int stateBits, cullType_t cullType ) {
	if ( ( stateBits & 0xFF ) != 0 ) {
		glEnable( GL_BLEND );
		glBlendFunc( BlendSourceForBits( stateBits ), BlendDestinationForBits( stateBits ) );
	} else {
		glDisable( GL_BLEND );
	}
	glDepthMask( ( stateBits & 0x100 ) != 0 ? GL_FALSE : GL_TRUE );
	switch ( stateBits & 0xF0000 ) {
		case 0x10000: glDepthFunc( GL_ALWAYS ); break;
		case 0x20000: glDepthFunc( GL_EQUAL ); break;
		case 0x40000: glDepthFunc( GL_LEQUAL ); break;
		default: glDepthFunc( GL_LESS ); break;
	}
	glColorMask( ( stateBits & 0x200 ) == 0, ( stateBits & 0x400 ) == 0,
		( stateBits & 0x800 ) == 0, ( stateBits & 0x1000 ) == 0 );
	glPolygonMode( GL_FRONT_AND_BACK, ( stateBits & 0x2000 ) != 0 ? GL_LINE : GL_FILL );
	if ( stateBits & 0x400000 ) {
		glDisable( GL_VERTEX_PROGRAM_ARB );
		glDisable( GL_FRAGMENT_PROGRAM_ARB );
	} else if ( stateBits & 0x300000 ) {
		if ( qglUseProgramObjectARB != NULL ) qglUseProgramObjectARB( 0 );
		if ( stateBits & 0x100000 ) glEnable( GL_VERTEX_PROGRAM_ARB );
		else glDisable( GL_VERTEX_PROGRAM_ARB );
		if ( stateBits & 0x200000 ) glEnable( GL_FRAGMENT_PROGRAM_ARB );
		else glDisable( GL_FRAGMENT_PROGRAM_ARB );
	}
	if ( cullType == CT_TWO_SIDED || cullType == CT_INVALID ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( cullType == CT_BACK_SIDED ? GL_FRONT : GL_BACK );
	}
}

}

sdRenderShader::sdRenderShader() {
	refCount = 1;
	shaderType = static_cast< shaderType_t >( -1 );
	numParameterBindings = 0;
	memset( parameterBindings, 0, sizeof( parameterBindings ) );
	numInfrequentParameterBindings = 0;
	memset( infrequentParameterBindings, 0, sizeof( infrequentParameterBindings ) );
	numVertexAttribBindings = 0;
	memset( vertexAttribBindings, 0, sizeof( vertexAttribBindings ) );
	numTextureBindings = 0;
	parameterState = NULL;
}

sdRenderShader::~sdRenderShader() {
	Purge();
}

void sdRenderShader::Purge() {
	if ( parameterState != NULL ) {
		_aligned_free( parameterState );
		parameterState = NULL;
	}
}

void sdRenderShader::ParseFlags( idLexer& ) {
}

void sdRenderShader::AllocStateCache() {
}

void sdRenderShader::ProcessRenderBindings() {
	for ( int i = 0; i < numParameterBindings; ) {
		const sdDeclRenderBinding* binding = parameterBindings[ i ];
		if ( binding->Infrequent() >= 0 ) {
			if ( numInfrequentParameterBindings < 32 ) {
				infrequentParameterBindings[ numInfrequentParameterBindings++ ] = binding;
			}
			for ( int j = i; j + 1 < numParameterBindings; ++j ) parameterBindings[ j ] = parameterBindings[ j + 1 ];
			--numParameterBindings;
			continue;
		}
		++i;
	}
}

sdRenderShaderARB::sdRenderShaderARB() {
	shader = 0;
	userDecompress = false;
}

sdRenderShaderARB::~sdRenderShaderARB() {
	Purge();
}

void sdRenderShaderARB::Purge() {
	sdRenderShader::Purge();
	if ( shader != 0 && qglDeleteProgramsARB != NULL ) qglDeleteProgramsARB( 1, &shader );
	shader = 0;
}

bool sdRenderShaderARB::IsSupported() const {
	if ( !glConfig.isInitialized ) return false;
	if ( shaderType == ST_VERTEX_SHADER ) return glConfig.ARBVertexProgramAvailable;
	if ( shaderType == ST_FRAGMENT_SHADER ) return glConfig.ARBFragmentProgramAvailable;
	return false;
}

sdRenderProgram* sdRenderShaderARB::CreateProgram() const {
	return new sdRenderProgramARB;
}

void sdRenderShaderARB::ParseFlags( idLexer& src ) {
	idToken token;
	if ( src.ReadToken( &token ) ) {
		if ( token.Icmp( "userDecompress" ) == 0 ) userDecompress = true;
		else src.UnreadToken( &token );
	}
}

bool sdRenderShaderARB::PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int ) {
	const bool decompressVertex = r_32ByteVtx.GetBool() && shaderType == ST_VERTEX_SHADER && !userDecompress;
	if ( decompressVertex ) {
		idStr preamble;
		bool needsSignAttrib = false;
		bool declaredAttribTemp = false;
		for ( int index = 0; index < numVertexAttribBindings; ++index ) {
			const sdDeclRenderBinding* binding = vertexAttribBindings[ index ];
			if ( binding == NULL ) continue;
			const char* name = binding->GetName();
			if ( idStr::Icmp( name, "texCoordAttrib" ) == 0 ) {
				preamble.Append( "#replace texCoordAttrib with _texCoordAttrib\n" );
				preamble.Append( "TEMP _texCoordAttrib;\n" );
				preamble.Append( va( "MUL _texCoordAttrib.xy, vertex.attrib[%i], %.9f;\n",
					binding->GetAttribIndex(), renderProgram.UsesLowRangeUVs() ? ( 1.0f / 32768.0f ) : ( 1.0f / 4096.0f ) ) );
				preamble.Append( "MOV _texCoordAttrib.zw, 1;\n" );
			} else if ( idStr::Icmp( name, "normalAttrib" ) == 0 ) {
				preamble.Append( "#replace normalAttrib with _normalAttrib\n" );
				preamble.Append( "TEMP _normalAttrib;\n" );
				if ( !declaredAttribTemp ) {
					preamble.Append( "TEMP _attribTemp;\n" );
					declaredAttribTemp = true;
				}
				preamble.Append( va( "MUL _normalAttrib, vertex.attrib[%i], %.9f;\n", binding->GetAttribIndex(), 1.0f / 32768.0f ) );
				preamble.Append( "MUL _attribTemp.xy, _normalAttrib, _normalAttrib;\n" );
				preamble.Append( "ADD _attribTemp.x, _attribTemp.xxxx, _attribTemp.yyyy;\n" );
				preamble.Append( "ADD _attribTemp.x, 1, -_attribTemp.x;\n" );
				preamble.Append( "RSQ _attribTemp.x, _attribTemp.x;\n" );
				preamble.Append( "RCP _normalAttrib.z, _attribTemp.x;\n" );
				preamble.Append( "ADD _attribTemp.x, $signattrib$.x, -1;\n" );
				preamble.Append( "MUL _normalAttrib.z, _normalAttrib.z, _attribTemp.x;\n" );
				needsSignAttrib = true;
			} else if ( idStr::Icmp( name, "tangentAttrib" ) == 0 ) {
				preamble.Append( "#replace tangentAttrib with _tangentAttrib\n" );
				preamble.Append( "TEMP _tangentAttrib;\n" );
				if ( !declaredAttribTemp ) {
					preamble.Append( "TEMP _attribTemp;\n" );
					declaredAttribTemp = true;
				}
				preamble.Append( va( "MUL _tangentAttrib, vertex.attrib[%i], %.9f;\n", binding->GetAttribIndex(), 1.0f / 32768.0f ) );
				preamble.Append( "MUL _attribTemp.xy, _tangentAttrib, _tangentAttrib;\n" );
				preamble.Append( "ADD _attribTemp.x, _attribTemp.xxxx, _attribTemp.yyyy;\n" );
				preamble.Append( "ADD _attribTemp.x, 1, -_attribTemp.x;\n" );
				preamble.Append( "RSQ _attribTemp.x, _attribTemp.x;\n" );
				preamble.Append( "RCP _tangentAttrib.z, _attribTemp.x;\n" );
				preamble.Append( "ADD _attribTemp.x, $signattrib$.y, -1;\n" );
				preamble.Append( "MUL _tangentAttrib.z, _tangentAttrib.z, _attribTemp.x;\n" );
				preamble.Append( "ADD _tangentAttrib.w, $signattrib$.z, -1;\n" );
				needsSignAttrib = true;
			}
		}
		if ( !preamble.IsEmpty() ) source.Insert( preamble.c_str(), PackedVertexPreamblePosition( source ) );
		if ( needsSignAttrib ) {
			const sdDeclRenderBinding* signAttrib = declHolder.FindRenderBinding( "signAttrib", false );
			// The packed-normal/tangent preamble introduces $signattrib$ after the
			// render-program parser has collected the source bindings.  Retail ETQW
			// therefore appends this implicit attribute unconditionally before the
			// replacement pass below.  Treating it like an optional/deduplicated
			// source binding leaves the literal marker in every packed vertex program,
			// causing the driver upload to fail and the decl to become orange-defaulted.
			if ( signAttrib == NULL || numVertexAttribBindings >= 8 ) {
				common->Warning( "cannot add implicit signAttrib to renderProg '%s'", renderProgram.GetName() );
				return false;
			}
			vertexAttribBindings[ numVertexAttribBindings++ ] = signAttrib;
		}
	}

	if ( shaderType == ST_VERTEX_SHADER && !r_useARBPositionInvariant.GetBool() ) ReplacePositionInvariant( source );

	idStr processed = shaderType == ST_VERTEX_SHADER ? "!!ARBvp1.0\n" : "!!ARBfp1.0\n";
	processed.Append( source );
	source = processed;

	for ( int i = 0; i < numParameterBindings; ++i ) {
		ReplaceBinding( source, parameterBindings[ i ], va( "program.local[%i]", i ) );
	}
	for ( int i = 0; i < numInfrequentParameterBindings; ++i ) {
		ReplaceBinding( source, infrequentParameterBindings[ i ], va( "program.env[%i]", infrequentParameterBindings[ i ]->Infrequent() ) );
	}
	for ( int i = 0; i < renderProgram.GetNumTextureBindings(); ++i ) {
		ReplaceBinding( source, renderProgram.GetTextureBinding( i ), va( "texture[%i]", i ) );
	}
	for ( int i = 0; i < numVertexAttribBindings; ++i ) {
		const sdDeclRenderBinding* binding = vertexAttribBindings[ i ];
		const int index = binding->GetAttribIndex();
		const char* replacement = index == 0 ? "vertex.position" : va( "vertex.attrib[%i]", index );
		if ( decompressVertex ) {
			if ( idStr::Icmp( binding->GetName(), "texCoordAttrib" ) == 0 ) replacement = "_texCoordAttrib";
			else if ( idStr::Icmp( binding->GetName(), "normalAttrib" ) == 0 ) replacement = "_normalAttrib";
			else if ( idStr::Icmp( binding->GetName(), "tangentAttrib" ) == 0 ) replacement = "_tangentAttrib";
		}
		ReplaceBinding( source, binding, replacement );
	}
	source.Append( "\nEND\n" );
	return true;
}

bool sdRenderShaderARB::Upload( const idStr& source, sdDeclRenderProgram& renderProgram ) {
	if ( qglGenProgramsARB == NULL || qglBindProgramARB == NULL || qglProgramStringARB == NULL ) {
		common->Warning( "renderProg '%s' cannot upload its ARB %s shader: ARB program entry points are unavailable",
			renderProgram.GetName(), shaderType == ST_VERTEX_SHADER ? "vertex" : "fragment" );
		return false;
	}
	if ( shader == 0 ) qglGenProgramsARB( 1, &shader );
	const GLenum target = sdRenderProgramARB::shaderTypes[ shaderType ];
	qglBindProgramARB( target, shader );
	while ( glGetError() != GL_NO_ERROR ) {}
	qglProgramStringARB( target, GL_PROGRAM_FORMAT_ASCII_ARB, source.Length(), source.c_str() );
	const GLenum error = glGetError();
	GLint errorPosition = -1;
	glGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, &errorPosition );
	if ( error == GL_INVALID_OPERATION || errorPosition != -1 ) {
		const char* errorString = reinterpret_cast< const char* >( glGetString( GL_PROGRAM_ERROR_STRING_ARB ) );
		common->Warning( "renderProg '%s' ARB %s error at %d: %s", renderProgram.GetName(),
			shaderType == ST_VERTEX_SHADER ? "vertex" : "fragment", errorPosition,
			errorString != NULL ? errorString : "unknown error" );
		return false;
	}
	return true;
}

bool sdRenderShaderARB::Compile( const idStr& source, sdDeclRenderProgram& renderProgram ) {
	const bool uploaded = Upload( source, renderProgram );
	// Retail ETQW always writes the preprocessed ARB source when an upload
	// fails, and writes every source when r_dumpShaders is enabled.  Keeping
	// this behavior is important: a failed declaration is replaced by the
	// intentionally orange default program, otherwise hiding the actual driver
	// error and the source that produced it.
	if ( !uploaded || r_dumpShaders.GetBool() ) {
		idStr outPath = "renderprogs/shaderdump/";
		outPath.Append( renderProgram.GetName() );
		outPath.Append( shaderType == ST_VERTEX_SHADER ? "_arbvp" : "_arbfp" );
		outPath.SetFileExtension( ".pre" );
		fileSystem->WriteFile( outPath.c_str(), source.c_str(), source.Length() );
	}
	return uploaded;
}

void sdRenderShaderARB::AllocStateCache() {
	if ( numParameterBindings <= 0 || parameterState != NULL ) return;
	parameterState = static_cast< float* >( _aligned_malloc( sizeof( float ) * 4 * numParameterBindings, 16 ) );
	if ( parameterState == NULL ) return;
	memset( parameterState, 0, sizeof( float ) * 4 * numParameterBindings );
	const GLenum target = sdRenderProgramARB::shaderTypes[ shaderType ];
	if ( qglBindProgramARB != NULL ) qglBindProgramARB( target, shader );
	if ( qglProgramLocalParameter4fvARB != NULL ) {
		for ( int index = 0; index < numParameterBindings; ++index ) {
			qglProgramLocalParameter4fvARB( target, index, parameterState + index * 4 );
		}
	}
}

sdRenderProgramARB::sdRenderProgramARB() {
	memset( shaders, 0, sizeof( shaders ) );
}

sdRenderProgramARB::~sdRenderProgramARB() {
	for ( int i = 0; i < ST_NUM_SHADERS; ++i ) if ( shaders[ i ] != NULL ) shaders[ i ]->DecRef();
}

bool sdRenderProgramARB::AttachShader( sdRenderShader* shader_ ) {
	if ( shader_ == NULL ) return false;
	const shaderType_t type = shader_->GetShaderType();
	if ( type < 0 || type >= ST_NUM_SHADERS || shaders[ type ] != NULL ) return false;
	sdRenderShaderARB* arbShader = dynamic_cast< sdRenderShaderARB* >( shader_ );
	if ( arbShader == NULL ) return false;
	shaders[ type ] = arbShader;
	return true;
}

sdRenderShader* sdRenderProgramARB::GetShader( shaderType_t shaderType ) {
	return shaderType >= 0 && shaderType < ST_NUM_SHADERS ? shaders[ shaderType ] : NULL;
}

bool sdRenderProgramARB::Link( const sdDeclRenderProgram& ) {
	return shaders[ ST_VERTEX_SHADER ] != NULL && shaders[ ST_FRAGMENT_SHADER ] != NULL;
}

void sdRenderProgramARB::Bind() {
	if ( qglUseProgramObjectARB != NULL ) qglUseProgramObjectARB( 0 );
	glEnable( GL_VERTEX_PROGRAM_ARB );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );
	for ( int i = 0; i < ST_NUM_SHADERS; ++i ) {
		if ( shaders[ i ] != NULL && qglBindProgramARB != NULL ) qglBindProgramARB( shaderTypes[ i ], shaders[ i ]->GetShader() );
	}
}

void sdRenderProgramARB::UpdateParameters() {
	if ( qglProgramLocalParameter4fvARB == NULL ) return;
	for ( int shaderIndex = 0; shaderIndex < ST_NUM_SHADERS; ++shaderIndex ) {
		sdRenderShaderARB* shader = shaders[ shaderIndex ];
		if ( shader == NULL ) continue;

		const int numBindings = shader->GetNumParameterBindings();
		float* parameterState = shader->GetParameterState();
		if ( !r_stateCache.GetBool() || parameterState == NULL ) {
			for ( int i = 0; i < numBindings; ++i ) {
				const sdDeclRenderBinding* binding = shader->GetParameterBinding( i );
				binding->Evaluate();
				qglProgramLocalParameter4fvARB( shaderTypes[ shaderIndex ], i, binding->GetVector() );
			}
			continue;
		}

		if ( glConfig.EXTGpuProgramParametersAvailable && qglProgramLocalParameters4fvEXT != NULL ) {
			int changedStart = -1;
			for ( int i = 0; i < numBindings; ++i ) {
				const sdDeclRenderBinding* binding = shader->GetParameterBinding( i );
				binding->Evaluate();
				float* cached = parameterState + i * 4;
				if ( memcmp( cached, binding->GetVector(), sizeof( float ) * 4 ) != 0 ) {
					memcpy( cached, binding->GetVector(), sizeof( float ) * 4 );
					if ( changedStart < 0 ) changedStart = i;
				} else if ( changedStart >= 0 ) {
					qglProgramLocalParameters4fvEXT( shaderTypes[ shaderIndex ], changedStart,
						i - changedStart, parameterState + changedStart * 4 );
					changedStart = -1;
				}
			}
			if ( changedStart >= 0 ) {
				qglProgramLocalParameters4fvEXT( shaderTypes[ shaderIndex ], changedStart,
					numBindings - changedStart, parameterState + changedStart * 4 );
			}
		} else {
			for ( int i = 0; i < numBindings; ++i ) {
				const sdDeclRenderBinding* binding = shader->GetParameterBinding( i );
				binding->Evaluate();
				float* cached = parameterState + i * 4;
				if ( memcmp( cached, binding->GetVector(), sizeof( float ) * 4 ) == 0 ) continue;
				memcpy( cached, binding->GetVector(), sizeof( float ) * 4 );
				qglProgramLocalParameter4fvARB( shaderTypes[ shaderIndex ], i, binding->GetVector() );
			}
		}
	}
}

bool sdRenderShaderCg::PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int shaderStartLine ) {
	for ( int i = 0; i < numParameterBindings; ++i ) ReplaceBinding( source, parameterBindings[ i ], va( "C%i", i ) );
	for ( int i = 0; i < numInfrequentParameterBindings; ++i ) {
		common->Warning( "CG shader references infrequent render binding '%s'", infrequentParameterBindings[ i ]->GetName() );
		ReplaceBinding( source, infrequentParameterBindings[ i ], "C0" );
	}
	for ( int i = 0; i < renderProgram.GetNumTextureBindings(); ++i ) {
		ReplaceBinding( source, renderProgram.GetTextureBinding( i ), va( " TEXUNIT%i", i ) );
	}
	for ( int i = 0; i < numVertexAttribBindings; ++i ) {
		const sdDeclRenderBinding* binding = vertexAttribBindings[ i ];
		ReplaceBinding( source, binding, binding->GetAttribIndex() == 0 ? "POSITION" : va( "ATTR%i", binding->GetAttribIndex() ) );
	}
	idStr line = va( "#line %i \"%s\"\n", shaderStartLine, renderProgram.GetFileName() );
	line.Append( source );
	source = line;
	return true;
}

bool sdRenderShaderCg::Compile( const idStr& source, sdDeclRenderProgram& renderProgram ) {
	if ( !glConfig.allowCgPath || cgContext == NULL || cgCreateProgram == NULL ) return false;
	const char* arguments[ 2 ] = { NULL, NULL };
	const char* entry = shaderType == ST_VERTEX_SHADER ? "vertex" : "fragment";
	const CGprofile profile = shaderType == ST_VERTEX_SHADER ? CG_PROFILE_ARBVP1 : CG_PROFILE_ARBFP1;
	if ( shaderType == ST_VERTEX_SHADER ) arguments[ 0 ] = "-posinv";
	CGprogram cgProgram = cgCreateProgram( cgContext, CG_SOURCE, source.c_str(), profile, entry, arguments );
	const char* listing = cgGetLastListing != NULL ? cgGetLastListing( cgContext ) : NULL;
	if ( cgProgram == NULL || ( listing != NULL && *listing != '\0' ) ) {
		common->Warning( "Cg compile failure in '%s': %s", renderProgram.GetName(), listing != NULL ? listing : "unknown error" );
		if ( cgProgram != NULL ) cgDestroyProgram( cgProgram );
		return false;
	}
	const char* compiled = cgGetProgramString( cgProgram, CG_COMPILED_PROGRAM );
	idStr arbSource = compiled != NULL ? compiled : "";
	cgDestroyProgram( cgProgram );
	if ( shaderType == ST_VERTEX_SHADER && !r_useARBPositionInvariant.GetBool() ) ReplacePositionInvariant( arbSource );
	return Upload( arbSource, renderProgram );
}

sdRenderShaderGLSL::sdRenderShaderGLSL() {
	shader = 0;
}

sdRenderShaderGLSL::~sdRenderShaderGLSL() {
	Purge();
}

void sdRenderShaderGLSL::Purge() {
	sdRenderShader::Purge();
	if ( shader != 0 && qglDeleteObjectARB != NULL ) qglDeleteObjectARB( shader );
	shader = 0;
}

bool sdRenderShaderGLSL::IsSupported() const {
	if ( !glConfig.isInitialized || !glConfig.ARBShaderObjectsAvailable ) return false;
	return shaderType == ST_VERTEX_SHADER ? glConfig.ARBVertexShaderAvailable : glConfig.ARBFragmentShaderAvailable;
}

sdRenderProgram* sdRenderShaderGLSL::CreateProgram() const {
	return new sdRenderProgramGLSL;
}

bool sdRenderShaderGLSL::PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int ) {
	for ( int i = 0; i < numParameterBindings; ++i ) ReplaceBinding( source, parameterBindings[ i ], parameterBindings[ i ]->GetName() );
	for ( int i = 0; i < numInfrequentParameterBindings; ++i ) ReplaceBinding( source, infrequentParameterBindings[ i ], infrequentParameterBindings[ i ]->GetName() );
	for ( int i = 0; i < renderProgram.GetNumTextureBindings(); ++i ) ReplaceBinding( source, renderProgram.GetTextureBinding( i ), renderProgram.GetTextureBinding( i )->GetName() );
	for ( int i = 0; i < numVertexAttribBindings; ++i ) ReplaceBinding( source, vertexAttribBindings[ i ], vertexAttribBindings[ i ]->GetName() );
	return true;
}

bool sdRenderShaderGLSL::Compile( const idStr& source, sdDeclRenderProgram& renderProgram ) {
	if ( qglCreateShaderObjectARB == NULL || qglShaderSourceARB == NULL || qglCompileShaderARB == NULL ) return false;
	if ( shader == 0 ) shader = qglCreateShaderObjectARB( sdRenderProgramGLSL::shaderTypes[ shaderType ] );
	const char* sourceText = source.c_str();
	const int sourceLength = source.Length();
	qglShaderSourceARB( shader, 1, &sourceText, &sourceLength );
	qglCompileShaderARB( shader );
	int status = 0;
	qglGetObjectParameterivARB( shader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status != 0 ) return true;
	common->Warning( "GLSL compile failure in '%s'", renderProgram.GetName() );
	sdRenderProgramGLSL::WarningInfoLog( "", shader );
	return false;
}

sdRenderProgramGLSL::sdRenderProgramGLSL() {
	memset( shaders, 0, sizeof( shaders ) );
	program = 0;
	for ( int i = 0; i < 32; ++i ) uniformLocations[ i ] = -1;
	hwSkinningUniformLocation = -1;
}

sdRenderProgramGLSL::~sdRenderProgramGLSL() {
	Purge();
}

void sdRenderProgramGLSL::Purge() {
	if ( program != 0 && qglDeleteObjectARB != NULL ) qglDeleteObjectARB( program );
	program = 0;
	for ( int i = 0; i < ST_NUM_SHADERS; ++i ) {
		if ( shaders[ i ] != NULL ) shaders[ i ]->DecRef();
		shaders[ i ] = NULL;
	}
}

bool sdRenderProgramGLSL::AttachShader( sdRenderShader* shader_ ) {
	if ( shader_ == NULL ) return false;
	const shaderType_t type = shader_->GetShaderType();
	if ( type < 0 || type >= ST_NUM_SHADERS || shaders[ type ] != NULL ) return false;
	sdRenderShaderGLSL* glslShader = dynamic_cast< sdRenderShaderGLSL* >( shader_ );
	if ( glslShader == NULL ) return false;
	shaders[ type ] = glslShader;
	return true;
}

sdRenderShader* sdRenderProgramGLSL::GetShader( shaderType_t shaderType ) {
	return shaderType >= 0 && shaderType < ST_NUM_SHADERS ? shaders[ shaderType ] : NULL;
}

void sdRenderProgramGLSL::WarningInfoLog( const char* prefix, unsigned int object ) {
	if ( qglGetObjectParameterivARB == NULL || qglGetInfoLogARB == NULL ) return;
	int length = 0;
	qglGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &length );
	if ( length <= 1 ) return;
	char* log = new char[ length ];
	qglGetInfoLogARB( object, length, NULL, log );
	common->Warning( "%s%s", prefix != NULL ? prefix : "", log );
	delete[] log;
}

bool sdRenderProgramGLSL::Link( const sdDeclRenderProgram& renderProgram ) {
	if ( shaders[ 0 ] == NULL || shaders[ 1 ] == NULL || qglCreateProgramObjectARB == NULL ) return false;
	program = qglCreateProgramObjectARB();
	for ( int shaderIndex = 0; shaderIndex < ST_NUM_SHADERS; ++shaderIndex ) {
		qglAttachObjectARB( program, shaders[ shaderIndex ]->GetShader() );
		for ( int i = 0; i < shaders[ shaderIndex ]->GetNumVertexAttribBindings(); ++i ) {
			const sdDeclRenderBinding* binding = shaders[ shaderIndex ]->GetVertexAttribBinding( i );
			qglBindAttribLocationARB( program, binding->GetAttribIndex(), binding->GetName() );
		}
	}
	qglLinkProgramARB( program );
	int status = 0;
	qglGetObjectParameterivARB( program, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == 0 ) {
		WarningInfoLog( "GLSL link failure: ", program );
		return false;
	}
	int locationIndex = 0;
	for ( int shaderIndex = 0; shaderIndex < ST_NUM_SHADERS; ++shaderIndex ) {
		sdRenderShaderGLSL* shader = shaders[ shaderIndex ];
		for ( int i = 0; i < shader->GetNumParameterBindings() && locationIndex < 32; ++i ) uniformLocations[ locationIndex++ ] = qglGetUniformLocationARB( program, shader->GetParameterBinding( i )->GetName() );
		for ( int i = 0; i < shader->GetNumInfrequentParameterBindings() && locationIndex < 32; ++i ) uniformLocations[ locationIndex++ ] = qglGetUniformLocationARB( program, shader->GetInfrequentParameterBinding( i )->GetName() );
	}
	hwSkinningUniformLocation = qglGetUniformLocationARB( program, "joints" );
	Bind();
	for ( int i = 0; i < renderProgram.GetNumTextureBindings(); ++i ) {
		const int location = qglGetUniformLocationARB( program, renderProgram.GetTextureBinding( i )->GetName() );
		if ( location >= 0 ) qglUniform1iARB( location, i );
	}
	return true;
}

void sdRenderProgramGLSL::Bind() {
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	if ( qglUseProgramObjectARB != NULL ) qglUseProgramObjectARB( program );
}

void sdRenderProgramGLSL::UpdateParameters() {
	if ( qglUniform4fvARB == NULL ) return;
	int locationIndex = 0;
	for ( int shaderIndex = 0; shaderIndex < ST_NUM_SHADERS; ++shaderIndex ) {
		sdRenderShaderGLSL* shader = shaders[ shaderIndex ];
		for ( int i = 0; i < shader->GetNumParameterBindings(); ++i ) {
			const sdDeclRenderBinding* binding = shader->GetParameterBinding( i );
			binding->Evaluate();
			if ( locationIndex < 32 && uniformLocations[ locationIndex ] >= 0 ) qglUniform4fvARB( uniformLocations[ locationIndex ], 1, binding->GetVector() );
			++locationIndex;
		}
		for ( int i = 0; i < shader->GetNumInfrequentParameterBindings(); ++i ) {
			const sdDeclRenderBinding* binding = shader->GetInfrequentParameterBinding( i );
			if ( locationIndex < 32 && uniformLocations[ locationIndex ] >= 0 ) qglUniform4fvARB( uniformLocations[ locationIndex ], 1, binding->GetVector() );
			++locationIndex;
		}
	}
}

void sdRenderProgramGLSL::UpdateHWSkinningParameters( const idJointMat* joints, int numJoints ) {
	if ( hwSkinningUniformLocation >= 0 && qglUniform4fvARB != NULL && joints != NULL ) {
		qglUniform4fvARB( hwSkinningUniformLocation, 3 * numJoints, joints[ 0 ].ToFloatPtr() );
	}
}

sdRenderShader* SD_AllocRenderShader( const char* type ) {
	if ( idStr::Icmp( type, "arb" ) == 0 ) return new sdRenderShaderARB;
	if ( idStr::Icmp( type, "cg" ) == 0 ) return new sdRenderShaderCg;
	if ( idStr::Icmp( type, "glsl" ) == 0 ) return new sdRenderShaderGLSL;
	return NULL;
}

void SD_UnbindRenderProgram() {
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	if ( qglUseProgramObjectARB != NULL ) qglUseProgramObjectARB( 0 );
}

void SD_ApplyRenderProgramState( int stateBits, cullType_t cullType ) {
	ApplyState( stateBits, cullType );
}
