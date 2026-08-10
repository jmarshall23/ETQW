// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW PDB and renderer Hex-Rays output.

#ifndef __RENDERER_SCREENRECT_H__
#define __RENDERER_SCREENRECT_H__

class idScreenRect {
public:
	idScreenRect() {
		Clear();
	}

	void	Clear();
	void	AddPoint( float x, float y );
	void	Expand();
	void	Intersect( const idScreenRect& rect );
	void	Union( const idScreenRect& rect );

	bool	Equals( const idScreenRect& rect ) const {
		return x1 == rect.x1 && y1 == rect.y1 &&
			x2 == rect.x2 && y2 == rect.y2 &&
			zmin == rect.zmin && zmax == rect.zmax;
	}

	bool	IsEmpty() const {
		return x1 > x2 || y1 > y2;
	}

	int		Area() const;

	short	x1;
	short	y1;
	short	x2;
	short	y2;
	float	zmin;
	float	zmax;
};

#endif
