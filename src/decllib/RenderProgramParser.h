// Copyright (C) 2007 Id Software, Inc.

#ifndef __RENDER_PROGRAM_PARSER_H__
#define __RENDER_PROGRAM_PARSER_H__

class idStr;
class sdDeclRenderProgram;
class sdRenderShader;

class sdRenderProgramParser {
public:
						sdRenderProgramParser();
						~sdRenderProgramParser();

	bool				PreCompile( sdDeclRenderProgram& renderProgramDecl, sdRenderShader& shader,
							idStr& program, int flags, int startLine, const char* fileName );

private:
	bool				ProcessText( const char* text, int length, idStr& output, int includeDepth );
	bool				ProcessBinding( const char* name, idStr& output );
	bool				ProcessInclude( const char* path, idStr& output, int includeDepth );
	bool				EvaluateCondition( const char* expression ) const;
	bool				IsDefined( const char* name ) const;
	void				Define( const char* name );

	int					flags;
	sdDeclRenderProgram*		renderProgramDecl;
	sdRenderShader*			shader;
	idStrList				defines;
};

#endif
