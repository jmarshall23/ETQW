// Copyright (C) 2007 Id Software, Inc.
//
// OpenGL render-program implementation recovered from the ETQW PDB layouts.

#ifndef __DECL_RENDER_PROGRAM_OPENGL_H__
#define __DECL_RENDER_PROGRAM_OPENGL_H__

class idLexer;
class idStr;
class idJointMat;
class sdDeclRenderBinding;
class sdDeclRenderProgram;
class sdRenderProgramParser;

enum shaderType_t {
	ST_VERTEX_SHADER,
	ST_FRAGMENT_SHADER,
	ST_NUM_SHADERS
};

class sdRenderProgram;

class sdRenderShader {
public:
						sdRenderShader();
	virtual				~sdRenderShader();

	void				IncRef() { ++refCount; }
	void				DecRef() { if ( --refCount == 0 ) delete this; }
	void				SetShaderType( shaderType_t type ) { shaderType = type; }
	shaderType_t		GetShaderType() const { return shaderType; }

	virtual sdRenderProgram* CreateProgram() const = 0;
	virtual bool			IsSupported() const = 0;
	virtual int			GetPreCompilerFlags() const { return 0; }
	virtual bool			PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int shaderStartLine ) = 0;
	virtual bool			Compile( const idStr& source, sdDeclRenderProgram& renderProgram ) = 0;
	virtual void			AllocStateCache();
	virtual void			ParseFlags( idLexer& src );

	void				ProcessRenderBindings();
	int				GetNumParameterBindings() const { return numParameterBindings; }
	const sdDeclRenderBinding* GetParameterBinding( int index ) const { return parameterBindings[ index ]; }
	int				GetNumInfrequentParameterBindings() const { return numInfrequentParameterBindings; }
	const sdDeclRenderBinding* GetInfrequentParameterBinding( int index ) const { return infrequentParameterBindings[ index ]; }
	int				GetNumVertexAttribBindings() const { return numVertexAttribBindings; }
	const sdDeclRenderBinding* GetVertexAttribBinding( int index ) const { return vertexAttribBindings[ index ]; }
	int				GetNumTextureBindings() const { return numTextureBindings; }
	float*				GetParameterState() { return parameterState; }

protected:
	virtual void			Purge();

	int					refCount;
	shaderType_t			shaderType;
	int					numParameterBindings;
	const sdDeclRenderBinding* parameterBindings[ 32 ];
	int					numInfrequentParameterBindings;
	const sdDeclRenderBinding* infrequentParameterBindings[ 32 ];
	int					numVertexAttribBindings;
	const sdDeclRenderBinding* vertexAttribBindings[ 8 ];
	int					numTextureBindings;
	float*				parameterState;

	friend class sdRenderProgramParser;
};

class sdRenderProgram {
public:
	virtual				~sdRenderProgram() {}
	virtual bool			Link( const sdDeclRenderProgram& renderProgram ) = 0;
	virtual void			Bind() = 0;
	virtual void			UpdateHWSkinningParameters( const idJointMat* joints, int numJoints ) {}
	virtual void			UpdateParameters() = 0;
	virtual int			GetStateBits() const = 0;
	virtual bool			AttachShader( sdRenderShader* shader ) = 0;
	virtual sdRenderShader* GetShader( shaderType_t shaderType ) = 0;
};

class sdRenderShaderARB : public sdRenderShader {
public:
						sdRenderShaderARB();
	virtual				~sdRenderShaderARB();

	virtual sdRenderProgram* CreateProgram() const;
	virtual bool			IsSupported() const;
	virtual bool			PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int shaderStartLine );
	virtual bool			Compile( const idStr& source, sdDeclRenderProgram& renderProgram );
	virtual void			AllocStateCache();
	virtual void			ParseFlags( idLexer& src );

	unsigned int&			GetShader() { return shader; }

protected:
	virtual void			Purge();
	bool				Upload( const idStr& source, sdDeclRenderProgram& renderProgram );

	unsigned int			shader;
	bool					userDecompress;
};

class sdRenderShaderCg : public sdRenderShaderARB {
public:
	virtual int			GetPreCompilerFlags() const { return 2; }
	virtual bool			PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int shaderStartLine );
	virtual bool			Compile( const idStr& source, sdDeclRenderProgram& renderProgram );
};

class sdRenderProgramARB : public sdRenderProgram {
public:
						sdRenderProgramARB();
	virtual				~sdRenderProgramARB();

	virtual bool			Link( const sdDeclRenderProgram& renderProgram );
	virtual void			Bind();
	virtual void			UpdateParameters();
	virtual int			GetStateBits() const { return 0x300000; }
	virtual bool			AttachShader( sdRenderShader* shader );
	virtual sdRenderShader* GetShader( shaderType_t shaderType );

	static const unsigned int shaderTypes[ ST_NUM_SHADERS ];

private:
	sdRenderShaderARB*		shaders[ ST_NUM_SHADERS ];
};

class sdRenderShaderGLSL : public sdRenderShader {
public:
						sdRenderShaderGLSL();
	virtual				~sdRenderShaderGLSL();

	virtual sdRenderProgram* CreateProgram() const;
	virtual bool			IsSupported() const;
	virtual bool			PreCompile( idStr& source, sdDeclRenderProgram& renderProgram, int shaderStartLine );
	virtual bool			Compile( const idStr& source, sdDeclRenderProgram& renderProgram );

	unsigned int&			GetShader() { return shader; }

protected:
	virtual void			Purge();
	unsigned int			shader;
};

class sdRenderProgramGLSL : public sdRenderProgram {
public:
						sdRenderProgramGLSL();
	virtual				~sdRenderProgramGLSL();

	virtual bool			Link( const sdDeclRenderProgram& renderProgram );
	virtual void			Bind();
	virtual void			UpdateHWSkinningParameters( const idJointMat* joints, int numJoints );
	virtual void			UpdateParameters();
	virtual int			GetStateBits() const { return 0x400000; }
	virtual bool			AttachShader( sdRenderShader* shader );
	virtual sdRenderShader* GetShader( shaderType_t shaderType );
	static void			WarningInfoLog( const char* prefix, unsigned int object );

	static const unsigned int shaderTypes[ ST_NUM_SHADERS ];

private:
	void				Purge();

	sdRenderShaderGLSL*		shaders[ ST_NUM_SHADERS ];
	unsigned int			program;
	int					uniformLocations[ 32 ];
	int					hwSkinningUniformLocation;
};

sdRenderShader* SD_AllocRenderShader( const char* type );
void SD_UnbindRenderProgram();
void SD_ApplyRenderProgramState( int stateBits, cullType_t cullType );

#if defined( _M_IX86 )
static_assert( sizeof( sdRenderShader ) == 0x140, "sdRenderShader must match the ETQW PDB layout" );
static_assert( sizeof( sdRenderShaderARB ) == 0x148, "sdRenderShaderARB must match the ETQW PDB layout" );
static_assert( sizeof( sdRenderProgramARB ) == 0x0C, "sdRenderProgramARB must match the ETQW PDB layout" );
static_assert( sizeof( sdRenderShaderGLSL ) == 0x144, "sdRenderShaderGLSL must match the ETQW PDB layout" );
static_assert( sizeof( sdRenderProgramGLSL ) == 0x94, "sdRenderProgramGLSL must match the ETQW PDB layout" );
#endif

#endif
