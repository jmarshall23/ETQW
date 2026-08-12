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

//start operation
void Undo_Start(char *operation);
//end operation
void Undo_End(void);
//add brush to the undo
void Undo_AddBrush(brush_t *pBrush);
//add a list with brushes to the undo
void Undo_AddBrushList(brush_t *brushlist);
//end a brush after the operation is performed
void Undo_EndBrush(brush_t *pBrush);
//end a list with brushes after the operation is performed
void Undo_EndBrushList(brush_t *brushlist);
//add entity to undo
void Undo_AddEntity(entity_t *entity);
//end an entity after the operation is performed
void Undo_EndEntity(entity_t *entity);
//undo last operation
void Undo_Undo(void);
//redo last undone operation
void Undo_Redo(void);
//returns true if there is something to be undone available
int  Undo_UndoAvailable(void);
//returns true if there is something to redo available
int  Undo_RedoAvailable(void);
//clear the undo buffer
void Undo_Clear(void);
//set maximum undo size (default 64)
void Undo_SetMaxSize(int size);
//get maximum undo size
int  Undo_GetMaxSize(void);
//set maximum undo memory in bytes (default 2 MB)
void Undo_SetMaxMemorySize(int size);
//get maximum undo memory in bytes
int  Undo_GetMaxMemorySize(void);
//returns the amount of memory used by undo
int  Undo_MemorySize(void);

