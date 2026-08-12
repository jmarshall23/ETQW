#ifndef __ETQW_RADIANT_PCH_H__
#define __ETQW_RADIANT_PCH_H__

#ifndef NOMINMAX
	#define NOMINMAX
#endif

// idMath deliberately undefines FLT_EPSILON. Instantiate the standard
// numeric limits first because ETQW renderer headers use them later.
#include <algorithm>
#include <limits>

#if defined( _WIN32 )
	// MFC must be included before idlib's LibOS header includes windows.h.
	#undef WIN32_LEAN_AND_MEAN
	#undef VC_EXTRALEAN
	#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
	#include <afxwin.h>
	#include <afxext.h>
	#include <afxdisp.h>
	#include <afxcmn.h>
#endif

#include "../../framework/precompiled.h"
#include "../../decllib/declEntityDef.h"
#include "../../decllib/declSkin.h"
#include "../../framework/BuildVersion.h"
#include "../../idlib/containers/HashTable.h"
#include "../../renderer/Image.h"
#include "../../renderer/tr_render.h"
#include "../../sys/sys_local.h"
#include "../compilers/compiler_public.h"
#include "../edit_public.h"

class idDeclParticle;

#ifndef EDITOR_WINDOWTEXT
	#define EDITOR_WINDOWTEXT GAME_NAME " Radiant"
#endif
#ifndef EDITOR_REGISTRY_KEY
	#define EDITOR_REGISTRY_KEY "ETQW Radiant"
#endif

ID_INLINE bool Sys_EditorDarkThemeEnabled() { return false; }

srfTriangles_t* R_CopyStaticTriSurf( const srfTriangles_t* triangles );
srfTriangles_t* R_AllocStaticTriSurf();
void R_AllocStaticTriSurfVerts( srfTriangles_t* triangles, int numVerts );
void R_AllocStaticTriSurfIndexes( srfTriangles_t* triangles, int numIndexes );
void R_FreeStaticTriSurf( srfTriangles_t* triangles );
void R_ToggleSmpFrame();
extern idCVar r_znear;

// Darklight2 Radiant still uses the classic idTech vector helpers. ETQW's
// idVec classes retain the same indexing contract but no longer expose the
// compatibility macros.
#ifndef VectorCopy
	#define DotProduct( a, b )            ( (a)[0] * (b)[0] + (a)[1] * (b)[1] + (a)[2] * (b)[2] )
	#define VectorSubtract( a, b, c )     ( (c)[0] = (a)[0] - (b)[0], (c)[1] = (a)[1] - (b)[1], (c)[2] = (a)[2] - (b)[2] )
	#define VectorAdd( a, b, c )          ( (c)[0] = (a)[0] + (b)[0], (c)[1] = (a)[1] + (b)[1], (c)[2] = (a)[2] + (b)[2] )
	#define VectorScale( v, s, o )        ( (o)[0] = (v)[0] * (s), (o)[1] = (v)[1] * (s), (o)[2] = (v)[2] * (s) )
	#define VectorMA( v, s, b, o )        ( (o)[0] = (v)[0] + (b)[0] * (s), (o)[1] = (v)[1] + (b)[1] * (s), (o)[2] = (v)[2] + (b)[2] * (s) )
	#define VectorCopy( a, b )            ( (b)[0] = (a)[0], (b)[1] = (a)[1], (b)[2] = (a)[2] )
#endif

void InitAfx( void );

#include "../../renderer/qgl.h"
#include "RadiantVulkan.h"

static const float RADIANT_METERS_TO_DOOM = 1.0f / 0.0254f;

#endif
