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

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "../framework/DeclEntityDef.h"
#include "BSE.h"

void BSE_ResolveModelSafeParticle( rvBSEParticle &particle ) {
	if ( particle.type == PTYPE_LIGHT ) {
		// The material/radius/tint are retained, but the renderer bridge emits a
		// camera-facing emissive quad instead of allocating a render-world light.
		particle.type = PTYPE_SPRITE;
		if ( particle.materialName.IsEmpty() ) particle.materialName = "_default";
		return;
	}
	if ( particle.type == PTYPE_DECAL ) {
		// Projected decals require render-world mutation.  An oriented quad is the
		// closest deterministic representation available to a dynamic model.
		particle.type = PTYPE_ORIENTED;
		if ( particle.materialName.IsEmpty() ) particle.materialName = "_default";
		return;
	}
	if ( particle.type != PTYPE_DEBRIS && particle.type != PTYPE_MODEL ) return;

	if ( particle.modelName.IsEmpty() && !particle.entityDefName.IsEmpty() ) {
		const idDeclEntityDef *entityDef = static_cast<const idDeclEntityDef *>(
			declManager->FindType( DECL_ENTITYDEF, particle.entityDefName, false ) );
		if ( entityDef != NULL ) {
			particle.modelName = entityDef->dict.GetString( "model" );
		}
	}
	if ( !particle.modelName.IsEmpty() ) {
		particle.type = PTYPE_MODEL;
	} else {
		particle.type = PTYPE_SPRITE;
		if ( particle.materialName.IsEmpty() ) particle.materialName = "_default";
	}
}
