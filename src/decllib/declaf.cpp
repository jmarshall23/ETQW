// Copyright (C) 2007 Id Software, Inc.
//
// The articulated-figure grammar is inherited from Doom 3.  The ETQW PDB
// keeps it in decllib/declaf.cpp and adds the dictionary cache hook below.

#include "precompiled.h"
#include "declAF.h"
#include "declTypeHolder.h"

#pragma hdrstop

#include "../framework/DeclAF.cpp"

void idDeclAF::CacheFromDict( const idDict& dict ) {
	const idKeyValue* keyValue = NULL;
	while ( ( keyValue = dict.MatchPrefix( "ragdoll", keyValue ) ) != NULL ) {
		if ( keyValue->GetValue().Length() != 0 ) {
			declHolder.FindAF( keyValue->GetValue(), false );
		}
	}
}
