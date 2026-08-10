// Copyright (C) 2007 Id Software, Inc.
//
// Declaration-template interface reconstructed from the ETQW Microsoft PDB.

#ifndef __DECL_TEMPLATE_H__
#define __DECL_TEMPLATE_H__

#include "../framework/DeclManager.h"

class sdDeclTemplate : public idDecl {
public:
	struct Parameter {
		idStr	name;
		idStr	value;
		bool	hasDefault;

		Parameter() : hasDefault( false ) {}
	};

	class Command {
	public:
		virtual ~Command() {}
		virtual void Evaluate( const idStrList& arguments, idStr& output ) = 0;
	};

	sdDeclTemplate();
	virtual ~sdDeclTemplate();

	virtual const char*	DefaultDefinition() const;
	virtual bool		Parse( const char* text, const int textLength );
	virtual void		FreeData();
	virtual size_t		Size() const { return sizeof( *this ); }

	bool				Evaluate( const idStrList& arguments, idStr& output ) const;
	const char*			GetParameterValue( const idStrList& arguments, const char* parameterName ) const;

private:
	bool				ParseParameters( idLexer& src );
	Command*			ParseAppend( idLexer& src );
	Command*			ParseConditional( idLexer& src );
	bool				ParseCommands( idLexer& src, idList< Command* >& commandList );
	void				ExpandArguments( idStrList& arguments ) const;

	int					numDefault;
	idStr				text;
	idList< Parameter* > parameters;
	idList< Command* >	commands;
};

#endif // __DECL_TEMPLATE_H__
