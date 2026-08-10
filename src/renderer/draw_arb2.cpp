// Copyright (C) 2007 Id Software, Inc.
//
// ETQW ARB2 back-end initialization.  This compiland and the function
// ownership come from renderer/draw_arb2.obj in the retail Microsoft PDB.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderSystem.h"
#include "Image.h"
#include "renderbindings.h"
#include "../decllib/declRenderBinding.h"
#include "../decllib/declRenderProgram.h"
#include "../decllib/declTypeHolder.h"
#include "../libs/qglLib/qcg.h"

extern glconfig_t glConfig;
extern idCVar r_megaDrawMethod;
extern idCVar r_skipSpecular;
extern idCVar r_skipBump;
extern idCVar r_skipDiffuse;
extern idCVar r_shaderQuality;

namespace {
	void Evaluator_SkipBump( const sdDeclRenderBinding* target ) {
		if ( target != NULL && globalImages != NULL ) {
			target->Set( globalImages->flatNormalMap );
		}
	}

	void Evaluator_SkipSpecular( const sdDeclRenderBinding* target ) {
		if ( target != NULL && globalImages != NULL ) {
			target->Set( globalImages->blackImage );
		}
	}

	void Evaluator_SkipDiffuse( const sdDeclRenderBinding* target ) {
		if ( target == NULL || globalImages == NULL ) {
			return;
		}
		if ( r_skipDiffuse.GetInteger() == 1 ) {
			target->Set( globalImages->blackImage );
		} else if ( r_skipDiffuse.GetInteger() == 2 ) {
			target->Set( globalImages->whiteImage );
		}
	}

	class idCVarCallback_SkipSpecular : public idCVarCallback {
	public:
		virtual void OnChanged() {
			if ( rbinds == NULL || rbinds->specularMap == NULL ) return;
			if ( r_skipSpecular.GetBool() ) rbinds->specularMap->SetEvaluator( Evaluator_SkipSpecular );
			else rbinds->specularMap->ClearEvaluator();
		}
	};

	class idCVarCallback_SkipBump : public idCVarCallback {
	public:
		virtual void OnChanged() {
			if ( rbinds == NULL || rbinds->bumpMap == NULL ) return;
			if ( r_skipBump.GetBool() ) rbinds->bumpMap->SetEvaluator( Evaluator_SkipBump );
			else rbinds->bumpMap->ClearEvaluator();
		}
	};

	class idCVarCallback_SkipDiffuse : public idCVarCallback {
	public:
		virtual void OnChanged() {
			if ( rbinds == NULL || rbinds->diffuseMap == NULL ) return;
			if ( r_skipDiffuse.GetInteger() != 0 ) rbinds->diffuseMap->SetEvaluator( Evaluator_SkipDiffuse );
			else rbinds->diffuseMap->ClearEvaluator();
		}
	};

	void ReparseRenderDecls() {
		renderSystem->LockThreads();
		if ( globalImages != NULL ) globalImages->SetInsideLevelLoad( true );

		for ( int index = 0; index < declHolder.declRenderProgramType.Num(); ++index ) {
			idDecl* decl = const_cast< idDecl* >( static_cast< const idDecl* >( declHolder.FindRenderProgramByIndex( index, false ) ) );
			if ( decl != NULL && decl->IsValid() ) decl->ReParse();
		}
		for ( int index = 0; index < declHolder.declMaterialType.Num(); ++index ) {
			idMaterial* material = const_cast< idMaterial* >( declHolder.FindMaterialByIndex( index, false ) );
			idMegaTexture* savedMegaTexture = NULL;
			if ( material != NULL && material->TestMaterialFlag( MF_HASMEGA ) && material->GetNumStages() > 0 ) {
				savedMegaTexture = material->GetStage( 0 )->megaTexture;
			}
			if ( material != NULL && material->IsValid() ) material->ReParse();
			if ( savedMegaTexture != NULL && material->GetNumStages() > 0 ) {
				const_cast< materialStage_t* >( material->GetStage( 0 ) )->megaTexture = savedMegaTexture;
			}
		}

		if ( globalImages != NULL ) globalImages->SetInsideLevelLoad( false );
	}

	class idCVarCallback_ShaderQuality : public idCVarCallback {
	public:
		virtual void OnChanged() { ReparseRenderDecls(); }
	};

	class idCVarCallback_MegaDrawMethod : public idCVarCallback {
	public:
		virtual void OnChanged() { ReparseRenderDecls(); }
	};

	idCVarCallback_SkipSpecular g_skipSpecularCallback;
	idCVarCallback_SkipBump g_skipBumpCallback;
	idCVarCallback_SkipDiffuse g_skipDiffuseCallback;
	idCVarCallback_ShaderQuality g_shaderQualityCallback;
	idCVarCallback_MegaDrawMethod g_megaDrawMethodCallback;
}

void R_ARB2_Init() {
	glConfig.backendInitialized = false;
	common->Printf( "---------- R_ARB2_Init ----------\n" );

	glConfig.allowCgPath = cgInit();
	if ( glConfig.allowCgPath ) {
		common->Printf( "Cg available.\n" );
	} else {
		common->Warning( "Cg not available.\n" );
	}

	r_skipSpecular.RegisterCallback( &g_skipSpecularCallback );
	r_skipBump.RegisterCallback( &g_skipBumpCallback );
	r_skipDiffuse.RegisterCallback( &g_skipDiffuseCallback );
	r_shaderQuality.RegisterCallback( &g_shaderQualityCallback );
	r_megaDrawMethod.RegisterCallback( &g_megaDrawMethodCallback );

	common->Printf( "---------------------------------\n" );
	glConfig.backendInitialized = true;
}

void R_ARB2_Shutdown() {
	if ( !glConfig.backendInitialized ) {
		return;
	}

	cgShutdown();
	r_skipSpecular.UnRegisterCallback( &g_skipSpecularCallback );
	r_skipBump.UnRegisterCallback( &g_skipBumpCallback );
	r_skipDiffuse.UnRegisterCallback( &g_skipDiffuseCallback );
	r_shaderQuality.UnRegisterCallback( &g_shaderQualityCallback );
	r_megaDrawMethod.UnRegisterCallback( &g_megaDrawMethodCallback );
	glConfig.allowCgPath = false;
	glConfig.backendInitialized = false;
}
