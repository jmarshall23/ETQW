// Copyright (C) 2007 Id Software, Inc.
//
// Cg is loaded dynamically by the renderer.  This header intentionally keeps
// only the core ABI used by ETQW, so building the engine does not require the
// discontinued NVIDIA import library.

#ifndef __QGLLIB_QCG_H__
#define __QGLLIB_QCG_H__

#define CG_VERSION_NUM 1502

#ifndef CGENTRY
#define CGENTRY __cdecl
#endif

typedef int CGbool;

#define CG_FALSE ( ( CGbool )0 )
#define CG_TRUE  ( ( CGbool )1 )

typedef struct _CGcontext* CGcontext;
typedef struct _CGprogram* CGprogram;
typedef struct _CGparameter* CGparameter;
typedef void* CGhandle;

typedef enum {
	CG_UNKNOWN_TYPE,
	CG_STRUCT,
	CG_ARRAY,

	CG_TYPE_START_ENUM = 1024,

#define CG_DATATYPE_MACRO( name, compilerName, enumName, baseName, columns, rows, parameterClass ) enumName,
#include "cg/cg_datatypes.h"
#undef CG_DATATYPE_MACRO
} CGtype;

typedef enum {
#define CG_BINDLOCATION_MACRO( name, enumName, compilerName, enumValue, addressable, parameterType ) enumName = enumValue,
#include "cg/cg_bindlocations.h"
#undef CG_BINDLOCATION_MACRO

	CG_UNDEFINED = 3256
} CGresource;

typedef enum {
	CG_PROFILE_START = 6144,
	CG_PROFILE_UNKNOWN,

#define CG_PROFILE_MACRO( name, compilerId, compilerIdCaps, compilerOption, integerId, vertexProfile ) CG_PROFILE_##compilerIdCaps = integerId,
#include "cg/cg_profiles.h"
#undef CG_PROFILE_MACRO

	CG_PROFILE_MAX = 7100
} CGprofile;

typedef enum {
#define CG_ERROR_MACRO( code, enumName, message ) enumName = code,
#include "cg/cg_errors.h"
#undef CG_ERROR_MACRO
} CGerror;

typedef enum {
#define CG_ENUM_MACRO( enumName, enumValue ) enumName = enumValue,
#include "cg/cg_enums.h"
#undef CG_ENUM_MACRO
} CGenum;

typedef void ( CGENTRY* CGerrorCallbackFunc )( void );

typedef CGcontext ( CGENTRY* PFNCGCREATECONTEXTPROC )( void );
typedef void ( CGENTRY* PFNCGDESTROYCONTEXTPROC )( CGcontext context );
typedef const char* ( CGENTRY* PFNCGGETLASTLISTINGPROC )( CGcontext context );
typedef CGprogram ( CGENTRY* PFNCGCREATEPROGRAMPROC )( CGcontext context, CGenum programType, const char* program, CGprofile profile, const char* entry, const char** arguments );
typedef void ( CGENTRY* PFNCGDESTROYPROGRAMPROC )( CGprogram program );
typedef const char* ( CGENTRY* PFNCGGETPROGRAMSTRINGPROC )( CGprogram program, CGenum name );
typedef CGerror ( CGENTRY* PFNCGGETERRORPROC )( void );
typedef const char* ( CGENTRY* PFNCGGETERRORSTRINGPROC )( CGerror error );
typedef void ( CGENTRY* PFNCGSETERRORCALLBACKPROC )( CGerrorCallbackFunc callback );

extern PFNCGCREATECONTEXTPROC cgCreateContext;
extern PFNCGDESTROYCONTEXTPROC cgDestroyContext;
extern PFNCGGETLASTLISTINGPROC cgGetLastListing;
extern PFNCGCREATEPROGRAMPROC cgCreateProgram;
extern PFNCGDESTROYPROGRAMPROC cgDestroyProgram;
extern PFNCGGETPROGRAMSTRINGPROC cgGetProgramString;
extern PFNCGGETERRORPROC cgGetError;
extern PFNCGGETERRORSTRINGPROC cgGetErrorString;
extern PFNCGSETERRORCALLBACKPROC cgSetErrorCallback;

extern CGcontext cgContext;

bool cgInit( void );
void cgShutdown( void );

#endif /* !__QGLLIB_QCG_H__ */
