// Copyright (C) 2007 Id Software, Inc.
//
// Clean translation of quakewars-hexrays/renderer/RenderSystemBackend.cpp.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderSystemBackend.h"

sdRenderSystemBackend renderSystemBackend;

void sdRenderSystemBackend::Init() {
	memset( this, 0, sizeof( *this ) );
}

void sdRenderSystemBackend::Shutdown() {
}

void sdRenderSystemBackend::RenderViewToViewport( const renderView_t* renderView, idScreenRect* viewport ) {
	if ( renderView == NULL || viewport == NULL ) {
		return;
	}

	const renderCrop_t& crop = renderCrops[ currentRenderCrop ];
	const float widthRatio = static_cast< float >( crop.width ) / SCREEN_WIDTH;
	const float heightRatio = static_cast< float >( crop.height ) / SCREEN_HEIGHT;

	viewport->x1 = static_cast< short >( crop.x + renderView->x * widthRatio );
	viewport->x2 = static_cast< short >(
		crop.x + idMath::Ftoi( ( renderView->x + renderView->width ) * widthRatio + 0.5f ) - 1
	);
	viewport->y1 = static_cast< short >(
		crop.y + crop.height -
		idMath::Ftoi( ( renderView->y + renderView->height ) * heightRatio + 0.5f )
	);
	viewport->y2 = static_cast< short >(
		crop.y + crop.height -
		idMath::Ftoi( renderView->y * heightRatio + 0.5f ) - 1
	);
	viewport->zmin = 0.0f;
	viewport->zmax = 1.0f;
}

void sdRenderSystemBackend::CropRenderSize( int width, int height, bool makePowerOfTwo ) {
	renderView_t renderView;
	memset( &renderView, 0, sizeof( renderView ) );
	renderView.width = width;
	renderView.height = height;

	idScreenRect viewport;
	RenderViewToViewport( &renderView, &viewport );

	int croppedWidth = viewport.x2 - viewport.x1 + 1;
	int croppedHeight = viewport.y2 - viewport.y1 + 1;
	if ( makePowerOfTwo ) {
		croppedWidth = idMath::FloorPowerOfTwo( Max( croppedWidth, 1 ) );
		croppedHeight = idMath::FloorPowerOfTwo( Max( croppedHeight, 1 ) );
	}

	const renderCrop_t& rootCrop = renderCrops[ 0 ];
	while ( croppedWidth > rootCrop.width ) {
		croppedWidth >>= 1;
	}
	while ( croppedHeight > rootCrop.height ) {
		croppedHeight >>= 1;
	}

	if ( currentRenderCrop == MAX_RENDER_CROPS - 1 ) {
		common->Error( "sdRenderSystemBackend::CropRenderSize: currentRenderCrop == MAX_RENDER_CROPS" );
	}

	renderCrop_t& crop = renderCrops[ ++currentRenderCrop ];
	crop.x = 0;
	crop.y = 0;
	crop.width = croppedWidth;
	crop.height = croppedHeight;
}

void sdRenderSystemBackend::UnCrop() {
	if ( currentRenderCrop < 1 ) {
		common->Error( "sdRenderSystemBackend::UnCrop: currentRenderCrop < 1" );
	}
	--currentRenderCrop;
}

void sdRenderSystemBackend::BeginFrame( int windowWidth, int windowHeight ) {
	renderCrop_t& crop = renderCrops[ 0 ];
	crop.x = 0;
	crop.y = 0;
	crop.width = windowWidth;
	crop.height = windowHeight;
	currentRenderCrop = 0;
}

void sdRenderSystemBackend::CaptureRenderToImage( idImage*, int, copyBuffer_t, int ) {
	// The decompiled implementation emits pending GUI geometry and copies the
	// selected framebuffer. That becomes active when Image_load.cpp and
	// GuiModel.cpp are translated; retaining the backend boundary here keeps
	// crop state and call ownership identical in the meantime.
}
