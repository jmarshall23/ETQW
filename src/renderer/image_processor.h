// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the original ETQW PDB and retail executable.

#ifndef __RENDERER_IMAGE_PROCESSOR_H__
#define __RENDERER_IMAGE_PROCESSOR_H__

#include "../idlib/Factory.h"

class sdImageProcessor {
public:
	class sdFilter {
	public:
		class sdImage {
		public:
			sdImage( const unsigned char* source, unsigned int imageWidth,
					 unsigned int imageHeight, unsigned int imageDepth,
					 unsigned char** destination ) {
				Init( source, imageWidth, imageHeight, imageDepth, destination );
			}

			void Init( const unsigned char* source, unsigned int imageWidth,
					   unsigned int imageHeight, unsigned int imageDepth,
					   unsigned char** destination ) {
				src = source;
				dst = destination;
				width = imageWidth;
				height = imageHeight;
				depth = imageDepth;
			}

			unsigned char GetPixel( unsigned int x, unsigned int y,
								unsigned int z ) const;
			void PutPixel( unsigned int x, unsigned int y, unsigned int z,
						   unsigned char value );

			const unsigned int GetWidth() const { return width; }
			const unsigned int GetHeight() const { return height; }
			const unsigned int GetDepth() const { return depth; }

		private:
			const unsigned char*	src;
			unsigned char**		dst;
			unsigned int		width;
			unsigned int		height;
			unsigned int		depth;
		};

		virtual void Filter( sdImage& image ) const = 0;
	};

	sdImageProcessor() {}
	~sdImageProcessor() {}

	void Init();
	bool Process( const char* filterType, const unsigned char* in,
				  const unsigned int width, const unsigned int height,
				  const unsigned int depth, unsigned char** out );

private:
	sdFactory< sdFilter > processorFiltersFactory;
};

class sdConvolutionFilter : public sdImageProcessor::sdFilter {
public:
	virtual void Filter( sdImage& image ) const = 0;

protected:
	sdConvolutionFilter( unsigned int width, unsigned int height ) :
		kernelWidth( width ), kernelHeight( height ) {}

	unsigned int	kernelWidth;
	unsigned int	kernelHeight;
};

class sdConvolve2D : public sdConvolutionFilter {
public:
	sdConvolve2D( unsigned int width, unsigned int height, float* kernel );
	virtual ~sdConvolve2D();
	virtual void Filter( sdImage& image ) const;

protected:
	float	kernelSum;
	float*	kernel;
};

class sdGaussianFilter {
public:
	static const char* GetType() { return "gaussian"; }
	static sdImageProcessor::sdFilter* Create();
};

class sdDiscreteLaplacianFilter {
public:
	static const char* GetType() { return "discreteLaplacian"; }
	static sdImageProcessor::sdFilter* Create();
};

class sdSharpenFilter {
public:
	static const char* GetType() { return "sharpen"; }
	static sdImageProcessor::sdFilter* Create();
};

static_assert( sizeof( sdImageProcessor::sdFilter::sdImage ) == 20,
	"sdImage must match the ETQW PDB layout" );
static_assert( sizeof( sdImageProcessor::sdFilter ) == 4,
	"sdImageProcessor::sdFilter must match the ETQW PDB layout" );
static_assert( sizeof( sdConvolutionFilter ) == 12,
	"sdConvolutionFilter must match the ETQW PDB layout" );
static_assert( sizeof( sdConvolve2D ) == 20,
	"sdConvolve2D must match the ETQW PDB layout" );
static_assert( sizeof( sdImageProcessor ) == 56,
	"sdImageProcessor must match the ETQW PDB layout" );

#endif /* !__RENDERER_IMAGE_PROCESSOR_H__ */
