// Copyright (C) 2007 Id Software, Inc.
//
// Render-program declaration recovered from the ETQW 1.5 SDK ABI and PDB.

#ifndef __DECL_RENDER_PROGRAM_H__
#define __DECL_RENDER_PROGRAM_H__

#include "../framework/declManager.h"
#include "../renderer/RendererEnums.h"

class sdDeclRenderBinding;
class sdRenderProgram;
class idJointMat;
class sdRenderProgramParser;

class sdDeclRenderProgram : public idDecl {
public:
	enum rpAltVersions_t {
		RPAV_AMBIENTLIGHTING = BIT( 0 ),
		RPAV_HWSKINNING = BIT( 1 ),
		RPAV_HARDSKINNING = BIT( 2 ),
		RPAV_INSTANCING = BIT( 3 ),
		RPAV_COVERAGE = BIT( 4 ),
		RPAV_LOD = BIT( 5 ),
		RPAV_ALPHATOCOVERAGE = BIT( 6 ),
		RPAV_DEPTH = BIT( 7 ),
		RPAV_EARLYCULL = BIT( 8 ),
		RPAV_AMBIENTLIT = BIT( 9 ),
		RPAV_NOTLIT = BIT( 10 ),
		RPAV_SKINNING = RPAV_HWSKINNING | RPAV_HARDSKINNING,
		RPAV_UNCOMMON = RPAV_INSTANCING | RPAV_COVERAGE | RPAV_DEPTH | RPAV_EARLYCULL
	};

	enum rpFlags_t {
		RP_DEFINESSTATE = BIT( 0 ),
		RP_DEFINESCULL = BIT( 1 ),
		RP_INTERACTION = BIT( 2 ),
		RP_LOWRANGEUV = BIT( 3 )
	};

					sdDeclRenderProgram();
	virtual			~sdDeclRenderProgram();

	virtual const char*	DefaultDefinition() const;
	virtual bool		Parse( const char* text, const int textLength );
	virtual size_t		Size() const { return sizeof( sdDeclRenderProgram ); }
	virtual void		FreeData();
	virtual void		List() const;
	virtual void		Dot() const;

	void				Bind() const;
	void				UpdateParameters() const;
	void				UpdateHWSkinningParameters( const idJointMat* joints, const int numJoints ) const;
	bool				IsInteraction() const { return ( flags & RP_INTERACTION ) != 0; }
	bool				UsesLowRangeUVs() const { return ( flags & RP_LOWRANGEUV ) != 0; }
	void				SetState( const int extraState, const cullType_t extraCull ) const;
	int					GetNumTextureBindings() const { return numTextureBindings; }
	const sdDeclRenderBinding* GetTextureBinding( const int index ) const {
		return index >= 0 && index < numTextureBindings ? textureBindings[ index ] : NULL;
	}
	int					GetRequiredVertexAttribs() const { return requiredVertexAttribs; }
	const sdDeclRenderProgram* GetAmbientProgram() const { return versionForAmbientLighting; }
	const sdDeclRenderProgram* GetHardwareSkinningProgram() const { return versionForHWSkinning; }
	const sdDeclRenderProgram* GetHardSkinningProgram() const { return versionForHardSkinning; }
	const sdDeclRenderProgram* GetInstanceProgram() const { return versionForInstancing; }
	const sdDeclRenderProgram* GetCoverageProgram() const { return versionForCoverage; }
	const sdDeclRenderProgram* GetLODProgram() const { return versionForLOD; }
	const sdDeclRenderProgram* GetAlphaToCoverageProgram() const { return versionForAlphaToCoverage; }
	const sdDeclRenderProgram* GetDepthProgram() const { return versionForDepth; }
	const sdDeclRenderProgram* GetEarlyCullProgram() const { return versionForEarlyCull; }
	const sdDeclRenderProgram* GetAmbientLitProgram() const { return versionForAmbientLit; }
	const sdDeclRenderProgram* GetNotLitProgram() const { return versionForNotLit; }
	const sdDeclRenderProgram* GetFallbackProgram() const { return versionForFallback; }
	int					GetMachineSpec() const { return machineSpec; }
	unsigned int			GetAltVersions() const { return altVersions; }
	float				GetImposterBrightness() const { return imposterBrightness; }
	sdRenderProgram*		GetProgram() const { return program; }

private:
	struct parseData_t {
		idStr			ambientVersion;
		idStr			shadowMapVersion;
		idStr			hwSkinningVersion;
		idStr			hardSkinningVersion;
		idStr			instanceVersion;
		idStr			coverageVersion;
		idStr			lodVersion;
		idStr			fallBackVersion;
		idStr			alphaToCoverageVersion;
		idStr			depthVersion;
		idStr			earlyCullVersion;
		idStr			ambLitVersion;
		idStr			notLitVersion;
		idStr			shaderReferences[ 2 ];
	};

	bool				ParseState( idLexer& src );
	bool				ParseProgram( idLexer& src, parseData_t& parseData );
	bool				ResolveReferences( parseData_t& parseData );

	int					flags;
	sdRenderProgram*		program;
	int					numTextureBindings;
	const sdDeclRenderBinding* textureBindings[ 16 ];
	int					stateBits;
	int					stateMask;
	cullType_t			cullType;
	int					requiredVertexAttribs;
	int					machineSpec;
	float				imposterBrightness;
	const sdDeclRenderProgram* versionForAmbientLighting;
	const sdDeclRenderProgram* versionForHWSkinning;
	const sdDeclRenderProgram* versionForHardSkinning;
	const sdDeclRenderProgram* versionForInstancing;
	const sdDeclRenderProgram* versionForCoverage;
	const sdDeclRenderProgram* versionForLOD;
	const sdDeclRenderProgram* versionForFallback;
	const sdDeclRenderProgram* versionForAlphaToCoverage;
	const sdDeclRenderProgram* versionForDepth;
	const sdDeclRenderProgram* versionForEarlyCull;
	const sdDeclRenderProgram* versionForAmbientLit;
	const sdDeclRenderProgram* versionForNotLit;
	unsigned int			altVersions;

	friend class sdRenderProgramParser;
};

#if defined( _M_IX86 )
static_assert( sizeof( sdDeclRenderProgram ) == 0xA0, "sdDeclRenderProgram must match the ETQW PDB layout" );
#endif

#endif
