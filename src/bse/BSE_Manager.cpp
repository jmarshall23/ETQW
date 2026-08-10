// Copyright (C) 2007 Id Software, Inc.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "BSEInterface.h"

namespace {

class rvBSEManagerLocal : public rvBSEManager {
public:
	virtual bool Init() { return true; }
	virtual bool Shutdown() { return true; }
	virtual bool PlayEffect( rvRenderEffectLocal*, float ) { return false; }
	virtual bool ServiceEffect( rvRenderEffectLocal*, float, bool& forcePush ) { forcePush = false; return false; }
	virtual void StopEffect( rvRenderEffectLocal* ) {}
	virtual void RestartEffect( rvRenderEffectLocal* ) {}
	virtual void FreeEffect( rvRenderEffectLocal* ) {}
	virtual float EffectDuration( const rvRenderEffectLocal* ) { return 0.0f; }
	virtual void BeginLevelLoad() {}
	virtual void EndLevelLoad() {}
	virtual void StartFrame() {}
	virtual void EndFrame() {}
	virtual bool Filtered( const char*, effectCategory_t ) { return false; }
	virtual void UpdateRateTimes() {}
	virtual bool CanPlayRateLimited( effectCategory_t ) { return true; }
	virtual int AddTraceModel( idTraceModel* ) { return -1; }
	virtual idTraceModel* GetTraceModel( int ) { return NULL; }
	virtual void FreeTraceModel( int ) {}
	virtual const idVec3& GetCubeNormals( int ) { return vec3_origin; }
	virtual void SetShakeParms( float, float ) {}
	virtual void SetTunnelParms( float, float ) {}
	virtual const idMat3& GetModelToBSE() { return mat3_identity; }
	virtual bool IsTimeLocked() const { return false; }
	virtual float GetLockedTime() const { return 0.0f; }
	virtual void MakeEditable( rvParticleTemplate* ) {}
	virtual void CopySegment( rvSegmentTemplate*, rvSegmentTemplate* ) {}
};

rvBSEManagerLocal bseManagerLocal;

}

rvBSEManager* bse = &bseManagerLocal;
