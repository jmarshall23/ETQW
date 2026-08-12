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

#ifndef __COMPILER_PUBLIC_H__
#define __COMPILER_PUBLIC_H__

/*
===============================================================================

	Compilers for map, model, video etc. processing.

===============================================================================
*/

// map processing
void Dmap_f( const idCmdArgs &args );

// MegaTexture authoring and validation
void MegaTextureCreate_f( const idCmdArgs &args );
void MegaTextureCompile_f( const idCmdArgs &args );
void MegaTextureVerify_f( const idCmdArgs &args );

// bump map generation
void RenderBump_f( const idCmdArgs &args );
void RenderBumpFlat_f( const idCmdArgs &args );

// AAS file compiler
void RunAAS_f( const idCmdArgs &args );
void RunAASDir_f( const idCmdArgs &args );
void RunReach_f( const idCmdArgs &args );
void RunAAS2_f( const idCmdArgs &args );
void RunAAS2Dir_f( const idCmdArgs &args );

// video file encoding
void RoQFileEncode_f( const idCmdArgs &args );

#endif	/* !__COMPILER_PUBLIC_H__ */
