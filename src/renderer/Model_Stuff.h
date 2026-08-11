// Copyright (C) 2007 Id Software, Inc.
//
// ETQW foliage/stuff model integration shared by the model loader and the
// reconstructed front end.

#ifndef __MODEL_STUFF_H__
#define __MODEL_STUFF_H__

class idRenderModel;
struct renderEntity_t;
struct viewDef_s;

// Load the generated .clustb representation belonging to a .clust model.
bool R_LoadStuffModel( idRenderModel* model, const char* fileName );

// Release any stuff data and view snapshot owned by model.
void R_FreeStuffModel( idRenderModel* model );

// Returns true when sourceModel is a stuff model.  In that case drawModel is
// replaced by its current visible snapshot, or NULL when no instances are
// visible (or stuff rendering is disabled).
bool R_GetStuffModelSnapshot( idRenderModel* sourceModel, const renderEntity_t* entity,
	const viewDef_s* view, idRenderModel*& drawModel );

#endif // __MODEL_STUFF_H__
