/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#ifndef __BSE_SPAWNDOMAINS_H__
#define __BSE_SPAWNDOMAINS_H__

typedef enum {
	BSE_DOMAIN_NONE = 0,
	BSE_DOMAIN_POINT,
	BSE_DOMAIN_LINE,
	BSE_DOMAIN_BOX,
	BSE_DOMAIN_SPHERE,
	BSE_DOMAIN_CYLINDER,
	BSE_DOMAIN_CONE,
	BSE_DOMAIN_SPIRAL,
	BSE_DOMAIN_MODEL
} bseDomainType_t;

// Model-surface sampling is supplied by Model_prt.cpp.  This keeps the BSE
// evaluator renderer-neutral and makes the model adapter its sole renderer
// dependency.
typedef bool ( *bseModelSample_t )( const char *modelName, idRandom &random, idVec3 &point, idVec3 &normal );

class rvBSEDomain {
public:
					rvBSEDomain();

	void			Clear();
	idVec4			Sample( int components, idRandom &random, float linearFraction = -1.0f,
						bseModelSample_t modelSampler = NULL, idVec3 *normal = NULL ) const;
	idBounds		GetBounds( int components ) const;
	int				Allocated() const { return values.Allocated() + modelName.Allocated(); }

	bseDomainType_t	type;
	idList<float>	values;
	idStr			modelName;
	bool			relative;
	bool			useEndOrigin;
	bool			surface;
	bool			cone;
	bool			linearSpacing;
	bool			attenuate;
	bool			inverseAttenuate;
};

#endif
