// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from renderer/Image_Sequence.obj in the original ETQW PDB
// and its address-matched Hex-Rays bodies.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Image.h"
#include "renderbindings.h"
#include "../decllib/declRenderBinding.h"
#include "../sys/sys_public.h"

sdImageSequence::sdImageSequence() :
	rate( 30.0f ) {
	images.SetGranularity( 16 );
}

sdImageSequence::~sdImageSequence() {
}

void sdImageSequence::SetRate( float r ) {
	rate = r;
}

void sdImageSequence::AddImage( idImage* image ) {
	images.Append( image );
}

void sdImageSequence::UpdateBindings() {
	const int numImages = images.Num();
	if ( numImages == 0 ) {
		rbinds->imgSequenceCur->Set( globalImages->blackImage );
		rbinds->imgSequenceNext->Set( globalImages->blackImage );
		return;
	}

	// tr.frameShaderTime is restored with the backend globals; wall-clock time
	// keeps image sequences functional while that original owner is brought in.
	const float frameTime = Sys_Milliseconds() * 0.001f * rate;
	int frame = static_cast< int >( frameTime ) % numImages;
	if ( frame < 0 ) {
		frame += numImages;
	}

	rbinds->imgSequenceCur->Set( images[ frame ] );
	rbinds->imgSequenceNext->Set( images[ ( frame + 1 ) % numImages ] );
	rbinds->imgSequenceBlend->Set( frameTime - floorf( frameTime ) );
}
