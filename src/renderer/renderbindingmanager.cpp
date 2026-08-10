// Copyright (C) 2007 Id Software, Inc.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "renderbindingmanager.h"
#include "RendererTypesImpl.h"
#include "../decllib/declRenderBinding.h"
#include "../libs/qglLib/qgl.h"

static sdRenderBindingManager renderBindingManagerLocal;
sdRenderBindingManager* renderBindingManager = &renderBindingManagerLocal;

void sdRenderBindingManager::UpdateParameter(
		const sdDeclRenderBinding* const renderBinding ) {
	if ( renderBinding == NULL || renderBinding->Infrequent() < 0 ||
		qglProgramEnvParameter4fvARB == NULL ) {
		return;
	}

	const unsigned int index = static_cast< unsigned int >( renderBinding->Infrequent() );
	qglProgramEnvParameter4fvARB(
		GL_VERTEX_PROGRAM_ARB, index, renderBinding->GetVector() );
	qglProgramEnvParameter4fvARB(
		GL_FRAGMENT_PROGRAM_ARB, index, renderBinding->GetVector() );
}

void sdRenderBindingManager::UpdateInfrequentRenderBindings() {
	for ( int i = 0; i < infrequentRenderBindings.Num(); ++i ) {
		const sdDeclRenderBinding* renderBinding =
			infrequentRenderBindings[ i ]->renderBinding;
		renderBinding->Evaluate();
		UpdateParameter( renderBinding );
	}
}

void sdRenderBindingManager::RenderBindingPostParse( idDecl* decl ) {
	sdDeclRenderBinding* renderBinding = static_cast< sdDeclRenderBinding* >( decl );
	if ( renderBinding->Infrequent() == -1 ) {
		return;
	}

	infrequentRenderBinding_t* entry = NULL;
	for ( int i = 0; i < infrequentRenderBindings.Num(); ++i ) {
		if ( infrequentRenderBindings[ i ]->renderBinding == renderBinding ) {
			renderBinding->SetInfrequentIndex( i );
			entry = infrequentRenderBindings[ i ];
			break;
		}
	}

	if ( glConfig.isInitialized && infrequentRenderBindings.Num() != 32 ) {
		if ( entry == NULL ) {
			entry = new infrequentRenderBinding_t;
			entry->renderBinding = renderBinding;
			renderBinding->SetInfrequentIndex( infrequentRenderBindings.Num() );
			infrequentRenderBindings.Append( entry );
		}

		memset( entry->state, 0, sizeof( entry->state ) );
		if ( qglProgramEnvParameter4fvARB != NULL ) {
			const unsigned int index = static_cast< unsigned int >( renderBinding->Infrequent() );
			qglProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, index, entry->state );
			qglProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, index, entry->state );
		}
	} else {
		renderBinding->SetInfrequentIndex( -1 );
	}
}

void sdRenderBindingManager::RenderBindingPostParseCallback( idDecl* decl ) {
	renderBindingManager->RenderBindingPostParse( decl );
}

void sdRenderBindingManager::Init() {
	idDeclTypeInterface* declType = declManager->GetDeclType( declRenderBindingIdentifier );
	if ( declType != NULL ) {
		declType->RegisterPostParse( RenderBindingPostParseCallback );
	}
}

void sdRenderBindingManager::Shutdown() {
	idDeclTypeInterface* declType = declManager->GetDeclType( declRenderBindingIdentifier );
	if ( declType != NULL ) {
		declType->UnregisterPostParse( RenderBindingPostParseCallback );
	}

	for ( int i = 0; i < infrequentRenderBindings.Num(); ++i ) {
		delete infrequentRenderBindings[ i ];
		infrequentRenderBindings[ i ] = NULL;
	}
	infrequentRenderBindings.Clear();
}
