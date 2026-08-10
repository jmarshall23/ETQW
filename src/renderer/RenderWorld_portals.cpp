// Copyright (C) 2007 Id Software, Inc.
//
// ETQW render-world implementation reconstructed under the retail PDB path.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderWorld_local.h"
#include "Image.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "RenderSystemBackend.h"
#include "GuiModel.h"
#include "DeviceContext.h"
#include "tr_render.h"
#include "../decllib/declTypeHolder.h"
#include "../sys/sys_render.h"

#include <GL/gl.h>

int idRenderWorldLocal::NumPortals() const {
	return 0;
}

qhandle_t idRenderWorldLocal::FindPortal( const idBounds& ) const {
	return 0;
}

void idRenderWorldLocal::SetPortalState( qhandle_t, int ) {
}

int idRenderWorldLocal::GetPortalState( qhandle_t ) {
	return PS_BLOCK_NONE;
}

void idRenderWorldLocal::UpdatePortalOccTestView( int ) {
}

bool idRenderWorldLocal::AreasAreConnected( int areaNum1, int areaNum2, portalConnection_t ) {
	return areaNum1 == 0 && areaNum2 == 0;
}

bool idRenderWorldLocal::AreasAreConnected( int areaNum1, int areaNum2, portalFlags_t ) {
	return areaNum1 == 0 && areaNum2 == 0;
}

bool idRenderWorldLocal::AreasAreConnected( int areaNum1, int areaNum2 ) {
	return areaNum1 == 0 && areaNum2 == 0;
}
