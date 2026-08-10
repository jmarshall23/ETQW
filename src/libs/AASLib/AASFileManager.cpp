// Copyright (C) 2007 Id Software, Inc.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "AASFileManager.h"

namespace {

class idAASFileManagerLocal : public idAASFileManager {
public:
	virtual idAASFile* LoadAAS( const char*, unsigned int ) { return NULL; }
	virtual void FreeAAS( idAASFile* ) {}
};

idAASFileManagerLocal aasFileManagerLocal;

}

idAASFileManager* AASFileManager = &aasFileManagerLocal;
