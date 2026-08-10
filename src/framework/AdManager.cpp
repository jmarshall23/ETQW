// Copyright (C) 2007 Id Software, Inc.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "AdManager.h"

namespace {

class sdAdManagerLocal : public sdAdManager {
public:
	virtual void Init() {}
	virtual void Shutdown() {}
	virtual sdAdObjectSubscriberInterface* AllocAdSubscriber( const char*, sdAdObjectCallback* ) { return NULL; }
	virtual void SetAdZone( const char* ) {}
};

sdAdManagerLocal adManagerLocal;
}

sdAdManager* adManager = &adManagerLocal;
