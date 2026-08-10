// Copyright (C) 2007 Id Software, Inc.
//
// ETQW declaration-template parser reconstructed from declTemplate.cpp PDB
// symbols and the corresponding Hex-Rays bodies in quakewars-hexrays/etqw.c.

#include "precompiled.h"
#pragma hdrstop

#include "declTemplate.h"

namespace {

void StripOuterBraces( idStr& value ) {
	value.StripTrailing( " " );
	value.StripLeadingOnce( "{" );
	value.StripLeadingWhiteSpace();
	value.StripTrailingOnce( "}" );
	value.StripTrailingWhiteSpace();
}

class sdTemplateAppendCommand : public sdDeclTemplate::Command {
public:
	explicit sdTemplateAppendCommand( const idStr& value ) : text( value ) {}

	virtual void Evaluate( const idStrList&, idStr& output ) {
		if ( !output.IsEmpty() && output[ output.Length() - 1 ] != '\n' ) {
			output.Append( '\n' );
		}
		output.Append( text );
	}

private:
	idStr text;
};

class sdTemplateConditionalCommand : public sdDeclTemplate::Command {
public:
	explicit sdTemplateConditionalCommand( const sdDeclTemplate* owner_ ) :
		owner( owner_ ),
		equality( true ),
		lookupLhs( false ),
		lookupRhs( false ) {
	}

	virtual ~sdTemplateConditionalCommand() {
		for ( int i = 0; i < commands.Num(); i++ ) {
			delete commands[ i ];
		}
		commands.Clear();
	}

	virtual void Evaluate( const idStrList& arguments, idStr& output );

	const sdDeclTemplate* owner;
	idStr lhs;
	idStr rhs;
	bool equality;
	bool lookupLhs;
	bool lookupRhs;
	idList< sdDeclTemplate::Command* > commands;
};

}

sdDeclTemplate::sdDeclTemplate() :
	numDefault( 0 ) {
}

sdDeclTemplate::~sdDeclTemplate() {
	FreeData();
}

const char* sdDeclTemplate::DefaultDefinition() const {
	return "{ parameters <> text {} }\n";
}

void sdDeclTemplate::FreeData() {
	for ( int i = 0; i < parameters.Num(); i++ ) {
		delete parameters[ i ];
	}
	parameters.Clear();
	for ( int i = 0; i < commands.Num(); i++ ) {
		delete commands[ i ];
	}
	commands.Clear();
	text.Clear();
	numDefault = 0;
}

bool sdDeclTemplate::ParseParameters( idLexer& src ) {
	if ( !src.ExpectTokenString( "<" ) ) {
		return false;
	}

	for ( ;; ) {
		idToken token;
		if ( !src.ReadToken( &token ) ) {
			src.Warning( "sdDeclTemplate::ParseParameters: unexpected end of file" );
			return false;
		}
		if ( token == ">" ) {
			return true;
		}
		if ( token == "," ) {
			continue;
		}

		Parameter* parameter = new Parameter;
		parameter->name = token;
		parameters.Append( parameter );

		if ( !src.ReadToken( &token ) ) {
			src.Warning( "sdDeclTemplate::ParseParameters: unexpected end of file after '%s'", parameter->name.c_str() );
			return false;
		}
		if ( token == "=" ) {
			if ( !src.ReadToken( &token ) ) {
				src.Warning( "sdDeclTemplate::ParseParameters: missing default value for '%s'", parameter->name.c_str() );
				return false;
			}
			parameter->value = token;
			parameter->hasDefault = true;
			numDefault++;
			if ( !src.ReadToken( &token ) ) {
				src.Warning( "sdDeclTemplate::ParseParameters: unexpected end of file" );
				return false;
			}
		} else if ( numDefault != 0 ) {
			src.Warning(
				"sdDeclTemplate::ParseParameters: all parameters after a default parameter must also have defaults"
			);
			return false;
		}

		if ( token == ">" ) {
			return true;
		}
		if ( token != "," ) {
			src.Warning( "sdDeclTemplate::ParseParameters: expected ',' or '>', found '%s'", token.c_str() );
			return false;
		}
	}
}

sdDeclTemplate::Command* sdDeclTemplate::ParseAppend( idLexer& src ) {
	idStr appendText;
	if ( !src.ParseBracedSectionExact( appendText, -1, true ) ) {
		return NULL;
	}
	StripOuterBraces( appendText );
	return new sdTemplateAppendCommand( appendText );
}

sdDeclTemplate::Command* sdDeclTemplate::ParseConditional( idLexer& src ) {
	if ( !src.ExpectTokenString( "(" ) ) {
		return NULL;
	}

	sdTemplateConditionalCommand* command = new sdTemplateConditionalCommand( this );
	idToken token;
	if ( !src.ReadToken( &token ) ) {
		delete command;
		return NULL;
	}
	command->lhs = token;
	command->lookupLhs = token.type != TT_STRING;

	if ( !src.ReadToken( &token ) || ( token != "==" && token != "!=" ) ) {
		src.Warning( "sdDeclTemplate::ParseConditional: expected == or !=" );
		delete command;
		return NULL;
	}
	command->equality = token == "==";

	if ( !src.ReadToken( &token ) ) {
		delete command;
		return NULL;
	}
	command->rhs = token;
	command->lookupRhs = token.type != TT_STRING;

	if ( !src.ExpectTokenString( ")" ) || !ParseCommands( src, command->commands ) ) {
		delete command;
		return NULL;
	}
	return command;
}

bool sdDeclTemplate::ParseCommands( idLexer& src, idList< Command* >& commandList ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}

		Command* command = NULL;
		if ( !token.Icmp( "append" ) ) {
			command = ParseAppend( src );
		} else if ( !token.Icmp( "if" ) ) {
			command = ParseConditional( src );
		} else {
			src.Warning( "sdDeclTemplate::ParseCommands: unknown command '%s'", token.c_str() );
			return false;
		}
		if ( command == NULL ) {
			return false;
		}
		commandList.Append( command );
	}
	src.Warning( "sdDeclTemplate::ParseCommands: unexpected end of file" );
	return false;
}

bool sdDeclTemplate::Parse( const char* sourceText, const int textLength ) {
	FreeData();

	idLexer src;
	src.SetFlags( DECL_LEXER_FLAGS | LEXFL_NOFATALERRORS );
	if ( !src.LoadMemory( sourceText, textLength, GetFileName(), GetLineNum() ) ) {
		return false;
	}
	if ( !src.SkipUntilString( "{" ) ) {
		return false;
	}

	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return !src.HadError();
		}
		if ( !token.Icmp( "parameters" ) ) {
			if ( !ParseParameters( src ) ) {
				return false;
			}
		} else if ( !token.Icmp( "text" ) ) {
			if ( !src.ParseBracedSectionExact( text, -1, true ) ) {
				return false;
			}
			StripOuterBraces( text );
			text.StripLeadingOnce( "<%" );
			text.StripTrailingOnce( "%>" );
		} else if ( !token.Icmp( "commands" ) ) {
			if ( !ParseCommands( src, commands ) ) {
				return false;
			}
		} else {
			src.Warning( "sdDeclTemplate::Parse: unknown token '%s'", token.c_str() );
			return false;
		}
	}
	src.Warning( "sdDeclTemplate::Parse: unexpected end of file" );
	return false;
}

const char* sdDeclTemplate::GetParameterValue(
	const idStrList& arguments,
	const char* parameterName
) const {
	if ( arguments.Num() != parameters.Num() ) {
		common->Warning( "sdDeclTemplate::GetParameterValue: argument/parameter mismatch" );
		return "";
	}
	for ( int i = 0; i < parameters.Num(); i++ ) {
		if ( !parameters[ i ]->name.Icmp( parameterName ) ) {
			return arguments[ i ].c_str();
		}
	}
	return "";
}

void sdDeclTemplate::ExpandArguments( idStrList& arguments ) const {
	while ( arguments.Num() < parameters.Num() ) {
		const Parameter* parameter = parameters[ arguments.Num() ];
		idStr value = parameter->hasDefault ? parameter->value : "";
		for ( int i = 0; i < arguments.Num(); i++ ) {
			value.Replace( parameters[ i ]->name, arguments[ i ] );
		}
		arguments.Append( value );
	}
}

bool sdDeclTemplate::Evaluate( const idStrList& inputArguments, idStr& output ) const {
	const int minimumArguments = parameters.Num() - numDefault;
	if ( inputArguments.Num() < minimumArguments || inputArguments.Num() > parameters.Num() ) {
		common->Warning(
			"sdDeclTemplate::Evaluate: '%s' expected %d arguments (%d optional), received %d",
			GetName(),
			parameters.Num(),
			numDefault,
			inputArguments.Num()
		);
		return false;
	}

	idStrList arguments = inputArguments;
	ExpandArguments( arguments );
	output = text;
	for ( int i = 0; i < commands.Num(); i++ ) {
		commands[ i ]->Evaluate( arguments, output );
	}
	for ( int i = 0; i < parameters.Num(); i++ ) {
		output.Replace( parameters[ i ]->name, arguments[ i ] );
	}
	return true;
}

void sdTemplateConditionalCommand::Evaluate( const idStrList& arguments, idStr& output ) {
	const char* left = lookupLhs ? owner->GetParameterValue( arguments, lhs ) : lhs.c_str();
	const char* right = lookupRhs ? owner->GetParameterValue( arguments, rhs ) : rhs.c_str();
	const bool equal = idStr::Icmp( left, right ) == 0;
	if ( equal != equality ) {
		return;
	}
	for ( int i = 0; i < commands.Num(); i++ ) {
		commands[ i ]->Evaluate( arguments, output );
	}
}

