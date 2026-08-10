// Copyright (C) 2007 Id Software, Inc.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "GraphManager.h"

namespace {

class sdGraphManagerLocal : public sdGraphManager {
public:
	virtual void Init() {}
	virtual void Shutdown() {}
	virtual sdGraph* AllocGraph( const char* ) { return NULL; }
	virtual sdGraph* FindGraph( const char* ) { return NULL; }
	virtual void FreeGraph( const char* ) {}
	virtual void Draw() {}
};

sdGraphManagerLocal graphManagerLocal;

}

sdGraphManager* graphManager = &graphManagerLocal;
