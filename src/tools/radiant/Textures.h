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

// a texturename of the form (0 0 0) will
// create a solid color texture

void		Texture_Init (bool bHardInit = true);
void		Texture_FlushUnused ();
void		Texture_Flush (bool bReload = false);
void		Texture_ClearInuse (void);
void		Texture_ShowInuse (void);
void		Texture_ShowDirectory (int menunum, bool bLinked = false);
void		Texture_ShowAll();
void		Texture_HideAll();
void		Texture_Cleanup(CStringList *pList = NULL);

// TTimo: added bNoAlpha flag to ignore alpha channel when parsing a .TGA file, transparency is usually achieved through qer_trans keyword in shaders
// in some cases loading an empty alpha channel causes display bugs (brushes not seen)
//qtexture_t *Texture_ForName (const char *name, bool bReplace = false, bool bShader = false, bool bNoAlpha = false, bool bReload = false, bool makeShader = true);

const idMaterial *Texture_ForName(const char *name);

void		Texture_Init (void);
void		Texture_SetTexture (texdef_t *texdef, brushprimit_texdef_t *brushprimit_texdef, bool bFitScale = false, bool bSetSelection = true);

void		Texture_SetMode(int iMenu);	// GL_TEXTURE_NEAREST, etc..
void		Texture_ResetPosition();

void		FreeShaders();
void		LoadShaders();
void		ReloadShaders();
int			WINAPI Texture_LoadSkin(char *pName, int *pnWidth, int *pnHeight);
void		Texture_StartPos (void);
qtexture_t *Texture_NextPos (int *x, int *y);
