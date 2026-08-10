// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#pragma hdrstop

#include "../game/Game.h"
#include "../sys/sys_local.h"

/*
===============================================================================

	ETQW key input and binding contexts

	The public SDK contains the final interfaces, while the implementation was
	not released.  This reconstruction follows the class layouts and behavior
	recorded in the Microsoft PDB.

===============================================================================
*/

idHashIndex					idKeyInput::keysHash;
idList< idKey* >			idKeyInput::keys;
idList< sdBindContext* >		idKeyInput::bindContexts;
idHashIndex					idKeyInput::bindContextsHash;
bool						idKeyInput::overStrikeMode = false;

/*
================
sdKeyCommand
================
*/
sdKeyCommand::sdKeyCommand( void ) :
	action( -1 ),
	type( B_COMMAND ) {
}

void sdKeyCommand::Set( const char* _binding ) {
	binding = _binding != NULL ? _binding : "";
	FixupBind();
	cvarSystem->SetModifiedFlags( CVAR_ARCHIVE );
}

void sdKeyCommand::FixupBind( void ) {
	if ( game != NULL ) {
		type = game->SetupBinding( binding.c_str(), action );
	} else {
		action = -1;
		type = B_COMMAND;
	}
}

/*
================
sdKeyBind
================
*/
void sdKeyBind::ClearCommand( int modifier ) {
	if ( modifier == -1 ) {
		defaultCommand.Set( "" );
		return;
	}

	for ( int i = 0; i < modifierCommands.Num(); i++ ) {
		if ( modifierCommands[i].first == modifier ) {
			modifierCommands.RemoveIndex( i );
			return;
		}
	}
}

void sdKeyBind::SetCommand( int modifier, const char* command ) {
	if ( modifier == -1 ) {
		defaultCommand.Set( command );
		return;
	}

	for ( int i = 0; i < modifierCommands.Num(); i++ ) {
		if ( modifierCommands[i].first == modifier ) {
			modifierCommands[i].second.Set( command );
			return;
		}
	}

	pair_t* pair = modifierCommands.Alloc();
	if ( pair == NULL ) {
		common->Warning( "sdKeyBind::SetCommand: too many modifiers on one key" );
		return;
	}
	pair->first = modifier;
	pair->second.Set( command );
}

sdKeyCommand& sdKeyBind::GetCommand( void ) {
	for ( int i = 0; i < modifierCommands.Num(); i++ ) {
		const int modifier = modifierCommands[i].first;
		if ( modifier >= 0 && idKeyInput::GetKeyByIndex( modifier ).IsDown() ) {
			return modifierCommands[i].second;
		}
	}
	return defaultCommand;
}

sdKeyCommand& sdKeyBind::GetCommand( int modifier ) {
	if ( modifier != -1 ) {
		for ( int i = 0; i < modifierCommands.Num(); i++ ) {
			if ( modifierCommands[i].first == modifier ) {
				return modifierCommands[i].second;
			}
		}
	}
	return defaultCommand;
}

void sdKeyBind::Write( idFile* f, const char* context, const char* keyName ) {
	if ( defaultCommand.GetBinding()[0] != '\0' ) {
		f->Printf( "bind \"%s\" \"%s\" \"\" \"%s\"\n",
			keyName, defaultCommand.GetBinding(), context );
	}

	for ( int i = 0; i < modifierCommands.Num(); i++ ) {
		f->Printf( "bind \"%s\" \"%s\" \"%s\" \"%s\"\n",
			keyName,
			modifierCommands[i].second.GetBinding(),
			idKeyInput::GetKeyByIndex( modifierCommands[i].first ).GetName(),
			context );
	}
}

void sdKeyBind::UnBindBinding( const char* binding ) {
	if ( idStr::Icmp( defaultCommand.GetBinding(), binding ) == 0 ) {
		defaultCommand.Set( "" );
	}

	for ( int i = 0; i < modifierCommands.Num(); ) {
		if ( idStr::Icmp( modifierCommands[i].second.GetBinding(), binding ) == 0 ) {
			modifierCommands.RemoveIndex( i );
		} else {
			i++;
		}
	}
}

void sdKeyBind::SetupBinds( void ) {
	defaultCommand.FixupBind();
	for ( int i = 0; i < modifierCommands.Num(); i++ ) {
		modifierCommands[i].second.FixupBind();
	}
}

/*
================
sdBindContext
================
*/
sdKeyBind* sdBindContext::GetBind( int key ) {
	const int hash = keyHash.GenerateKey( key );
	for ( int i = keyHash.GetFirst( hash ); i != idHashIndex::NULL_INDEX; i = keyHash.GetNext( i ) ) {
		if ( keys[i].first == key ) {
			return keys[i].second;
		}
	}
	return NULL;
}

sdKeyBind* sdBindContext::AllocBind( int key ) {
	sdKeyBind* bind = GetBind( key );
	if ( bind != NULL ) {
		return bind;
	}

	const int index = keys.Num();
	pair_t& pair = keys.Alloc();
	pair.first = key;
	pair.second = new sdKeyBind;
	keyHash.Add( keyHash.GenerateKey( key ), index );
	return pair.second;
}

sdKeyCommand* sdBindContext::GetCommand( int key ) {
	sdKeyBind* bind = GetBind( key );
	if ( bind == NULL ) {
		return NULL;
	}

	sdKeyCommand& command = bind->GetCommand();
	return command.GetBinding()[0] != '\0' ? &command : NULL;
}

void sdBindContext::WriteBindings( idFile* f ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		keys[i].second->Write( f, name.c_str(), idKeyInput::GetKeyByIndex( keys[i].first ).GetName() );
	}
}

void sdBindContext::Bind( int key, int modifierKey, const char* binding ) {
	AllocBind( key )->SetCommand( modifierKey, binding );
}

void sdBindContext::UnBind( int key, int modifierKey ) {
	sdKeyBind* bind = GetBind( key );
	if ( bind != NULL ) {
		bind->ClearCommand( modifierKey );
	}
}

void sdBindContext::UnBindAll( void ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		delete keys[i].second;
	}
	keys.Clear();
	keyHash.Clear();
}

void sdBindContext::UnBindBinding( const char* binding ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		keys[i].second->UnBindBinding( binding );
	}
}

void sdBindContext::SetupBinds( void ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		keys[i].second->SetupBinds();
	}
}

/*
================
idKey
================
*/
void idKey::SetDown( bool _down ) {
	down = _down;
	if ( !down && activeCommand != NULL ) {
		if ( usercmdGen != NULL ) {
			usercmdGen->HandleCommand( activeCommand, false );
		}
		activeCommand = NULL;
	}
}

/*
================
idKeyInput lookup and allocation
================
*/
int idKeyInput::GetKeyIndex( const char* name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return idHashIndex::NULL_INDEX;
	}

	const int hash = keysHash.GenerateKey( name, false );
	for ( int i = keysHash.GetFirst( hash ); i != idHashIndex::NULL_INDEX; i = keysHash.GetNext( i ) ) {
		if ( idStr::Icmp( keys[i]->GetName(), name ) == 0 ) {
			return i;
		}
	}
	return idHashIndex::NULL_INDEX;
}

idKey* idKeyInput::GetKey( const char* name ) {
	const int index = GetKeyIndex( name );
	return index == idHashIndex::NULL_INDEX ? NULL : keys[index];
}

int idKeyInput::AllocKey( const char* name, const char* locName, const wchar_t* fixedText ) {
	int index = GetKeyIndex( name );
	if ( index != idHashIndex::NULL_INDEX ) {
		return index;
	}

	index = keys.Num();
	keysHash.Add( keysHash.GenerateKey( name, false ), index );
	keys.Append( new idKey( index, name, locName, fixedText ) );
	return index;
}

int idKeyInput::GetContextIndex( const char* name ) {
	if ( name == NULL || name[0] == '\0' ) {
		return idHashIndex::NULL_INDEX;
	}

	const int hash = bindContextsHash.GenerateKey( name, false );
	for ( int i = bindContextsHash.GetFirst( hash ); i != idHashIndex::NULL_INDEX; i = bindContextsHash.GetNext( i ) ) {
		if ( idStr::Icmp( bindContexts[i]->GetName(), name ) == 0 ) {
			return i;
		}
	}
	return idHashIndex::NULL_INDEX;
}

sdBindContext* idKeyInput::GetContext( const char* name ) {
	const int index = GetContextIndex( name );
	if ( index != idHashIndex::NULL_INDEX ) {
		return bindContexts[index];
	}
	return bindContexts.Num() != 0 ? bindContexts[0] : NULL;
}

sdBindContext* idKeyInput::AllocContext( const char* name ) {
	const int existing = GetContextIndex( name );
	if ( existing != idHashIndex::NULL_INDEX ) {
		return bindContexts[existing];
	}

	const int index = bindContexts.Num();
	sdBindContext* context = new sdBindContext( name );
	bindContextsHash.Add( bindContextsHash.GenerateKey( name, false ), index );
	bindContexts.Append( context );
	return context;
}

/*
================
idKeyInput state and binding operations
================
*/
void idKeyInput::ArgCompletion_KeyName( const idCmdArgs& args, void( *callback )( const char* s ) ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		callback( va( "%s %s", args.Argv( 0 ), keys[i]->GetName() ) );
	}
}

bool idKeyInput::IsDown( keyNum_e keyNum ) {
	if ( keyNum == K_INVALID ) {
		return false;
	}
	return idKeyboard::GetStandardKey( keyNum ).IsDown();
}

bool idKeyInput::GetOverstrikeMode( void ) {
	return overStrikeMode;
}

void idKeyInput::SetOverstrikeMode( bool state ) {
	overStrikeMode = state;
}

void idKeyInput::ClearStates( void ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		if ( keys[i] != NULL ) {
			keys[i]->SetDown( false );
		}
	}
	if ( usercmdGen != NULL ) {
		usercmdGen->Clear();
	}
}

void idKeyInput::SetBinding( idKey& key, const char* binding, idKey* modifier, sdBindContext* context, bool doPrint ) {
	if ( context == NULL ) {
		return;
	}

	if ( doPrint ) {
		const char* current = GetBinding( context, key, modifier );
		if ( current[0] != '\0' ) {
			common->Printf( "\"%s\" = \"%s\"\n", key.GetName(), current );
		} else {
			common->Printf( "\"%s\" is not bound\n", key.GetName() );
		}
		return;
	}

	const int modifierIndex = modifier != NULL ? modifier->GetId() : -1;
	if ( binding != NULL && binding[0] != '\0' ) {
		context->Bind( key.GetId(), modifierIndex, binding );
	} else {
		context->UnBind( key.GetId(), modifierIndex );
	}
}

const char* idKeyInput::GetBinding( sdBindContext* context, idKey& key, idKey* modifier ) {
	if ( context == NULL ) {
		return "";
	}

	sdKeyBind* bind = context->GetBind( key.GetId() );
	if ( bind == NULL ) {
		return "";
	}
	return bind->GetCommand( modifier != NULL ? modifier->GetId() : -1 ).GetBinding();
}

void idKeyInput::KeysFromBinding( sdBindContext* context, const char* binding, int& numKeys, idKey** resultKeys ) {
	numKeys = 0;
	if ( context == NULL || binding == NULL || binding[0] == '\0' ) {
		return;
	}

	for ( int i = 0; i < keys.Num(); i++ ) {
		sdKeyBind* bind = context->GetBind( keys[i]->GetId() );
		if ( bind == NULL ) {
			continue;
		}
		if ( idStr::Icmp( bind->GetCommand().GetBinding(), binding ) == 0 ) {
			if ( resultKeys != NULL ) {
				resultKeys[numKeys] = keys[i];
			}
			numKeys++;
		}
	}
}

void idKeyInput::KeysFromBinding( sdBindContext* context, const char* binding, bool useBindStrWhenEmpty, idWStr& keyName ) {
	idWStrList localizedKeys;

	if ( context != NULL && binding != NULL && binding[0] != '\0' ) {
		for ( int i = 0; i < keys.Num() && localizedKeys.Num() < 2; i++ ) {
			sdKeyBind* bind = context->GetBind( keys[i]->GetId() );
			if ( bind == NULL || idStr::Icmp( bind->GetCommand().GetBinding(), binding ) != 0 ) {
				continue;
			}

			idWStr& localized = localizedKeys.Alloc();
			keys[i]->GetLocalizedText( localized );
		}
	}

	if ( localizedKeys.Num() == 1 ) {
		keyName = localizedKeys[0];
	} else if ( localizedKeys.Num() > 1 ) {
		keyName = common->LocalizeText( "engine/keys/dualbind", localizedKeys );
	} else if ( useBindStrWhenEmpty ) {
		keyName = va( L"<%hs>", binding != NULL ? binding : "" );
	} else {
		keyName = common->LocalizeText( "engine/keys/emptykey" );
	}
}

void idKeyInput::UnbindKey( sdBindContext* context, idKey& key, idKey* modifier ) {
	if ( context != NULL ) {
		context->UnBind( key.GetId(), modifier != NULL ? modifier->GetId() : -1 );
	}
}

void idKeyInput::ListBinds( sdBindContext* context ) {
	if ( context == NULL ) {
		return;
	}

	for ( int i = 0; i < keys.Num(); i++ ) {
		sdKeyBind* bind = context->GetBind( keys[i]->GetId() );
		if ( bind == NULL ) {
			continue;
		}
		const char* command = bind->GetCommand().GetBinding();
		if ( command[0] != '\0' ) {
			common->Printf( "%s \"%s\"\n", keys[i]->GetName(), command );
		}
	}
}

void idKeyInput::ExecKeyBinding( const sdKeyCommand* cmd ) {
	if ( cmd != NULL && cmd->GetBinding()[0] != '\0' ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, cmd->GetBinding() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
	}
}

void idKeyInput::WriteBindings( idFile* f ) {
	f->Printf( "unbindall\n" );
	for ( int i = 0; i < bindContexts.Num(); i++ ) {
		bindContexts[i]->WriteBindings( f );
	}
}

void idKeyInput::SetupBinds( void ) {
	for ( int i = 0; i < bindContexts.Num(); i++ ) {
		bindContexts[i]->SetupBinds();
	}
}

void idKeyInput::UnbindAll( void ) {
	for ( int i = 0; i < bindContexts.Num(); i++ ) {
		bindContexts[i]->UnBindAll();
	}
}

bool idKeyInput::AnyKeysDown( void ) {
	for ( int i = 0; i < keys.Num(); i++ ) {
		if ( keys[i] != NULL && keys[i]->IsDown() ) {
			return true;
		}
	}
	return false;
}

/*
================
Console commands
================
*/
static void Key_Bind_f( const idCmdArgs& args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "bind <key> [command] [modifier] [context]: attach a command to a key\n" );
		return;
	}

	idKey* key = idKeyInput::GetKey( args.Argv( 1 ) );
	if ( key == NULL ) {
		common->Printf( "\"%s\" isn't a valid key\n", args.Argv( 1 ) );
		return;
	}

	idKey* modifier = NULL;
	if ( args.Argv( 3 )[0] != '\0' ) {
		modifier = idKeyInput::GetKey( args.Argv( 3 ) );
		if ( modifier == NULL ) {
			common->Printf( "\"%s\" isn't a valid key\n", args.Argv( 3 ) );
			return;
		}
	}

	const char* contextName = args.Argv( 4 )[0] != '\0' ? args.Argv( 4 ) : "default";
	idKeyInput::SetBinding( *key, args.Argv( 2 ), modifier, idKeyInput::AllocContext( contextName ), args.Argc() == 2 );
}

static void Key_Unbind_f( const idCmdArgs& args ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "unbind <key> [modifier] [context]: remove commands from a key\n" );
		return;
	}

	idKey* key = idKeyInput::GetKey( args.Argv( 1 ) );
	if ( key == NULL ) {
		common->Printf( "Unknown Key '%s'\n", args.Argv( 1 ) );
		return;
	}

	idKey* modifier = NULL;
	if ( args.Argv( 2 )[0] != '\0' ) {
		modifier = idKeyInput::GetKey( args.Argv( 2 ) );
		if ( modifier == NULL ) {
			common->Printf( "Unknown Key '%s'\n", args.Argv( 2 ) );
			return;
		}
	}

	const char* contextName = args.Argv( 3 )[0] != '\0' ? args.Argv( 3 ) : "default";
	idKeyInput::UnbindKey( idKeyInput::GetContext( contextName ), *key, modifier );
}

static void Key_Unbindall_f( const idCmdArgs& args ) {
	idKeyInput::UnbindAll();
}

static void Key_ListBinds_f( const idCmdArgs& args ) {
	const char* contextName = args.Argv( 1 )[0] != '\0' ? args.Argv( 1 ) : "default";
	idKeyInput::ListBinds( idKeyInput::GetContext( contextName ) );
}

/*
================
idKeyInput initialization
================
*/
void idKeyInput::Init( void ) {
	idKeyboard::AllocateKeys();
	idMouse::AllocateMouseButtons();
	sdControllerManager::AllocateControllerButtons();
	AllocContext( "default" );

	cmdSystem->AddCommand( "bind", Key_Bind_f, CMD_FL_SYSTEM, "binds a command to a key", ArgCompletion_KeyName );
	cmdSystem->AddCommand( "unbind", Key_Unbind_f, CMD_FL_SYSTEM, "unbinds any command from a key", ArgCompletion_KeyName );
	cmdSystem->AddCommand( "unbindall", Key_Unbindall_f, CMD_FL_SYSTEM, "unbinds any commands from all keys" );
	cmdSystem->AddCommand( "listBinds", Key_ListBinds_f, CMD_FL_SYSTEM, "lists key bindings" );

	if ( game != NULL ) {
		game->OnInputInit();
	}
}

void idKeyInput::Shutdown( void ) {
	if ( game != NULL ) {
		game->OnInputShutdown();
	}

	ClearStates();
	keysHash.Clear();
	for ( int i = 0; i < keys.Num(); i++ ) {
		delete keys[i];
	}
	keys.Clear();

	for ( int i = 0; i < bindContexts.Num(); i++ ) {
		delete bindContexts[i];
	}
	bindContexts.Clear();
	bindContextsHash.Clear();

	cmdSystem->RemoveCommand( "bind" );
	cmdSystem->RemoveCommand( "unbind" );
	cmdSystem->RemoveCommand( "unbindall" );
	cmdSystem->RemoveCommand( "listBinds" );
}

/*
===============================================================================

	Game-facing key input bridge

===============================================================================
*/
class sdKeyInputManagerLocal : public sdKeyInputManager {
public:
	virtual void			SetBinding( sdBindContext* context, idKey& key, const char* binding, idKey* modifierKey ) {
		idKeyInput::SetBinding( key, binding, modifierKey, context, false );
	}

	virtual const char*		GetBinding( sdBindContext* context, idKey& key, idKey* modifierKey ) {
		return idKeyInput::GetBinding( context, key, modifierKey );
	}

	virtual void			UnbindBinding( sdBindContext* context, const char* bind ) {
		if ( context != NULL ) {
			context->UnBindBinding( bind );
		}
	}

	virtual void			KeysFromBinding( sdBindContext* context, const char* binding, bool useBindStrWhenEmpty, idWStr& keyName ) {
		idKeyInput::KeysFromBinding( context, binding, useBindStrWhenEmpty, keyName );
	}

	virtual void			KeysFromBinding( sdBindContext* context, const char* binding, int& numKeys, idKey** keys ) {
		idKeyInput::KeysFromBinding( context, binding, numKeys, keys );
	}

	virtual bool			IsDown( const idKey& key ) {
		return key.IsDown();
	}

	virtual bool			IsDown( keyNum_e key ) {
		return idKeyInput::IsDown( key );
	}

	virtual idKey*			GetKey( const char* name ) {
		return idKeyInput::GetKey( name );
	}

	virtual idKey*			GetKeyForEvent( const sdSysEvent& evt, bool& down ) {
		if ( evt.IsKeyEvent() ) {
			down = evt.IsKeyDown();
			return &idKeyboard::GetStandardKey( evt.GetKey() );
		}
		if ( evt.IsMouseButtonEvent() ) {
			down = evt.IsButtonDown();
			return &idMouse::GetMouseButton( evt.GetMouseButton() );
		}
		if ( evt.IsControllerButtonEvent() ) {
			sdController* controller = sys->GetControllerManager().GetControllerByHash( evt.GetControllerHash() );
			if ( controller != NULL ) {
				down = evt.IsButtonDown();
				return &controller->GetButton( evt.GetButton() );
			}
		}
		down = false;
		return NULL;
	}

	virtual void			ProcessUserCmdEvent( const sdSysEvent& event ) {
		if ( usercmdGen != NULL ) {
			usercmdGen->ProcessEvent( event );
		}
	}

	virtual sdKeyCommand*	GetCommand( sdBindContext* context, const idKey& key ) {
		return context != NULL ? context->GetCommand( key.GetId() ) : NULL;
	}

	virtual sdBindContext*	AllocBindContext( const char* context ) {
		return idKeyInput::AllocContext( context );
	}

	virtual void			UnbindKey( sdBindContext* context, idKey& key, idKey* modifier ) {
		idKeyInput::UnbindKey( context, key, modifier );
	}

	virtual bool			AnyKeysDown( void ) {
		return idKeyInput::AnyKeysDown();
	}
};

static sdKeyInputManagerLocal	keyInputManagerLocal;
sdKeyInputManager*				keyInputManager = &keyInputManagerLocal;
