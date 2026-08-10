// Copyright (C) 2007 Id Software, Inc.
//
// ETQW render-program preprocessor.  Its source ownership and public method
// names come from RenderProgramParser.obj in the retail PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderProgramParser.h"
#include "DeclRenderProgram_opengl.h"
#include "declRenderProgram.h"
#include "declRenderBinding.h"
#include "declTypeHolder.h"
#include "../framework/CVarSystem.h"
#include "../framework/FileSystem.h"

#include <ctype.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace {

struct conditionalState_t {
	bool parentActive;
	bool condition;
	bool elseSeen;
};

class includeTextSetter_t {
public:
	explicit includeTextSetter_t( idStr& destination_ ) : destination( destination_ ) {}

	void operator()( const char* text, const int length ) const {
		if ( text == NULL || length <= 0 ) destination.Clear();
		else destination = idStr( text, 0, length );
	}

private:
	idStr& destination;
};

bool IsNameStart( char c ) {
	return isalpha( static_cast< unsigned char >( c ) ) != 0 || c == '_';
}

bool IsNameChar( char c ) {
	return isalnum( static_cast< unsigned char >( c ) ) != 0 || c == '_';
}

std::string Trim( const std::string& value ) {
	size_t first = 0;
	while ( first < value.length() && isspace( static_cast< unsigned char >( value[ first ] ) ) ) ++first;
	size_t last = value.length();
	while ( last > first && isspace( static_cast< unsigned char >( value[ last - 1 ] ) ) ) --last;
	return value.substr( first, last - first );
}

bool CurrentActive( const std::vector< conditionalState_t >& stack ) {
	if ( stack.empty() ) return true;
	const conditionalState_t& state = stack.back();
	return state.parentActive && state.condition;
}

void AppendText( idStr& output, const char* text, int length ) {
	if ( text == NULL || length <= 0 ) return;
	for ( int i = 0; i < length; ++i ) output.Append( text[ i ] );
}

}

sdRenderProgramParser::sdRenderProgramParser() {
	flags = 0;
	renderProgramDecl = NULL;
	shader = NULL;
	defines.SetGranularity( 1 );
}

sdRenderProgramParser::~sdRenderProgramParser() {
}

bool sdRenderProgramParser::IsDefined( const char* name ) const {
	for ( int i = 0; i < defines.Num(); ++i ) {
		if ( defines[ i ].Icmp( name ) == 0 ) return true;
	}
	return false;
}

void sdRenderProgramParser::Define( const char* name ) {
	if ( name != NULL && *name != '\0' && !IsDefined( name ) ) defines.Append( name );
}

bool sdRenderProgramParser::EvaluateCondition( const char* expression ) const {
	std::string condition = Trim( expression != NULL ? expression : "" );
	while ( condition.length() >= 2 && condition.front() == '(' && condition.back() == ')' ) {
		condition = Trim( condition.substr( 1, condition.length() - 2 ) );
	}
	bool invert = false;
	if ( !condition.empty() && condition[ 0 ] == '!' && ( condition.length() == 1 || condition[ 1 ] != '=' ) ) {
		invert = true;
		condition = Trim( condition.substr( 1 ) );
	}

	const char* operators[] = { "!=", "==", ">=", "<=", ">", "<" };
	std::string lhs = condition;
	std::string rhs;
	const char* comparison = NULL;
	for ( int i = 0; i < 6; ++i ) {
		const size_t position = condition.find( operators[ i ] );
		if ( position != std::string::npos ) {
			comparison = operators[ i ];
			lhs = Trim( condition.substr( 0, position ) );
			rhs = Trim( condition.substr( position + strlen( operators[ i ] ) ) );
			break;
		}
	}

	auto ResolveValue = [this]( const std::string& token ) -> double {
		if ( token.empty() ) return 0.0;
		char* end = NULL;
		const double numeric = strtod( token.c_str(), &end );
		if ( end != token.c_str() && *end == '\0' ) return numeric;
		if ( IsDefined( token.c_str() ) ) return 1.0;
		return cvarSystem != NULL ? cvarSystem->GetCVarFloat( token.c_str() ) : 0.0;
	};

	const double leftValue = ResolveValue( lhs );
	bool result;
	if ( comparison == NULL ) {
		result = leftValue != 0.0;
	} else {
		const double rightValue = ResolveValue( rhs );
		if ( strcmp( comparison, "!=" ) == 0 ) result = leftValue != rightValue;
		else if ( strcmp( comparison, "==" ) == 0 ) result = leftValue == rightValue;
		else if ( strcmp( comparison, ">=" ) == 0 ) result = leftValue >= rightValue;
		else if ( strcmp( comparison, "<=" ) == 0 ) result = leftValue <= rightValue;
		else if ( strcmp( comparison, ">" ) == 0 ) result = leftValue > rightValue;
		else result = leftValue < rightValue;
	}
	return invert ? !result : result;
}

bool sdRenderProgramParser::ProcessBinding( const char* name, idStr& output ) {
	const sdDeclRenderBinding* binding = declHolder.FindRenderBinding( name, false );
	if ( binding == NULL ) {
		common->Warning( "unknown render binding $%s in '%s'", name, renderProgramDecl->GetName() );
		return false;
	}
	declManager->AddDependency( renderProgramDecl, binding );

	switch ( binding->GetBindingType() ) {
		case sdDeclRenderBinding::BT_VECTOR: {
			int index = 0;
			for ( ; index < shader->numParameterBindings; ++index ) {
				if ( shader->parameterBindings[ index ] == binding ) break;
			}
			if ( index == shader->numParameterBindings ) {
				if ( index == 32 ) return false;
				shader->parameterBindings[ shader->numParameterBindings++ ] = binding;
			}
			break;
		}
		case sdDeclRenderBinding::BT_TEXTURE: {
			int index = 0;
			for ( ; index < renderProgramDecl->numTextureBindings; ++index ) {
				if ( renderProgramDecl->textureBindings[ index ] == binding ) break;
			}
			if ( index == renderProgramDecl->numTextureBindings ) {
				if ( index == 16 ) return false;
				renderProgramDecl->textureBindings[ renderProgramDecl->numTextureBindings++ ] = binding;
			}
			++shader->numTextureBindings;
			break;
		}
		case sdDeclRenderBinding::BT_ATTRIB: {
			int index = 0;
			for ( ; index < shader->numVertexAttribBindings; ++index ) {
				if ( shader->vertexAttribBindings[ index ] == binding ) break;
			}
			if ( index == shader->numVertexAttribBindings ) {
				if ( index == 8 ) return false;
				shader->vertexAttribBindings[ shader->numVertexAttribBindings++ ] = binding;
			}
			break;
		}
	}

	output.Append( '$' );
	output.Append( binding->GetName() );
	output.Append( '$' );
	return true;
}

bool sdRenderProgramParser::ProcessInclude( const char* path, idStr& output, int includeDepth ) {
	if ( includeDepth >= 32 ) {
		common->Warning( "render program include nesting exceeded for '%s'", path );
		return false;
	}
	idStr fullPath = "renderprogs/";
	fullPath.Append( path );
	void* fileBuffer = NULL;
	const int length = fileSystem->ReadFile( fullPath.c_str(), &fileBuffer, NULL );
	if ( length < 0 || fileBuffer == NULL ) {
		common->Warning( "couldn't load render program include '%s'", fullPath.c_str() );
		return false;
	}
	declManager->AddDependency( renderProgramDecl, fullPath.c_str() );

	const char* includeText = static_cast< const char* >( fileBuffer );
	int includeLength = length;
	idStr expandedText;
	if ( idStr::FindText( includeText, "useTemplate", false ) >= 0 ) {
		includeTextSetter_t setter( expandedText );
		sdFunctions::sdCallable< void( const char*, const int ) > callback( setter );
		// The original Directive_include expands templates after loading the
		// include and deliberately preserves comments (stripComments=false).
		if ( !declManager->EvaluateTemplates( renderProgramDecl, includeText, callback, false ) ) {
			fileSystem->FreeFile( fileBuffer );
			return false;
		}
		includeText = expandedText.c_str();
		includeLength = expandedText.Length();
	}

	const bool result = ProcessText( includeText, includeLength, output, includeDepth + 1 );
	fileSystem->FreeFile( fileBuffer );
	return result;
}

bool sdRenderProgramParser::ProcessText( const char* text, int length, idStr& output, int includeDepth ) {
	std::vector< conditionalState_t > conditionalStack;
	for ( int i = 0; i < length; ) {
		if ( text[ i ] != '$' ) {
			if ( CurrentActive( conditionalStack ) ) output.Append( text[ i ] );
			++i;
			continue;
		}

		const int dollar = i++;
		if ( i >= length || !IsNameStart( text[ i ] ) ) {
			if ( CurrentActive( conditionalStack ) ) output.Append( '$' );
			continue;
		}
		const int nameStart = i;
		while ( i < length && IsNameChar( text[ i ] ) ) ++i;
		const std::string name( text + nameStart, text + i );
		const bool directive =
			idStr::Icmp( name.c_str(), "if" ) == 0 || idStr::Icmp( name.c_str(), "ifdef" ) == 0 ||
			idStr::Icmp( name.c_str(), "ifndef" ) == 0 || idStr::Icmp( name.c_str(), "elif" ) == 0 ||
			idStr::Icmp( name.c_str(), "else" ) == 0 || idStr::Icmp( name.c_str(), "endif" ) == 0 ||
			idStr::Icmp( name.c_str(), "define" ) == 0 || idStr::Icmp( name.c_str(), "include" ) == 0;
		if ( !directive ) {
			if ( CurrentActive( conditionalStack ) && !ProcessBinding( name.c_str(), output ) ) return false;
			continue;
		}

		int lineEnd = i;
		while ( lineEnd < length && text[ lineEnd ] != '\n' && text[ lineEnd ] != '\r' ) ++lineEnd;
		const std::string arguments = Trim( std::string( text + i, text + lineEnd ) );
		const bool wasActive = CurrentActive( conditionalStack );

		if ( idStr::Icmp( name.c_str(), "if" ) == 0 || idStr::Icmp( name.c_str(), "ifdef" ) == 0 || idStr::Icmp( name.c_str(), "ifndef" ) == 0 ) {
			bool condition = false;
			if ( idStr::Icmp( name.c_str(), "if" ) == 0 ) condition = EvaluateCondition( arguments.c_str() );
			else if ( idStr::Icmp( name.c_str(), "ifdef" ) == 0 ) condition = IsDefined( arguments.c_str() );
			else condition = !IsDefined( arguments.c_str() );
			conditionalState_t state = { wasActive, condition, false };
			conditionalStack.push_back( state );
		} else if ( idStr::Icmp( name.c_str(), "elif" ) == 0 ) {
			if ( conditionalStack.empty() || conditionalStack.back().elseSeen ) return false;
			conditionalState_t& state = conditionalStack.back();
			if ( state.condition ) state.condition = false;
			else state.condition = EvaluateCondition( arguments.c_str() );
		} else if ( idStr::Icmp( name.c_str(), "else" ) == 0 ) {
			if ( conditionalStack.empty() || conditionalStack.back().elseSeen ) return false;
			conditionalState_t& state = conditionalStack.back();
			state.condition = !state.condition;
			state.elseSeen = true;
		} else if ( idStr::Icmp( name.c_str(), "endif" ) == 0 ) {
			if ( conditionalStack.empty() ) return false;
			conditionalStack.pop_back();
		} else if ( idStr::Icmp( name.c_str(), "define" ) == 0 ) {
			if ( wasActive ) {
				const size_t end = arguments.find_first_of( " \t" );
				Define( arguments.substr( 0, end ).c_str() );
			}
		} else if ( idStr::Icmp( name.c_str(), "include" ) == 0 && wasActive ) {
			std::string path = arguments;
			if ( path.length() >= 2 && path.front() == '"' && path.back() == '"' ) path = path.substr( 1, path.length() - 2 );
			if ( !ProcessInclude( path.c_str(), output, includeDepth ) ) return false;
		}

		i = lineEnd;
		if ( i < length && text[ i ] == '\r' ) ++i;
		if ( i < length && text[ i ] == '\n' ) {
			if ( CurrentActive( conditionalStack ) ) output.Append( '\n' );
			++i;
		}
		(void)dollar;
	}
	return conditionalStack.empty();
}

bool sdRenderProgramParser::PreCompile( sdDeclRenderProgram& renderProgramDecl_, sdRenderShader& shader_,
		idStr& program, int flags_, int, const char* ) {
	flags = flags_;
	renderProgramDecl = &renderProgramDecl_;
	shader = &shader_;
	defines.Clear();
	idStr output;
	if ( !ProcessText( program.c_str(), program.Length(), output, 0 ) ) return false;
	program = output;
	return true;
}
