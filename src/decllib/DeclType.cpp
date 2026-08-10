// Copyright (C) 2007 Id Software, Inc.
//


#include "precompiled.h"
#pragma hdrstop

const char* declTableIdentifier				= "table";
const char* declMaterialIdentifier			= "material";
const char* declSkinIdentifier				= "skin";
const char* declSoundShaderIdentifier		= "sound";
const char* declEntityDefIdentifier			= "entityDef";
const char* declEffectsIdentifier			= "effect";
const char* declAFIdentifier				= "articulatedFigure";
const char* declAtmosphereIdentifier		= "atmosphere";
const char* declAmbientCubeMapIdentifier	= "ambientCubeMap";
const char* declStuffTypeIdentifier			= "stuffType";
const char* declDecalIdentifier				= "decal";
const char* declSurfaceTypeIdentifier		= "surfaceType";
const char* declSurfaceTypeMapIdentifier	= "surfaceTypeMap";
const char* declRenderProgramIdentifier		= "renderProgram";
const char* declRenderBindingIdentifier		= "renderBinding";
const char* declTemplateIdentifier			= "template";
const char* declImposterIdentifier			= "imposter";
const char* declImposterGeneratorIdentifier	= "imposterGenerator";
const char* declLocStrIdentifier			= "locString";
const char* declModelExportIdentifier		= "modelExport";

const char* declIdentifierList[] = {
	declTableIdentifier,
	declMaterialIdentifier,
	declSkinIdentifier,
	declSoundShaderIdentifier,
	declEntityDefIdentifier,
	declEffectsIdentifier,
	declAFIdentifier,
	declAtmosphereIdentifier,
	declAmbientCubeMapIdentifier,
	declStuffTypeIdentifier,
	declSurfaceTypeIdentifier,
	declSurfaceTypeMapIdentifier,
	declRenderProgramIdentifier,
	declRenderBindingIdentifier,
	declTemplateIdentifier,
	declImposterIdentifier,
	declImposterGeneratorIdentifier,
	declLocStrIdentifier,
	declDecalIdentifier,
	declModelExportIdentifier,
};

#if defined( ETQW_ENGINE_RECONSTRUCTION )

#include "declTable.h"
#include "declSkin.h"
#include "declEntityDef.h"
#include "declAF.h"
#include "declAtmosphere.h"
#include "declAmbientCubeMap.h"
#include "declStuffType.h"
#include "declDecal.h"
#include "DeclSurfaceType.h"
#include "DeclSurfaceTypeMap.h"
#include "declRenderBinding.h"
#include "declImposter.h"
#include "declLocStr.h"
#include "declTemplate.h"
#include "declmodelexport.h"
#include "../renderer/Material.h"
#include "../sound/SoundShader.h"
#include "../bse/BSEInterface.h"
#include "../bse/BSE_Envelope.h"
#include "../bse/BSE_SpawnDomains.h"
#include "../bse/BSE_Particle.h"
#include "../bse/BSE.h"

// Flags and cache callbacks are taken from the Microsoft executable's
// sdDeclInfo objects.
sdDeclInfo declTableInfo(
	declTableIdentifier,
	DIF_ALLOW_TEMPLATES | DIF_NOT_PRECACHED | DIF_WRITE_BINARY );
sdDeclInfo declMaterialInfo(
	declMaterialIdentifier,
	DIF_ALLOW_TEMPLATES,
	idMaterial::CacheFromDict );
sdDeclInfo declSkinInfo(
	declSkinIdentifier,
	DIF_ALLOW_TEMPLATES | DIF_WRITE_BINARY,
	idDeclSkin::CacheFromDict );
sdDeclInfo declSoundShaderInfo(
	declSoundShaderIdentifier,
	DIF_ALLOW_TEMPLATES | DIF_WRITE_BINARY,
	idSoundShader::CacheFromDict );
sdDeclInfo declEntityDefInfo(
	declEntityDefIdentifier,
	DIF_ALLOW_TEMPLATES | DIF_WRITE_BINARY,
	idDeclEntityDef::CacheFromDict );
sdDeclInfo declEffectsInfo(
	declEffectsIdentifier,
	DIF_ALLOW_TEMPLATES | DIF_WRITE_BINARY,
	rvDeclEffect::CacheFromDict );
sdDeclInfo declAFInfo(
	declAFIdentifier,
	DIF_ALLOW_TEMPLATES | DIF_WRITE_BINARY,
	idDeclAF::CacheFromDict );
sdDeclInfo declAtmosphereInfo(
	declAtmosphereIdentifier,
	DIF_ALLOW_TEMPLATES,
	sdDeclAtmosphere::CacheFromDict );
sdDeclInfo declAmbientCubeMapInfo(
	declAmbientCubeMapIdentifier,
	DIF_ALLOW_TEMPLATES,
	sdDeclAmbientCubeMap::CacheFromDict );
sdDeclInfo declStuffTypeInfo(
	declStuffTypeIdentifier,
	DIF_ALLOW_TEMPLATES );
sdDeclInfo declDecalInfo(
	declDecalIdentifier,
	DIF_ALLOW_TEMPLATES,
	sdDeclDecal::CacheFromDict );
sdDeclInfo declSurfaceTypeInfo(
	declSurfaceTypeIdentifier,
	DIF_NOT_PRECACHED );
sdDeclInfo declSurfaceTypeMapInfo(
	declSurfaceTypeMapIdentifier,
	DIF_ALLOW_TEMPLATES );
sdDeclInfo declRenderProgramInfo(
	declRenderProgramIdentifier,
	DIF_ALLOW_TEMPLATES );
sdDeclInfo declRenderBindingInfo(
	declRenderBindingIdentifier,
	DIF_ALLOW_TEMPLATES );
sdDeclInfo declImposterInfo(
	declImposterIdentifier,
	DIF_ALLOW_TEMPLATES,
	sdDeclImposter::CacheFromDict );
sdDeclInfo declImposterGeneratorInfo(
	declImposterGeneratorIdentifier,
	DIF_ALLOW_TEMPLATES );
sdDeclInfo declLocStrInfo(
	declLocStrIdentifier,
	DIF_NOT_PRECACHED | DIF_WRITE_BINARY );
sdDeclInfo declTemplateInfo(
	declTemplateIdentifier,
	DIF_WRITE_BINARY );
sdDeclInfo declModelExportInfo(
	declModelExportIdentifier,
	DIF_ALLOW_TEMPLATES );

namespace {

idDeclTypeTemplate< idDeclTable, &declTableInfo > declTableType;
idDeclTypeTemplate< idMaterial, &declMaterialInfo > declMaterialType;
idDeclTypeTemplate< idDeclSkin, &declSkinInfo > declSkinType;
idDeclTypeTemplate< idSoundShader, &declSoundShaderInfo > declSoundShaderType;
idDeclTypeTemplate< idDeclEntityDef, &declEntityDefInfo > declEntityDefType;
idDeclTypeTemplate< rvDeclEffect, &declEffectsInfo > declEffectsType;
idDeclTypeTemplate< idDeclAF, &declAFInfo > declAFType;
idDeclTypeTemplate< sdDeclAtmosphere, &declAtmosphereInfo > declAtmosphereType;
idDeclTypeTemplate< sdDeclAmbientCubeMap, &declAmbientCubeMapInfo > declAmbientCubeMapType;
idDeclTypeTemplate< sdDeclStuffType, &declStuffTypeInfo > declStuffTypeType;
idDeclTypeTemplate< sdDeclDecal, &declDecalInfo > declDecalType;
idDeclTypeTemplate< sdDeclSurfaceType, &declSurfaceTypeInfo > declSurfaceTypeType;
idDeclTypeTemplate< sdDeclSurfaceTypeMap, &declSurfaceTypeMapInfo > declSurfaceTypeMapType;
idDeclTypeTemplate< sdDeclRenderProgram, &declRenderProgramInfo > declRenderProgramType;
idDeclTypeTemplate< sdDeclRenderBinding, &declRenderBindingInfo > declRenderBindingType;
idDeclTypeTemplate< sdDeclImposter, &declImposterInfo > declImposterType;
idDeclTypeTemplate< sdDeclImposterGenerator, &declImposterGeneratorInfo > declImposterGeneratorType;
idDeclTypeTemplate< sdDeclLocStr, &declLocStrInfo > declLocStrType;
idDeclTypeTemplate< sdDeclTemplate, &declTemplateInfo > declTemplateType;
idDeclTypeTemplate< sdDeclModelExport, &declModelExportInfo > declModelExportType;

}

void Decl_RegisterBuiltinTypes( idDeclManager* manager ) {
	if ( manager == NULL ) {
		return;
	}

	manager->RegisterDeclType( &declTableType );
	manager->RegisterDeclType( &declMaterialType );
	manager->RegisterDeclType( &declSkinType );
	manager->RegisterDeclType( &declSoundShaderType );
	manager->RegisterDeclType( &declEntityDefType );
	manager->RegisterDeclType( &declEffectsType );
	manager->RegisterDeclType( &declAFType );
	manager->RegisterDeclType( &declAtmosphereType );
	manager->RegisterDeclType( &declAmbientCubeMapType );
	manager->RegisterDeclType( &declStuffTypeType );
	manager->RegisterDeclType( &declDecalType );
	manager->RegisterDeclType( &declSurfaceTypeType );
	manager->RegisterDeclType( &declSurfaceTypeMapType );
	manager->RegisterDeclType( &declRenderProgramType );
	manager->RegisterDeclType( &declRenderBindingType );
	manager->RegisterDeclType( &declTemplateType );
	manager->RegisterDeclType( &declImposterType );
	manager->RegisterDeclType( &declImposterGeneratorType );
	manager->RegisterDeclType( &declLocStrType );
	manager->RegisterDeclType( &declModelExportType );

	manager->RegisterDeclFolder( "templates", ".template" );
	manager->RegisterDeclFolder( "materials", ".mtr" );
	manager->RegisterDeclFolder( "skins", ".skin" );
	manager->RegisterDeclFolder( "sounds", ".sndshd" );
	manager->RegisterDeclFolder( "atmosphere", ".atm" );
	manager->RegisterDeclFolder( "stuff", ".stuff" );
	manager->RegisterDeclFolder( "decal", ".decal" );
	manager->RegisterDeclFolder( "surfacetypes", ".stp" );
	manager->RegisterDeclFolder( "surfacetypes", ".stmap" );
	manager->RegisterDeclFolder( "renderprogs", ".rprog" );
	manager->RegisterDeclFolder( "imposters", ".imp" );
	manager->RegisterDeclFolder( "localization", ".locstr" );
	manager->FinishedRegistering();
}

#endif
