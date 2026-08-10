// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW PDB and renderer Hex-Rays output.

#ifndef __RENDERER_RENDERSYSTEMBACKEND_H__
#define __RENDERER_RENDERSYSTEMBACKEND_H__

#include "RenderSystem.h"
#include "RenderWorld.h"
#include "ScreenRect.h"

struct renderCrop_t {
	int x;
	int y;
	int width;
	int height;
};

class sdRenderSystemBackend {
public:
	void	Init();
	void	Shutdown();
	void	BeginFrame( int windowWidth, int windowHeight );
	void	RenderViewToViewport( const renderView_t* renderView, idScreenRect* viewport );
	void	CropRenderSize( int width, int height, bool makePowerOfTwo );
	void	UnCrop();
	void	CaptureRenderToImage( idImage* image, int faceNum, copyBuffer_t buffer, int endGuiPos );

	const renderCrop_t& CurrentCrop() const {
		return renderCrops[ currentRenderCrop ];
	}

private:
	static const int MAX_RENDER_CROPS = 8;

	renderCrop_t	renderCrops[ MAX_RENDER_CROPS ];
	int				currentRenderCrop;
};

extern sdRenderSystemBackend renderSystemBackend;

#endif
