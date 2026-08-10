// Copyright (C) 2007 Id Software, Inc.
//
// ETQW engine precompiled header reconstructed from the Microsoft PDB's
// framework/precompiled.cpp compiland and the public SDK header graph.

#ifndef __FRAMEWORK_PRECOMPILED_H__
#define __FRAMEWORK_PRECOMPILED_H__

#include "../idlib/precompiled.h"

#ifndef ID_TIME_T
#define ID_TIME_T unsigned int
#endif

#include "BuildVersion.h"
#include "Licensee.h"
#include "CmdSystem.h"
#include "CVarSystem.h"
#include "DeclManager.h"
#include "UsercmdGen.h"
#include "KeyInput.h"
#include "EditField.h"
#include "Console.h"
#include "EventLoop.h"
#include "Session.h"
#include "Compressor.h"
#include "DemoFile.h"

#include "../renderer/Material.h"
#include "../renderer/Cinematic.h"
#include "../renderer/RenderSystem.h"
#include "../renderer/RenderWorld.h"
#include "../renderer/Model.h"
#include "../renderer/ModelManager.h"

struct MemInfo_t;
class idListGUI;

#include "../sound/SoundShader.h"
#include "../sound/SoundEmitter.h"
#include "../sound/SoundWorld.h"
#include "../sound/SoundSystem.h"
#include "async/NetworkSystem.h"
#include "async/AsyncNetwork.h"

#include "../openal/include/al.h"
#include "../openal/include/alc.h"
#include "../openal/idal.h"

#include "../cm/CollisionModel.h"
#include "../decllib/declTypeHolder.h"
#include "../game/Game.h"

#endif /* !__FRAMEWORK_PRECOMPILED_H__ */
