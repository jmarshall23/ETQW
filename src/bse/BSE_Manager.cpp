// Copyright (C) 2007 Id Software, Inc.
//
// ETQW BSE manager and renderer-neutral particle preparation.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "BSE.h"

static rvBSEManagerLocal bseManagerLocal;
rvBSEManager *bse = &bseManagerLocal;

bool rvBSEManagerLocal::Init() {
	BeginFrame();
	return true;
}

bool rvBSEManagerLocal::Shutdown() {
	BeginFrame();
	return true;
}

// These entries retain the retail game-DLL ABI.  Render-world effect defs are
// serviced by the renderer-neutral overload below, so no rvRenderEffectLocal
// object crosses the reconstructed renderer boundary.
bool rvBSEManagerLocal::PlayEffect( rvRenderEffectLocal *, float ) { return false; }
bool rvBSEManagerLocal::ServiceEffect( rvRenderEffectLocal *, float, bool &forcePush ) {
	forcePush = false;
	return false;
}
void rvBSEManagerLocal::StopEffect( rvRenderEffectLocal * ) {}
void rvBSEManagerLocal::RestartEffect( rvRenderEffectLocal * ) {}
void rvBSEManagerLocal::FreeEffect( rvRenderEffectLocal * ) {}
float rvBSEManagerLocal::EffectDuration( const rvRenderEffectLocal * ) { return 0.0f; }
void rvBSEManagerLocal::BeginLevelLoad() {}
void rvBSEManagerLocal::EndLevelLoad() {}
void rvBSEManagerLocal::StartFrame() { BeginFrame(); }
void rvBSEManagerLocal::EndFrame() {}
bool rvBSEManagerLocal::Filtered( const char *, effectCategory_t ) { return false; }
void rvBSEManagerLocal::UpdateRateTimes() {}
bool rvBSEManagerLocal::CanPlayRateLimited( effectCategory_t ) { return true; }
int rvBSEManagerLocal::AddTraceModel( idTraceModel * ) { return -1; }
idTraceModel *rvBSEManagerLocal::GetTraceModel( int ) { return NULL; }
void rvBSEManagerLocal::FreeTraceModel( int ) {}
const idVec3 &rvBSEManagerLocal::GetCubeNormals( int ) { return vec3_origin; }
void rvBSEManagerLocal::SetShakeParms( float, float ) {}
void rvBSEManagerLocal::SetTunnelParms( float, float ) {}
const idMat3 &rvBSEManagerLocal::GetModelToBSE() { return mat3_identity; }
bool rvBSEManagerLocal::IsTimeLocked() const { return false; }
float rvBSEManagerLocal::GetLockedTime() const { return 0.0f; }
void rvBSEManagerLocal::MakeEditable( rvParticleTemplate * ) {}
void rvBSEManagerLocal::CopySegment( rvSegmentTemplate *, rvSegmentTemplate * ) {}

void rvBSEManagerLocal::BeginFrame() {
	memset( &stats, 0, sizeof( stats ) );
}

void rvBSEManagerLocal::ServiceEffect( rvBSE &effect, const rvBSEOwner &owner,
		idList<rvBSEParticle> &particles ) {
	particles.Clear();
	effect.ServiceInternal( owner, particles, 0, &stats );
}

void rvBSEManagerLocal::PrepareRender( const rvBSEOwner &owner,
		const idList<rvBSEParticle> &particles, idList<rvBSEParticle> &renderParticles ) {
	renderParticles.Clear();
	for ( int i = 0; i < particles.Num(); i++ ) {
		const rvBSEParticle &particle = particles[i];
		if ( particle.type == PTYPE_ELECTRICITY ) {
			BSE_CreateElectricity( owner, particle, renderParticles );
		} else {
			renderParticles.Append( particle );
		}
		BSE_CreateTrail( particle, renderParticles );
	}
	BSE_SortParticles( owner, renderParticles );
	stats.particlesRendered += renderParticles.Num();
}
