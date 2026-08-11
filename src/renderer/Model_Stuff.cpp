// Copyright (C) 2007 Id Software, Inc.
//
// ETQW foliage/stuff renderer.  The retail renderer loads generated .clustb
// files and creates a cached render-model snapshot around the current view.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Model.h"
#include "ModelManager.h"
#include "Model_Stuff.h"
#include "Material.h"
#include "draw_local.h"
#include "../decllib/declStuffType.h"
#include "../decllib/declTypeHolder.h"

idCVar r_useQuadTree(
	"r_useQuadTree", "1", CVAR_RENDERER | CVAR_BOOL,
	"Use the spatial hierarchy for stuff models\n"
);

idCVar r_stuffFadeStart(
	"r_stuffFadeStart", "1500", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Distance at which stuff starts fading\n"
);

idCVar r_stuffFadeEnd(
	"r_stuffFadeEnd", "2500", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT,
	"Max vis distance for the stuff models\n"
);

idCVar r_stuffLod(
	"r_stuffLod", "100000", CVAR_RENDERER | CVAR_FLOAT,
	"Where lod models stop drawing (they start at r_stuffFadeEnd)\n"
);

idCVar r_skipStuff(
	"r_skipStuff", "0", CVAR_RENDERER | CVAR_BOOL,
	"Don't draw stuff models\n"
);

idCVar r_sortStuff(
	"r_sortStuff", "1", CVAR_RENDERER | CVAR_BOOL,
	"Depth sort enable/disable\n"
);

idCVar r_showStuffCache(
	"r_showStuffCache", "0", CVAR_RENDERER | CVAR_BOOL,
	"Show allocation events of the stuff cache\n"
);

idCVar r_stuffCacheMegs(
	"r_stuffCacheMegs", "6", CVAR_RENDERER | CVAR_INTEGER,
	"Number of megabytes to cache stuff models\n"
);

idCVar r_stuffUpdateDistance(
	"r_stuffUpdateDistance", "100", CVAR_RENDERER | CVAR_FLOAT,
	"Camera needs to move more than X units for stuff models to update\n"
);

idCVar r_stuffUpdateAngle(
	"r_stuffUpdateAngle", "10", CVAR_RENDERER | CVAR_FLOAT,
	"Camera needs to rotate more than X degrees for stuff models to update\n"
);

namespace {

const int STUFF_FILE_VERSION = 2;
const int STUFF_INSTANCE_DISK_BYTES = sizeof( idVec3 ) * 3;
const int STUFF_MAX_TYPES = 1024;
const int STUFF_MAX_INSTANCES = 1 << 24;
const int STUFF_MAX_VERTS_PER_SURFACE = 65500;
const float STUFF_BATCH_SIZE = 1024.0f;

struct stuffInstance_t {
	idVec3 origin;
	byte rgb[ 3 ];
	byte angles[ 3 ];
	byte modelIndex;
	byte pad;
};

struct stuffSurface_t {
	stuffSurface_t() : distanceScale( 1.0f ) {
		bounds.Clear();
	}

	idList< idRenderModel* > models;
	idList< stuffInstance_t > instances;
	idBounds bounds;
	float distanceScale;
};

struct stuffSnapshot_t {
	stuffSnapshot_t() : entity( NULL ), model( NULL ), valid( false ), fadeEnd( -1.0f ) {
		viewOrigin.Zero();
		viewForward.Zero();
	}

	const renderEntity_t* entity;
	idRenderModel* model;
	idVec3 viewOrigin;
	idVec3 viewForward;
	bool valid;
	float fadeEnd;
};

struct stuffModelData_t {
	stuffModelData_t() : source( NULL ) {
		bounds.Clear();
	}

	idRenderModel* source;
	idList< stuffSurface_t* > surfaces;
	idList< stuffSnapshot_t* > snapshots;
	idBounds bounds;
};

struct stuffBuildGroup_t {
	stuffBuildGroup_t() : material( NULL ), distanceScale( 1.0f ), cellX( 0 ), cellY( 0 ) {
		verts.SetGranularity( 2048 );
		indexes.SetGranularity( 4096 );
		bounds.Clear();
	}

	const idMaterial* material;
	float distanceScale;
	int cellX;
	int cellY;
	idList< idDrawVert > verts;
	idList< glIndex_t > indexes;
	idBounds bounds;
};

idList< stuffModelData_t* > stuffModels;

stuffModelData_t* FindStuffModel( const idRenderModel* source ) {
	for ( int i = 0; i < stuffModels.Num(); ++i ) {
		if ( stuffModels[ i ]->source == source ) {
			return stuffModels[ i ];
		}
	}
	return NULL;
}

void FreeSnapshot( stuffSnapshot_t* snapshot ) {
	if ( snapshot == NULL ) {
		return;
	}
	if ( snapshot->model != NULL && renderModelManager != NULL ) {
		renderModelManager->FreeModel( snapshot->model );
		snapshot->model = NULL;
	}
	delete snapshot;
}

void FreeStuffData( stuffModelData_t* data ) {
	if ( data == NULL ) {
		return;
	}
	for ( int i = 0; i < data->snapshots.Num(); ++i ) {
		FreeSnapshot( data->snapshots[ i ] );
	}
	data->snapshots.Clear();
	data->surfaces.DeleteContents( true );
	delete data;
}

byte PackColor( float value ) {
	return static_cast< byte >( idMath::ClampInt( 0, 255, static_cast< int >( value * 255.0f ) ) );
}

byte PackAngle( float value ) {
	while ( value < 0.0f ) {
		value += 360.0f;
	}
	while ( value > 360.0f ) {
		value -= 360.0f;
	}
	return static_cast< byte >( idMath::ClampInt( 0, 255, static_cast< int >( value * ( 255.0f / 360.0f ) ) ) );
}

idMat3 InstanceAxis( const stuffInstance_t& instance ) {
	return idAngles(
		instance.angles[ 0 ] * ( 360.0f / 255.0f ),
		instance.angles[ 1 ] * ( 360.0f / 255.0f ),
		instance.angles[ 2 ] * ( 360.0f / 255.0f )
	).ToMat3();
}

bool ReadStuffSurface( idFile* file, stuffSurface_t* surface, const char* fileName ) {
	int numTypes = 0;
	if ( file->ReadInt( numTypes ) != sizeof( numTypes ) || numTypes <= 0 || numTypes > STUFF_MAX_TYPES ) {
		common->Warning( "Stuff model '%s': invalid stuff type count", fileName );
		return false;
	}

	const sdDeclStuffType* type = NULL;
	for ( int i = 0; i < numTypes; ++i ) {
		idStr typeName;
		if ( file->ReadString( typeName ) < 0 || typeName.IsEmpty() ) {
			common->Warning( "Stuff model '%s': invalid stuff type name", fileName );
			return false;
		}
		// SetupStuffType in the retail implementation replaces the previous
		// model list, so the last declaration in this compatibility list wins.
		type = declHolder.FindStuffType( typeName, true );
	}

	if ( type == NULL || type->GetNumModels() <= 0 ) {
		common->Warning( "Stuff model '%s': stuff type has no models", fileName );
		return false;
	}

	for ( int i = 0; i < type->GetNumModels(); ++i ) {
		idRenderModel* model = renderModelManager->FindModel( type->GetModelName( i ) );
		if ( model == NULL ) {
			common->Warning( "Stuff model '%s': couldn't load '%s'", fileName, type->GetModelName( i ) );
			return false;
		}
		if ( model->IsDefaultModel() ) {
			common->Warning( "Stuff model '%s': '%s' is using the default model", fileName, type->GetModelName( i ) );
		}
		surface->models.Append( model );
	}

	int numInstances = 0;
	if ( file->ReadFloat( surface->distanceScale ) != sizeof( surface->distanceScale ) ||
		file->ReadInt( numInstances ) != sizeof( numInstances ) ||
		numInstances < 0 || numInstances > STUFF_MAX_INSTANCES ) {
		common->Warning( "Stuff model '%s': invalid instance count", fileName );
		return false;
	}
	if ( surface->distanceScale <= 0.0f ) {
		surface->distanceScale = 1.0f;
	}

	const long long bytesRemaining = static_cast< long long >( file->Length() ) - file->Tell();
	const long long bytesRequired = static_cast< long long >( numInstances ) * STUFF_INSTANCE_DISK_BYTES;
	if ( bytesRemaining < bytesRequired ) {
		common->Warning( "Stuff model '%s': truncated instance list", fileName );
		return false;
	}

	surface->instances.SetNum( numInstances );
	int randomSeed = numInstances;
	for ( int i = 0; i < numInstances; ++i ) {
		idVec3 angles;
		idVec3 color;
		stuffInstance_t& instance = surface->instances[ i ];
		if ( file->ReadVec3( instance.origin ) != sizeof( idVec3 ) ||
			file->ReadVec3( angles ) != sizeof( idVec3 ) ||
			file->ReadVec3( color ) != sizeof( idVec3 ) ) {
			common->Warning( "Stuff model '%s': truncated instance", fileName );
			return false;
		}

		randomSeed = 69069 * randomSeed + 1;
		instance.modelIndex = static_cast< byte >( ( randomSeed & 0x7fff ) % surface->models.Num() );
		instance.rgb[ 0 ] = PackColor( color.x );
		instance.rgb[ 1 ] = PackColor( color.y );
		instance.rgb[ 2 ] = PackColor( color.z );
		instance.angles[ 0 ] = PackAngle( angles.x );
		instance.angles[ 1 ] = PackAngle( angles.y );
		instance.angles[ 2 ] = PackAngle( angles.z );
		instance.pad = 0;

		const idBounds modelBounds = surface->models[ instance.modelIndex ]->Bounds();
		idBounds instanceBounds;
		instanceBounds.FromTransformedBounds( modelBounds, instance.origin, InstanceAxis( instance ) );
		surface->bounds += instanceBounds;
	}

	return true;
}

stuffSnapshot_t* FindSnapshot( stuffModelData_t* data, const renderEntity_t* entity ) {
	for ( int i = 0; i < data->snapshots.Num(); ++i ) {
		if ( data->snapshots[ i ]->entity == entity ) {
			return data->snapshots[ i ];
		}
	}
	stuffSnapshot_t* snapshot = new stuffSnapshot_t;
	snapshot->entity = entity;
	data->snapshots.Append( snapshot );
	return snapshot;
}

bool SnapshotNeedsUpdate( const stuffSnapshot_t* snapshot, const viewDef_s* view ) {
	if ( snapshot == NULL || !snapshot->valid || snapshot->model == NULL ) {
		return true;
	}
	if ( snapshot->fadeEnd != r_stuffFadeEnd.GetFloat() ) {
		return true;
	}
	if ( view->isSubview ) {
		return false;
	}

	const float updateDistance = Max( 0.0f, r_stuffUpdateDistance.GetFloat() );
	if ( ( snapshot->viewOrigin - view->renderView.vieworg ).LengthSqr() > updateDistance * updateDistance ) {
		return true;
	}

	const float updateAngle = idMath::ClampFloat( 0.0f, 180.0f, r_stuffUpdateAngle.GetFloat() );
	const float minDot = idMath::Cos( updateAngle * idMath::M_DEG2RAD );
	return idMath::Fabs( snapshot->viewForward * view->renderView.viewaxis[ 0 ] ) < minDot;
}

stuffBuildGroup_t* FindBuildGroup( idList< stuffBuildGroup_t* >& groups, const idMaterial* material,
	float distanceScale, int cellX, int cellY, int additionalVerts ) {
	for ( int i = groups.Num() - 1; i >= 0; --i ) {
		stuffBuildGroup_t* group = groups[ i ];
		if ( group->material == material && group->distanceScale == distanceScale &&
			group->cellX == cellX && group->cellY == cellY &&
			group->verts.Num() + additionalVerts <= STUFF_MAX_VERTS_PER_SURFACE ) {
			return group;
		}
	}

	stuffBuildGroup_t* group = new stuffBuildGroup_t;
	group->material = material;
	group->distanceScale = distanceScale;
	group->cellX = cellX;
	group->cellY = cellY;
	groups.Append( group );
	return group;
}

void AppendInstanceSurface( stuffBuildGroup_t* group, const srfTriangles_t* source,
	const stuffInstance_t& instance, const idMat3& axis ) {
	const int baseVertex = group->verts.Num();
	const bool clusterTransform = group->material != NULL && group->material->TestMaterialFlag( MF_CLUSTERTRANSFORM );
	for ( int i = 0; i < source->numVerts; ++i ) {
		idDrawVert vertex = source->verts[ i ];
		vertex.xyz = source->verts[ i ].xyz * axis + instance.origin;
		if ( clusterTransform ) {
			vertex.SetNormal( idVec3( 0.0f, 0.0f, 1.0f ) * axis );
		} else {
			vertex.SetNormal( source->verts[ i ].GetNormal() * axis );
		}
		vertex.SetTangent( source->verts[ i ].GetTangent() * axis );
		vertex.color[ 0 ] = instance.rgb[ 0 ];
		vertex.color[ 1 ] = instance.rgb[ 1 ];
		vertex.color[ 2 ] = instance.rgb[ 2 ];
		vertex.color[ 3 ] = source->verts[ i ].color[ 3 ];
		group->bounds += vertex.xyz;
		group->verts.Append( vertex );
	}

	for ( int i = 0; i < source->numIndexes; ++i ) {
		const int sourceIndex = source->indexes[ i ];
		if ( sourceIndex >= 0 && sourceIndex < source->numVerts ) {
			group->indexes.Append( static_cast< glIndex_t >( baseVertex + sourceIndex ) );
		}
	}
}

idRenderModel* BuildSnapshot( stuffModelData_t* data, stuffSnapshot_t* snapshot,
	const renderEntity_t* entity, const viewDef_s* view ) {
	if ( snapshot->model != NULL ) {
		renderModelManager->FreeModel( snapshot->model );
		snapshot->model = NULL;
	}

	snapshot->model = renderModelManager->AllocModel();
	snapshot->model->InitEmpty( va( "<stuff-snapshot:%s>", data->source->Name() ) );
	snapshot->viewOrigin = view->renderView.vieworg;
	snapshot->viewForward = view->renderView.viewaxis[ 0 ];
	snapshot->fadeEnd = r_stuffFadeEnd.GetFloat();
	snapshot->valid = true;

	const idVec3 localViewOrigin = ( view->renderView.vieworg - entity->origin ) * entity->axis.Transpose();
	const float fadeEnd = Max( 0.0f, snapshot->fadeEnd );
	const float fadeEndSqr = fadeEnd * fadeEnd;
	idList< stuffBuildGroup_t* > groups;

	for ( int surfaceIndex = 0; surfaceIndex < data->surfaces.Num(); ++surfaceIndex ) {
		const stuffSurface_t* stuffSurface = data->surfaces[ surfaceIndex ];
		for ( int instanceIndex = 0; instanceIndex < stuffSurface->instances.Num(); ++instanceIndex ) {
			const stuffInstance_t& instance = stuffSurface->instances[ instanceIndex ];
			const float distanceSqr = ( instance.origin - localViewOrigin ).LengthSqr() * stuffSurface->distanceScale;
			if ( distanceSqr > fadeEndSqr ) {
				continue;
			}

			if ( instance.modelIndex >= stuffSurface->models.Num() ) {
				continue;
			}
			idRenderModel* instanceModel = stuffSurface->models[ instance.modelIndex ];
			const idMat3 instanceAxis = InstanceAxis( instance );
			const int cellX = static_cast< int >( idMath::Floor( instance.origin.x / STUFF_BATCH_SIZE ) );
			const int cellY = static_cast< int >( idMath::Floor( instance.origin.y / STUFF_BATCH_SIZE ) );
			for ( int modelSurfaceIndex = 0; modelSurfaceIndex < instanceModel->NumSurfaces(); ++modelSurfaceIndex ) {
				const modelSurface_t* modelSurface = instanceModel->Surface( modelSurfaceIndex );
				if ( modelSurface == NULL || modelSurface->material == NULL || modelSurface->geometry == NULL ||
					modelSurface->geometry->verts == NULL || modelSurface->geometry->indexes == NULL ||
					modelSurface->geometry->numVerts <= 0 || modelSurface->geometry->numIndexes <= 0 ||
					modelSurface->geometry->numVerts > STUFF_MAX_VERTS_PER_SURFACE ) {
					continue;
				}

				stuffBuildGroup_t* group = FindBuildGroup( groups, modelSurface->material,
					stuffSurface->distanceScale, cellX, cellY, modelSurface->geometry->numVerts );
				AppendInstanceSurface( group, modelSurface->geometry, instance, instanceAxis );
			}
		}
	}

	idBounds snapshotBounds;
	snapshotBounds.Clear();
	int outputSurface = 0;
	for ( int i = 0; i < groups.Num(); ++i ) {
		stuffBuildGroup_t* group = groups[ i ];
		if ( group->verts.Num() == 0 || group->indexes.Num() == 0 ) {
			continue;
		}
		// Cull the 1024-unit material batches, not every rotated blade/model.
		// The per-instance transformed-box test was rejecting every nearby Valley
		// instance at several valid camera orientations.  Batching first also matches
		// the purpose of the retail stuff quadtree and leaves a final surface-bounds
		// cull in R_AddAmbientDrawsurfs.
		if ( r_useQuadTree.GetBool() &&
			R_CullLocalBoxToViewdef( group->bounds, entity->axis, entity->origin, view ) ) {
			continue;
		}

		srfTriangles_t* triangles = snapshot->model->AllocSurfaceTriangles( group->verts.Num(), group->indexes.Num() );
		if ( triangles == NULL ) {
			continue;
		}
		memcpy( triangles->verts, group->verts.Begin(), group->verts.Num() * sizeof( idDrawVert ) );
		memcpy( triangles->indexes, group->indexes.Begin(), group->indexes.Num() * sizeof( glIndex_t ) );
		triangles->bounds = group->bounds;
		triangles->tangentsCalculated = true;
		triangles->generateNormals = false;
		triangles->params[ TRIPARM_DISTANCESCALE ] = group->distanceScale;

		modelSurface_t modelSurface;
		modelSurface.id = outputSurface++;
		modelSurface.material = group->material;
		modelSurface.geometry = triangles;
		snapshot->model->AddSurface( modelSurface );
		snapshotBounds += group->bounds;
	}

	groups.DeleteContents( true );
	if ( outputSurface == 0 ) {
		return NULL;
	}

	snapshot->model->SetBounds( snapshotBounds );
	snapshot->model->FinishSurfaces( false, false, false );
	if ( r_showStuffCache.GetBool() ) {
		common->Printf( "stuff: %s built %d visible surfaces\n", data->source->Name(), outputSurface );
	}
	return snapshot->model;
}

} // namespace

bool R_LoadStuffModel( idRenderModel* model, const char* fileName ) {
	if ( model == NULL || fileName == NULL || fileName[ 0 ] == '\0' || fileSystem == NULL || renderModelManager == NULL ) {
		return false;
	}

	R_FreeStuffModel( model );
	idStr binaryName = fileName;
	binaryName.SetFileExtension( "clustb" );
	idFile* file = fileSystem->OpenFileRead( binaryName, true, NULL, true );
	if ( file == NULL ) {
		return false;
	}

	idStr version;
	bool valid = file->ReadString( version ) >= 0 && !version.Icmp( va( "Version %d", STUFF_FILE_VERSION ) );
	if ( !valid ) {
		common->Warning( "Stuff model '%s': bad id '%s' instead of 'Version %d'",
			binaryName.c_str(), version.c_str(), STUFF_FILE_VERSION );
	}

	stuffModelData_t* data = new stuffModelData_t;
	data->source = model;
	while ( valid && file->Tell() < file->Length() ) {
		stuffSurface_t* surface = new stuffSurface_t;
		if ( !ReadStuffSurface( file, surface, binaryName ) ) {
			delete surface;
			valid = false;
			break;
		}
		data->surfaces.Append( surface );
		data->bounds += surface->bounds;
	}
	fileSystem->CloseFile( file );

	if ( !valid || data->surfaces.Num() == 0 || data->bounds.IsCleared() ) {
		FreeStuffData( data );
		return false;
	}

	model->SetBounds( data->bounds );
	stuffModels.Append( data );
	common->Printf( "Loaded '%s' (%d stuff surfaces)\n", binaryName.c_str(), data->surfaces.Num() );
	return true;
}

void R_FreeStuffModel( idRenderModel* model ) {
	for ( int i = 0; i < stuffModels.Num(); ++i ) {
		if ( stuffModels[ i ]->source == model ) {
			stuffModelData_t* data = stuffModels[ i ];
			stuffModels.RemoveIndex( i );
			FreeStuffData( data );
			return;
		}
	}
}

bool R_GetStuffModelSnapshot( idRenderModel* sourceModel, const renderEntity_t* entity,
	const viewDef_s* view, idRenderModel*& drawModel ) {
	stuffModelData_t* data = FindStuffModel( sourceModel );
	if ( data == NULL ) {
		return false;
	}

	drawModel = NULL;
	if ( entity == NULL || view == NULL || r_skipStuff.GetBool() ) {
		return true;
	}

	stuffSnapshot_t* snapshot = FindSnapshot( data, entity );
	if ( SnapshotNeedsUpdate( snapshot, view ) ) {
		drawModel = BuildSnapshot( data, snapshot, entity, view );
	} else if ( snapshot->model != NULL && snapshot->model->NumSurfaces() > 0 ) {
		drawModel = snapshot->model;
	}
	return true;
}
