// Copyright (C) 2007 Id Software, Inc.
//
// Runtime GLSL-to-SPIR-V compiler support for the Vulkan renderer.

#ifndef __RENDERER_RUNTIMESPIRVCOMPILER_H__
#define __RENDERER_RUNTIMESPIRVCOMPILER_H__

#include "../idlib/text/Str.h"
#include "../idlib/containers/List.h"

enum sdSpirvShaderStage {
	SPIRV_SHADER_STAGE_VERTEX,
	SPIRV_SHADER_STAGE_FRAGMENT,
	SPIRV_SHADER_STAGE_GEOMETRY,
	SPIRV_SHADER_STAGE_TESS_CONTROL,
	SPIRV_SHADER_STAGE_TESS_EVALUATION,
	SPIRV_SHADER_STAGE_COMPUTE
};

struct sdSpirvCompilerConfig {
	idStr	compilerPath;
	idStr	validatorPath;
	idStr	cacheDirectory;
	int		timeoutMilliseconds;
	bool	validate;

	sdSpirvCompilerConfig();
};

struct sdSpirvCompileRequest {
	const char*			sourceName;
	const char*			source;
	int					sourceLength;
	sdSpirvShaderStage	stage;
	const char*			cacheSalt;
	bool				forceRecompile;

	sdSpirvCompileRequest();
};

struct sdSpirvCompileResult {
	idList< unsigned int >	words;
	idStr					diagnostics;
	idStr					cachePath;
	bool					cacheHit;
	int					elapsedMilliseconds;

	sdSpirvCompileResult();
	void Clear();
};

// This class deliberately has no Vulkan dependency. A Vulkan render-program
// implementation can pass result.words directly to VkShaderModuleCreateInfo.
// The compiler executable may be 64-bit even though etqw.exe is 32-bit.
class sdRuntimeSpirvCompiler {
public:
	bool Compile( const sdSpirvCompilerConfig& config,
		const sdSpirvCompileRequest& request, sdSpirvCompileResult& result ) const;

	static const char* StageName( sdSpirvShaderStage stage );
	static const char* StageExtension( sdSpirvShaderStage stage );
};

// Registers developer commands and reports compiler availability. Safe to
// call repeatedly across renderer restarts.
void R_InitRuntimeSpirvCompiler();

// Renderer-facing convenience entry point. Compiler and cache locations are
// supplied by r_vkShaderCompiler, r_vkSpirvValidator, and r_vkShaderCache.
bool R_CompileVulkanGLSL( const char* sourceName, const char* source,
	int sourceLength, sdSpirvShaderStage stage, const char* cacheSalt,
	bool forceRecompile, sdSpirvCompileResult& result );

#endif /* !__RENDERER_RUNTIMESPIRVCOMPILER_H__ */
