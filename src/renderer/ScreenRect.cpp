// Copyright (C) 2007 Id Software, Inc.
//
// Clean translation of quakewars-hexrays/renderer/ScreenRect.cpp.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "ScreenRect.h"

#include <float.h>

void idScreenRect::Clear() {
	x1 = y1 = 32000;
	x2 = y2 = -32000;
	zmin = FLT_MAX;
	zmax = -FLT_MAX;
}

void idScreenRect::Expand() {
	--x1;
	--y1;
	++x2;
	++y2;
}

void idScreenRect::Intersect( const idScreenRect& rect ) {
	if ( rect.x1 > x1 ) {
		x1 = rect.x1;
	}
	if ( rect.y1 > y1 ) {
		y1 = rect.y1;
	}
	if ( rect.x2 < x2 ) {
		x2 = rect.x2;
	}
	if ( rect.y2 < y2 ) {
		y2 = rect.y2;
	}
	if ( rect.zmin > zmin ) {
		zmin = rect.zmin;
	}
	if ( rect.zmax < zmax ) {
		zmax = rect.zmax;
	}
}

void idScreenRect::Union( const idScreenRect& rect ) {
	if ( rect.x1 < x1 ) {
		x1 = rect.x1;
	}
	if ( rect.y1 < y1 ) {
		y1 = rect.y1;
	}
	if ( rect.x2 > x2 ) {
		x2 = rect.x2;
	}
	if ( rect.y2 > y2 ) {
		y2 = rect.y2;
	}
	if ( rect.zmin < zmin ) {
		zmin = rect.zmin;
	}
	if ( rect.zmax > zmax ) {
		zmax = rect.zmax;
	}
}

int idScreenRect::Area() const {
	if ( IsEmpty() ) {
		return 0;
	}
	return ( x2 - x1 + 1 ) * ( y2 - y1 + 1 );
}

void idScreenRect::AddPoint( float x, float y ) {
	const int ix = idMath::FtoiFast( x );
	const int iy = idMath::FtoiFast( y );
	if ( ix < x1 ) {
		x1 = static_cast< short >( ix );
	}
	if ( iy < y1 ) {
		y1 = static_cast< short >( iy );
	}
	if ( ix > x2 ) {
		x2 = static_cast< short >( ix );
	}
	if ( iy > y2 ) {
		y2 = static_cast< short >( iy );
	}
}
