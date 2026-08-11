// Copyright (C) 2007 Id Software, Inc.
//
// ETQW BSE public boundary.  The original virtual function order is kept
// intact because gamex86.dll receives this object through gameImport_t.

#ifndef _BSE_INTERFACE_H_INC_
#define _BSE_INTERFACE_H_INC_

class idCVar;
class idTraceModel;
class rvBSE;
class rvDeclEffect;
class rvParticleTemplate;
class rvRenderEffectLocal;
class rvSegmentTemplate;
struct rvBSEOwner;
struct rvBSEParticle;

const char* const BSE_EFFECT_EXTENSION = "effect";

enum {
	VIEWEFFECT_SHAKE = 0,
	VIEWEFFECT_TUNNEL
};

typedef enum {
	EC_IGNORE = 0,
	EC_IMPACT,
	EC_IMPACT_PARTICLES,
	EC_MAX,
} effectCategory_t;

extern idCVar bse_enabled;
extern idCVar bse_render;
extern idCVar bse_debug;
extern idCVar bse_showBounds;
extern idCVar bse_physics;
extern idCVar bse_debris;
extern idCVar bse_singleEffect;
extern idCVar bse_maxParticles;
extern idCVar bse_detailLevel;
extern idCVar bse_simple;

struct rvBSEStats {
	int effectsServiced;
	int segmentsServiced;
	int particlesEvaluated;
	int particlesRendered;
	int particlesCapped;
};

class rvBSEManager {
public:
	virtual ~rvBSEManager() {}

	// Original ETQW ABI -- do not reorder these entries.
	virtual bool Init() = 0;
	virtual bool Shutdown() = 0;
	virtual bool PlayEffect( rvRenderEffectLocal* def, float time ) = 0;
	virtual bool ServiceEffect( rvRenderEffectLocal* def, float time, bool& forcePush ) = 0;
	virtual void StopEffect( rvRenderEffectLocal* def ) = 0;
	virtual void RestartEffect( rvRenderEffectLocal* def ) = 0;
	virtual void FreeEffect( rvRenderEffectLocal* def ) = 0;
	virtual float EffectDuration( const rvRenderEffectLocal* def ) = 0;
	virtual void BeginLevelLoad() = 0;
	virtual void EndLevelLoad() = 0;
	virtual void StartFrame() = 0;
	virtual void EndFrame() = 0;
	virtual bool Filtered( const char* name, effectCategory_t category ) = 0;
	virtual void UpdateRateTimes() = 0;
	virtual bool CanPlayRateLimited( effectCategory_t category ) = 0;
	virtual int AddTraceModel( idTraceModel* model ) = 0;
	virtual idTraceModel* GetTraceModel( int index ) = 0;
	virtual void FreeTraceModel( int index ) = 0;
	virtual const idVec3& GetCubeNormals( int index ) = 0;
	virtual void SetShakeParms( float time, float scale ) = 0;
	virtual void SetTunnelParms( float time, float scale ) = 0;
	virtual const idMat3& GetModelToBSE() = 0;
	virtual bool IsTimeLocked() const = 0;
	virtual float GetLockedTime() const = 0;
	virtual void MakeEditable( rvParticleTemplate* particle ) = 0;
	virtual void CopySegment( rvSegmentTemplate* dest, rvSegmentTemplate* src ) = 0;

	// Renderer-neutral ETQW evaluator appended after the retail ABI.
	virtual void BeginFrame() = 0;
	virtual void ServiceEffect( rvBSE& effect, const rvBSEOwner& owner,
		idList<rvBSEParticle>& particles ) = 0;
	virtual void PrepareRender( const rvBSEOwner& owner,
		const idList<rvBSEParticle>& particles,
		idList<rvBSEParticle>& renderParticles ) = 0;
	virtual const rvBSEStats& GetStats() const = 0;
};

extern rvBSEManager* bse;

#endif
