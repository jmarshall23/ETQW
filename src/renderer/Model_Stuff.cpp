// Copyright (C) 2007 Id Software, Inc.
//
// ETQW foliage/stuff renderer declarations.  This compiland is owned by
// renderer/Model_Stuff.obj in the retail PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

idCVar r_stuffFadeStart(
	"r_stuffFadeStart", "1500", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Distance at which stuff starts fading\n"
);

idCVar r_stuffFadeEnd(
	"r_stuffFadeEnd", "2500", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Max vis distance for the stuff models\n"
);
