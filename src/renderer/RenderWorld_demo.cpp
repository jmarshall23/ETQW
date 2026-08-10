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

void idRenderWorldLocal::StartWritingDemo( idDemoFile *demo ) {
	writeDemo = demo;
}

void idRenderWorldLocal::StopWritingDemo() {
	writeDemo = NULL;
}

bool idRenderWorldLocal::ProcessDemoCommand( idDemoFile*, renderView_t*, int* ) {
	return false;
}
