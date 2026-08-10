// Copyright (C) 2007 Id Software, Inc.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "dynamicmodelcache.h"

idCVar sdDynamicModelCache::r_dynamicModelCacheMegs(
	"r_dynamicModelCacheMegs",
	"64",
	CVAR_RENDERER | CVAR_INTEGER,
	"Number of megabytes to cache dynamic model instantiations in.",
	1.0f,
	-1.0f
);
