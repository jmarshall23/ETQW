// Copyright (C) 2007 Id Software, Inc.
//

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "sys_local.h"
#include "win32/input/win_dinput.h"
#include "win32/input/win_xinput.h"

namespace {

struct keyName_t {
	const char*	name;
	keyNum_t	keyNum;
	const char*	strId;
};

struct mouseButtonName_t {
	const char*	name;
	mouseButton_t button;
	const char*	strId;
};

static int standardKeys[ K_NUM_KEYS ];
static int mouseButtons[ M_NUM_MOUSEBUTTONS ];

static const keyName_t keyNames[] = {
	{ "BACKSPACE", K_BACKSPACE, "engine/keys/backspace" },
	{ "TAB", K_TAB, "engine/keys/tab" },
	{ "ENTER", K_ENTER, "engine/keys/enter" },
	{ "ESCAPE", K_ESCAPE, "engine/keys/escape" },
	{ "SPACE", K_SPACE, "engine/keys/space" },
	{ "!", K_EXCLAMATION, NULL },
	{ "#", K_HASH, NULL },
	{ "$", K_DOLLAR, NULL },
	{ "&", K_AMPERSAND, NULL },
	{ "APOSTROPHE", K_APOSTROPHE, "engine/keys/apostrophe" },
	{ "(", K_LEFTPARENTHESIS, NULL },
	{ ")", K_RIGHTPARENTHESIS, NULL },
	{ "*", K_ASTERISK, NULL },
	{ "+", K_PLUS, NULL },
	{ ",", K_COMMA, NULL },
	{ "-", K_MINUS, NULL },
	{ ".", K_PERIOD, NULL },
	{ "/", K_SLASH, NULL },
	{ "0", K_0, NULL },
	{ "1", K_1, NULL },
	{ "2", K_2, NULL },
	{ "3", K_3, NULL },
	{ "4", K_4, NULL },
	{ "5", K_5, NULL },
	{ "6", K_6, NULL },
	{ "7", K_7, NULL },
	{ "8", K_8, NULL },
	{ "9", K_9, NULL },
	{ "SEMICOLON", K_SEMICOLON, "engine/keys/semicolon" },
	{ "=", K_EQUALS, NULL },
	{ "[", K_LEFTBRACKET, NULL },
	{ "\\", K_BACKSLASH, NULL },
	{ "]", K_RIGHTBRACKET, NULL },
	{ "`", K_BACKQUOTE, NULL },
	{ "a", K_A, NULL }, { "b", K_B, NULL }, { "c", K_C, NULL },
	{ "d", K_D, NULL }, { "e", K_E, NULL }, { "f", K_F, NULL },
	{ "g", K_G, NULL }, { "h", K_H, NULL }, { "i", K_I, NULL },
	{ "j", K_J, NULL }, { "k", K_K, NULL }, { "l", K_L, NULL },
	{ "m", K_M, NULL }, { "n", K_N, NULL }, { "o", K_O, NULL },
	{ "p", K_P, NULL }, { "q", K_Q, NULL }, { "r", K_R, NULL },
	{ "s", K_S, NULL }, { "t", K_T, NULL }, { "u", K_U, NULL },
	{ "v", K_V, NULL }, { "w", K_W, NULL }, { "x", K_X, NULL },
	{ "y", K_Y, NULL }, { "z", K_Z, NULL },
	{ "CAPSLOCK", K_CAPSLOCK, "engine/keys/capslock" },
	{ "SCROLL", K_SCROLL, "engine/keys/scroll" },
	{ "PAUSE", K_PAUSE, "engine/keys/pause" },
	{ "UPARROW", K_UPARROW, "engine/keys/uparrow" },
	{ "DOWNARROW", K_DOWNARROW, "engine/keys/downarrow" },
	{ "LEFTARROW", K_LEFTARROW, "engine/keys/leftarrow" },
	{ "RIGHTARROW", K_RIGHTARROW, "engine/keys/rightarrow" },
	{ "LWIN", K_LWIN, "engine/keys/lwin" },
	{ "RWIN", K_RWIN, "engine/keys/rwin" },
	{ "MENU", K_MENU, "engine/keys/menu" },
	{ "ALT", K_ALT, "engine/keys/alt" },
	{ "CTRL", K_CTRL, "engine/keys/ctrl" },
	{ "SHIFT", K_SHIFT, "engine/keys/shift" },
	{ "INS", K_INS, "engine/keys/ins" },
	{ "DEL", K_DEL, "engine/keys/del" },
	{ "PGDN", K_PGDN, "engine/keys/pgdn" },
	{ "PGUP", K_PGUP, "engine/keys/pgup" },
	{ "HOME", K_HOME, "engine/keys/home" },
	{ "END", K_END, "engine/keys/end" },
	{ "F1", K_F1, "engine/keys/f1" }, { "F2", K_F2, "engine/keys/f2" },
	{ "F3", K_F3, "engine/keys/f3" }, { "F4", K_F4, "engine/keys/f4" },
	{ "F5", K_F5, "engine/keys/f5" }, { "F6", K_F6, "engine/keys/f6" },
	{ "F7", K_F7, "engine/keys/f7" }, { "F8", K_F8, "engine/keys/f8" },
	{ "F9", K_F9, "engine/keys/f9" }, { "F10", K_F10, "engine/keys/f10" },
	{ "F11", K_F11, "engine/keys/f11" }, { "F12", K_F12, "engine/keys/f12" },
	{ "F13", K_F13, "engine/keys/f13" }, { "F14", K_F14, "engine/keys/f14" },
	{ "F15", K_F15, "engine/keys/f15" }, { "F16", K_F16, "engine/keys/f16" },
	{ "KP_HOME", K_KP_HOME, "engine/keys/kp_home" },
	{ "KP_UPARROW", K_KP_UPARROW, "engine/keys/kp_uparrow" },
	{ "KP_PGUP", K_KP_PGUP, "engine/keys/kp_pgup" },
	{ "KP_LEFTARROW", K_KP_LEFTARROW, "engine/keys/kp_leftarrow" },
	{ "KP_5", K_KP_5, "engine/keys/kp_5" },
	{ "KP_RIGHTARROW", K_KP_RIGHTARROW, "engine/keys/kp_rightarrow" },
	{ "KP_END", K_KP_END, "engine/keys/kp_end" },
	{ "KP_DOWNARROW", K_KP_DOWNARROW, "engine/keys/kp_downarrow" },
	{ "KP_PGDN", K_KP_PGDN, "engine/keys/kp_pgdn" },
	{ "KP_ENTER", K_KP_ENTER, "engine/keys/kp_enter" },
	{ "KP_INS", K_KP_INS, "engine/keys/kp_ins" },
	{ "KP_DEL", K_KP_DEL, "engine/keys/kp_del" },
	{ "KP_SLASH", K_KP_SLASH, "engine/keys/kp_slash" },
	{ "KP_MINUS", K_KP_MINUS, "engine/keys/kp_minus" },
	{ "KP_PLUS", K_KP_PLUS, "engine/keys/kp_plus" },
	{ "KP_NUMLOCK", K_KP_NUMLOCK, "engine/keys/kp_numlock" },
	{ "KP_STAR", K_KP_STAR, "engine/keys/kp_star" },
	{ "KP_EQUALS", K_KP_EQUALS, "engine/keys/kp_equals" },
	{ "PRINTSCREEN", K_PRINT_SCR, "engine/keys/printscr" },
	{ "RIGHTALT", K_RIGHT_ALT, "engine/keys/rightalt" },
	{ "RIGHTSHIFT", K_RIGHT_SHIFT, "engine/keys/rightshift" },
	{ "RIGHTCTRL", K_RIGHT_CTRL, "engine/keys/rightctrl" },
	{ "COMMAND", K_COMMAND, "engine/keys/command" },
	{ "OEM102", K_OEM_102, "engine/keys/oem102" },
	{ "AUX1", K_AUX1, "engine/keys/aux1" }, { "AUX2", K_AUX2, "engine/keys/aux2" },
	{ "AUX3", K_AUX3, "engine/keys/aux3" }, { "AUX4", K_AUX4, "engine/keys/aux4" },
	{ "AUX5", K_AUX5, "engine/keys/aux5" }, { "AUX6", K_AUX6, "engine/keys/aux6" },
	{ "AUX7", K_AUX7, "engine/keys/aux7" }, { "AUX8", K_AUX8, "engine/keys/aux8" },
	{ "AUX9", K_AUX9, "engine/keys/aux9" }, { "AUX10", K_AUX10, "engine/keys/aux10" },
	{ NULL, K_INVALID, NULL }
};

static const mouseButtonName_t mouseButtonNames[] = {
	{ "MOUSE1", M_MOUSE1, "engine/keys/mouse1" },
	{ "MOUSE2", M_MOUSE2, "engine/keys/mouse2" },
	{ "MOUSE3", M_MOUSE3, "engine/keys/mouse3" },
	{ "MOUSE4", M_MOUSE4, "engine/keys/mouse4" },
	{ "MOUSE5", M_MOUSE5, "engine/keys/mouse5" },
	{ "MOUSE6", M_MOUSE6, "engine/keys/mouse6" },
	{ "MOUSE7", M_MOUSE7, "engine/keys/mouse7" },
	{ "MOUSE8", M_MOUSE8, "engine/keys/mouse8" },
	{ "MOUSE9", M_MOUSE9, "engine/keys/mouse9" },
	{ "MOUSE10", M_MOUSE10, "engine/keys/mouse10" },
	{ "MOUSE11", M_MOUSE11, "engine/keys/mouse11" },
	{ "MOUSE12", M_MOUSE12, "engine/keys/mouse12" },
	{ "MWHEELDOWN", M_MWHEELDOWN, "engine/keys/mwheeldown" },
	{ "MWHEELUP", M_MWHEELUP, "engine/keys/mwheelup" },
	{ NULL, M_INVALID, NULL }
};

static void Controller_List_f( const idCmdArgs& args ) {
	const sdControllerManager& manager = sys->GetControllerManager();
	for ( int i = 0; i < manager.GetMaxControllers(); i++ ) {
		const sdController& controller = const_cast< sdControllerManager& >( manager ).GetController( i );
		if ( controller.GetState() == sdController::CS_OK ) {
			common->Printf( "  %i: %s\n", i, controller.GetName() );
		}
	}
}

} // namespace

idCVar sdControllerManager::in_joy1_device( "in_joy1_device", "232429", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE | CVAR_PROFILE, "the hash of the controller device named joy1" );
idCVar sdControllerManager::in_joy2_device( "in_joy2_device", "232429", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE | CVAR_PROFILE, "the hash of the controller device named joy2" );
idCVar sdControllerManager::in_joy3_device( "in_joy3_device", "232429", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE | CVAR_PROFILE, "the hash of the controller device named joy3" );
idCVar sdControllerManager::in_joy4_device( "in_joy4_device", "232429", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE | CVAR_PROFILE, "the hash of the controller device named joy4" );

sdDeviceMappingCallback sdControllerManager::deviceMappingCallback;

sdControllerManager::buttonMap_t sdControllerManager::specialButtons[] = {
	{ "left_trigger", C_LEFT_TRIGGER },
	{ "right_trigger", C_RIGHT_TRIGGER },
	{ "dpad_up", C_DPAD_UP },
	{ "dpad_down", C_DPAD_DOWN },
	{ "dpad_left", C_DPAD_LEFT },
	{ "dpad_right", C_DPAD_RIGHT }
};

const int sdControllerManager::numSpecialButtons = sizeof( specialButtons ) / sizeof( specialButtons[ 0 ] );

void idKeyboard::AllocateKeys() {
	memset( standardKeys, 0xff, sizeof( standardKeys ) );

	idWStr fixedText;
	idStr locName;
	for ( const keyName_t* key = keyNames; key->name != NULL; key++ ) {
		KeyNumToString( key->keyNum, fixedText, locName );
		standardKeys[ key->keyNum ] = idKeyInput::AllocKey( key->name, locName.c_str(), fixedText.c_str() );
	}

	for ( int key = K_INVALID; key < K_NUM_KEYS; key++ ) {
		if ( standardKeys[ key ] != -1 ) {
			continue;
		}
		const char* name = va( "0x%02x", key );
		KeyNumToString( static_cast< keyNum_t >( key ), fixedText, locName );
		standardKeys[ key ] = idKeyInput::AllocKey( name, locName.c_str(), fixedText.c_str() );
	}
}

unsigned int idKeyboard::StringToScanCode( const char* str ) {
	return static_cast< unsigned int >( StringToKeyNum( str ) );
}

keyNum_t idKeyboard::StringToKeyNum( const char* str ) {
	idKey* key = idKeyInput::GetKey( str );
	if ( key == NULL || key->GetId() < K_INVALID || key->GetId() >= K_NUM_KEYS ) {
		return K_INVALID;
	}
	return static_cast< keyNum_t >( key->GetId() );
}

void idKeyboard::KeyNumToString( const keyNum_t keyNum, idWStr& fixedText, idStr& locName ) {
	fixedText.Clear();
	locName.Clear();

	if ( keyNum != K_SPACE && keyNum != K_BACKSPACE && keyNum != K_TAB && keyNum != K_ENTER &&
		( keyNum < K_KP_HOME || keyNum > K_KP_EQUALS ) ) {
		const char ch = sys->Keyboard().ConvertKeyToChar( keyNum );
		if ( ch != '\0' ) {
			fixedText = va( L"%c", static_cast< unsigned char >( ch ) );
			return;
		}
	}

	for ( const keyName_t* key = keyNames; key->name != NULL; key++ ) {
		if ( key->keyNum != keyNum ) {
			continue;
		}
		if ( key->strId != NULL ) {
			locName = key->strId;
		} else {
			fixedText = va( L"%hs", key->name );
		}
		return;
	}

	fixedText = va( L"0x%02X", static_cast< unsigned int >( keyNum ) );
}

idKey& idKeyboard::GetStandardKey( const keyNum_t key ) {
	return idKeyInput::GetKeyByIndex( standardKeys[ key ] );
}

void idMouse::AllocateMouseButtons() {
	for ( const mouseButtonName_t* button = mouseButtonNames; button->name != NULL; button++ ) {
		mouseButtons[ button->button ] = idKeyInput::AllocKey( button->name, button->strId, NULL );
	}
}

mouseButton_t idMouse::StringToMouseButton( const char* str ) {
	if ( str != NULL ) {
		for ( const mouseButtonName_t* button = mouseButtonNames; button->name != NULL; button++ ) {
			if ( idStr::Icmp( str, button->name ) == 0 ) {
				return button->button;
			}
		}
	}
	return M_INVALID;
}

const wchar_t* idMouse::MouseButtonToString( const mouseButton_t button, bool localized ) {
	static idWStr text;
	text.Clear();
	if ( button <= M_INVALID || button >= M_NUM_MOUSEBUTTONS ) {
		return text.c_str();
	}
	if ( localized ) {
		GetMouseButton( button ).GetLocalizedText( text );
	} else {
		text = va( L"%hs", GetMouseButton( button ).GetName() );
	}
	return text.c_str();
}

idKey& idMouse::GetMouseButton( const mouseButton_t button ) {
	return idKeyInput::GetKeyByIndex( mouseButtons[ button ] );
}

sdController::sdController() : mapped( false ) {
	memset( axis, 0, sizeof( axis ) );
}

void sdController::InitButtons() {
	const int slot = sys->GetControllerManager().GetJoySlotByController( *this );
	mapped = slot != 0;
	if ( !mapped ) {
		for ( int i = 0; i < MAX_CONTROLLER_BUTTONS; i++ ) {
			buttons[ i ] = -1;
		}
		return;
	}

	for ( int i = 0; i < MAX_CONTROLLER_BUTTONS; i++ ) {
		idStr name;
		idStr locName;
		sdControllerManager::GetKeyNameForSlotButton( slot, i, name, locName );
		buttons[ i ] = idKeyInput::AllocKey( name.c_str(), locName.c_str(), NULL );
	}
}

sdControllerAPI::sdControllerAPI() : state( CAS_UNINITIALIZED ) {
}

void sdControllerAPI::Shutdown() {
	state = CAS_UNINITIALIZED;
}

sdController* sdControllerManager::GetControllerByHash( int hash ) {
	for ( int i = 0; i < controllers.Num(); i++ ) {
		if ( controllers[ i ]->GetHash() == hash ) {
			return controllers[ i ];
		}
	}
	return NULL;
}

void sdControllerManager::OnDeviceMappingChanged() {
	for ( int i = 0; i < controllers.Num(); i++ ) {
		controllers[ i ]->InitButtons();
	}
}

sdController* sdControllerManager::GetControllerByJoySlot( int slot ) {
	int hash = -1;
	switch ( slot ) {
		case 1: hash = in_joy1_device.GetInteger(); break;
		case 2: hash = in_joy2_device.GetInteger(); break;
		case 3: hash = in_joy3_device.GetInteger(); break;
		case 4: hash = in_joy4_device.GetInteger(); break;
		default:
			common->Error( "sdControllerManager::GetControllerByJoySlot - Slot out of range 1 - 4: %i", slot );
			break;
	}
	return GetControllerByHash( hash );
}

int sdControllerManager::GetJoySlotByController( const sdController& controller ) {
	return GetJoySlotByHash( controller.GetHash() );
}

int sdControllerManager::GetJoySlotByHash( int hash ) {
	if ( in_joy1_device.GetInteger() == hash ) return 1;
	if ( in_joy2_device.GetInteger() == hash ) return 2;
	if ( in_joy3_device.GetInteger() == hash ) return 3;
	if ( in_joy4_device.GetInteger() == hash ) return 4;
	return 0;
}

void sdControllerManager::GetKeyNameForSlotButton( int slot, int button, idStr& name, idStr& locName ) {
	name.Clear();
	locName.Clear();
	if ( button < C_BUTTON1 || button > C_BUTTON_MAX ) {
		common->Error( "sdControllerManagerLocal::GetKeyNameForSlotButton Invalid Button Requested" );
		return;
	}

	if ( button <= C_NUMBERED_BUTTON_MAX ) {
		name = va( "joy%d_%d", slot, button + 1 );
		locName = va( "engine/keys/controller/%d/button%d", slot, button + 1 );
		return;
	}

	const int specialIndex = button - C_LEFT_TRIGGER;
	if ( specialIndex < 0 || specialIndex >= numSpecialButtons ) {
		common->Error( "sdControllerManagerLocal::GetKeyNameForSlotButton Invalid Button Requested" );
		return;
	}
	idStr specialName = specialButtons[ specialIndex ].name;
	name = va( "joy%d_%s", slot, specialName.c_str() );
	specialName.ToLower();
	locName = va( "engine/keys/controller/%d/%s", slot, specialName.c_str() );
}

void sdControllerManager::AllocateControllerButtons() {
	in_joy1_device.RegisterCallback( &deviceMappingCallback );
	in_joy2_device.RegisterCallback( &deviceMappingCallback );
	in_joy3_device.RegisterCallback( &deviceMappingCallback );
	in_joy4_device.RegisterCallback( &deviceMappingCallback );

	for ( int slot = 1; slot <= 4; slot++ ) {
		for ( int button = C_BUTTON1; button <= C_BUTTON_MAX; button++ ) {
			idStr name;
			idStr locName;
			GetKeyNameForSlotButton( slot, button, name, locName );
			idKeyInput::AllocKey( name.c_str(), locName.c_str(), NULL );
		}
	}
}

void sdDeviceMappingCallback::OnChanged() {
	sys->GetControllerManager().OnDeviceMappingChanged();
}

sdControllerManagerLocal::sdControllerManagerLocal() {
}

sdControllerManagerLocal::~sdControllerManagerLocal() {
}

void sdControllerManagerLocal::Init() {
	sdControllerAPIXInput* xInput = new sdControllerAPIXInput;
	controllerAPIs.Append( xInput );
	xInput->Init( controllerAPIs.Num() - 1 );

	sdControllerAPIDInput* dInput = new sdControllerAPIDInput;
	controllerAPIs.Append( dInput );
	dInput->Init( controllerAPIs.Num() - 1 );

	cmdSystem->AddCommand( "listControllers", Controller_List_f, CMD_FL_SYSTEM, "lists connected controller devices" );
}

void sdControllerManagerLocal::Shutdown() {
	cmdSystem->RemoveCommand( "listControllers" );
	for ( int i = 0; i < controllerAPIs.Num(); i++ ) {
		controllerAPIs[ i ]->Shutdown();
	}
	controllers.DeleteContents( true );
	controllerAPIs.DeleteContents( true );
}

sdControllerAPI::controllerApiState_e sdControllerManagerLocal::GetAPIState( const char* APIName ) {
	for ( int i = 0; i < controllerAPIs.Num(); i++ ) {
		if ( idStr::Icmp( controllerAPIs[ i ]->GetName(), APIName ) == 0 ) {
			return controllerAPIs[ i ]->GetState();
		}
	}
	return sdControllerAPI::CAS_BAD_API;
}

int sdControllerManagerLocal::GetNumConnectedControllers() const {
	int count = 0;
	for ( int i = 0; i < controllers.Num(); i++ ) {
		if ( controllers[ i ]->GetState() == sdController::CS_OK ) {
			count++;
		}
	}
	return count;
}
