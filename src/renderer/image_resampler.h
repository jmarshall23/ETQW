// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the original ETQW PDB and retail executable.

#ifndef __RENDERER_IMAGE_RESAMPLER_H__
#define __RENDERER_IMAGE_RESAMPLER_H__

#include "../idlib/Factory.h"

class sdImageResampler {
public:
	class sdFilter {
	public:
		virtual const float	Filter( float t ) const = 0;
		const float			GetWidth() const { return width; }

	protected:
		float	width;
	};

	sdImageResampler() {}
	~sdImageResampler() {}

	void	Init();
	bool	Resample( const char* filterType, const unsigned char* in,
					unsigned int inWidth, unsigned int inHeight,
					unsigned char** out, unsigned int outWidth,
					unsigned int outHeight, unsigned int depth );

private:
	struct contribution_s {
		unsigned int	pixel;
		float			weight;
	};
	typedef contribution_s contribution_t;

	struct contributionList_s {
		unsigned int		num;
		contribution_t*	contributions;
	};
	typedef contributionList_s contributionList_t;

	unsigned char GetPixel( unsigned int x, unsigned int y, unsigned int z,
							const unsigned char* image, unsigned int width,
							unsigned int height, unsigned int depth ) const;
	void CalculateHorizontalContribution( contributionList_t& contributions,
									  int column, const sdFilter* filter,
									  float scaleX, unsigned int inWidth,
									  unsigned int outWidth );

	sdFactory< sdFilter > resampleFiltersFactory;
};

class sdBoxFilter : public sdImageResampler::sdFilter {
public:
	sdBoxFilter() { width = 0.5f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "box"; }
	static sdImageResampler::sdFilter* Create();
};

class sdFilterFilter : public sdImageResampler::sdFilter {
public:
	sdFilterFilter() { width = 1.0f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "filter"; }
	static sdImageResampler::sdFilter* Create();
};

class sdTriangleFilter : public sdImageResampler::sdFilter {
public:
	sdTriangleFilter() { width = 1.0f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "triangle"; }
	static sdImageResampler::sdFilter* Create();
};

class sdBellFilter : public sdImageResampler::sdFilter {
public:
	sdBellFilter() { width = 1.5f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "bell"; }
	static sdImageResampler::sdFilter* Create();
};

class sdBSplineFilter : public sdImageResampler::sdFilter {
public:
	sdBSplineFilter() { width = 2.0f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "bspline"; }
	static sdImageResampler::sdFilter* Create();
};

class sdLanczos3Filter : public sdImageResampler::sdFilter {
public:
	sdLanczos3Filter() { width = 3.0f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "lanczos3"; }
	static sdImageResampler::sdFilter* Create();

private:
	const float SinC( float t ) const;
};

class sdMitchellFilter : public sdImageResampler::sdFilter {
public:
	sdMitchellFilter() { width = 2.0f; }
	virtual const float Filter( float t ) const;
	static const char* GetType() { return "mitchell"; }
	static sdImageResampler::sdFilter* Create();
};

static_assert( sizeof( sdImageResampler::sdFilter ) == 8,
	"sdImageResampler::sdFilter must match the ETQW PDB layout" );
static_assert( sizeof( sdImageResampler ) == 56,
	"sdImageResampler must match the ETQW PDB layout" );

#endif /* !__RENDERER_IMAGE_RESAMPLER_H__ */
