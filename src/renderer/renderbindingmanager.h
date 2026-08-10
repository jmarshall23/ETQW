// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the original ETQW PDB and retail executable.

#ifndef __RENDERER_RENDERBINDINGMANAGER_H__
#define __RENDERER_RENDERBINDINGMANAGER_H__

class idDecl;
class sdDeclRenderBinding;

class sdRenderBindingManager {
public:
	void Init();
	void Shutdown();
	void UpdateInfrequentRenderBindings();
	void UpdateParameter( const sdDeclRenderBinding* const renderBinding );

private:
	struct infrequentRenderBinding_t {
		const sdDeclRenderBinding*	renderBinding;
		float					state[ 4 ];
	};

	static void RenderBindingPostParseCallback( idDecl* decl );
	void RenderBindingPostParse( idDecl* decl );

	idList< infrequentRenderBinding_t* > infrequentRenderBindings;
};

extern sdRenderBindingManager* renderBindingManager;

static_assert( sizeof( sdRenderBindingManager ) == 16,
	"sdRenderBindingManager must match the ETQW PDB layout" );

#endif /* !__RENDERER_RENDERBINDINGMANAGER_H__ */
