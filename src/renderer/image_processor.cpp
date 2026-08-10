// Copyright (C) 2007 Id Software, Inc.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "image_processor.h"

sdConvolve2D::sdConvolve2D( unsigned int width, unsigned int height,
		float* sourceKernel ) :
	sdConvolutionFilter( width, height ),
	kernelSum( 0.0f ),
	kernel( NULL ) {
	const unsigned int count = kernelWidth * kernelHeight;
	kernel = new float[ count ];
	memcpy( kernel, sourceKernel, count * sizeof( float ) );
	for ( unsigned int i = 0; i < count; ++i ) {
		kernelSum += sourceKernel[ i ];
	}
}

sdConvolve2D::~sdConvolve2D() {
	delete[] kernel;
}

void sdConvolve2D::Filter( sdImage& image ) const {
	for ( unsigned int y = 1; y < image.GetHeight() - 1; ++y ) {
		for ( unsigned int x = 1; x < image.GetWidth() - 1; ++x ) {
			for ( unsigned int channel = 0; channel < image.GetDepth(); ++channel ) {
				float sum = 0.0f;
				for ( unsigned int kernelY = 0; kernelY < kernelHeight; ++kernelY ) {
					for ( unsigned int kernelX = 0; kernelX < kernelWidth; ++kernelX ) {
						const unsigned int sourceX = x + kernelX - ( ( kernelWidth - 1 ) >> 1 );
						const unsigned int sourceY = y + kernelY - ( ( kernelHeight - 1 ) >> 1 );
						sum += kernel[ kernelX + kernelY * kernelWidth ] *
							image.GetPixel( sourceX, sourceY, channel );
					}
				}

				float value = sum / kernelSum;
				if ( value < 0.0f ) {
					value = 0.0f;
				} else if ( value > 255.0f ) {
					value = 255.0f;
				}
				image.PutPixel( x, y, channel, static_cast< unsigned char >( value ) );
			}
		}
	}
}

sdImageProcessor::sdFilter* sdGaussianFilter::Create() {
	float kernel[ 9 ] = {
		1.0f, 2.0f, 1.0f,
		2.0f, 5.0f, 2.0f,
		1.0f, 2.0f, 1.0f
	};
	return new sdConvolve2D( 3, 3, kernel );
}

sdImageProcessor::sdFilter* sdDiscreteLaplacianFilter::Create() {
	float kernel[ 9 ] = {
		0.0f, -1.0f, 0.0f,
		-1.0f, 5.0f, -1.0f,
		0.0f, -1.0f, 0.0f
	};
	return new sdConvolve2D( 3, 3, kernel );
}

sdImageProcessor::sdFilter* sdSharpenFilter::Create() {
	float kernel[ 9 ] = {
		-1.0f, -2.0f, -1.0f,
		-2.0f, 13.0f, -2.0f,
		-1.0f, -2.0f, -1.0f
	};
	return new sdConvolve2D( 3, 3, kernel );
}

unsigned char sdImageProcessor::sdFilter::sdImage::GetPixel(
		unsigned int x, unsigned int y, unsigned int z ) const {
	if ( x >= width || y >= height || z >= depth ) {
		common->Warning(
			"sdImageProcessor::sdFilter::sdImage::GetPixel : pixel requested out of image" );
		return 0;
	}
	return src[ depth * ( x + y * width ) + z ];
}

void sdImageProcessor::sdFilter::sdImage::PutPixel( unsigned int x,
		unsigned int y, unsigned int z, unsigned char value ) {
	if ( x >= width || y >= height || z >= depth ) {
		common->Warning(
			"sdImageProcessor::sdFilter::sdImage::PutPixel : pixel requested out of image" );
		return;
	}
	( *dst )[ depth * ( x + y * width ) + z ] = value;
}

void sdImageProcessor::Init() {
	processorFiltersFactory.RegisterType(
		sdGaussianFilter::GetType(), sdGaussianFilter::Create );
	processorFiltersFactory.RegisterType(
		sdDiscreteLaplacianFilter::GetType(), sdDiscreteLaplacianFilter::Create );
	processorFiltersFactory.RegisterType(
		sdSharpenFilter::GetType(), sdSharpenFilter::Create );
}

bool sdImageProcessor::Process( const char* filterType, const unsigned char* in,
		const unsigned int width, const unsigned int height,
		const unsigned int depth, unsigned char** out ) {
	sdFilter* filter = processorFiltersFactory.CreateType( filterType );
	if ( filter == NULL ) {
		return false;
	}
	if ( in == NULL || out == NULL || *out == NULL ) {
		delete filter;
		return false;
	}

	sdFilter::sdImage image( in, width, height, depth, out );
	filter->Filter( image );
	delete filter;
	return true;
}
