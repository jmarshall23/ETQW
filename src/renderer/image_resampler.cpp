// Copyright (C) 2007 Id Software, Inc.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "image_resampler.h"

#include <math.h>

namespace {

ID_INLINE unsigned int ReflectPixel( int pixel, unsigned int size ) {
	if ( pixel < 0 ) {
		return static_cast< unsigned int >( -pixel );
	}
	if ( pixel >= static_cast< int >( size ) ) {
		return 2U * size - static_cast< unsigned int >( pixel ) - 1U;
	}
	return static_cast< unsigned int >( pixel );
}

ID_INLINE unsigned char ClampByte( float value ) {
	if ( value <= 0.0f ) {
		return 0;
	}
	if ( value >= 255.0f ) {
		return 255;
	}
	return static_cast< unsigned char >( value );
}

}

const float sdFilterFilter::Filter( float t ) const {
	t = static_cast< float >( fabs( t ) );
	return t < 1.0f ? t * ( ( 2.0f * t - 3.0f ) * t ) + 1.0f : 0.0f;
}

sdImageResampler::sdFilter* sdFilterFilter::Create() {
	return new sdFilterFilter;
}

const float sdTriangleFilter::Filter( float t ) const {
	t = static_cast< float >( fabs( t ) );
	return t < 1.0f ? 1.0f - t : 0.0f;
}

sdImageResampler::sdFilter* sdTriangleFilter::Create() {
	return new sdTriangleFilter;
}

const float sdBellFilter::Filter( float t ) const {
	t = static_cast< float >( fabs( t ) );
	if ( t < 0.5f ) {
		return 0.75f - t * t;
	}
	if ( t < 1.5f ) {
		t -= 1.5f;
		return 0.5f * t * t;
	}
	return 0.0f;
}

sdImageResampler::sdFilter* sdBellFilter::Create() {
	return new sdBellFilter;
}

const float sdBSplineFilter::Filter( float t ) const {
	t = static_cast< float >( fabs( t ) );
	if ( t < 1.0f ) {
		const float t2 = t * t;
		return 0.5f * t * t2 - t2 + 2.0f / 3.0f;
	}
	if ( t < 2.0f ) {
		t = 2.0f - t;
		return t * t * t / 6.0f;
	}
	return 0.0f;
}

sdImageResampler::sdFilter* sdBSplineFilter::Create() {
	return new sdBSplineFilter;
}

const float sdLanczos3Filter::SinC( float t ) const {
	const float angle = idMath::PI * t;
	return angle == 0.0f ? 1.0f : static_cast< float >( sin( angle ) ) / angle;
}

const float sdLanczos3Filter::Filter( float t ) const {
	t = static_cast< float >( fabs( t ) );
	return t < 3.0f ? SinC( t ) * SinC( t / 3.0f ) : 0.0f;
}

sdImageResampler::sdFilter* sdLanczos3Filter::Create() {
	return new sdLanczos3Filter;
}

const float sdMitchellFilter::Filter( float t ) const {
	t = static_cast< float >( fabs( t ) );
	const float t2 = t * t;
	if ( t < 1.0f ) {
		return ( 7.0f * t * t2 - 12.0f * t2 + 16.0f / 3.0f ) / 6.0f;
	}
	if ( t < 2.0f ) {
		return ( -7.0f / 3.0f * t * t2 + 12.0f * t2 - 20.0f * t + 32.0f / 3.0f ) / 6.0f;
	}
	return 0.0f;
}

sdImageResampler::sdFilter* sdMitchellFilter::Create() {
	return new sdMitchellFilter;
}

const float sdBoxFilter::Filter( float t ) const {
	return t > -0.5f && t <= 0.5f ? 1.0f : 0.0f;
}

sdImageResampler::sdFilter* sdBoxFilter::Create() {
	return new sdBoxFilter;
}

void sdImageResampler::Init() {
	resampleFiltersFactory.RegisterType( sdBoxFilter::GetType(), sdBoxFilter::Create );
	resampleFiltersFactory.RegisterType( sdFilterFilter::GetType(), sdFilterFilter::Create );
	resampleFiltersFactory.RegisterType( sdTriangleFilter::GetType(), sdTriangleFilter::Create );
	resampleFiltersFactory.RegisterType( sdBellFilter::GetType(), sdBellFilter::Create );
	resampleFiltersFactory.RegisterType( sdBSplineFilter::GetType(), sdBSplineFilter::Create );
	resampleFiltersFactory.RegisterType( sdLanczos3Filter::GetType(), sdLanczos3Filter::Create );
	resampleFiltersFactory.RegisterType( sdMitchellFilter::GetType(), sdMitchellFilter::Create );
}

unsigned char sdImageResampler::GetPixel( unsigned int x, unsigned int y,
		unsigned int z, const unsigned char* image, unsigned int width,
		unsigned int height, unsigned int depth ) const {
	if ( x >= width || y >= height || z >= depth ) {
		common->Warning( "sdImageResampler::GetPixel : pixel requested out of image" );
		return 0;
	}
	return image[ depth * ( x + y * width ) + z ];
}

void sdImageResampler::CalculateHorizontalContribution(
		contributionList_t& list, int column, const sdFilter* filter,
		float scaleX, unsigned int inWidth, unsigned int outWidth ) {
	list.num = 0;
	list.contributions = NULL;
	if ( filter == NULL || scaleX <= 0.0f || inWidth == 0 || outWidth == 0 ) {
		return;
	}

	const float center = static_cast< float >( column ) / scaleX;
	const float scaleFactor = scaleX < 1.0f ? 1.0f / scaleX : 1.0f;
	const float filterWidth = filter->GetWidth() * scaleFactor;
	const int left = static_cast< int >( ceil( center - filterWidth ) );
	const int right = static_cast< int >( floor( center + filterWidth ) );
	const unsigned int capacity = right >= left
		? static_cast< unsigned int >( right - left + 1 ) : 0;
	if ( capacity == 0 ) {
		return;
	}

	list.contributions = static_cast< contribution_t* >(
		Mem_Alloc( capacity * sizeof( contribution_t ) ) );
	for ( int source = left; source <= right; ++source ) {
		contribution_t& contribution = list.contributions[ list.num++ ];
		contribution.pixel = ReflectPixel( source, inWidth );
		contribution.weight = filter->Filter( ( center - source ) / scaleFactor ) / scaleFactor;
	}
}

bool sdImageResampler::Resample( const char* filterType,
		const unsigned char* in, unsigned int inWidth, unsigned int inHeight,
		unsigned char** out, unsigned int outWidth, unsigned int outHeight,
		unsigned int depth ) {
	sdFilter* filter = resampleFiltersFactory.CreateType( filterType );
	if ( filter == NULL ) {
		return false;
	}
	if ( in == NULL || out == NULL || *out == NULL || inWidth == 0 ||
		inHeight == 0 || outWidth == 0 || outHeight == 0 || depth == 0 ) {
		delete filter;
		return false;
	}

	unsigned char* temporary = static_cast< unsigned char* >(
		Mem_Alloc( depth * inHeight ) );
	contributionList_t* contributionsY = static_cast< contributionList_t* >(
		Mem_Alloc( outHeight * sizeof( contributionList_t ) ) );

	const float scaleY = static_cast< float >( outHeight ) / inHeight;
	for ( unsigned int y = 0; y < outHeight; ++y ) {
		contributionList_t& list = contributionsY[ y ];
		list.num = 0;
		list.contributions = NULL;

		const float center = static_cast< float >( y ) / scaleY;
		const float scaleFactor = scaleY < 1.0f ? 1.0f / scaleY : 1.0f;
		const float filterWidth = filter->GetWidth() * scaleFactor;
		const int top = static_cast< int >( ceil( center - filterWidth ) );
		const int bottom = static_cast< int >( floor( center + filterWidth ) );
		const unsigned int capacity = bottom >= top
			? static_cast< unsigned int >( bottom - top + 1 ) : 0;

		if ( capacity != 0 ) {
			list.contributions = static_cast< contribution_t* >(
				Mem_Alloc( capacity * sizeof( contribution_t ) ) );
			for ( int source = top; source <= bottom; ++source ) {
				contribution_t& contribution = list.contributions[ list.num++ ];
				contribution.pixel = ReflectPixel( source, inHeight );
				contribution.weight = filter->Filter( ( center - source ) / scaleFactor ) / scaleFactor;
			}
		}
	}

	for ( unsigned int x = 0; x < outWidth; ++x ) {
		contributionList_t contributionsX;
		const float scaleX = static_cast< float >( outWidth ) / inWidth;
		CalculateHorizontalContribution( contributionsX, static_cast< int >( x ),
			filter, scaleX, inWidth, outWidth );

		for ( unsigned int y = 0; y < inHeight; ++y ) {
			for ( unsigned int channel = 0; channel < depth; ++channel ) {
				const unsigned char first = GetPixel(
					contributionsX.contributions[ 0 ].pixel, y, channel,
					in, inWidth, inHeight, depth );
				bool pixelDelta = false;
				float value = 0.0f;
				for ( unsigned int i = 0; i < contributionsX.num; ++i ) {
					const contribution_t& contribution = contributionsX.contributions[ i ];
					const unsigned char sample = GetPixel( contribution.pixel, y,
						channel, in, inWidth, inHeight, depth );
					pixelDelta |= sample != first;
					value += contribution.weight * sample;
				}
				temporary[ depth * y + channel ] = pixelDelta
					? ClampByte( static_cast< float >( floor( value + 0.5f ) ) )
					: first;
			}
		}
		Mem_Free( contributionsX.contributions );

		for ( unsigned int y = 0; y < outHeight; ++y ) {
			const contributionList_t& list = contributionsY[ y ];
			for ( unsigned int channel = 0; channel < depth; ++channel ) {
				const unsigned char first = temporary[ depth * list.contributions[ 0 ].pixel + channel ];
				bool pixelDelta = false;
				float value = 0.0f;
				for ( unsigned int i = 0; i < list.num; ++i ) {
					const contribution_t& contribution = list.contributions[ i ];
					const unsigned char sample = temporary[ depth * contribution.pixel + channel ];
					pixelDelta |= sample != first;
					value += contribution.weight * sample;
				}
				( *out )[ depth * ( x + y * outWidth ) + channel ] = pixelDelta
					? ClampByte( static_cast< float >( floor( value + 0.5f ) ) )
					: first;
			}
		}
	}

	Mem_Free( temporary );
	for ( unsigned int y = 0; y < outHeight; ++y ) {
		Mem_Free( contributionsY[ y ].contributions );
	}
	Mem_Free( contributionsY );
	delete filter;
	return true;
}
