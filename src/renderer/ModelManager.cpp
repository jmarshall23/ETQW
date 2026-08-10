// Copyright (C) 2007 Id Software, Inc.
//
// ETQW render-model manager reconstructed in its original PDB source unit.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Model.h"
#include "ModelManager.h"

idRenderModel* R_AllocStaticModel();

class idRenderModelManagerLocal : public idRenderModelManager {
public:
	idRenderModelManagerLocal() : defaultModel( NULL ), insideLevelLoad( false ) {}

	virtual void Init() {
		if ( defaultModel != NULL ) {
			return;
		}
		idRenderModel* model = R_AllocStaticModel();
		model->InitEmpty( "_DEFAULT" );
		defaultModel = model;
		models.Append( model );
	}

	virtual void Shutdown() {
		models.DeleteContents( true );
		defaultModel = NULL;
		insideLevelLoad = false;
	}

	virtual void BeginLevelLoad() {
		insideLevelLoad = true;
		for ( int i = 0; i < models.Num(); i++ ) {
			models[ i ]->SetLevelLoadReferenced( false );
		}
	}

	virtual void EndLevelLoad() { insideLevelLoad = false; }

	virtual idRenderModel* AllocModel() {
		idRenderModel* model = R_AllocStaticModel();
		model->InitEmpty( "_allocated" );
		return model;
	}

	virtual void FreeModel( idRenderModel* model ) {
		if ( model == NULL || model == defaultModel ) {
			return;
		}
		RemoveModel( model );
		delete model;
	}

	virtual idRenderModel* FindModel( const char* name ) { return FindOrCreateModel( name, true ); }

	virtual idRenderModel* CheckModel( const char* name ) {
		idRenderModel* model = FindOrCreateModel( name, false );
		return model != NULL && !model->IsDefaultModel() ? model : NULL;
	}

	virtual idRenderModel* GetModel( const char* name ) { return FindExistingModel( name ); }
	virtual idRenderModel* DefaultModel() { return defaultModel; }

	virtual void AddModel( idRenderModel* model ) {
		if ( model != NULL && models.FindIndex( model ) < 0 ) {
			models.Append( model );
		}
	}

	virtual void RemoveModel( idRenderModel* model ) {
		const int index = models.FindIndex( model );
		if ( index >= 0 ) {
			models.RemoveIndex( index );
		}
	}

	virtual void ReloadModels( bool forceAll ) {
		for ( int i = 0; i < models.Num(); i++ ) {
			if ( forceAll || models[ i ]->IsReloadable() ) {
				models[ i ]->LoadModel();
			}
		}
	}

	virtual void WritePrecacheCommands( idFile* file ) {
		if ( file == NULL ) {
			return;
		}
		for ( int i = 0; i < models.Num(); i++ ) {
			if ( models[ i ]->IsReloadable() ) {
				file->Printf( "touchModel %s\n", models[ i ]->Name() );
			}
		}
	}

	virtual void FreeModelVertexCaches() {
		for ( int i = 0; i < models.Num(); i++ ) {
			models[ i ]->FreeVertexCache();
		}
	}
	virtual bool WriteSurfaceModel( const char*, idList< idSurface* >&, idStrList& ) { return false; }
	virtual bool WriteTriangleModelB( const char*, idRenderModel* ) { return false; }
	virtual bool WriteTriangleModel( const char*, idRenderModel* ) { return false; }

private:
	idRenderModel* FindExistingModel( const char* name ) const {
		if ( name == NULL || name[ 0 ] == '\0' ) {
			return NULL;
		}
		for ( int i = 0; i < models.Num(); i++ ) {
			if ( !idStr::Icmp( models[ i ]->Name(), name ) ) {
				return models[ i ];
			}
		}
		return NULL;
	}

	idRenderModel* FindOrCreateModel( const char* name, bool keepDefault ) {
		idRenderModel* existing = FindExistingModel( name );
		if ( existing != NULL ) {
			existing->SetLevelLoadReferenced( true );
			if ( !insideLevelLoad ) {
				existing->SetReferencedOutsideLevelLoad( true );
			}
			return existing;
		}
		if ( name == NULL || name[ 0 ] == '\0' ) {
			return NULL;
		}

		idRenderModel* model = R_AllocStaticModel();
		model->InitFromFile( name );
		model->SetLevelLoadReferenced( true );
		model->SetReferencedOutsideLevelLoad( !insideLevelLoad );
		if ( model->IsDefaultModel() && !keepDefault ) {
			delete model;
			return NULL;
		}
		models.Append( model );
		return model;
	}

	idList< idRenderModel* > models;
	idRenderModel* defaultModel;
	bool insideLevelLoad;
};

idRenderModelManagerLocal localModelManager;
idRenderModelManager* renderModelManager = &localModelManager;

