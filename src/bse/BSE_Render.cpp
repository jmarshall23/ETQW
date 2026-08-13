/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "../renderer/Model.h"
#include "../renderer/ModelManager.h"
#include "../renderer/Material.h"
#include "../decllib/declTypeHolder.h"
#include "BSE.h"

namespace {

struct bseSurfaceBuilder_t {
	idStr materialName;
	bseBlendType_t blend;
	idList<idDrawVert> verts;
	idList<glIndex_t> indexes;
};

static bseBlendType_t SurfaceBlendForParticle( const rvBSEParticle &particle ) {
	if ( particle.particleTemplate == NULL ) {
		return BSE_BLEND_DEFAULT;
	}
	return particle.particleTemplate->blend;
}

static bseSurfaceBuilder_t &SurfaceForMaterial( idList<bseSurfaceBuilder_t> &surfaces,
		const idStr &materialName, bseBlendType_t blend ) {
	for ( int i = 0; i < surfaces.Num(); i++ ) {
		if ( surfaces[i].blend == blend && !surfaces[i].materialName.Icmp( materialName ) ) return surfaces[i];
	}
	bseSurfaceBuilder_t &surface = surfaces.Alloc();
	surface.materialName = materialName;
	surface.blend = blend;
	return surface;
}

static byte ColorByte( float value ) {
	return static_cast<byte>( idMath::ClampInt( 0, 255, idMath::Ftoi( value * 255.0f ) ) );
}

static void SetVertex( idDrawVert &vert, const idVec3 &position, float s, float t,
		const idVec4 &color, const idVec3 &normal, const idVec3 &tangent0, const idVec3 &tangent1 ) {
	vert.Clear();
	vert.xyz = position;
	vert.SetST( s, t );
	vert.SetNormal( normal );
	vert.SetTangent( tangent0 );
	vert.SetBiTangent( tangent1 );
	vert.color[0] = ColorByte( color.x );
	vert.color[1] = ColorByte( color.y );
	vert.color[2] = ColorByte( color.z );
	vert.color[3] = ColorByte( color.w );
}

static void AppendQuad( bseSurfaceBuilder_t &surface, const idVec3 positions[4], const idVec4 &color,
		float s0, float s1, float t0 = 0.0f, float t1 = 1.0f ) {
	const int firstVert = surface.verts.Num();
	surface.verts.SetNum( firstVert + 4 );
	idVec3 tangent0 = positions[0] - positions[1];
	idVec3 tangent1 = positions[0] - positions[3];
	if ( tangent0.Normalize() == 0.0f ) tangent0.Set( 0.0f, 1.0f, 0.0f );
	if ( tangent1.Normalize() == 0.0f ) tangent1.Set( 0.0f, 0.0f, 1.0f );
	idVec3 normal;
	normal.Cross( tangent0, tangent1 );
	if ( normal.Normalize() == 0.0f ) normal.Set( 1.0f, 0.0f, 0.0f );
	SetVertex( surface.verts[firstVert + 0], positions[0], s0, t0, color, normal, tangent0, tangent1 );
	SetVertex( surface.verts[firstVert + 1], positions[1], s1, t0, color, normal, tangent0, tangent1 );
	SetVertex( surface.verts[firstVert + 2], positions[2], s1, t1, color, normal, tangent0, tangent1 );
	SetVertex( surface.verts[firstVert + 3], positions[3], s0, t1, color, normal, tangent0, tangent1 );
	const int firstIndex = surface.indexes.Num();
	surface.indexes.SetNum( firstIndex + 6 );
	surface.indexes[firstIndex + 0] = firstVert + 0;
	surface.indexes[firstIndex + 1] = firstVert + 1;
	surface.indexes[firstIndex + 2] = firstVert + 3;
	surface.indexes[firstIndex + 3] = firstVert + 1;
	surface.indexes[firstIndex + 4] = firstVert + 2;
	surface.indexes[firstIndex + 5] = firstVert + 3;
}

static void AppendSprite( bseSurfaceBuilder_t &surface, const rvBSEParticle &particle,
		const idVec3 &viewRight, const idVec3 &viewUp ) {
	const float angle = particle.angles.x * idMath::TWO_PI;
	const float sine = idMath::Sin( angle );
	const float cosine = idMath::Cos( angle );
	// Quake Wars stores sprite size as a half extent and emits position +/-
	// each evaluated axis. Do not halve the authored envelope a second time.
	const idVec3 right = ( viewRight * cosine + viewUp * sine ) * particle.size.x;
	const idVec3 up = ( viewUp * cosine - viewRight * sine ) * particle.size.y;
	idVec3 positions[4];
	positions[0] = particle.position + up + right;
	positions[1] = particle.position + up - right;
	positions[2] = particle.position - up - right;
	positions[3] = particle.position - up + right;
	AppendQuad( surface, positions, particle.color, particle.textureS0, particle.textureS1 );
}

static void AppendOriented( bseSurfaceBuilder_t &surface, const rvBSEParticle &particle ) {
	const idAngles angles( particle.angles.x * 360.0f, particle.angles.y * 360.0f, particle.angles.z * 360.0f );
	const idMat3 axis = angles.ToMat3();
	const idVec3 right = axis[1] * particle.size.x;
	const idVec3 up = axis[2] * particle.size.y;
	idVec3 positions[4];
	positions[0] = particle.position + up + right;
	positions[1] = particle.position + up - right;
	positions[2] = particle.position - up - right;
	positions[3] = particle.position - up + right;
	AppendQuad( surface, positions, particle.color, particle.textureS0, particle.textureS1 );
}

static void AppendLine( bseSurfaceBuilder_t &surface, const rvBSEParticle &particle,
		const rvBSEOwner &owner, const idVec3 &localViewOrigin ) {
	idVec3 end = particle.position + particle.length;
	if ( particle.useEndOrigin && owner.hasEndOrigin ) end = owner.endOrigin;
	idVec3 major = end - particle.position;
	if ( major.LengthSqr() < 0.0001f ) return;
	idVec3 minor;
	minor.Cross( major, ( particle.position + end ) * 0.5f - localViewOrigin );
	if ( minor.Normalize() == 0.0f ) {
		idVec3 unusedUp;
		major.OrthogonalBasis( minor, unusedUp );
	}
	minor *= Max( 0.01f, particle.size.x );
	idVec3 positions[4];
	positions[0] = particle.position + minor;
	positions[1] = particle.position - minor;
	positions[2] = end - minor;
	positions[3] = end + minor;
	AppendQuad( surface, positions, particle.color, particle.textureS0 * particle.tiling,
		particle.textureS1 * particle.tiling );
}

static void AppendLinkedStrip( bseSurfaceBuilder_t &surface,
		const idList<const rvBSEParticle *> &chain, const idVec3 &localViewOrigin, bool oriented ) {
	if ( chain.Num() < 2 ) return;
	int previousFirstVert = -1;
	for ( int i = 0; i < chain.Num(); i++ ) {
		const rvBSEParticle &particle = *chain[i];
		idVec3 direction;
		if ( i == 0 ) direction = chain[1]->position - particle.position;
		else if ( i == chain.Num() - 1 ) direction = particle.position - chain[i - 1]->position;
		else direction = chain[i + 1]->position - chain[i - 1]->position;
		if ( direction.Normalize() == 0.0f ) continue;

		idVec3 widthDirection;
		if ( oriented ) {
			const idAngles angles( particle.angles.x * 360.0f, particle.angles.y * 360.0f,
				particle.angles.z * 360.0f );
			widthDirection = angles.ToMat3()[1];
			widthDirection -= direction * ( widthDirection * direction );
		} else {
			widthDirection.Cross( direction, particle.position - localViewOrigin );
		}
		if ( widthDirection.Normalize() == 0.0f ) {
			idVec3 unusedUp;
			direction.OrthogonalBasis( widthDirection, unusedUp );
		}
		const idVec3 width = widthDirection * Max( 0.01f, particle.size.x );
		idVec3 normal;
		normal.Cross( direction, widthDirection );
		if ( normal.Normalize() == 0.0f ) normal.Set( 0.0f, 0.0f, 1.0f );

		const int firstVert = surface.verts.Num();
		surface.verts.SetNum( firstVert + 2 );
		const float fraction = chain.Num() > 1 ? static_cast<float>( i ) / ( chain.Num() - 1 ) : 0.0f;
		const float s = fraction * particle.tiling;
		SetVertex( surface.verts[firstVert + 0], particle.position - width, s, 0.0f,
			particle.color, normal, direction, widthDirection );
		SetVertex( surface.verts[firstVert + 1], particle.position + width, s, 1.0f,
			particle.color, normal, direction, widthDirection );

		if ( previousFirstVert >= 0 ) {
			const int firstIndex = surface.indexes.Num();
			surface.indexes.SetNum( firstIndex + 6 );
			surface.indexes[firstIndex + 0] = previousFirstVert + 0;
			surface.indexes[firstIndex + 1] = previousFirstVert + 1;
			surface.indexes[firstIndex + 2] = firstVert + 0;
			surface.indexes[firstIndex + 3] = previousFirstVert + 1;
			surface.indexes[firstIndex + 4] = firstVert + 1;
			surface.indexes[firstIndex + 5] = firstVert + 0;
		}
		previousFirstVert = firstVert;
	}
}

static void AppendModel( idList<bseSurfaceBuilder_t> &surfaces, const rvBSEParticle &particle ) {
	idRenderModel *model = renderModelManager->FindModel( particle.modelName );
	if ( model == NULL || model->IsDefaultModel() || model->IsDynamicModel() != DM_STATIC ) return;
	const idAngles angles( particle.angles.x * 360.0f, particle.angles.y * 360.0f, particle.angles.z * 360.0f );
	const idMat3 axis = angles.ToMat3();
	for ( int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); surfaceIndex++ ) {
		const modelSurface_t *sourceSurface = model->Surface( surfaceIndex );
		if ( sourceSurface == NULL || sourceSurface->geometry == NULL || sourceSurface->geometry->numVerts == 0 ) continue;
		const char *materialName = sourceSurface->material != NULL ? sourceSurface->material->GetName() : "_default";
		bseSurfaceBuilder_t &surface = SurfaceForMaterial( surfaces, materialName,
			SurfaceBlendForParticle( particle ) );
		const int firstVert = surface.verts.Num();
		const srfTriangles_t *source = sourceSurface->geometry;
		surface.verts.SetNum( firstVert + source->numVerts );
		for ( int vertIndex = 0; vertIndex < source->numVerts; vertIndex++ ) {
			idDrawVert &dest = surface.verts[firstVert + vertIndex];
			dest = source->verts[vertIndex];
			const idVec3 scaled( source->verts[vertIndex].xyz.x * Max( 0.001f, particle.size.x ),
				source->verts[vertIndex].xyz.y * Max( 0.001f, particle.size.y ),
				source->verts[vertIndex].xyz.z * Max( 0.001f, particle.size.z ) );
			dest.xyz = particle.position + scaled * axis;
			dest.SetNormal( source->verts[vertIndex].GetNormal() * axis );
			dest.SetTangent( source->verts[vertIndex].GetTangent() * axis );
			dest.SetBiTangent( source->verts[vertIndex].GetBiTangent() * axis );
			dest.color[0] = ColorByte( source->verts[vertIndex].color[0] * ( 1.0f / 255.0f ) * particle.color.x );
			dest.color[1] = ColorByte( source->verts[vertIndex].color[1] * ( 1.0f / 255.0f ) * particle.color.y );
			dest.color[2] = ColorByte( source->verts[vertIndex].color[2] * ( 1.0f / 255.0f ) * particle.color.z );
			dest.color[3] = ColorByte( source->verts[vertIndex].color[3] * ( 1.0f / 255.0f ) * particle.color.w );
		}
		const int firstIndex = surface.indexes.Num();
		surface.indexes.SetNum( firstIndex + source->numIndexes );
		for ( int index = 0; index < source->numIndexes; index++ ) surface.indexes[firstIndex + index] = firstVert + source->indexes[index];
	}
}

} // namespace

void BSE_CreateTrail( const rvBSEParticle &particle, idList<rvBSEParticle> &output ) {
	if ( particle.trailType == BSE_TRAIL_NONE || particle.trailCount <= 0 || particle.trailTime <= 0.0f ) return;
	if ( particle.velocity.LengthSqr() < 0.0001f ) return;
	const int count = idMath::ClampInt( 1, 64, particle.trailCount );
	const idVec3 trailDelta = -particle.velocity * ( particle.trailTime / count );
	for ( int i = 0; i < count; i++ ) {
		const float fraction0 = static_cast<float>( i ) / count;
		const float fraction1 = static_cast<float>( i + 1 ) / count;
		rvBSEParticle trail = particle;
		trail.type = PTYPE_LINE;
		trail.position = particle.position + trailDelta * i;
		trail.length = trailDelta;
		trail.size *= particle.trailScale * Max( 0.05f, 1.0f - fraction0 );
		trail.color.w *= 1.0f - fraction1;
		trail.materialName = particle.trailMaterialName.IsEmpty() ? particle.materialName : particle.trailMaterialName;
		trail.textureS0 = fraction0 * particle.tiling;
		trail.textureS1 = fraction1 * particle.tiling;
		trail.useEndOrigin = false;
		trail.trailType = BSE_TRAIL_NONE;
		trail.trailCount = 0;
		output.Append( trail );
	}
}

void BSE_SortParticles( const rvBSEOwner &owner, idList<rvBSEParticle> &particles ) {
	// ETQW's depth sort and inverse-order flags are segment-local.  Keep that
	// locality so sorting one translucent stage cannot reorder unrelated stages.
	for ( int first = 0; first < particles.Num(); ) {
		const int segment = particles[first].segmentIndex;
		const rvParticleTemplate *particleTemplate = particles[first].particleTemplate;
		int last = first + 1;
		while ( last < particles.Num() && particles[last].segmentIndex == segment &&
			particles[last].particleTemplate == particleTemplate ) last++;
		const bool depthSort = particles[first].depthSort;
		const bool inverse = particles[first].inverseDrawOrder;
		if ( depthSort || inverse ) {
			for ( int i = first + 1; i < last; i++ ) {
				rvBSEParticle key = particles[i];
				const float keyDistance = ( key.position - owner.viewOrigin ).LengthSqr();
				int j = i - 1;
				while ( j >= first ) {
					const float previousDistance = ( particles[j].position - owner.viewOrigin ).LengthSqr();
					bool move;
					if ( depthSort ) {
						move = previousDistance < keyDistance;
						if ( inverse ) move = !move;
					} else {
						move = inverse;
					}
					if ( !move ) break;
					particles[j + 1] = particles[j];
					j--;
				}
				particles[j + 1] = key;
			}
		}
		first = last;
	}
}

void BSE_BuildRenderModel( idRenderModel *snapshot, const char *snapshotName,
		const idList<rvBSEParticle> &particles, const rvBSEOwner &owner,
		const idVec3 &localViewOrigin, const idVec3 &viewRight, const idVec3 &viewUp ) {
	idList<bseSurfaceBuilder_t> builders;
	idList<byte> consumed;
	consumed.SetNum( particles.Num() );
	if ( consumed.Num() > 0 ) memset( consumed.Begin(), 0, consumed.Num() * sizeof( consumed[0] ) );
	for ( int i = 0; i < particles.Num(); i++ ) {
		if ( consumed[i] ) continue;
		const rvBSEParticle &particle = particles[i];
		if ( particle.color.w <= 0.0f || particle.size.x <= 0.0f ) continue;
		if ( particle.type == PTYPE_MODEL ) {
			AppendModel( builders, particle );
			continue;
		}
		const bseBlendType_t blend = SurfaceBlendForParticle( particle );
		bseSurfaceBuilder_t &surface = SurfaceForMaterial( builders,
			particle.materialName.IsEmpty() ? idStr( "_default" ) : particle.materialName, blend );
		if ( particle.type == PTYPE_LINKED || particle.type == PTYPE_ORIENTEDLINKED ) {
			idList<const rvBSEParticle *> chain;
			for ( int j = i; j < particles.Num(); j++ ) {
				const rvBSEParticle &candidate = particles[j];
				if ( candidate.type != particle.type || candidate.segmentIndex != particle.segmentIndex ||
					candidate.particleTemplate != particle.particleTemplate ||
					candidate.materialName.Icmp( particle.materialName ) ) continue;
				consumed[j] = 1;
				if ( candidate.color.w > 0.0f && candidate.size.x > 0.0f ) chain.Append( &candidate );
			}
			AppendLinkedStrip( surface, chain, localViewOrigin, particle.type == PTYPE_ORIENTEDLINKED );
			continue;
		}
		switch ( particle.type ) {
			case PTYPE_SPRITE:
				AppendSprite( surface, particle, viewRight, viewUp );
				break;
			case PTYPE_ORIENTED:
				AppendOriented( surface, particle );
				break;
			case PTYPE_LINE:
				AppendLine( surface, particle, owner, localViewOrigin );
				break;
		}
	}

	snapshot->InitEmpty( snapshotName );
	for ( int i = 0; i < builders.Num(); i++ ) {
		const bseSurfaceBuilder_t &builder = builders[i];
		if ( builder.verts.Num() == 0 || builder.indexes.Num() == 0 ) continue;
		srfTriangles_t *tri = snapshot->AllocSurfaceTriangles( builder.verts.Num(), builder.indexes.Num() );
		if ( tri == NULL ) continue;
		// BSE snapshots are rebuilt every frame.  Vulkan must stream these through
		// the persistent frame vertex cache; treating them as static geometry
		// creates two device allocations per material and retires them two frames
		// later, which turns weapon effects into large vkFreeMemory hitches.
		tri->streamVertexCache = true;
		memcpy( tri->verts, builder.verts.Begin(), builder.verts.Num() * sizeof( idDrawVert ) );
		memcpy( tri->indexes, builder.indexes.Begin(), builder.indexes.Num() * sizeof( glIndex_t ) );
		tri->numVerts = builder.verts.Num();
		tri->numIndexes = builder.indexes.Num();
		tri->tangentsCalculated = true;
		tri->facePlanesCalculated = false;
		tri->bounds.Clear();
		for ( int vertIndex = 0; vertIndex < tri->numVerts; vertIndex++ ) tri->bounds.AddPoint( tri->verts[vertIndex].xyz );
		modelSurface_t modelSurface;
		modelSurface.id = i;
		modelSurface.material = declHolder.FindMaterial( builder.materialName.IsEmpty() ? "_default" : builder.materialName.c_str(), true );
		modelSurface.geometry = tri;
		snapshot->AddSurface( modelSurface );
	}
}
