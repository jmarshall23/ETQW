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

#ifndef __BSE_PARTICLE_H__
#define __BSE_PARTICLE_H__

#include "BSE_SpawnDomains.h"
#include "BSE_Envelope.h"

typedef enum {
	PTYPE_NONE = 0,
	PTYPE_SPRITE,
	PTYPE_LINE,
	PTYPE_ORIENTED,
	PTYPE_DECAL,
	PTYPE_MODEL,
	PTYPE_LIGHT,
	PTYPE_ELECTRICITY,
	PTYPE_LINKED,
	PTYPE_ORIENTEDLINKED,
	PTYPE_DEBRIS,
	PTYPE_COUNT
} eBSEParticle;

typedef enum {
	BSE_PARM_POSITION = 0,
	BSE_PARM_DIRECTION,
	BSE_PARM_VELOCITY,
	BSE_PARM_ACCELERATION,
	BSE_PARM_FRICTION,
	BSE_PARM_TINT,
	BSE_PARM_FADE,
	BSE_PARM_SIZE,
	BSE_PARM_ROTATE,
	BSE_PARM_ANGLE,
	BSE_PARM_OFFSET,
	BSE_PARM_LENGTH,
	BSE_PARM_WINDSTRENGTH,
	BSE_PARM_COUNT
} bseParmType_t;

typedef enum {
	BSE_TRAIL_NONE = 0,
	BSE_TRAIL_MOTION,
	BSE_TRAIL_BURN
} bseTrailType_t;

typedef enum {
	BSE_BLEND_DEFAULT = 0,
	BSE_BLEND_ADD,
	BSE_BLEND_ALPHA,
	BSE_BLEND_PREMULTIPLIED
} bseBlendType_t;

class rvBSEParm {
public:
					rvBSEParm();
	void			Clear();
	idVec4			Evaluate( float fraction, int components, const idVec4 &defaultValue,
						idRandom &random, float linearFraction = -1.0f,
						bseModelSample_t modelSampler = NULL, idVec3 *normal = NULL ) const;
	int				Allocated() const;

	rvBSEDomain		start;
	rvBSEDomain		end;
	rvBSEEnvelope	envelope;
	bool			hasStart;
	bool			hasEnd;
};

struct rvBSEAction {
	rvBSEAction();
	void Clear();
	int Allocated() const;

	idList<idStr>	effects;
	float			bounce;
	float			physicsDistance;
	bool			remove;
};

class rvParticleTemplate {
public:
					rvParticleTemplate();
	void			Init();
	int				Allocated() const;

	int			type;
	idStr			materialName;
	idStr			modelName;
	idStr			entityDefName;
	idVec2			duration;
	idVec2			gravity;
	float			fadeIn;
	float			tiling;
	float			parentVelocity;
	float			windDeviationAngle;
	bseTrailType_t	trailType;
	idStr			trailMaterialName;
	idVec2			trailTime;
	idVec2			trailCount;
	float			trailScale;
	int				trailRepeat;
	int				numFrames;
	int				numForks;
	idVec3			forkMins;
	idVec3			forkMaxs;
	float			jitterRate;
	idVec3			jitterSize;
	idStr			jitterTableName;
	const idDeclTable *jitterTable;
	bseBlendType_t	blend;
	bool			persist;
	bool			generatedLine;
	bool			generatedNormal;
	bool			generatedOriginNormal;
	bool			lineHit;
	bool			flipNormal;
	bool			useLightningAxis;
	bool			shadows;
	bool			specular;
	rvBSEAction		impact;
	rvBSEAction		timeout;
	rvBSEParm		parms[BSE_PARM_COUNT];
};

// Evaluated visual particle consumed by the final Model_prt tessellation
// bridge.  It contains no renderer-owned types.
struct rvBSEParticle {
	int				type;
	int				segmentIndex;
	const rvParticleTemplate *particleTemplate;
	idStr			materialName;
	idStr			modelName;
	idStr			entityDefName;
	idVec3			position;
	idVec3			velocity;
	idVec3			length;
	idVec3			angles;
	idVec3			size;
	idVec4			color;
	idVec3			jitterSize;
	float			jitterRate;
	const idDeclTable *jitterTable;
	idVec3			forkMins;
	idVec3			forkMaxs;
	int				numForks;
	int				seed;
	float			age;
	float			life;
	float			textureS0;
	float			textureS1;
	float			tiling;
	float			trailTime;
	float			trailScale;
	int				trailCount;
	bseTrailType_t	trailType;
	idStr			trailMaterialName;
	bool			useEndOrigin;
	bool			depthSort;
	bool			inverseDrawOrder;
};

struct rvParticleSpawnInfo {
	const rvParticleTemplate *particleTemplate;
	const class rvSegmentTemplate *segmentTemplate;
	const struct rvBSEOwner *owner;
	int				segmentIndex;
	int				particleIndex;
	int				seed;
	float			birthTime;
	float			life;
	float			linearFraction;
};

class rvParticle {
public:
	static bool		Evaluate( const rvParticleSpawnInfo &spawn, rvBSEParticle &result );
};

#endif

