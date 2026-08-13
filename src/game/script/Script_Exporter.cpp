// Copyright (C) 2007 Id Software, Inc.
//

#include <set>

#include "../precompiled.h"
#pragma hdrstop

#include "../../framework/Licensee.h"

#include "Script_Exporter.h"
#include "Script_Program.h"
#include "Script_Compiler.h"

idCVar g_compiledScriptSafety( "g_compiledScriptSafety", "1", CVAR_BOOL | CVAR_GAME, "enables extra safety checks in exported scripts" );

enum cs_fixedHFileType {
	CS_FFH_EVENTS,
	CS_FFH_EVENTCALLS,
	CS_FFH_FUNCTIONWRAPPERS,
	CS_FFH_GLOBALFUNCTIONS,
	CS_FFH_GLOBALVARIABLES,
	CS_FFH_SYSCALLS,
	CS_FFH_VIRTUALFUNCTIONS,
	CS_FFH_VIRTUALFUNCTIONDEPENDANCIES,
	CS_FFH_NUM,
};

const char* g_fixedHFileName[ CS_FFH_NUM ] = {
	"generated/Generated_Events.h",
	"generated/Generated_EventCalls.h",
	"generated/Generated_FunctionWrappers.h",
	"generated/Generated_GlobalFunctions.h",
	"generated/Generated_GlobalVariables.h",
	"generated/Generated_SysCalls.h",
	"generated/Generated_VirtualFunctions.h",
	"generated/Generated_VirtualFunctionsDependencies.h",
};

enum cs_fixedCPPFileType {
	CS_FFC_EVENTS,
	CS_FFC_EVENTCALLS,
	CS_FFC_FUNCTIONWRAPPERS,
	CS_FFC_GLOBALFUNCTIONS,
	CS_FFC_GLOBALVARIABLES,
	CS_FFC_SYSCALLS,
	CS_FFC_NUM,
};

const char* g_fixedCppFileName[ CS_FFC_NUM ] = {
	"generated/Generated_Events.cpp",
	"generated/Generated_EventCalls.cpp",
	"generated/Generated_FunctionWrappers.cpp",
	"generated/Generated_GlobalFunctions.cpp",
	"generated/Generated_GlobalVariables.cpp",
	"generated/Generated_SysCalls.cpp",
};

void sdScriptExporter::EnterNamespace( const char* name ) {
	namespaceDef_t* ns = new namespaceDef_t;
	ns->parentNamespace = currentNameSpace;
	ns->name = name;
	currentNameSpace->namespaces.Alloc() = ns;
	currentNameSpace = ns;
	RegisterClass( NULL, NULL );
}

void sdScriptExporter::ExitNamespace( void ) {
	assert( currentNameSpace->parentNamespace != NULL );
	currentNameSpace = currentNameSpace->parentNamespace;
}

void sdScriptExporter::RegisterEventDef( const function_t& f ) {
	eventDef_t& e = events.Alloc();
	e.name = f.Name();
}

const char* sdScriptExporter::BuildEventName( const char* name ) {
	return va( "GeneratedEvent_%s", name );
}

const char* sdScriptExporter::BuildClassName( const idTypeDef* type ) {
	if ( type == &type_object ) {
		return BASE_COMPILED_SCRIPT_CLASS;
	}
	return va( "GeneratedClass_%s", type->Name() );
}

const char* sdScriptExporter::BuildGlobalName( const stackVar_t& var ) {
	return var.name.c_str();
}

const char* sdScriptExporter::BuildGlobalFunctionName( const function_t* function ) {
	return va( "__gf_%s", function->type->Name() );
}

const char* sdScriptExporter::BuildFieldNameByType( etype_t type, etype_t defaultType ) {
	switch( type ) {
		case ev_string:
			return "sdCompiledScriptType_String";
		case ev_wstring:
			return "sdCompiledScriptType_WString";
		case ev_float:
			return "sdCompiledScriptType_Float";
		case ev_boolean:
			return "sdCompiledScriptType_Boolean";
		case ev_vector:
			return "sdCompiledScriptType_Vector";
		case ev_void:
			return "void";
		case ev_pointer:
			return BuildFieldNameByType( defaultType, ev_error );
		case ev_object:
			return va( "sdCompiledScriptType_Object< %s >", BuildClassName( &type_object ) );
	}

	assert( false );
//	gameLocal.Error( "Unknown type: %s", type->Name() );
	return "<invalid>";
}

const char* sdScriptExporter::BuildFieldName( const idTypeDef* type, etype_t defaultType ) {
	if ( type->Inherits( &type_object ) ) {
		return va( "sdCompiledScriptType_Object< %s >", BuildClassName( type ) );
	}
	etype_t t = type->Type();
	if ( t == ev_pointer ) {
		t = type->PointerType()->Type();
	}
	return BuildFieldNameByType( t, ev_error );
}

const char* sdScriptExporter::BuildClassHeaderName( const idTypeDef* type, bool full ) {
	assert( type->Inherits( &type_object ) );
	if ( type == &type_object ) {
		return BASE_COMPILED_SCRIPT_CLASS_HEADER;
	}

	if ( full ) {
		return va( "generated/GeneratedClass_%s.h", type->Name() );
	}

	return va( "GeneratedClass_%s.h", type->Name() );
}

const char* sdScriptExporter::BuildClassCPPName( const idTypeDef* type, bool full ) {
	assert( type->Inherits( &type_object ) );
	if ( type == &type_object ) {
		return BASE_COMPILED_SCRIPT_CLASS_CPP;
	}

	if ( full ) {
		return va( "generated/GeneratedClass_%s.cpp", type->Name() );
	}

	return va( "GeneratedClass_%s.cpp", type->Name() );
}

int sdScriptExporter::GetVarDefNum( const idVarDef* var ) {
	int count = program->GetNumVarDefs();
	for ( int i = 0; i < count; i++ ) {
		if ( program->GetVarDef( i ) == var ) {
			return i;
		}
	}
	return -1;
}

const char* sdScriptExporter::BuildConstantName( int index ) {
	return va( "__g_constant%d", index );
}

int sdScriptExporter::AllocConstant( const idVarDef* var ) {
	idTypeDef* type = var->TypeDef();

	switch ( type->Type() ) {
		case ev_string:
		case ev_float:
		case ev_boolean:
		case ev_vector:
			break;
		default:			
			return -1;
	}

	for ( int i = 0; i < constants.Num(); i++ ) {
		if ( var == constants[ i ].value ) {
			return i;
		}
	}

	int index = constants.Num();

	constantDef_t& def = constants.Alloc();
	def.value = var;

	return index;
}

void sdScriptExporter::AllocGlobal( const idVarDef* var ) {
	idStr name = var->Name();
	namespaceDef_t* ns = currentNameSpace;

	if ( name[ 0 ] == '$' ) {
		name = name.Right( name.Length() - 1 );
		ns = &globalNameSpace;
	}

	if ( FindGlobal( ns, var ) != NULL ) {
		return;
	}

	name = "__g_" + name;

	stackVar_t& stackVar = *new stackVar_t;
	stackVar.name = name;
	stackVar.allocated = true;
	stackVar.offset = GetVarDefNum( var );
	stackVar.type = var->TypeDef();

	assert( stackVar.offset != -1 );

	ns->globalVars.Alloc() = &stackVar;

	if ( var->Type() == ev_vector ) {
		stackVar_t& stackVar1 = *new stackVar_t;
		stackVar_t& stackVar2 = *new stackVar_t;
		stackVar_t& stackVar3 = *new stackVar_t;

		stackVar1.name = va( "%s.GetX()", name.c_str() );
		stackVar1.allocated = false;
		stackVar1.offset = stackVar.offset + 1;
		stackVar1.type = &type_float;

		stackVar2.name = va( "%s.GetY()", name.c_str() );
		stackVar2.allocated = false;
		stackVar2.offset = stackVar.offset + 2;
		stackVar2.type = &type_float;

		stackVar3.name = va( "%s.GetZ()", name.c_str() );
		stackVar3.allocated = false;
		stackVar3.offset = stackVar.offset + 3;
		stackVar3.type = &type_float;

		ns->globalVars.Alloc() = &stackVar1;
		ns->globalVars.Alloc() = &stackVar2;
		ns->globalVars.Alloc() = &stackVar3;
	}
}

int sdScriptExporter::FieldSize( const idTypeDef* typeDef ) {
	if ( typeDef->Inherits( &type_object ) ) {
		return type_object.Size();
	}
	return typeDef->Size();
}

const char* sdScriptExporter::FindFieldName( const namespaceDef_t* ns, idTypeDef* type, idVarDef* ptr ) {
	objectDef_t* objectDef = FindClass( ns, type );

	if ( objectDef->superType != NULL ) {
		const char* fieldName = FindFieldName( ns, objectDef->superType->type, ptr );
		if ( fieldName != NULL ) {
			return fieldName;
		}
	}

	int offset = ptr->value.ptrOffset;
	if ( offset < objectDef->baseOffset ) {
		return NULL;
	}

	int localOffset = offset - objectDef->baseOffset;
	for ( int i = 0; i < objectDef->fields.Num(); i++ ) {
		const idVarDef* field = objectDef->fields[ i ];

		int size = FieldSize( field->TypeDef()->FieldType() );

		if ( localOffset < size ) {
			if ( field->TypeDef()->FieldType()->Type() == ev_vector ) {
				if ( ptr->TypeDef()->FieldType()->Type() == ev_float ) {
					int index = localOffset / 4;
					switch ( index ) {
						case 0:
							return va( "%s.GetX()", field->Name() );
						case 1:
							return va( "%s.GetY()", field->Name() );
						case 2:
							return va( "%s.GetZ()", field->Name() );
					}
					assert( false );
					return NULL;
				}
			}
			if ( localOffset != 0 ) {
				assert( false );
			}
			return field->Name();
		}

		localOffset -= size;
	}

	return NULL;
}

void sdScriptExporter::WriteFunctionStub( idFile* file, const functionDef_t& funcDef, idTypeDef* object, int baseparm ) {
	const function_t* func = funcDef.function;
	if ( !object && funcDef.isVirtual ) {
		file->Printf( "virtual " );
	}
	file->Printf( "%s ", BuildFieldName( func->type->ReturnType() ) );

	if ( object ) {
		file->Printf( "%s::", BuildClassName( object ) );
	}

	if ( func->def->scope->Type() == ev_namespace && !funcDef.isVirtual ) {
		file->Printf( "%s(", BuildGlobalFunctionName( func ) );		
	} else {
		file->Printf( "%s(", func->type->Name() );
	}

	int count = func->type->NumParameters();
	if ( count > baseparm ) {
		file->Printf( " " );
		for ( int k = baseparm; k < count; k++ ) {
			file->Printf( "%s", BuildFieldName( func->type->GetParmType( k ) ) );
			const char* parmName = func->type->GetParmName( k );
			if ( *parmName != '\0' ) {
				file->Printf( " %s", parmName );
			}
			if ( k != count - 1 ) {
				file->Printf( ", " );
			}
		}
		file->Printf( " " );
	}
	file->Printf( ")" );
}

void sdScriptExporter::WriteThreadCallWrapperClass( idFile* file, const objectDef_t* obj, int index, int baseparm, bool gui ) {
	threadDef_t* t = gui ? obj->guiThreadCalls[ index ] : obj->threadCalls[ index ];

	PrintTabs( file );
	file->Printf( "class sdProcedureCallLocal : public sdProcedureCall {\n" );

	PrintTabs( file );
	file->Printf( "public:\n" );

	tabCount++;

	PrintTabs( file );
	file->Printf( "typedef " );
	WriteThreadCallType( file, obj, index, baseparm, "callback_t", gui );
	file->Printf( ";\n" );

	PrintTabs( file );
	file->Printf( "sdProcedureCallLocal( " );

	if ( obj->type != NULL ) {
		file->Printf( "%s _object, ", BuildFieldName( obj->type ) );
	}
	file->Printf( "callback_t _callback" );
	int count = t->parms.Num();
	for ( int i = baseparm; i < count; i++ ) {
		file->Printf( ", %s _parm%d", BuildFieldName( t->parms[ i ] ), i - baseparm );
	}
	file->Printf( " ) {\n" );

	tabCount++;
	if ( obj->type != NULL ) {
		PrintTabs( file );
		file->Printf( "object = _object;\n" );
	}
	PrintTabs( file );
	file->Printf( "callback	= _callback;\n" );

	for ( int i = baseparm; i < count; i++ ) {
		PrintTabs( file );
		file->Printf( "parm%d = _parm%d;\n", i - baseparm, i - baseparm );
	}

	tabCount--;

	PrintTabs( file );
	file->Printf( "}\n" );

	PrintTabs( file );
	file->Printf( "virtual void Go( void ) {\n" );

	tabCount++;
	PrintTabs( file );
	if ( obj->type != NULL ) {
		file->Printf( "( *object.*callback )" );
	} else {
		file->Printf( "( *callback )" );
	}
	file->Printf( "(" );
	if ( count > baseparm ) {
		file->Printf( " " );
		for ( int i = baseparm; i < count; i++ ) {
			if ( i != baseparm ) {
				file->Printf( ", " );
			}
			file->Printf( "parm%d", i - baseparm );
		}
		file->Printf( " " );
	}
	file->Printf( ");\n" );

	tabCount--;

	PrintTabs( file );
	file->Printf( "}\n" );

	tabCount--;

	PrintTabs( file );
	file->Printf( "private:\n" );

	tabCount++;
	if ( obj->type != NULL ) {
		PrintTabs( file );
		file->Printf( "%s object;\n", BuildFieldName( obj->type ) );
	}

	PrintTabs( file );
	file->Printf( "callback_t callback;\n" );

	for ( int i = baseparm; i < count; i++ ) {
		PrintTabs( file );
		file->Printf( "%s parm%d;\n", BuildFieldName( t->parms[ i ] ), i - baseparm );
	}

	tabCount--;

	PrintTabs( file );
	file->Printf( "};\n" );
}

void sdScriptExporter::WriteThreadCallType( idFile* file, const objectDef_t* obj, int index, int baseparm, const char* name, bool gui ) {
	threadDef_t* t = gui ? obj->guiThreadCalls[ index ] : obj->threadCalls[ index ];

	file->Printf( "%s ( ", BuildFieldName( t->returnType ) );
	if ( obj->type != NULL ) {
		file->Printf( "%s::", BuildClassName( obj->type ) );
	}
	file->Printf( "* %s )(", name );

	int count = t->parms.Num();
	if ( count > baseparm ) {
		file->Printf( " " );
		for ( int k = baseparm; k < count; k++ ) {
			file->Printf( "%s", BuildFieldName( t->parms[ k ] ) );
			if ( k != count - 1 ) {
				file->Printf( ", " );
			}
		}
		file->Printf( " " );
	}

	file->Printf( ")" );
}

void sdScriptExporter::WriteThreadCallStub( idFile* file, const objectDef_t* obj, int index, int baseparm, bool clarify, bool includeName, bool gui ) {
	threadDef_t* t = gui ? obj->guiThreadCalls[ index ] : obj->threadCalls[ index ];

	file->Printf( "%s ", BuildFieldName( &type_float ) );

	if ( obj->type != NULL && clarify ) {
		file->Printf( "%s::", BuildClassName( obj->type ) );
	}

	file->Printf( "Create%sThread%d( ", gui ? "Gui" : "", index );

	WriteThreadCallType( file, obj, index, baseparm, "parm0", gui );

	if ( includeName ) {
		file->Printf( ", const char* name" );
	}

	int count = t->parms.Num();
	if ( count > baseparm ) {
		file->Printf( ", " );
		for ( int k = baseparm; k < count; k++ ) {
			file->Printf( "%s parm%d", BuildFieldName( t->parms[ k ] ), ( k - baseparm ) + 1 );
			if ( k != count - 1 ) {
				file->Printf( ", " );
			}
		}
	}
	file->Printf( " )" );
}

void sdScriptExporter::WriteFunctionStartSpecial( idFile* file, const objectDef_t& cls, const function_t* function ) {
	const char* specialFuncs[] = {
		"init",
		"preinit",
		"syncFields",
		NULL,
	};

	for ( int i = 0; ; i++ ) {
		if ( specialFuncs[ i ] == NULL ) {
			return;
		}
		if ( idStr::Cmp( function->type->Name(), specialFuncs[ i ] ) == 0 ) {
			break;
		}
	}

	for ( const objectDef_t* o = cls.superType; o != NULL; o = o->superType ) {
		for ( int i = 0; i < o->functions.Num(); i++ ) {
			if ( idStr::Cmp( o->functions[ i ].function->type->Name(), function->type->Name() ) == 0 ) {
				PrintTabs( file );
				file->Printf( "%s::%s();\n", BuildClassName( o->type ), function->type->Name() );
				return;
			}
		}
	}
}

int sdScriptExporter::RegisterSysCall( const function_t* function ) {
	return RegisterCall( function, 0, sysCalls );
}

int sdScriptExporter::RegisterEventCall( const function_t* function ) {
	return RegisterCall( function, 0, eventCalls );
}

int sdScriptExporter::RegisterCall( const function_t* function, int baseParm, idList< callDef_t* >& list ) {
	const idTypeDef* funcType = function->type;

	for ( int i = 0; i < list.Num(); ) {
		if ( funcType->ReturnType() != list[ i ]->returnType ) {
			goto next;
		}

		if ( list[ i ]->parms.Num() != ( funcType->NumParameters() - baseParm ) ) {
			goto next;
		}

		for ( int j = 0; j < list[ i ]->parms.Num(); j++ ) {
			if ( funcType->GetParmType( j + baseParm ) != list[ i ]->parms[ j ] ) {
				goto next;
			}
		}

		if ( function->eventdef != NULL ) {
			assert( list[ i ]->event != NULL );
			assert( baseParm == 0 );

			if ( function->eventdef->GetReturnType() != list[ i ]->event->GetReturnType() ) {
				goto next;
			}

			if ( list[ i ]->event->GetNumArgs() != function->eventdef->GetNumArgs() ) {
				assert( false );
				goto next;
			}

			for ( int j = 0; j < function->eventdef->GetNumArgs(); j++ ) {
				if ( function->eventdef->GetArgFormat()[ j ] != list[ i ]->event->GetArgFormat()[ j ] ) {
					goto next;
				}
			}
		}

		return i;
next:
		i++;
	}

	int index = list.Num();

	callDef_t* call = new callDef_t;
	list.Alloc() = call;

	call->event = function->eventdef;
	call->returnType = funcType->ReturnType();
	call->parms.SetNum( funcType->NumParameters() - baseParm );
	for ( int i = 0; i < funcType->NumParameters() - baseParm; i++ ) {
		call->parms[ i ] = funcType->GetParmType( i + baseParm );
	}

	return index;
}

void sdScriptExporter::RegisterExternalClassCall( functionDef_t& funcDef ) {
	int index = RegisterCall( funcDef.function, 1, externalClassCalls );
	funcDef.externalCall = externalClassCalls[ index ];
}

void sdScriptExporter::RegisterExternalFunctionCall( functionDef_t& funcDef ) {
	int index = RegisterCall( funcDef.function, 0, externalFunctionCalls );
	funcDef.externalCall = externalFunctionCalls[ index ];
}

void sdScriptExporter::WriteClass( const namespaceDef_t* ns, objectDef_t& cls ) {
	idStr filenameCPP	= BuildClassCPPName( cls.type, true );
	idStr filenameH		= BuildClassHeaderName( cls.type, true );

	idFile* cppFile		= fileSystem->OpenFileWrite( filenameCPP.c_str(), "fs_devpath" );
	idFile* headerFile	= fileSystem->OpenFileWrite( filenameH.c_str(), "fs_devpath" );

	idStr className		= BuildClassName( cls.type );

	idStr guardString = va( "__%s__", filenameH.c_str() );
	guardString.Replace( "/", "_" );
	guardString.Replace( ".", "_" );
	guardString.ToUpper();

	idList< const idTypeDef* > dependencies;

	for ( int j = 0; j < cls.functions.Num(); j++ ) {
		AddDependency( cls.functions[ j ].function, dependencies, false );
	}

	for ( int j = 0; j < cls.fields.Num(); j++ ) {
		AddDependency( cls.fields[ j ]->TypeDef()->FieldType(), dependencies );
	}

	headerFile->Printf( "#ifndef %s\n", guardString.c_str() );
	headerFile->Printf( "#define %s\n", guardString.c_str() );
	headerFile->Printf( "\n" );
	headerFile->Printf( "#include \"%s\"\n", BuildClassHeaderName( cls.type->SuperClass(), false ) );
	headerFile->Printf( "#include \"CompiledScript_Types.h\"\n" );

	headerFile->Printf( "\n" );

	if ( dependencies.Num() > 0 ) {
		headerFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			headerFile->Printf( "class %s;\n", BuildClassName( dependencies[ i ] ) );
		}

		headerFile->Printf( "\n" );
	}

	WriteNamespaceEntry( headerFile, ns );

	PrintTabs( headerFile );
	headerFile->Printf( "class %s : public ", className.c_str() );

	idStr baseClassName = BuildClassName( cls.type->SuperClass() );
	headerFile->Printf( "%s", baseClassName.c_str() );

	headerFile->Printf( " {\n" );

	PrintTabs( headerFile );
	headerFile->Printf( "SD_CS_DECLARE_CLASS( %s );\n", className.c_str() );

	PrintTabs( headerFile );
	headerFile->Printf( "public:\n" );

	tabCount++;

	for ( int j = 0; j < cls.functions.Num(); j++ ) {
		PrintTabs( headerFile );
		WriteFunctionStub( headerFile, cls.functions[ j ], NULL, 1 );

		RegisterExternalClassCall( cls.functions[ j ] );

		headerFile->Printf( ";\n" );
	}

	for ( int j = 0; j < cls.threadCalls.Num(); j++ ) {
		PrintTabs( headerFile );
		WriteThreadCallStub( headerFile, &cls, j, 1, false, true, false );
		headerFile->Printf( ";\n" );
	}

	for ( int j = 0; j < cls.guiThreadCalls.Num(); j++ ) {
		PrintTabs( headerFile );
		WriteThreadCallStub( headerFile, &cls, j, 1, false, true, true );
		headerFile->Printf( ";\n" );
	}

	for ( int j = 0; j < cls.fields.Num(); j++ ) {
		const idVarDef* var = cls.fields[ j ];

		PrintTabs( headerFile );
		headerFile->Printf( "%s %s;\n", BuildFieldName( var->TypeDef()->FieldType() ), var->Name() );

		PrintTabs( headerFile );
		headerFile->Printf( "byte* __GetVariable_%s() { return %s.GetPointer(); }\n", var->Name(), var->Name() );
	}

	PrintTabs( headerFile );
	headerFile->Printf( "static sdClassFunctionInfo __functionInfo[];\n" );

	PrintTabs( headerFile );
	headerFile->Printf( "static sdClassVariableInfo __variableInfo[];\n" );

	PrintTabs( headerFile );
	headerFile->Printf( "static %s* Allocate( void ) { return new %s; }\n", BASE_COMPILED_SCRIPT_CLASS_ALLOCATE, className.c_str() );

	PrintTabs( headerFile );
	headerFile->Printf( "static sdClassInfo __classInfo;\n" );

	tabCount--;

	PrintTabs( headerFile );
	headerFile->Printf( "};\n" );

	WriteNamespaceExit( headerFile, ns );

	headerFile->Printf( "\n#endif // %s\n", guardString.c_str() );

	dependencies.Clear();

	for ( int j = 0; j < cls.functions.Num(); j++ ) {
		AddDependency( cls.functions[ j ].function, dependencies, true );
	}

	cppFile->Printf( "\n" );
	cppFile->Printf( "#include \"Precompiled.h\"\n" );
	cppFile->Printf( "#pragma hdrstop\n\n" );
	cppFile->Printf( "#include \"Generated_Events.h\"\n" );
	cppFile->Printf( "#include \"Generated_GlobalVariables.h\"\n" );
	cppFile->Printf( "#include \"Generated_GlobalFunctions.h\"\n" );
	cppFile->Printf( "#include \"Generated_SysCalls.h\"\n" );
	cppFile->Printf( "#include \"Generated_EventCalls.h\"\n" );
	cppFile->Printf( "#include \"Generated_FunctionWrappers.h\"\n" );
	cppFile->Printf( "#include \"%s\"\n", BuildClassHeaderName( cls.type, false ) );
	cppFile->Printf( "\n" );

	dependencies.Remove( cls.type );
	dependencies.Remove( cls.type->SuperClass() );
	if ( dependencies.Num() > 0 ) {
		cppFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			cppFile->Printf( "#include \"%s\"\n", BuildClassHeaderName( dependencies[ i ], false ) );
		}

		cppFile->Printf( "\n" );
	}

	WriteNamespaceEntry( cppFile, ns );

	PrintTabs( cppFile );
	cppFile->Printf( "SD_CS_IMPLEMENT_CLASS( %s, %s );\n", className.c_str(), baseClassName.c_str() );



	PrintTabs( cppFile );
	cppFile->Printf( "sdClassFunctionInfo %s::__functionInfo[] = {\n", className.c_str() );
	tabCount++;

	for ( int i = 0; i < cls.functions.Num(); i++ ) {
		const function_t* func = cls.functions[ i ].function;
		callDef_t* wrapper = cls.functions[ i ].externalCall;

		int index = externalClassCalls.FindIndex( wrapper );

		PrintTabs( cppFile );
		cppFile->Printf( "{ \"%s\", &ClassFunctionWrapper%d, ( classFunctionCallback_t )( &%s::%s ), %d, %d },\n", func->type->Name(), index, className.c_str(), func->type->Name(), func->type->NumParameters() - 1, func->parmTotal - type_object.Size() );
	}

	PrintTabs( cppFile );
	cppFile->Printf( "{ NULL, NULL },\n" );

	tabCount--;

	PrintTabs( cppFile );
	cppFile->Printf( "};\n" );





	PrintTabs( cppFile );
	cppFile->Printf( "sdClassVariableInfo %s::__variableInfo[] = {\n", className.c_str() );
	tabCount++;

	for ( int i = 0; i < cls.fields.Num(); i++ ) {
		const char* fieldType = NULL;
		switch ( cls.fields[ i ]->TypeDef()->FieldType()->Type() ) {
			case ev_float:
				fieldType = "V_FLOAT";
				break;
			case ev_boolean:
				fieldType = "V_BOOLEAN";
				break;
			case ev_vector:
				fieldType = "V_VECTOR";
				break;
			case ev_object:
				fieldType = "V_OBJECT";
				break;
			case ev_string:
				fieldType = "V_STRING";
				break;
			case ev_wstring:
				fieldType = "V_WSTRING";
				break;
			default:
				assert( false );
				break;
		}

		PrintTabs( cppFile );
		cppFile->Printf( "{ \"%s\", %s, ( variableLookup_t )( &%s::__GetVariable_%s ) },\n", cls.fields[ i ]->Name(), fieldType, className.c_str(), cls.fields[ i ]->Name() );
	}

	PrintTabs( cppFile );
	cppFile->Printf( "{ NULL, V_NONE, NULL },\n" );

	tabCount--;

	PrintTabs( cppFile );
	cppFile->Printf( "};\n" );




	PrintTabs( cppFile );
	cppFile->Printf( "sdClassInfo %s::__classInfo = {\n", className.c_str() );
	tabCount++;

	PrintTabs( cppFile );
	cppFile->Printf( "\"%s\",\n", cls.type->Name() );

	PrintTabs( cppFile );
	cppFile->Printf( "%s::__functionInfo,\n", className.c_str() );

	PrintTabs( cppFile );
	cppFile->Printf( "%s::__variableInfo,\n", className.c_str() );

	PrintTabs( cppFile );
	if ( cls.superType != NULL ) {
		cppFile->Printf( "&%s::__classInfo,\n", program->scriptExporter.BuildClassName( cls.superType->type ) );
	} else {
		cppFile->Printf( "NULL,\n" );
	}

	PrintTabs( cppFile );
	cppFile->Printf( "&%s::Allocate,\n", className.c_str() );

	tabCount--;
	PrintTabs( cppFile );
	cppFile->Printf( "};\n" );

	for ( int i = 0; i < cls.functions.Num(); i++ ) {
		const function_t* func = cls.functions[ i ].function;

		PrintTabs( cppFile );
		WriteFunctionStub( cppFile, cls.functions[ i ], cls.type, 1 );
		cppFile->Printf( " {\n" );
		tabCount++;

		WriteFunctionStartSpecial( cppFile, cls, func );

		sdFunctionCompileState* state = new sdFunctionCompileState( &cls.functions[ i ], ns, cppFile, tabCount, program );
		state->Run();
		delete state;

		tabCount--;
		PrintTabs( cppFile );
		cppFile->Printf( "}\n" );

		cppFile->Printf( "\n" );
	}

	for ( int j = 0; j < cls.threadCalls.Num(); j++ ) {
		PrintTabs( cppFile );
		WriteThreadCallStub( cppFile, &cls, j, 1, true, true, false );
		cppFile->Printf( " {\n" );

		tabCount++;
		WriteThreadCallWrapperClass( cppFile, &cls, j, 1, false );

		PrintTabs( cppFile );
		cppFile->Printf( "return compilerInterface->AllocThread( this, name, new sdProcedureCallLocal( this, parm0" );
		for ( int k = 1; k < cls.threadCalls[ j ]->parms.Num(); k++ ) {
			cppFile->Printf( ", parm%d", k );
		}
		cppFile->Printf( " ) );\n" );

		tabCount--;

		PrintTabs( cppFile );
		cppFile->Printf( "}\n" );
	}

	for ( int j = 0; j < cls.guiThreadCalls.Num(); j++ ) {
		PrintTabs( cppFile );
		WriteThreadCallStub( cppFile, &cls, j, 1, true, true, true );
		cppFile->Printf( " {\n" );

		tabCount++;
		WriteThreadCallWrapperClass( cppFile, &cls, j, 1, true );

		PrintTabs( cppFile );
		cppFile->Printf( "return compilerInterface->AllocGuiThread( this, name, new sdProcedureCallLocal( this, parm0" );
		for ( int k = 1; k < cls.guiThreadCalls[ j ]->parms.Num(); k++ ) {
			cppFile->Printf( ", parm%d", k );
		}
		cppFile->Printf( " ) );\n" );

		tabCount--;

		PrintTabs( cppFile );
		cppFile->Printf( "}\n" );
	}

	WriteNamespaceExit( cppFile, ns );

	fileSystem->CloseFile( headerFile );
	fileSystem->CloseFile( cppFile );
}

void sdScriptExporter::WriteNamespaceTitle( idFile* file, const namespaceDef_t* ns ) {
	file->Printf( "namespace __ns_%s", ns->name.c_str() );
}

void sdScriptExporter::WriteNamespaceScope( idFile* file, const namespaceDef_t* ns ) {
	idList< const namespaceDef_t* > list;

	while ( ns ) {
		list.Insert( ns );
		ns = ns->parentNamespace;
	}

	for ( int i = 0; i < list.Num(); i++ ) {
		if ( list[ i ]->name.Length() <= 0 ) {
			continue;
		}

		WriteNamespaceTitle( file, list[ i ] );
		file->Printf( "::" );
	}
}

void sdScriptExporter::WriteNamespaceEntry( idFile* file, const namespaceDef_t* ns ) {
	idList< const namespaceDef_t* > list;

	while ( ns ) {
		list.Insert( ns );
		ns = ns->parentNamespace;
	}

	for ( int i = 0; i < list.Num(); i++ ) {
		if ( list[ i ]->name.Length() <= 0 ) {
			continue;
		}

		PrintTabs( file );
		WriteNamespaceTitle( file, list[ i ] );
		file->Printf( " {\n" );
		tabCount++;
	}
}

void sdScriptExporter::WriteNamespaceExit( idFile* file, const namespaceDef_t* ns ) {
	idList< const namespaceDef_t* > list;

	while ( ns ) {
		list.Insert( ns );
		ns = ns->parentNamespace;
	}

	for ( int i = 0; i < list.Num(); i++ ) {
		if ( list[ i ]->name.Length() <= 0 ) {
			continue;
		}

		tabCount--;
		PrintTabs( file );
		file->Printf( "}\n" );
	}
}

void sdScriptExporter::PrintTabs( idFile* file ) {
	for ( int i = 0; i < tabCount; i++ ) {
		file->Printf( "\t" );
	}
}

void sdScriptExporter::WriteGlobalVariablesHeader( idFile* file, const namespaceDef_t* ns ) {
	if ( ns->name.Length() > 0 ) {
		PrintTabs( file );
		WriteNamespaceTitle( file, ns );
		file->Printf( " {\n" );
		tabCount++;
	}

	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteGlobalVariablesHeader( file, ns->namespaces[ i ] );
	}

	for ( int i = 0; i < ns->globalVars.Num(); i++ ) {
		if ( !ns->globalVars[ i ]->allocated ) {
			continue;
		}
		PrintTabs( file );
		file->Printf( "extern " );
		file->Printf( "%s ", BuildFieldName( ns->globalVars[ i ]->type ) );
		file->Printf( "%s", BuildGlobalName( *ns->globalVars[ i ] ) );
		file->Printf( ";\n" );
	}

	if ( ns->name.Length() > 0 ) {
		tabCount--;
		PrintTabs( file );
		file->Printf( "}\n" );
	}
}

void sdScriptExporter::WriteGlobalVariablesCPP( idFile* file, const namespaceDef_t* ns ) {
	if ( ns->name.Length() > 0 ) {
		PrintTabs( file );
		WriteNamespaceTitle( file, ns );
		file->Printf( " {\n" );
		tabCount++;
	}

	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteGlobalVariablesCPP( file, ns->namespaces[ i ] );
	}

	for ( int i = 0; i < ns->globalVars.Num(); i++ ) {
		if ( !ns->globalVars[ i ]->allocated ) {
			continue;
		}
		PrintTabs( file );
		file->Printf( "%s ", BuildFieldName( ns->globalVars[ i ]->type ) );
		file->Printf( "%s", BuildGlobalName( *ns->globalVars[ i ] ) );
		file->Printf( ";\n" );
	}

	if ( ns->name.Length() > 0 ) {
		tabCount--;
		PrintTabs( file );
		file->Printf( "}\n" );
	}
}

void sdScriptExporter::AddDependency( const function_t* function, idList< const idTypeDef* >& dependencies, bool contents ) {
	int count = function->type->NumParameters();

	if ( !contents ) {
		AddDependency( function->type->ReturnType(), dependencies );
		for ( int i = 0; i < count; i++ ) {
			AddDependency( function->type->GetParmType( i ), dependencies );
		}
	} else {
		for ( int i = function->firstStatement; i < function->firstStatement + function->numStatements; i++ ) {
			statement_t& statement = program->GetStatement( i );
			
			if ( statement.a != NULL ) {
				AddDependency( statement.a->TypeDef(), dependencies );
			}

			if ( statement.b != NULL ) {
				AddDependency( statement.b->TypeDef(), dependencies );
			}

			if ( statement.c != NULL ) {
				AddDependency( statement.c->TypeDef(), dependencies );
			}
		}
	}
}

void sdScriptExporter::AddDependency( const idTypeDef* type, idList< const idTypeDef* >& dependencies ) {
	if ( !type->Inherits( &type_object ) ) {
		return;
	}
	dependencies.AddUnique( type );
}

void sdScriptExporter::FindGlobalFunctionDependencies( namespaceDef_t* ns, idList< const idTypeDef* >& dependencies ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		FindGlobalFunctionDependencies( ns->namespaces[ i ], dependencies );
	}

	objectDef_t* globalObj = ns->classes[ 0 ];
	for ( int i = 0; i < globalObj->functions.Num(); i++ ) {
		AddDependency( globalObj->functions[ i ].function, dependencies, false );
	}
}

void sdScriptExporter::FindGlobalVariablesDependencies( namespaceDef_t* ns, idList< const idTypeDef* >& dependencies ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		FindGlobalVariablesDependencies( ns->namespaces[ i ], dependencies );
	}

	for ( int i = 0; i < ns->globalVars.Num(); i++ ) {
		AddDependency( ns->globalVars[ i ]->type, dependencies );
	}
}

void sdScriptExporter::WriteGlobalVariables( void ) {
	idFile* headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_GLOBALVARIABLES ], "fs_devpath" );

	headerFile->Printf( "#ifndef __GENERATED_GLOBALVARIABLES_H__\n" );
	headerFile->Printf( "#define __GENERATED_GLOBALVARIABLES_H__\n" );
	headerFile->Printf( "\n" );
	headerFile->Printf( "#include \"CompiledScript_Types.h\"\n" );
	headerFile->Printf( "\n" );

	idList< const idTypeDef* > dependencies;
	FindGlobalVariablesDependencies( &globalNameSpace, dependencies );

	if ( dependencies.Num() > 0 ) {
		headerFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			headerFile->Printf( "class %s;\n", BuildClassName( dependencies[ i ] ) );
		}

		headerFile->Printf( "\n" );
	}

	WriteGlobalVariablesHeader( headerFile, &globalNameSpace );

	for ( int i = 0; i < constants.Num(); i++ ) {
		switch ( constants[ i ].value->Type() ) {
			case ev_string:
				headerFile->Printf( "extern const sdCompiledScriptType_String %s;\n", BuildConstantName( i ) );
				break;
			case ev_float:
				headerFile->Printf( "extern const sdCompiledScriptType_Float %s;\n", BuildConstantName( i ) );
				break;
			case ev_boolean:
				headerFile->Printf( "extern const sdCompiledScriptType_Boolean %s;\n", BuildConstantName( i ) );
				break;
			case ev_vector:
				headerFile->Printf( "extern const sdCompiledScriptType_Vector %s;\n", BuildConstantName( i ) );
				break;
		}
	}

	headerFile->Printf( "#endif // __GENERATED_GLOBALVARIABLES_H__\n" );

	fileSystem->CloseFile( headerFile );



	idFile* cppFile = fileSystem->OpenFileWrite( g_fixedCppFileName[ CS_FFC_GLOBALVARIABLES ], "fs_devpath" );

	cppFile->Printf( "\n" );
	cppFile->Printf( "#include \"Precompiled.h\"\n" );
	cppFile->Printf( "#pragma hdrstop\n\n" );
	cppFile->Printf( "#include \"Generated_GlobalVariables.h\"\n" );
	cppFile->Printf( "\n" );

	WriteGlobalVariablesCPP( cppFile, &globalNameSpace );

	for ( int i = 0; i < constants.Num(); i++ ) {
		switch ( constants[ i ].value->Type() ) {
			case ev_string: {
				idStr temp = constants[ i ].value->value.stringPtr;
				temp.Replace( "\\", "\\\\" );
				temp.Replace( "\r", "\\r" );
				temp.Replace( "\n", "\\n" );
				temp.Replace( "\t", "\\t" );
				temp.Replace( "\"", "\\\"" );
				cppFile->Printf( "const sdCompiledScriptType_String %s( \"%s\" );\n", BuildConstantName( i ), temp.c_str() );
				break;
			}
			case ev_float:
				cppFile->Printf( "const sdCompiledScriptType_Float %s( %ff );\n", BuildConstantName( i ), *constants[ i ].value->value.floatPtr );
				break;
			case ev_boolean:
				cppFile->Printf( "const sdCompiledScriptType_Boolean %s( %s );\n", BuildConstantName( i ), *constants[ i ].value->value.intPtr ? "true" : "false" );
				break;
			case ev_vector: {
				idVec3& v = *constants[ i ].value->value.vectorPtr;
				cppFile->Printf( "const sdCompiledScriptType_Vector %s( %ff, %ff, %ff );\n", BuildConstantName( i ), v[ 0 ], v[ 1 ], v[ 2 ] );
				break;
			}
		}
	}

	fileSystem->CloseFile( cppFile );
}

void sdScriptExporter::WriteNamespaceFunctions( const namespaceDef_t* ns ) {
	idFile* headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_GLOBALFUNCTIONS ], "fs_devpath" );
	headerFile->Printf( "#ifndef __GENERATED_GLOBALFUNCTIONS_H__\n" );
	headerFile->Printf( "#define __GENERATED_GLOBALFUNCTIONS_H__\n" );
	headerFile->Printf( "\n" );
	headerFile->Printf( "#include \"CompiledScript_Types.h\"\n" );
	headerFile->Printf( "\n" );
	headerFile->Printf( "class sdCompiledScript_Class;\n" );
	headerFile->Printf( "\n" );

	idList< const idTypeDef* > dependencies;
	FindGlobalFunctionDependencies( &globalNameSpace, dependencies );

	if ( dependencies.Num() > 0 ) {
		headerFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			headerFile->Printf( "class %s;\n", BuildClassName( dependencies[ i ] ) );
		}

		headerFile->Printf( "\n" );
	}

	idFile* cppFile = fileSystem->OpenFileWrite( g_fixedCppFileName[ CS_FFC_GLOBALFUNCTIONS ], "fs_devpath" );
	cppFile->Printf( "\n" );
	cppFile->Printf( "#include \"Precompiled.h\"\n" );
	cppFile->Printf( "#pragma hdrstop\n\n" );
	cppFile->Printf( "#include \"Generated_GlobalVariables.h\"\n" );
	cppFile->Printf( "#include \"Generated_GlobalFunctions.h\"\n" );
	cppFile->Printf( "#include \"Generated_Events.h\"\n" );
	cppFile->Printf( "#include \"Generated_SysCalls.h\"\n" );
	cppFile->Printf( "#include \"Generated_EventCalls.h\"\n" );
	cppFile->Printf( "#include \"CompiledScript_Class.h\"\n" );
	WriteNamespaceClassIncludes( ns, cppFile );
	cppFile->Printf( "\n" );

	WriteNamespaceFunctions( ns, headerFile, cppFile );
	WriteFunctionWrappers( cppFile );
	WriteNamespaceFunctionInfo( ns, cppFile );

	cppFile->Printf( "void sdCompiledScript_Class::InitClasses() {\n" );
	tabCount++;
	WriteNamespaceClassInit( ns, cppFile );
	tabCount--;
	cppFile->Printf( "}\n" );

	cppFile->Printf( "void sdCompiledScript_Class::InitFunctions() {\n" );
	tabCount++;
	WriteNamespaceFunctionInit( ns, cppFile );
	tabCount--;
	cppFile->Printf( "}\n" );

	fileSystem->CloseFile( cppFile );

	headerFile->Printf( "\n" );
	headerFile->Printf( "#endif // __GENERATED_GLOBALFUNCTIONS_H__\n" );
	fileSystem->CloseFile( headerFile );
}

void sdScriptExporter::WriteNamespaceClassIncludes( const namespaceDef_t* ns, idFile* cppFile ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteNamespaceClassIncludes( ns->namespaces[ i ], cppFile );
	}

	for ( int i = 1; i < ns->classes.Num(); i++ ) {
		cppFile->Printf( "#include \"%s\"\n", BuildClassHeaderName( ns->classes[ i ]->type, false ) );
	}	
}

void sdScriptExporter::WriteNamespaceClassInit( const namespaceDef_t* ns, idFile* cppFile ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteNamespaceClassInit( ns->namespaces[ i ], cppFile );
	}

	if ( ns->classes.Num() > 1 ) {
		WriteNamespaceEntry( cppFile, ns );
		for ( int i = 1; i < ns->classes.Num(); i++ ) {
			PrintTabs( cppFile );
			cppFile->Printf( "sdCompiledScript_Class::AddClassInfo( &%s::__classInfo );\n", BuildClassName( ns->classes[ i ]->type ) );
		}
		WriteNamespaceExit( cppFile, ns );
	}
}

void sdScriptExporter::WriteNamespaceFunctionInfo( const namespaceDef_t* ns, idFile* cppFile ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteNamespaceFunctionInfo( ns->namespaces[ i ], cppFile );
	}

	objectDef_t& info = *ns->classes[ 0 ];

	for ( int i = 0; i < info.functions.Num(); i++ ) {
		PrintTabs( cppFile );

		const char* name = info.functions[ i ].function->type->Name();

		cppFile->Printf( "sdFunctionInfo __functionInfo_%s = {", name );
		cppFile->Printf( " \"%s\", ", name );

		int index = externalFunctionCalls.FindIndex( info.functions[ i ].externalCall );
		cppFile->Printf( " &GlobalFunctionWrapper%d, ", index );

		cppFile->Printf( " ( functionCallback_t )( &" );
		WriteNamespaceScope( cppFile, ns );
		cppFile->Printf( "%s ) };\n", BuildGlobalFunctionName( info.functions[ i ].function ) );
	}
}

void sdScriptExporter::WriteNamespaceFunctionInit( const namespaceDef_t* ns, idFile* cppFile ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteNamespaceFunctionInit( ns->namespaces[ i ], cppFile );
	}

	objectDef_t& info = *ns->classes[ 0 ];

	for ( int i = 0; i < info.functions.Num(); i++ ) {
		PrintTabs( cppFile );
		cppFile->Printf( "sdCompiledScript_Class::AddFunctionInfo( &__functionInfo_%s );\n", info.functions[ i ].function->type->Name() );
	}
}

void sdScriptExporter::WriteNamespaceFunctions( const namespaceDef_t* ns, idFile* headerFile, idFile* cppFile ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteNamespaceFunctions( ns->namespaces[ i ], headerFile, cppFile );
	}

	objectDef_t& info = *ns->classes[ 0 ];

	WriteNamespaceEntry( headerFile, ns );
	for ( int i = 0; i < info.functions.Num(); i++ ) {
		PrintTabs( headerFile );
		WriteFunctionStub( headerFile, info.functions[ i ], NULL, 0 );
		headerFile->Printf( ";\n" );

		RegisterExternalFunctionCall( info.functions[ i ] );
	}
	for ( int j = 0; j < info.threadCalls.Num(); j++ ) {
		PrintTabs( headerFile );
		WriteThreadCallStub( headerFile, &info, j, 0, false, false, false );
		headerFile->Printf( ";\n" );
	}
	for ( int j = 0; j < info.guiThreadCalls.Num(); j++ ) {
		PrintTabs( headerFile );
		WriteThreadCallStub( headerFile, &info, j, 0, false, false, true );
		headerFile->Printf( ";\n" );
	}
	WriteNamespaceExit( headerFile, ns );

	WriteNamespaceEntry( cppFile, ns );
	for ( int i = 0; i < info.functions.Num(); i++ ) {
		PrintTabs( cppFile );
		WriteFunctionStub( cppFile, info.functions[ i ], NULL, 0 );
		cppFile->Printf( " {\n" );
		tabCount++;

		sdFunctionCompileState* state = new sdFunctionCompileState( &info.functions[ i ], ns, cppFile, tabCount, program );
		state->Run();
		delete state;

		tabCount--;
		PrintTabs( cppFile );
		cppFile->Printf( "}\n" );
	}
	for ( int j = 0; j < info.threadCalls.Num(); j++ ) {
		PrintTabs( cppFile );
		WriteThreadCallStub( cppFile, &info, j, 0, false, false, false );
		cppFile->Printf( " {\n" );

		tabCount++;
		WriteThreadCallWrapperClass( cppFile, &info, j, 0, false );

		PrintTabs( cppFile );
		cppFile->Printf( "return compilerInterface->AllocThread( new sdProcedureCallLocal( parm0" );
		for ( int k = 0; k < info.threadCalls[ j ]->parms.Num(); k++ ) {
			cppFile->Printf( ", parm%d", k + 1 );
		}
		cppFile->Printf( " ) );\n" );
		tabCount--;

		PrintTabs( cppFile );
		cppFile->Printf( "}\n" );
	}
	for ( int j = 0; j < info.guiThreadCalls.Num(); j++ ) {
		PrintTabs( cppFile );
		WriteThreadCallStub( cppFile, &info, j, 0, false, false, true );
		cppFile->Printf( " {\n" );

		tabCount++;
		WriteThreadCallWrapperClass( cppFile, &info, j, 0, true );

		PrintTabs( cppFile );
		cppFile->Printf( "return compilerInterface->AllocGuiThread( new sdProcedureCallLocal( parm0" );
		for ( int k = 0; k < info.guiThreadCalls[ j ]->parms.Num(); k++ ) {
			cppFile->Printf( ", parm%d", k + 1 );
		}
		cppFile->Printf( " ) );\n" );
		tabCount--;

		PrintTabs( cppFile );
		cppFile->Printf( "}\n" );
	}
	WriteNamespaceExit( cppFile, ns );
}

int sdScriptExporter::FindThreadCall( const objectDef_t& obj, const function_t* function, bool guithreadcall ) {
	const idList< threadDef_t* >& list = guithreadcall ? obj.guiThreadCalls : obj.threadCalls;

	for ( int i = 0; i < list.Num(); i++ ) {
		if ( list[ i ]->returnType != function->type->ReturnType() ) {
			continue;
		}

		if ( function->type->NumParameters() != list[ i ]->parms.Num() ) {
			continue;
		}

		int j;
		for ( j = 0; j < list[ i ]->parms.Num(); j++ ) {
			if ( list[ i ]->parms[ j ] != function->type->GetParmType( j ) ) {
				break;
			}
		}
		if ( j < list[ i ]->parms.Num() ) {
			continue;
		}

		return i;
	}

	return -1;
}

void sdScriptExporter::RegisterClassThreadCall( idTypeDef* type, const function_t* function, bool guithreadcall ) {
	namespaceDef_t* ns = currentNameSpace;
	if ( type == NULL ) {
		ns = &globalNameSpace;
	}
	objectDef_t* o = FindClass( ns, type );
	assert( o != NULL );

	int index = FindThreadCall( *o, function, guithreadcall );
	if ( index == -1 ) {
		threadDef_t* t = new threadDef_t;
		t->returnType = function->type->ReturnType();
		for ( int i = 0; i < function->type->NumParameters(); i++ ) {
			t->parms.Alloc() = function->type->GetParmType( i );
		}
		if ( guithreadcall ) {
			o->guiThreadCalls.Alloc() = t;
		} else {
			o->threadCalls.Alloc() = t;
		}
	}
}

void sdScriptExporter::Finish( void ) {
	idFile* file;
	
	file = fileSystem->OpenFileWrite( g_fixedCppFileName[ CS_FFC_EVENTS ], "fs_devpath" );
	if ( file != NULL ) {
		file->Printf( "\n" );
		file->Printf( "#include \"Precompiled.h\"\n" );
		file->Printf( "#pragma hdrstop\n\n" );
		file->Printf( "#include \"Generated_Events.h\"\n" );
		file->Printf( "\n" );

		for ( int i = 0; i < events.Num(); i++ ) {
			idStr eventName = BuildEventName( events[ i ].name );

			file->Printf( "sdCompiledScript_Event %s( \"%s\" );\n", eventName.c_str(), events[ i ].name.c_str() );
		}
		file->Printf( "\n" );

		fileSystem->CloseFile( file );
		file = NULL;
	}

	file = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_EVENTS ], "fs_devpath" );
	if ( file != NULL ) {
		file->Printf( "\n" );
		file->Printf( "#ifndef __GENERATED_EVENTS_H__\n" );
		file->Printf( "#define __GENERATED_EVENTS_H__\n" );
		file->Printf( "#include \"CompiledScript_Event.h\"\n" );
		file->Printf( "\n" );

		for ( int i = 0; i < events.Num(); i++ ) {
			idStr eventName = BuildEventName( events[ i ].name );

			file->Printf( "extern sdCompiledScript_Event %s;\n", eventName.c_str(), events[ i ].name.c_str() );
		}
		file->Printf( "\n" );

		file->Printf( "#endif // __GENERATED_EVENTS_H__\n" );

		fileSystem->CloseFile( file );
		file = NULL;
	}

	WriteVirtualFunctions();
	WriteNamespaceFunctions( &globalNameSpace );
	WriteNamespaceClasses( &globalNameSpace );
	WriteGlobalVariables();
	WriteClassFunctionWrappers();
	WriteSysCalls();
	WriteEventCalls();

	idFile* completeFile = fileSystem->OpenFileWrite( "generated/Generated_Complete.stamp", "fs_devpath" );
	if ( completeFile == NULL ) {
		gameLocal.Error( "Failed to write compiled-script completion marker" );
	}
	completeFile->Printf( "ETQW compiled-script export complete\n" );
	fileSystem->CloseFile( completeFile );

	gameLocal.Printf( "Compiled-script C++ generated in '%s/%s/generated'\n",
		cvarSystem->GetCVarString( "fs_devpath" ), fileSystem->GetGamePath() );
}

void sdScriptExporter::WriteSysCalls( void ) {
	idList< const idTypeDef* > dependencies;

	idFile* headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_SYSCALLS ], "fs_devpath" );

	headerFile->Printf( "#ifndef __GENERATED_SYSCALLS_H__\n" );
	headerFile->Printf( "#define __GENERATED_SYSCALLS_H__\n\n" );

	for ( int i = 0; i < sysCalls.Num(); i++ ) {
		callDef_t* t = sysCalls[ i ];
		headerFile->Printf( "%s SysCall%d( const sdCompiledScript_Event& _event", BuildFieldName( t->returnType ), i );
		if ( t->parms.Num() > 0 ) {
			headerFile->Printf( ", " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				headerFile->Printf( "const %s& parm%d", BuildFieldName( t->parms[ j ] ), j );
				if ( j != t->parms.Num() - 1 ) {
					headerFile->Printf( ", " );
				}
			}
		}
		headerFile->Printf( " );\n" );



		AddDependency( t->returnType, dependencies );
		for ( int j = 0; j < t->parms.Num(); j++ ) {
			AddDependency( t->parms[ j ], dependencies );
		}
	}

	headerFile->Printf( "\n#endif // __GENERATED_SYSCALLS_H__\n" );

	fileSystem->CloseFile( headerFile );

	idFile* cppFile = fileSystem->OpenFileWrite( g_fixedCppFileName[ CS_FFC_SYSCALLS ], "fs_devpath" );

	cppFile->Printf( "#include \"Precompiled.h\"\n" );
	cppFile->Printf( "#pragma hdrstop\n\n" );
	cppFile->Printf( "#include \"CompiledScript_Types.h\"\n" );
	cppFile->Printf( "#include \"CompiledScript_Event.h\"\n" );

	if ( dependencies.Num() > 0 ) {
		cppFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			cppFile->Printf( "class %s;\n", BuildClassName( dependencies[ i ] ) );
		}

		cppFile->Printf( "\n" );
	}

	for ( int i = 0; i < sysCalls.Num(); i++ ) {
		callDef_t* t = sysCalls[ i ];

		cppFile->Printf( "%s SysCall%d( const sdCompiledScript_Event& _event", BuildFieldName( t->returnType ), i );
		if ( t->parms.Num() > 0 ) {
			cppFile->Printf( ", " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				cppFile->Printf( "const %s& parm%d", BuildFieldName( t->parms[ j ] ), j );
				if ( j != t->parms.Num() - 1 ) {
					cppFile->Printf( ", " );
				}
			}
		}
		cppFile->Printf( " ) {\n" );
		tabCount++;

		if ( t->parms.Num() > 0 ) {
			PrintTabs( cppFile );
			cppFile->Printf( "UINT_PTR data[ %d ];\n", t->parms.Num() );

			for ( int j = 0; j < t->parms.Num(); j++ ) {
				const char* eventDataTypeName = NULL;
				switch ( t->event->GetArgFormat()[ j ] ) {
					case D_EVENT_VECTOR:
						eventDataTypeName = "Vector";
						break;
					case D_EVENT_STRING:
						eventDataTypeName = "String";
						break;
					case D_EVENT_WSTRING:
						eventDataTypeName = "WString";
						break;
					case D_EVENT_ENTITY:
					case D_EVENT_ENTITY_NULL:
						eventDataTypeName = "Entity";
						break;
					case D_EVENT_OBJECT:
						eventDataTypeName = "Object";
						break;
					case D_EVENT_BOOLEAN:
						eventDataTypeName = "Boolean";
						break;
					case D_EVENT_FLOAT:
						eventDataTypeName = "Float";
						break;
					case D_EVENT_INTEGER:
						eventDataTypeName = "Integer";
						break;
					case D_EVENT_HANDLE:
						eventDataTypeName = "Handle";
						break;
					default:
						assert( false );
						eventDataTypeName = "Unknown";
						break;
				}

				if ( t->event->GetArgFormat()[ j ] == D_EVENT_ENTITY ) {
					PrintTabs( cppFile );
					cppFile->Printf( "if ( !parm%d.To%sData( data[ %d ] ) ) {\n", j, eventDataTypeName, j );

					tabCount++;

					PrintTabs( cppFile );
					cppFile->Printf( "// TODO: Print warning\n" );

					PrintTabs( cppFile );
					if ( t->returnType->Type() != ev_void ) {
						cppFile->Printf( "return %s();\n", BuildFieldName( t->returnType ) );
					} else {
						cppFile->Printf( "return;\n" );
					}

					tabCount--;
					PrintTabs( cppFile );
					cppFile->Printf( "}\n" );
				} else {
					PrintTabs( cppFile );
					cppFile->Printf( "parm%d.To%sData( data[ %d ] );\n", j, eventDataTypeName, j );
				}
			}
		}

		PrintTabs( cppFile );
		cppFile->Printf( "compilerInterface->SysCall( _event.GetEvent(), %s );\n", t->parms.Num() > 0 ? "data" : "NULL" );

		const char* typeName;
		switch ( t->event->GetReturnType() ) {
			case D_EVENT_VECTOR:
				typeName = "Vector";
				break;
			case D_EVENT_STRING:
				typeName = "String";
				break;
			case D_EVENT_WSTRING:
				typeName = "WString";
				break;
			case D_EVENT_ENTITY:
			case D_EVENT_ENTITY_NULL:
			case D_EVENT_OBJECT:
				typeName = "Object";
				break;
			case D_EVENT_BOOLEAN:
				typeName = "Boolean";
				break;
			case D_EVENT_HANDLE:
				typeName = "Integer";
				break;
			case D_EVENT_INTEGER: // integers get returned as floats
			case D_EVENT_FLOAT:
				typeName = "Float";
				break;
			case D_EVENT_VOID:
				typeName = NULL;
				break;
			default:
				assert( false );
				typeName = "Unknown";
				break;
		}

		if ( typeName != NULL ) {
			PrintTabs( cppFile );
			cppFile->Printf( "return compilerInterface->GetReturned%s();\n", typeName );
		}

		tabCount--;
		cppFile->Printf( "}\n\n" );
	}

	fileSystem->CloseFile( cppFile );
}

void sdScriptExporter::WriteEventCalls( void ) {
	idList< const idTypeDef* > dependencies;

	idFile* headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_EVENTCALLS ], "fs_devpath" );

	headerFile->Printf( "#ifndef __GENERATED_EVENTCALLS_H__\n" );
	headerFile->Printf( "#define __GENERATED_EVENTCALLS_H__\n\n" );

	for ( int i = 0; i < eventCalls.Num(); i++ ) {
		callDef_t* t = eventCalls[ i ];
		headerFile->Printf( "%s EventCall%d( const sdCompiledScript_Event& _event, %s* _obj", BuildFieldName( t->returnType ), i, BASE_COMPILED_SCRIPT_CLASS_ALLOCATE );
		if ( t->parms.Num() > 0 ) {
			headerFile->Printf( ", " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				headerFile->Printf( "const %s& parm%d", BuildFieldName( t->parms[ j ] ), j );
				if ( j != t->parms.Num() - 1 ) {
					headerFile->Printf( ", " );
				}
			}
		}
		headerFile->Printf( " );\n" );



		AddDependency( t->returnType, dependencies );
		for ( int j = 0; j < t->parms.Num(); j++ ) {
			AddDependency( t->parms[ j ], dependencies );
		}
	}

	headerFile->Printf( "\n#endif // __GENERATED_EVENTCALLS_H__\n" );

	fileSystem->CloseFile( headerFile );

	idFile* cppFile = fileSystem->OpenFileWrite( g_fixedCppFileName[ CS_FFC_EVENTCALLS ], "fs_devpath" );

	cppFile->Printf( "#include \"Precompiled.h\"\n" );
	cppFile->Printf( "#pragma hdrstop\n\n" );
	cppFile->Printf( "#include \"CompiledScript_Types.h\"\n" );
	cppFile->Printf( "#include \"CompiledScript_Event.h\"\n" );

	if ( dependencies.Num() > 0 ) {
		cppFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			cppFile->Printf( "class %s;\n", BuildClassName( dependencies[ i ] ) );
		}

		cppFile->Printf( "\n" );
	}

	for ( int i = 0; i < eventCalls.Num(); i++ ) {
		callDef_t* t = eventCalls[ i ];

		cppFile->Printf( "%s EventCall%d( const sdCompiledScript_Event& _event, %s* _obj", BuildFieldName( t->returnType ), i, BASE_COMPILED_SCRIPT_CLASS_ALLOCATE );
		if ( t->parms.Num() > 0 ) {
			cppFile->Printf( ", " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				cppFile->Printf( "const %s& parm%d", BuildFieldName( t->parms[ j ] ), j );
				if ( j != t->parms.Num() - 1 ) {
					cppFile->Printf( ", " );
				}
			}
		}
		cppFile->Printf( " ) {\n" );
		tabCount++;

		if ( t->parms.Num() > 0 ) {
			PrintTabs( cppFile );
			cppFile->Printf( "UINT_PTR data[ %d ];\n", t->parms.Num() );

			for ( int j = 0; j < t->parms.Num(); j++ ) {
				const char* eventDataTypeName = NULL;
				switch ( t->event->GetArgFormat()[ j ] ) {
					case D_EVENT_VECTOR:
						eventDataTypeName = "Vector";
						break;
					case D_EVENT_STRING:
						eventDataTypeName = "String";
						break;
					case D_EVENT_WSTRING:
						eventDataTypeName = "WString";
						break;
					case D_EVENT_ENTITY:
					case D_EVENT_ENTITY_NULL:
						eventDataTypeName = "Entity";
						break;
					case D_EVENT_OBJECT:
						eventDataTypeName = "Object";
						break;
					case D_EVENT_BOOLEAN:
						eventDataTypeName = "Boolean";
						break;
					case D_EVENT_FLOAT:
						eventDataTypeName = "Float";
						break;
					case D_EVENT_INTEGER:
						eventDataTypeName = "Integer";
						break;
					case D_EVENT_HANDLE:
						eventDataTypeName = "Handle";
						break;
					default:
						assert( false );
						eventDataTypeName = "Unknown";
						break;
				}

				if ( t->event->GetArgFormat()[ j ] == D_EVENT_ENTITY ) {
					PrintTabs( cppFile );
					cppFile->Printf( "if ( !parm%d.To%sData( data[ %d ] ) ) {\n", j, eventDataTypeName, j );

					tabCount++;

					PrintTabs( cppFile );
					cppFile->Printf( "// TODO: Print warning\n" );

					PrintTabs( cppFile );
					if ( t->returnType->Type() != ev_void ) {
						cppFile->Printf( "return %s();\n", BuildFieldName( t->returnType ) );
					} else {
						cppFile->Printf( "return;\n" );
					}

					tabCount--;
					PrintTabs( cppFile );
					cppFile->Printf( "}\n" );
				} else {
					PrintTabs( cppFile );
					cppFile->Printf( "parm%d.To%sData( data[ %d ] );\n", j, eventDataTypeName, j );
				}
			}
		}

		PrintTabs( cppFile );
		cppFile->Printf( "compilerInterface->EventCall( _event.GetEvent(), _obj, %s );\n", t->parms.Num() > 0 ? "data" : "NULL" );

		const char* typeName;
		switch ( t->event->GetReturnType() ) {
			case D_EVENT_VECTOR:
				typeName = "Vector";
				break;
			case D_EVENT_STRING:
				typeName = "String";
				break;
			case D_EVENT_WSTRING:
				typeName = "WString";
				break;
			case D_EVENT_ENTITY:
			case D_EVENT_ENTITY_NULL:
			case D_EVENT_OBJECT:
				typeName = "Object";
				break;
			case D_EVENT_BOOLEAN:
				typeName = "Boolean";
				break;
			case D_EVENT_HANDLE:
				typeName = "Integer";
				break;
			case D_EVENT_INTEGER: // integers get returned as floats
			case D_EVENT_FLOAT:
				typeName = "Float";
				break;
			case D_EVENT_VOID:
				typeName = NULL;
				break;
			default:
				assert( false );
				typeName = "Unknown";
				break;
		}

		if ( typeName != NULL ) {
			PrintTabs( cppFile );
			cppFile->Printf( "return compilerInterface->GetReturned%s();\n", typeName );
		}

		tabCount--;
		cppFile->Printf( "}\n\n" );
	}

	fileSystem->CloseFile( cppFile );
}

void sdScriptExporter::WriteClassFunctionWrappers( void ) {
	idList< const idTypeDef* > dependencies;

	idFile* headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_FUNCTIONWRAPPERS ], "fs_devpath" );

	headerFile->Printf( "#ifndef __GENERATED_CLASSFUNCTIONWRAPPERS_H__\n" );
	headerFile->Printf( "#define __GENERATED_CLASSFUNCTIONWRAPPERS_H__\n\n" );

	for ( int i = 0; i < externalClassCalls.Num(); i++ ) {
		callDef_t* t = externalClassCalls[ i ];
		headerFile->Printf( "void ClassFunctionWrapper%d( %s* obj, classFunctionCallback_t callback, const byte* data );\n", i, BASE_COMPILED_SCRIPT_CLASS_ALLOCATE );

		AddDependency( t->returnType, dependencies );
		for ( int j = 0; j < t->parms.Num(); j++ ) {
			AddDependency( t->parms[ j ], dependencies );
		}
	}

	headerFile->Printf( "\n#endif // __GENERATED_CLASSFUNCTIONWRAPPERS_H__\n" );

	fileSystem->CloseFile( headerFile );

	idFile* cppFile = fileSystem->OpenFileWrite( g_fixedCppFileName[ CS_FFC_FUNCTIONWRAPPERS ], "fs_devpath" );

	cppFile->Printf( "#include \"Precompiled.h\"\n" );
	cppFile->Printf( "#pragma hdrstop\n\n" );
	cppFile->Printf( "#include \"CompiledScript_Types.h\"\n" );

	if ( dependencies.Num() > 0 ) {
		cppFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			cppFile->Printf( "#include \"%s\"\n", BuildClassHeaderName( dependencies[ i ], false ) );
		}

		cppFile->Printf( "\n" );
	}

	for ( int i = 0; i < externalClassCalls.Num(); i++ ) {
		callDef_t* t = externalClassCalls[ i ];

		cppFile->Printf( "void ClassFunctionWrapper%d( %s* obj, classFunctionCallback_t callback, const byte* data ) {\n", i, BASE_COMPILED_SCRIPT_CLASS_ALLOCATE );
		tabCount++;

		PrintTabs( cppFile );
		cppFile->Printf( "typedef %s ( %s::*internalCallback_t )(", BuildFieldName( t->returnType ), BASE_COMPILED_SCRIPT_CLASS_ALLOCATE );
		if ( t->parms.Num() > 0 ) {
			cppFile->Printf( " " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				cppFile->Printf( "%s parm%d", BuildFieldName( t->parms[ j ] ), j );
				if ( j != t->parms.Num() - 1 ) {
					cppFile->Printf( ", " );
				}
			}
			cppFile->Printf( " " );
		}
		cppFile->Printf( ");\n" );

		if ( t->parms.Num() > 0 ) {
			PrintTabs( cppFile );
			cppFile->Printf( "const byte* p = data;\n" );

			for ( int j = 0; j < t->parms.Num(); j++ ) {
				PrintTabs( cppFile );
				cppFile->Printf( "%s parm%d;\n", BuildFieldName( t->parms[ j ] ), j );

				PrintTabs( cppFile );
				cppFile->Printf( "p = parm%d.FromData( p );\n", j );
			}
		}

		PrintTabs( cppFile );
		cppFile->Printf( "internalCallback_t internalCallback = ( internalCallback_t )callback;\n" );
	
		PrintTabs( cppFile );
		if ( t->returnType->Type() != ev_void ) {
			cppFile->Printf( "compilerInterface->Return" );
			switch ( t->returnType->Type() ) {
				case ev_float:
					cppFile->Printf( "Float" );
					break;
				case ev_boolean:
					cppFile->Printf( "Boolean" );
					break;
				case ev_vector:
					cppFile->Printf( "Vector" );
					break;
				case ev_object:
					cppFile->Printf( "Object" );
					break;
				case ev_string:
					cppFile->Printf( "String" );
					break;
				case ev_wstring:
					cppFile->Printf( "WString" );
					break;
				default:
					assert( false );
					break;
			}
			cppFile->Printf( "( " );
		}
		cppFile->Printf( "( *obj.*internalCallback )(" );
		if ( t->parms.Num() > 0 ) {
			cppFile->Printf( " " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				cppFile->Printf( "parm%d", j );
				if ( j != t->parms.Num() - 1 ) {
					cppFile->Printf( ", " );
				}
			}
			cppFile->Printf( " " );
		}
		if ( t->returnType->Type() != ev_void ) {
			cppFile->Printf( " ) " );
		}
		cppFile->Printf( ");\n" );

		tabCount--;
		cppFile->Printf( "}\n\n" );
	}

	fileSystem->CloseFile( cppFile );
}

void sdScriptExporter::WriteFunctionWrappers( idFile* cppFile ) {
	for ( int i = 0; i < externalFunctionCalls.Num(); i++ ) {
		callDef_t* t = externalFunctionCalls[ i ];

		cppFile->Printf( "void GlobalFunctionWrapper%d( functionCallback_t callback, const byte* data ) {\n", i );
		tabCount++;

		PrintTabs( cppFile );
		cppFile->Printf( "typedef %s ( *internalCallback_t )(", BuildFieldName( t->returnType ) );
		if ( t->parms.Num() > 0 ) {
			cppFile->Printf( " " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				cppFile->Printf( "%s parm%d", BuildFieldName( t->parms[ j ] ), j );
				if ( j != t->parms.Num() - 1 ) {
					cppFile->Printf( ", " );
				}
			}
			cppFile->Printf( " " );
		}
		cppFile->Printf( ");\n" );

		if ( t->parms.Num() > 0 ) {
			PrintTabs( cppFile );
			cppFile->Printf( "const byte* p = data;\n" );

			for ( int j = 0; j < t->parms.Num(); j++ ) {
				PrintTabs( cppFile );
				cppFile->Printf( "%s parm%d;\n", BuildFieldName( t->parms[ j ] ), j );

				PrintTabs( cppFile );
				cppFile->Printf( "p = parm%d.FromData( p );\n", j );
			}
		}

		PrintTabs( cppFile );
		cppFile->Printf( "internalCallback_t internalCallback = ( internalCallback_t )callback;\n" );
	
		PrintTabs( cppFile );
		if ( t->returnType->Type() != ev_void ) {
			cppFile->Printf( "compilerInterface->Return" );
			switch ( t->returnType->Type() ) {
				case ev_float:
					cppFile->Printf( "Float" );
					break;
				case ev_boolean:
					cppFile->Printf( "Boolean" );
					break;
				case ev_vector:
					cppFile->Printf( "Vector" );
					break;
				case ev_object:
					cppFile->Printf( "Object" );
					break;
				case ev_string:
					cppFile->Printf( "String" );
					break;
				case ev_wstring:
					cppFile->Printf( "WString" );
					break;
				default:
					assert( false );
					break;
			}
			cppFile->Printf( "( " );
		}
		cppFile->Printf( "( *internalCallback )(" );
		if ( t->parms.Num() > 0 ) {
			cppFile->Printf( " " );
			for ( int j = 0; j < t->parms.Num(); j++ ) {
				cppFile->Printf( "parm%d", j );
				if ( j != t->parms.Num() - 1 ) {
					cppFile->Printf( ", " );
				}
			}
			cppFile->Printf( " " );
		}
		if ( t->returnType->Type() != ev_void ) {
			cppFile->Printf( " ) " );
		}
		cppFile->Printf( ");\n" );

		tabCount--;
		cppFile->Printf( "}\n\n" );
	}
}

void sdScriptExporter::WriteVirtualFunctions( void ) {
	idFile* headerFile;
	
	headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_VIRTUALFUNCTIONS ], "fs_devpath" );

	idList< const idTypeDef* > dependencies;

	for ( int i = 0; i < virtualFunctions.Num(); i++ ) {
		functionDef_t funcDef;
		funcDef.function = virtualFunctions[ i ]->def->value.functionPtr;
		funcDef.isVirtual = true;

		AddDependency( funcDef.function, dependencies, false );
		
		WriteFunctionStub( headerFile, funcDef, NULL, 0 );
		if ( funcDef.function->type->ReturnType()->Type() != ev_void ) {
			headerFile->Printf( "{ return %s(); }\n", BuildFieldName( funcDef.function->type->ReturnType() ) );
		} else {
			headerFile->Printf( "{ ; }\n" );
		}
	}

	fileSystem->CloseFile( headerFile );

	headerFile = fileSystem->OpenFileWrite( g_fixedHFileName[ CS_FFH_VIRTUALFUNCTIONDEPENDANCIES ], "fs_devpath" );

	if ( dependencies.Num() > 0 ) {
		headerFile->Printf( "// dependencies\n" );
		for ( int i = 0; i < dependencies.Num(); i++ ) {
			headerFile->Printf( "class %s;\n", BuildClassName( dependencies[ i ] ) );
		}

		headerFile->Printf( "\n" );
	}

	fileSystem->CloseFile( headerFile );
}

void sdScriptExporter::WriteNamespaceClasses( const namespaceDef_t* ns ) {
	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		WriteNamespaceClasses( ns->namespaces[ i ] );
	}

	for ( int i = 1; i < ns->classes.Num(); i++ ) {
		WriteClass( ns, *ns->classes[ i ] );
	}
}

void sdScriptExporter::ClearNamespace( namespaceDef_t* ns ) {
	ns->classes.DeleteContents( true );
	ns->globalVars.Clear();

	for ( int i = 0; i < ns->namespaces.Num(); i++ ) {
		ClearNamespace( ns->namespaces[ i ] );
	}
	ns->namespaces.DeleteContents( true );
	ns->parentNamespace = NULL;
	ns->name = "";
}

void sdScriptExporter::Clear( bool finished ) {
	ClearNamespace( &globalNameSpace );
	events.Clear();
	currentNameSpace = &globalNameSpace;
	virtualFunctions.Clear();
	if ( !finished ) {
		RegisterClass( NULL, NULL );
	}

	externalClassCalls.DeleteContents( true );
	externalFunctionCalls.DeleteContents( true );
	sysCalls.DeleteContents( true );
	eventCalls.DeleteContents( true );
	constants.Clear();
}

void sdScriptExporter::RegisterVirtualFunction( idTypeDef* type ) {
	virtualFunctions.Alloc() = type;
}

void sdScriptExporter::RegisterClass( idTypeDef* type, idTypeDef* superType ) {
	objectDef_t* def = new objectDef_t;
	currentNameSpace->classes.Alloc() = def;

	def->type = type;

	if ( superType != NULL && superType != &type_object ) {
		objectDef_t* superClass = FindClass( currentNameSpace, superType );
		assert( superClass != NULL );
		def->superType	= superClass;
		def->baseOffset = superClass->baseOffset;
		for ( int i = 0; i < superClass->fields.Num(); i++ ) {
			def->baseOffset += FieldSize( superClass->fields[ i ]->TypeDef()->FieldType() );
		}
	} else {
		def->superType	= NULL;
		def->baseOffset	= 0;
	}
}

sdScriptExporter::stackVar_t* sdScriptExporter::FindGlobal( const namespaceDef_t* ns, const idVarDef* var ) {
	int varNum = GetVarDefNum( var );
	assert( varNum != -1 );

	for ( int i = 0; i < ns->globalVars.Num(); i++ ) {
		if ( ns->globalVars[ i ]->offset != varNum ) {
			continue;
		}

		if ( ns->globalVars[ i ]->type->Type() == ev_vector ) {
			if ( var->Type() == ev_float ) {
				assert( false );
			}
		}

		return ns->globalVars[ i ];
	}

	if ( ns->parentNamespace != NULL ) {
		return FindGlobal( ns->parentNamespace, var );
	}

	return NULL;
}

sdScriptExporter::objectDef_t* sdScriptExporter::FindClass( const namespaceDef_t* ns, const idTypeDef* type ) {
	for ( int i = 0; i < ns->classes.Num(); i++ ) {
		if ( ns->classes[ i ]->type == type ) {
			return ns->classes[ i ];
		}
	}

	if ( ns->parentNamespace != NULL ) {
		return FindClass( ns->parentNamespace, type );
	}

	gameLocal.Error( "sdScriptExporter::FindClass Unknown Type '%s'", type->Name() );

	return NULL;
}

void sdScriptExporter::RegisterClassFunction( idTypeDef* type, const function_t* function ) {
	objectDef_t* o = FindClass( currentNameSpace, type );

	functionDef_t& f = o->functions.Alloc();
	f.function = function;
	f.isVirtual = false;

	bool isSpecial = false;
	const char* specialFuncs[] = {
		"init",
		"preinit",
		"destroy",
		"syncFields",
		NULL
	};


	bool makeVirtual = false;

	if ( function->virtualIndex != -1 ) {
		makeVirtual = true;
	}

	if ( !makeVirtual ) {
		for ( int i = 0; specialFuncs[ i ] != NULL; i++ ) {
			if ( idStr::Cmp( specialFuncs[ i ], function->type->Name() ) == 0 ) {
				isSpecial = true;
				break;
			}
		}

		if ( !isSpecial ) {
			for ( objectDef_t* p = o->superType; p != NULL; p = p->superType ) {
				for ( int i = 0; i < p->functions.Num(); i++ ) {
					if ( idStr::Cmp( p->functions[ i ].function->type->Name(), function->type->Name() ) != 0 ) {
						continue;
					}

					makeVirtual = true;
					goto end;
				}
			}
		}
	}

end:
	if ( makeVirtual ) {
		f.isVirtual = true;

		for ( objectDef_t* p = o->superType; p != NULL; p = p->superType ) {
			for ( int i = 0; i < p->functions.Num(); i++ ) {
				if ( idStr::Cmp( p->functions[ i ].function->type->Name(), function->type->Name() ) != 0 ) {
					continue;
				}

				p->functions[ i ].isVirtual = true;
				break;
			}
		}
	}
}

void sdScriptExporter::RegisterClassFunctionVariable( idTypeDef* type, const function_t* function, const idVarDef* var ) {
	objectDef_t* o = FindClass( currentNameSpace, type );

	for ( int i = 0; i < o->functions.Num(); i++ ) {
		if ( o->functions[ i ].function != function ) {
			continue;
		}

		o->functions[ i ].variables.Alloc() = var;
		break;
	}
}

void sdScriptExporter::RegisterClassField( idTypeDef* type, idVarDef* field ) {
	objectDef_t* o = FindClass( currentNameSpace, type );

	o->fields.Alloc() = field;
}




sdFunctionCompileState::sdFunctionCompileState( const sdScriptExporter::functionDef_t* _func, const sdScriptExporter::namespaceDef_t* _nameSpace, idFile* _output, int initialTabCount, idProgram* _program ) {
	fileOutput = _output;
	func = _func;
	tabCount = initialTabCount;
	returnVar = NULL;
	skipNextStatement = false;
	nameSpace = _nameSpace;
	program = _program;
	output = fileSystem->OpenMemoryFile( "sdFunctionCompileState output" );
	variables = fileSystem->OpenMemoryFile( "sdFunctionCompileState variables" );
}

sdFunctionCompileState::~sdFunctionCompileState() {
	stackVars.DeleteContents( true );
	fileSystem->CloseFile( output );
	fileSystem->CloseFile( variables );
}

void sdFunctionCompileState::Run( void ) {
	isSpecial = false;
	if ( idStr::Cmp( func->function->type->Name(), "destroy" ) == 0 ) {
		isSpecial = true;
	}

	AllocBaseStackVars();
	ScanForLabels();
	ScanOpCodes();

	fileOutput->Write( variables->GetDataPtr(), variables->Tell() );
	fileOutput->Write( output->GetDataPtr(), output->Tell() );
}

void sdFunctionCompileState::ScanForLabels( void ) {
	int lastStatement = func->function->firstStatement + func->function->numStatements;
	for ( int i = func->function->firstStatement; i < lastStatement; i++ ) {
		statement_t& s = program->GetStatement( i );
		switch ( s.op ) {
			case OP_IF:
			case OP_IFNOT: {
				assert( s.b->settings.initialized == idVarDef::initializedConstant && s.b->TypeDef()->Type() == ev_jumpoffset );

				int address = i + s.b->value.jumpOffset;
				labels.AddUnique( address );
				break;
			}
			case OP_GOTO: {
				assert( s.a->settings.initialized == idVarDef::initializedConstant && s.a->TypeDef()->Type() == ev_jumpoffset );

				int address = i + s.a->value.jumpOffset;
				labels.AddUnique( address );
				break;
			}
		}
	}
}

void sdFunctionCompileState::ScanOpCodes( void ) {
	for ( int i = func->function->firstStatement; i < func->function->firstStatement + func->function->numStatements; i++ ) {
		if ( skipNextStatement ) {
			skipNextStatement = false;
			continue;
		}
		ScanOpCode( i );
	}

	assert( compileStack.Num() == 0 );
}

bool sdFunctionCompileState::IsReturn( const idVarDef* var ) {
	return var == program->returnDef || var == program->returnStringDef;
}

sdScriptExporter::stackVar_t* sdFunctionCompileState::FindStackVar( const idVarDef* var ) {
	for ( int i = 0; i < stackVars.Num(); i++ ) {
		if ( stackVars[ i ]->offset == var->value.stackOffset && stackVars[ i ]->type->Type() == var->Type() ) {
			return stackVars[ i ];
		}
	}

	return NULL;
}

void sdFunctionCompileState::AllocateSubVars( const sdScriptExporter::stackVar_t& var ) {
	if ( var.type->Type() != ev_vector ) {
		return;
	}

	sdScriptExporter::stackVar_t& newVar1 = *new sdScriptExporter::stackVar_t;
	sdScriptExporter::stackVar_t& newVar2 = *new sdScriptExporter::stackVar_t;
	sdScriptExporter::stackVar_t& newVar3 = *new sdScriptExporter::stackVar_t;
	
	stackVars.Alloc() = &newVar1;
	stackVars.Alloc() = &newVar2;
	stackVars.Alloc() = &newVar3;

	newVar1.type		= &type_float;
	newVar1.offset		= var.offset + type_float.Size() * 0;
	newVar1.name		= var.name + ".GetX()";
	newVar1.allocated	= true;

	newVar2.type		= &type_float;
	newVar2.offset		= var.offset + type_float.Size() * 1;
	newVar2.name		= var.name + ".GetY()";
	newVar2.allocated	= true;

	newVar3.type		= &type_float;
	newVar3.offset		= var.offset + type_float.Size() * 2;
	newVar3.name		= var.name + ".GetZ()";
	newVar3.allocated	= true;
}

void sdFunctionCompileState::AllocBaseStackVars( void ) {
	for ( int i = 0; i < func->variables.Num(); i++ ) {
		const idVarDef* var = func->variables[ i ];

		sdScriptExporter::stackVar_t& newVar	= *new sdScriptExporter::stackVar_t;
		newVar.type			= var->TypeDef();
		newVar.offset		= var->value.stackOffset;
		newVar.name			= var->Name();
		newVar.allocated	= false;

		stackVars.Alloc() = &newVar;

		InitStackVar( newVar );

		AllocateSubVars( newVar );
	}

	int base = func->function->def->scope->Type() == ev_namespace ? 0 : 1;

	int localOffset = 0;
	for ( int i = 0; i < func->function->type->NumParameters(); i++ ) {
		idTypeDef* type = func->function->type->GetParmType( i );

		sdScriptExporter::stackVar_t& newVar	= *new sdScriptExporter::stackVar_t;
		newVar.type			= type;
		newVar.offset		= localOffset;
		newVar.name			= func->function->type->GetParmName( i );
		newVar.allocated	= true;

		stackVars.Alloc() = &newVar;

		AllocateSubVars( newVar );

		localOffset += sdScriptExporter::FieldSize( type );
	}
}

sdScriptExporter::stackVar_t& sdFunctionCompileState::AllocStackVar( const idVarDef* var ) {
	sdScriptExporter::stackVar_t* s = FindStackVar( var );
	if ( s != NULL ) {
		return *s;
	}

	if ( idStr::Cmp( var->Name(), "<RESULT>" ) != 0 ) {
		assert( false );
	}

	assert( var->TypeDef() != NULL );

	sdScriptExporter::stackVar_t& newVar	= *new sdScriptExporter::stackVar_t;
	newVar.type			= var->TypeDef();
	newVar.offset		= var->value.stackOffset;
	newVar.name			= va( "__stackVar%d", stackVars.Num() );
	newVar.allocated	= false;

	stackVars.Alloc() = &newVar;

	return newVar;
}

void sdFunctionCompileState::PrintField( idTypeDef* type, idVarDef* ptr ) {
	const char* fieldName = program->scriptExporter.FindFieldName( nameSpace, type, ptr );
	if ( fieldName == NULL ) {
		output->Printf( "[unknown field]" );
		return;
	}

	output->Printf( fieldName );
}

void sdFunctionCompileState::InitStackVar( sdScriptExporter::stackVar_t& var ) {
	if ( var.allocated ) {
		assert( false );
		return;
	}
	variables->Printf( "\t%s %s;\n", program->scriptExporter.BuildFieldName( var.type, ev_error /* defaultType */ ), var.name.c_str() );
	var.allocated = true;
}

void sdFunctionCompileState::CheckVariable( const idVarDef* var, etype_t defaultType ) {
	if ( var->settings.initialized == var->initializedConstant || var->settings.initialized == var->initializedVariable ) {
		return;
	}

	if ( var->settings.initialized == var->stackVariable ) {
		if ( func->function->def->scope->Type() != ev_namespace ) {
			if ( idStr::Cmp( var->Name(), "self" ) == 0 ) {
				return;
			}
		}

		sdScriptExporter::stackVar_t& stackVar = AllocStackVar( var );
		if ( !stackVar.allocated ) {
			InitStackVar( stackVar );
		}
		return;
	}
}

void sdFunctionCompileState::GetVariableName( const idVarDef* var, etype_t expectedType, idStr& name ) {
	if ( var->settings.initialized == var->initializedConstant ) {
		idTypeDef* type = var->TypeDef();

		int index = program->scriptExporter.AllocConstant( var );
		if ( index == -1 ) {
			name = "[unknown constant]";
			assert( false );
		} else {
			name = program->scriptExporter.BuildConstantName( index );
		}
		return;
	}

	if ( var->settings.initialized == var->stackVariable ) {
		if ( func->function->def->scope->Type() != ev_namespace ) {
			if ( idStr::Cmp( var->Name(), "self" ) == 0 ) {
				name = "this";
				return;
			}
		}

		sdScriptExporter::stackVar_t& stackVar = AllocStackVar( var );

//		assert( stackVar.allocated );
		name = stackVar.name.c_str();
		return;
	}

	sdScriptExporter::stackVar_t* stackVar = program->scriptExporter.FindGlobal( nameSpace, var );
	assert( stackVar != NULL );
	name = program->scriptExporter.BuildGlobalName( *stackVar );
}

void sdFunctionCompileState::PrintVariable( const idVarDef* var, etype_t expectedType ) {
	idStr name;
	GetVariableName( var, expectedType, name );
	output->Printf( "%s", name.c_str() );
}

void sdFunctionCompileState::PrintTabs( void ) {
	for ( int t = tabCount; t > 0; t-- ) {
		output->Printf( "\t" );
	}
}

bool sdFunctionCompileState::LookAheadStore( int statement ) {
	int lookahead = statement + 1;
	if ( lookahead >= program->NumStatements() ) {
		return false;
	}

	statement_t& s = program->GetStatement( lookahead );
	switch ( s.op ) {
		case OP_STORE_F:
		case OP_STORE_V:
		case OP_STORE_S:
		case OP_STORE_W:
		case OP_STORE_BOOL:
		case OP_STORE_OBJ:
		case OP_STORE_FTOS:
		case OP_STORE_BTOS:
		case OP_STORE_VTOS:
		case OP_STORE_FTOBOOL:
		case OP_STORE_BOOLTOF:
		case OP_STOREP_F:
		case OP_STOREP_V:
		case OP_STOREP_S:
		case OP_STOREP_W:
		case OP_STOREP_FLD:
		case OP_STOREP_BOOL:
		case OP_STOREP_OBJ:
		case OP_STOREP_FTOS:
		case OP_STOREP_BTOS:
		case OP_STOREP_VTOS:
		case OP_STOREP_FTOBOOL:
		case OP_STOREP_BOOLTOF: {
			if ( IsReturn( s.a ) ) {
				ScanOpCode( lookahead );
				return true;
			}
			return false;
		}
		default:
			return false;
	}
}

#define TYPE_OF( TYPE ) idCompiler::opcodes[ s.op ].TYPE->Type()

void sdFunctionCompileState::ScanOpCode( int statement ) {
//	output->Printf( "//" );
//	program->DisassembleStatement( output, statement );

	int labelIndex = labels.FindIndex( statement );
	if ( labelIndex != -1 ) {
		output->Printf( "label%d:\n", labelIndex );
	}

	statement_t& s = program->GetStatement( statement );

	switch ( s.op ) {
		case OP_PUSH_F:
		case OP_PUSH_V:
		case OP_PUSH_S:
		case OP_PUSH_W:
		case OP_PUSH_OBJ:
		case OP_PUSH_FTOS:
		case OP_PUSH_FTOW:
		case OP_PUSH_BTOF:
		case OP_PUSH_FTOB:
		case OP_PUSH_VTOS:
		case OP_PUSH_BTOS: {
			compileStack.Alloc() = s.a;
			return;
		}

		case OP_EQ_B:
		case OP_EQ_F:
		case OP_EQ_V:
		case OP_EQ_S:
		case OP_EQ_W:
		case OP_EQ_O: {
			CheckVariable( s.c, TYPE_OF( type_c ) );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " == " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_ADDRESS: {
			CheckVariable( s.a );

			idStr varName;
			GetVariableName( s.a, ev_object, varName );
			varName += va( "->%s", program->scriptExporter.FindFieldName( nameSpace, s.a->TypeDef(), s.b ) );

			sdScriptExporter::stackVar_t* stackVar = FindStackVar( s.c );
			if ( stackVar == NULL ) {
				stackVar = new sdScriptExporter::stackVar_t;
				stackVars.Alloc() = stackVar;
			}

			stackVar->allocated = true;
			stackVar->name = varName;
			stackVar->offset = s.c->value.stackOffset;
			stackVar->type = s.c->TypeDef();

			return;
		}

		case OP_UAND_F: {
			CheckVariable( s.b, idCompiler::opcodes[ s.op ].type_b->Type() );

			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( " &= " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_UOR_F: {
			CheckVariable( s.b, idCompiler::opcodes[ s.op ].type_b->Type() );

			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( " |= " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_BITOR: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " | " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_BITAND: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " & " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_OR:
		case OP_OR_BOOLBOOL:
		case OP_OR_FBOOL:
		case OP_OR_BOOLF: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " || " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_AND:
		case OP_AND_BOOLBOOL:
		case OP_AND_BOOLF:
		case OP_AND_FBOOL: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " && " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_NE_B:
		case OP_NE_F:
		case OP_NE_V:
		case OP_NE_S:
		case OP_NE_W:
		case OP_NE_O: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " != " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_NEG_V:
		case OP_NEG_F: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = -" );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_INT_F: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ".ToInt();\n" );
			return;
		}

		case OP_DIV_F: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " / " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_MUL_V:
		case OP_MUL_VF:
		case OP_MUL_FV:
		case OP_MUL_F: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " * " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_ADD_F:
		case OP_ADD_V:
		case OP_ADD_S:
		case OP_ADD_FS:
		case OP_ADD_SF:
		case OP_ADD_VS:
		case OP_ADD_SV: {
			CheckVariable( s.a, idCompiler::opcodes[ s.op ].type_a->Type() );
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " + " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_SUB_F:
		case OP_SUB_V: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " - " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_LE: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " <= " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_GE: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " >= " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_GT: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " > " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_MOD_F: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " %% " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_USUB_V:
		case OP_USUB_F: {
			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( " -= " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_UMUL_F:
		case OP_UMUL_V: {
			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( " *= " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_UDIV_F:
		case OP_UDIV_V: {
			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( " /= " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_UADD_F:
		case OP_UADD_V: {
			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( " += " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_UINCP_F: {
			CheckVariable( s.a );

			PrintTabs();
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( "->%s++;\n", program->scriptExporter.FindFieldName( nameSpace, s.a->TypeDef(), s.b ) );
			return;
		}

		case OP_UDECP_F: {
			CheckVariable( s.a );

			PrintTabs();
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( "->%s--;\n", program->scriptExporter.FindFieldName( nameSpace, s.a->TypeDef(), s.b ) );
			return;
		}

		case OP_UINC_F: {
			PrintTabs();
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( "++;\n" );
			return;
		}

		case OP_UDEC_F: {
			PrintTabs();
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( "--;\n" );
			return;
		}

		case OP_LT: {
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " < " );
			PrintVariable( s.b, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_NOT_BOOL:
		case OP_NOT_F:
		case OP_NOT_V:
		case OP_NOT_S:
		case OP_NOT_OBJ: {
			CheckVariable( s.a );
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = !" );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_IFNOT: {
			PrintTabs();
			output->Printf( "if ( !" );
			PrintVariable( s.a, ev_error );
			output->Printf( " ) {\n" );

			tabCount++;
			PrintTabs();
			output->Printf( "goto " );
			tabCount--;

			int index = labels.FindIndex( statement + s.b->value.jumpOffset );
			assert( index != -1 );
			output->Printf( "label%d;\n", index );
			PrintTabs();
			output->Printf( "}\n", index );
			return;
		}

		case OP_IF: {
			PrintTabs();
			output->Printf( "if ( " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " ) {\n" );

			tabCount++;
			PrintTabs();
			output->Printf( "goto " );
			tabCount--;

			int index = labels.FindIndex( statement + s.b->value.jumpOffset );
			assert( index != -1 );
			output->Printf( "label%d;\n", index );
			PrintTabs();
			output->Printf( "}\n", index );
			return;
		}

		case OP_GOTO: {
			int address = statement + s.a->value.jumpOffset;

			PrintTabs();
			output->Printf( "goto " );

			int index = labels.FindIndex( address );
			assert( index != -1 );
			output->Printf( "label%d;\n", index );
			return;
		}

		case OP_STORE_F:
		case OP_STORE_V:
		case OP_STORE_S:
		case OP_STORE_W:
		case OP_STORE_BOOL:
		case OP_STORE_FTOS:
		case OP_STORE_BTOS:
		case OP_STORE_VTOS:
		case OP_STORE_FTOBOOL:
		case OP_STORE_BOOLTOF:
		case OP_STOREP_F:
		case OP_STOREP_V:
		case OP_STOREP_S:
		case OP_STOREP_W:
		case OP_STOREP_FLD:
		case OP_STOREP_BOOL:
		case OP_STOREP_FTOS:
		case OP_STOREP_BTOS:
		case OP_STOREP_VTOS:
		case OP_STOREP_FTOBOOL:
		case OP_STOREP_BOOLTOF: {
			if ( IsReturn( s.b ) ) {
				returnVar = s.a;
				return;
			}

			CheckVariable( s.b, TYPE_OF( type_c ) );

			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_c ) );
			output->Printf( " = " );
			if ( IsReturn( s.a ) ) {
				return;
			}
			PrintVariable( s.a, TYPE_OF( type_b ) );
			output->Printf( ";\n" );
			return;
		}

		case OP_STORE_OBJ:
		case OP_STOREP_OBJ: {
			if ( IsReturn( s.b ) ) {
				returnVar = s.a;
				return;
			}

			CheckVariable( s.b, TYPE_OF( type_c ) );

			PrintTabs();
			PrintVariable( s.b, TYPE_OF( type_c ) );
			output->Printf( " = " );
			if ( IsReturn( s.a ) ) {
				return;
			}
			PrintVariable( s.a, TYPE_OF( type_b ) );

			idTypeDef* setType = s.b->TypeDef();
			idTypeDef* getType = s.a->TypeDef();

			if ( setType != getType && setType->Inherits( getType ) ) {
				output->Printf( "->Cast< %s >()", program->scriptExporter.BuildClassName( setType ) );
			}

			output->Printf( ";\n" );
			return;
		}

		case OP_EVENTCALL: {
			const function_t* f = s.a->value.functionPtr;

			int parms = f->type->NumParameters() + 1; // doesn't include self
			assert( compileStack.Num() >= parms );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - parms;
			if ( !skipNextStatement ) {
				PrintTabs();
			}

			int index = program->scriptExporter.RegisterEventCall( f );
			output->Printf( "EventCall%d", index );
			output->Printf( "( %s, ", program->scriptExporter.BuildEventName( f->type->Name() ) );
			for ( int i = 0; i < parms; i++ ) {
				etype_t expectedType;
				if ( i > 0 ) {
					expectedType = f->type->GetParmType( i - 1 )->Type();
				} else {
					expectedType = ev_error;
				}
				PrintVariable( compileStack[ base + i ], expectedType );
				if ( i != parms - 1 ) {
					output->Printf( ", " );
				}
			}
			output->Printf( " );\n" );

			compileStack.SetNum( compileStack.Num() - parms );
			return;
		}

		case OP_CALL: {
			const function_t* f = s.a->value.functionPtr;

			int parms = f->type->NumParameters();
			assert( compileStack.Num() >= parms );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - parms;
			if ( !skipNextStatement ) {
				PrintTabs();
			}
			output->Printf( "%s(", program->scriptExporter.BuildGlobalFunctionName( f ) );
			if ( parms > 0 ) {
				output->Printf( " " );
				for ( int i = 0; i < parms; i++ ) {
					PrintVariable( compileStack[ base + i ], f->type->GetParmType( i )->Type() );
					if ( i != parms - 1 ) {
						output->Printf( ", " );
					}
				}
				output->Printf( " " );
			}
			output->Printf( ");\n" );

			compileStack.SetNum( compileStack.Num() - parms );
			return;
		}

		case OP_VIRTUALEVENTCALL: {
			const function_t* f = s.c->value.functionPtr;

			int parms = f->type->NumParameters();
			assert( compileStack.Num() >= parms );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - ( parms + 1 );
			if ( !skipNextStatement ) {
				if ( g_compiledScriptSafety.GetBool() ) {
					PrintTabs();
					output->Printf( "if ( " );
					PrintVariable( compileStack[ base ], ev_error );
					output->Printf( " != NULL ) {\n" );
					tabCount++;
				}

				PrintTabs();
			} else {
				if ( g_compiledScriptSafety.GetBool() ) {
					PrintVariable( compileStack[ base ], ev_error );
					output->Printf( " == NULL ? %s() : ", program->scriptExporter.BuildFieldName( f->type->ReturnType() ) );
				}
			}

			PrintVariable( compileStack[ base ], ev_error );
			output->Printf( "->%s(", f->type->Name() );
			if ( parms > 0 ) {
				output->Printf( " " );
				for ( int i = 0; i < parms; i++ ) {
					PrintVariable( compileStack[ base + i + 1 ], f->type->GetParmType( i )->Type() );
					if ( i != parms - 1 ) {
						output->Printf( ", " );
					}
				}
				output->Printf( " " );
			}
			output->Printf( ");\n" );

			if ( g_compiledScriptSafety.GetBool() ) {
				if ( !skipNextStatement ) {
					tabCount--;
					PrintTabs();
					output->Printf( "}\n" );
				}
			}

			compileStack.SetNum( compileStack.Num() - ( parms + 1 ) );
			return;
		}

		case OP_OBJECTCALL: {
			const function_t* f = s.c->value.functionPtr;

			int parms = f->type->NumParameters();
			assert( compileStack.Num() >= parms );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - parms;
			if ( !skipNextStatement ) {
				if ( g_compiledScriptSafety.GetBool() ) {
					PrintTabs();
					output->Printf( "if ( " );
					PrintVariable( compileStack[ base ], ev_error );
					output->Printf( " != NULL ) {\n" );
					tabCount++;
				}

				PrintTabs();
			} else {
				if ( g_compiledScriptSafety.GetBool() ) {
					PrintVariable( compileStack[ base ], ev_error );
					output->Printf( " == NULL ? %s() : ", program->scriptExporter.BuildFieldName( f->type->ReturnType() ) );
				}
			}

			PrintVariable( compileStack[ base ], f->type->GetParmType( 0 )->Type() );
			output->Printf( "->%s(", f->type->Name() );
			if ( parms > 1 ) {
				output->Printf( " " );
				for ( int i = 1; i < parms; i++ ) {
					PrintVariable( compileStack[ base + i ], f->type->GetParmType( i )->Type() );
					if ( i != parms - 1 ) {
						output->Printf( ", " );
					}
				}
				output->Printf( " " );
			}
			output->Printf( ");\n" );

			if ( g_compiledScriptSafety.GetBool() ) {
				if ( !skipNextStatement ) {
					tabCount--;
					PrintTabs();
					output->Printf( "}\n" );
				}
			}

			compileStack.SetNum( compileStack.Num() - parms );
			return;
		}

		case OP_SYSCALL: {
			const function_t* f = s.a->value.functionPtr;

			if ( idStr::Cmp( f->GetName(), "wait" ) == 0 ) {
				int base = compileStack.Num() - 1;

				PrintTabs();
				output->Printf( "compilerInterface->Wait( " );

				PrintVariable( compileStack[ base ], f->type->GetParmType( 0 )->Type() );

				output->Printf( " );\n" );

				compileStack.SetNum( compileStack.Num() - 1 );

				return;
			}
			
			if ( idStr::Cmp( f->GetName(), "waitFrame" ) == 0 ) {
				PrintTabs();
				output->Printf( "compilerInterface->WaitFrame();\n" );
				return;
			}

			int parms = f->type->NumParameters();
			assert( compileStack.Num() >= parms );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - parms;
			if ( !skipNextStatement ) {
				PrintTabs();
			}

			int index = program->scriptExporter.RegisterSysCall( f );
			output->Printf( "SysCall%d", index );
			output->Printf( "( %s", program->scriptExporter.BuildEventName( f->type->Name() ) );
			if ( parms > 0 ) {
				output->Printf( ", " );
				for ( int i = 0; i < parms; i++ ) {
					PrintVariable( compileStack[ base + i ], f->type->GetParmType( i )->Type() );
					if ( i != parms - 1 ) {
						output->Printf( ", " );
					}
				}
			}
			output->Printf( " );\n" );

			compileStack.SetNum( compileStack.Num() - parms );
			return;
		}

		case OP_GUITHREAD:
		case OP_THREAD: {
			const function_t* f = s.a->value.functionPtr;

			int parms = f->type->NumParameters();
			assert( compileStack.Num() >= parms );

			sdScriptExporter::objectDef_t* o = program->scriptExporter.FindClass( &program->scriptExporter.GetGlobalNamespace(), NULL );
			int index = program->scriptExporter.FindThreadCall( *o, f, s.op == OP_GUITHREAD );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - parms;
			if ( !skipNextStatement ) {
				PrintTabs();
			}

			output->Printf( "::Create%sThread%d( %s", s.op == OP_GUITHREAD ? "Gui" : "", index, program->scriptExporter.BuildGlobalFunctionName( f ) );
			if ( parms > 0 ) {
				output->Printf( ", " );
				for ( int i = 0; i < parms; i++ ) {
					PrintVariable( compileStack[ base + i ], f->type->GetParmType( i )->Type() );
					if ( i != parms - 1 ) {
						output->Printf( ", " );
					}
				}
			}
			output->Printf( " );\n" );

			compileStack.SetNum( compileStack.Num() - parms );
			return;
		}

		case OP_GUIOBJTHREAD:
		case OP_OBJTHREAD: {
			const function_t* f = s.c->value.functionPtr;

			int parms = f->type->NumParameters();
			assert( compileStack.Num() >= parms );

			skipNextStatement = LookAheadStore( statement );

			int base = compileStack.Num() - parms;
			if ( !skipNextStatement ) {
				PrintTabs();
			}

			sdScriptExporter::objectDef_t* o = program->scriptExporter.FindClass( nameSpace, f->def->scope->TypeDef() );
			int index = program->scriptExporter.FindThreadCall( *o, f, s.op == OP_GUIOBJTHREAD );

			PrintVariable( compileStack[ base ], f->type->GetParmType( 0 )->Type() );

			output->Printf( "->Create%sThread%d( &%s::%s, \"%s\"", s.op == OP_GUIOBJTHREAD ? "Gui" : "", index, program->scriptExporter.BuildClassName( f->def->scope->TypeDef() ), f->type->Name(), f->type->Name() );
			if ( parms > 1 ) {
				output->Printf( ", " );
				for ( int i = 1; i < parms; i++ ) {
					PrintVariable( compileStack[ base + i ], f->type->GetParmType( i )->Type() );
					if ( i != parms - 1 ) {
						output->Printf( ", " );
					}
				}
			}
			output->Printf( " );\n" );

			compileStack.SetNum( compileStack.Num() - parms );
			return;
		}

		case OP_INDIRECT_F:
		case OP_INDIRECT_V:
		case OP_INDIRECT_S:
		case OP_INDIRECT_W:
		case OP_INDIRECT_BOOL:
		case OP_INDIRECT_OBJ: {
			CheckVariable( s.a );
			CheckVariable( s.c, idCompiler::opcodes[ s.op ].type_c->Type() );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( "->" );
			PrintField( s.a->TypeDef(), s.b );
			output->Printf( ";\n" );
			return;
		}

		case OP_RETURN: {
			idVarDef* returnValue = NULL;

			if ( func->function->type->ReturnType() != &type_void ) {
				if ( s.a != NULL && !IsReturn( s.a ) ) {
					returnValue = s.a;
				} else {
					if ( returnVar == NULL ) {
						// Gordon: this is likely a dodgy automatically added return, so just skip
						return;
					}
					returnValue = returnVar;
				}
			}

			returnVar = NULL;

			if ( isSpecial ) {
				const sdScriptExporter::objectDef_t* cls = program->scriptExporter.FindClass( nameSpace, func->function->def->scope->TypeDef() );
				for ( const sdScriptExporter::objectDef_t* o = cls->superType; o != NULL; o = o->superType ) {
					for ( int i = 0; i < o->functions.Num(); i++ ) {
						if ( idStr::Cmp( o->functions[ i ].function->type->Name(), func->function->type->Name() ) == 0 ) {
							PrintTabs();
							output->Printf( "%s::%s();\n", program->scriptExporter.BuildClassName( o->type ), func->function->type->Name() );
							goto done;
						}
					}
				}
			}

done:
			PrintTabs();
			output->Printf( "return" );
			if ( func->function->type->ReturnType() != &type_void ) {
				output->Printf( " " );
				PrintVariable( returnValue, func->function->type->ReturnType()->Type() );

				idTypeDef* setType = func->function->type->ReturnType();
				idTypeDef* getType = returnValue->TypeDef();

				if ( setType != getType && setType->Inherits( getType ) ) {
					output->Printf( "->Cast< %s >()", program->scriptExporter.BuildClassName( setType ) );
				}
			}
			output->Printf( ";\n" );
			return;
		}

		case OP_ALLOC_TYPE: {
			CheckVariable( s.c );

			PrintTabs();
			PrintVariable( s.c, TYPE_OF( type_c ) );
			output->Printf( " = compilerInterface->AllocObject( \"%s\" );\n", s.a->Name() );
			return;
		}
		case OP_FREE_TYPE: {
			PrintTabs();
			output->Printf( "compilerInterface->FreeObject( " );
			PrintVariable( s.a, TYPE_OF( type_a ) );
			output->Printf( " );\n" );
			return;
		}
	}

	assert( false );

	output->Printf( "//" );
	program->DisassembleStatement( output, statement );
}
