// Copyright (C) 2007 Id Software, Inc.
//
// Retail renderer/draw_shadowmap.obj contains the shadow-map raster controls;
// the actual map submission lives with the interaction path.

#include "../idlib/precompiled.h"
#pragma hdrstop

idCVar sm_backOffsetFactor( "sm_backOffsetFactor", "0", CVAR_RENDERER | CVAR_FLOAT, "Offet factor for shadow buffer rendering." );
idCVar sm_backOffsetUnits( "sm_backOffsetUnits", "0", CVAR_RENDERER | CVAR_FLOAT, "Offet units for shadow buffer rendering." );
idCVar sm_frontOffsetFactor( "sm_frontOffsetFactor", "3", CVAR_RENDERER | CVAR_FLOAT, "Offet factor for shadow buffer rendering." );
idCVar sm_frontOffsetUnits( "sm_frontOffsetUnits", "0", CVAR_RENDERER | CVAR_FLOAT, "Offet units for shadow buffer rendering." );
idCVar sm_frontFaces( "sm_frontFaces", "0", CVAR_RENDERER | CVAR_BOOL, "Render front faces as well as back faces." );
