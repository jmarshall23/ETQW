// Copyright (C) 2007 Id Software, Inc.
//

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../sys_local.h"
#include "win_xinput.h"

extern int com_frameTime;

namespace {

typedef DWORD ( WINAPI* xInputGetState_t )( DWORD, XINPUT_STATE* );
typedef DWORD ( WINAPI* xInputSetState_t )( DWORD, XINPUT_VIBRATION* );
typedef DWORD ( WINAPI* xInputGetCapabilities_t )( DWORD, DWORD, XINPUT_CAPABILITIES* );
typedef void ( WINAPI* xInputEnable_t )( BOOL );
typedef DWORD ( WINAPI* xInputGetDSoundAudioDeviceGuids_t )( DWORD, GUID*, GUID* );
typedef DWORD ( WINAPI* xInputGetBatteryInformation_t )( DWORD, BYTE, void* );
typedef DWORD ( WINAPI* xInputGetKeystroke_t )( DWORD, DWORD, void* );

xInputGetState_t sdXInputGetState;
xInputSetState_t sdXInputSetState;
xInputGetCapabilities_t sdXInputGetCapabilities;
xInputEnable_t sdXInputEnable;
xInputGetDSoundAudioDeviceGuids_t sdXInputGetDSoundAudioDeviceGuids;
xInputGetBatteryInformation_t sdXInputGetBatteryInformation;
xInputGetKeystroke_t sdXInputGetKeystroke;

const int XInputButtonRemap[ 16 ] = {
	C_DPAD_UP,
	C_DPAD_DOWN,
	C_DPAD_LEFT,
	C_DPAD_RIGHT,
	C_BUTTON9,
	C_BUTTON10,
	C_BUTTON7,
	C_BUTTON8,
	C_BUTTON5,
	C_BUTTON6,
	-1,
	-1,
	C_BUTTON1,
	C_BUTTON2,
	C_BUTTON3,
	C_BUTTON4
};

template< class T >
bool LoadXInputProc( HMODULE library, T& function, const char* name ) {
	function = reinterpret_cast< T >( GetProcAddress( library, name ) );
	if ( function == NULL ) {
		common->Warning( "Couldn't find proc address for: %s\n", name );
		return false;
	}
	return true;
}

short ClampToShort( double value ) {
	if ( value < -32768.0 ) {
		return -32768;
	}
	if ( value > 32767.0 ) {
		return 32767;
	}
	return static_cast< short >( value );
}

} // namespace

sdXInputController::sdXInputController() :
	numEvents( 0 ),
	lastFrameTime( 0 ),
	isGamePad( false ) {
	memset( events, 0, sizeof( events ) );
	memset( &oldInputState, 0, sizeof( oldInputState ) );
}

bool sdXInputController::Init() {
	lastFrameTime = com_frameTime;
	idStr::snPrintf( name, sizeof( name ), "XInput controller %d", index );
	hash = idStr::Hash( name ) & 0x7fffffff;
	common->Printf( "...%s given hash = %i\n", name, hash );
	return true;
}

void sdXInputController::UpdateState() {
	XINPUT_STATE inputState;
	memset( &inputState, 0, sizeof( inputState ) );
	state = CS_NOT_CONNECTED;
	if ( sdXInputGetState == NULL || sdXInputGetState( index, &inputState ) != ERROR_SUCCESS ) {
		return;
	}

	state = CS_OK;
	isGamePad = false;
	XINPUT_CAPABILITIES inputCapabilities;
	memset( &inputCapabilities, 0, sizeof( inputCapabilities ) );
	if ( sdXInputGetCapabilities != NULL &&
		sdXInputGetCapabilities( index, XINPUT_FLAG_GAMEPAD, &inputCapabilities ) == ERROR_SUCCESS &&
		inputCapabilities.Type == XINPUT_DEVTYPE_GAMEPAD &&
		inputCapabilities.SubType == XINPUT_DEVSUBTYPE_GAMEPAD ) {
		isGamePad = true;
	}
}

int sdXInputController::ReturnInputEvent( const int n, int& action, int& value ) {
	if ( static_cast< unsigned int >( n ) >= MAX_CONTROLLER_EVENTS ) {
		return 0;
	}
	action = events[ n ].event;
	value = events[ n ].value;
	return 1;
}

void sdXInputController::ConvertCircleInputToSquare( short& xInput, short& yInput ) {
	if ( xInput == 0 || yInput == 0 ) {
		return;
	}
	const int absX = abs( static_cast< int >( xInput ) );
	const int absY = abs( static_cast< int >( yInput ) );
	const int minAxis = absX < absY ? absX : absY;
	const int maxAxis = absX > absY ? absX : absY;
	const double aspect = static_cast< double >( minAxis ) / static_cast< double >( maxAxis );
	const double scale = sqrt( aspect * aspect + 1.0 );
	xInput = ClampToShort( static_cast< double >( xInput ) * scale );
	yInput = ClampToShort( static_cast< double >( yInput ) * scale );
}

void sdXInputController::PostInputEvent( int event, int value ) {
	if ( !mapped ) {
		return;
	}

	if ( event <= C_BUTTON_MAX ) {
		const int controllerValue = ( hash << 1 ) | ( value > 0 ? 1 : 0 );
		sys->QueEvent( SE_CONTROLLER_BUTTON, event, controllerValue, 0, NULL );
	} else if ( event >= C_AXIS1 && event <= C_AXIS_MAX ) {
		sys->QueEvent( SE_CONTROLLER_AXIS, event - C_AXIS1, value, 0, NULL );
	}

	if ( numEvents < MAX_CONTROLLER_EVENTS ) {
		events[ numEvents ].event = event;
		events[ numEvents ].value = value;
		numEvents++;
	}
}

int sdXInputController::PollInputEvents() {
	XINPUT_STATE inputState;
	memset( &inputState, 0, sizeof( inputState ) );
	numEvents = 0;

	if ( sdXInputGetState == NULL || sdXInputGetState( index, &inputState ) != ERROR_SUCCESS ) {
		if ( state == CS_OK ) {
			for ( int event = 0; event < MAX_CONTROLLER_EVENTS; event++ ) {
				PostInputEvent( event, 0 );
			}
		}
		state = CS_NOT_CONNECTED;
		return numEvents;
	}

	if ( state == CS_NOT_CONNECTED ) {
		XINPUT_CAPABILITIES inputCapabilities;
		memset( &inputCapabilities, 0, sizeof( inputCapabilities ) );
		isGamePad = sdXInputGetCapabilities != NULL &&
			sdXInputGetCapabilities( index, XINPUT_FLAG_GAMEPAD, &inputCapabilities ) == ERROR_SUCCESS &&
			inputCapabilities.Type == XINPUT_DEVTYPE_GAMEPAD &&
			inputCapabilities.SubType == XINPUT_DEVSUBTYPE_GAMEPAD;
	}
	state = CS_OK;

	if ( abs( static_cast< int >( inputState.Gamepad.sThumbRX ) ) < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ) inputState.Gamepad.sThumbRX = 0;
	if ( abs( static_cast< int >( inputState.Gamepad.sThumbRY ) ) < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ) inputState.Gamepad.sThumbRY = 0;
	if ( abs( static_cast< int >( inputState.Gamepad.sThumbLX ) ) < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ) inputState.Gamepad.sThumbLX = 0;
	if ( abs( static_cast< int >( inputState.Gamepad.sThumbLY ) ) < XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ) inputState.Gamepad.sThumbLY = 0;
	if ( inputState.Gamepad.bLeftTrigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD ) inputState.Gamepad.bLeftTrigger = 0;
	if ( inputState.Gamepad.bRightTrigger < XINPUT_GAMEPAD_TRIGGER_THRESHOLD ) inputState.Gamepad.bRightTrigger = 0;

	const float frameSeconds = static_cast< float >( com_frameTime - lastFrameTime ) * 0.001f;
	const int mouseX = static_cast< int >( frameSeconds * static_cast< float >( inputState.Gamepad.sThumbRX ) / 100.0f );
	const int mouseY = -static_cast< int >( frameSeconds * static_cast< float >( inputState.Gamepad.sThumbRY ) / 100.0f );
	if ( mouseX != 0 || mouseY != 0 ) {
		sys->QueEvent( SE_CONTROLLER_MOUSE, mouseX, mouseY, 0, NULL );
	}
	lastFrameTime = com_frameTime;

	if ( memcmp( &inputState.Gamepad, &oldInputState.Gamepad, sizeof( inputState.Gamepad ) ) != 0 ) {
		for ( int bit = 0; bit < 16; bit++ ) {
			const WORD mask = static_cast< WORD >( 1 << bit );
			if ( ( inputState.Gamepad.wButtons & mask ) != ( oldInputState.Gamepad.wButtons & mask ) && XInputButtonRemap[ bit ] >= 0 ) {
				PostInputEvent( XInputButtonRemap[ bit ], ( inputState.Gamepad.wButtons & mask ) != 0 );
			}
		}

		if ( isGamePad ) {
			ConvertCircleInputToSquare( inputState.Gamepad.sThumbRX, inputState.Gamepad.sThumbRY );
			ConvertCircleInputToSquare( inputState.Gamepad.sThumbLX, inputState.Gamepad.sThumbLY );
		}

		if ( inputState.Gamepad.sThumbRX != oldInputState.Gamepad.sThumbRX ) PostInputEvent( C_AXIS1, inputState.Gamepad.sThumbRX );
		if ( inputState.Gamepad.sThumbRY != oldInputState.Gamepad.sThumbRY ) PostInputEvent( C_AXIS2, -inputState.Gamepad.sThumbRY );
		if ( inputState.Gamepad.sThumbLX != oldInputState.Gamepad.sThumbLX ) PostInputEvent( C_AXIS3, inputState.Gamepad.sThumbLX );
		if ( inputState.Gamepad.sThumbLY != oldInputState.Gamepad.sThumbLY ) PostInputEvent( C_AXIS4, -inputState.Gamepad.sThumbLY );
		if ( inputState.Gamepad.bLeftTrigger != oldInputState.Gamepad.bLeftTrigger ) PostInputEvent( C_LEFT_TRIGGER, inputState.Gamepad.bLeftTrigger > 127 );
		if ( inputState.Gamepad.bRightTrigger != oldInputState.Gamepad.bRightTrigger ) PostInputEvent( C_RIGHT_TRIGGER, inputState.Gamepad.bRightTrigger > 127 );

		oldInputState = inputState;
	}

	return numEvents;
}

sdControllerAPIXInput::sdControllerAPIXInput() : XInputDll( NULL ) {
}

bool sdControllerAPIXInput::LoadAPI() {
	common->Printf( "...calling LoadLibrary( '%s' ): ", "xinput1_3.dll" );
	XInputDll = LoadLibraryA( "xinput1_3.dll" );
	if ( XInputDll == NULL ) {
		common->Printf( "failed\n" );
		return false;
	}
	common->Printf( "succeeded\n" );
	common->Printf( "...initializing XInput\n" );

	return LoadXInputProc( XInputDll, sdXInputGetState, "XInputGetState" ) &&
		LoadXInputProc( XInputDll, sdXInputSetState, "XInputSetState" ) &&
		LoadXInputProc( XInputDll, sdXInputGetCapabilities, "XInputGetCapabilities" ) &&
		LoadXInputProc( XInputDll, sdXInputEnable, "XInputEnable" ) &&
		LoadXInputProc( XInputDll, sdXInputGetDSoundAudioDeviceGuids, "XInputGetDSoundAudioDeviceGuids" ) &&
		LoadXInputProc( XInputDll, sdXInputGetBatteryInformation, "XInputGetBatteryInformation" ) &&
		LoadXInputProc( XInputDll, sdXInputGetKeystroke, "XInputGetKeystroke" );
}

void sdControllerAPIXInput::FreeAPI() {
	if ( XInputDll != NULL ) {
		common->Printf( "...unloading XInput DLL\n" );
		FreeLibrary( XInputDll );
		XInputDll = NULL;
	}
	sdXInputGetState = NULL;
	sdXInputSetState = NULL;
	sdXInputGetCapabilities = NULL;
	sdXInputEnable = NULL;
	sdXInputGetDSoundAudioDeviceGuids = NULL;
	sdXInputGetBatteryInformation = NULL;
	sdXInputGetKeystroke = NULL;
}

void sdControllerAPIXInput::Shutdown() {
	FreeAPI();
	sdControllerAPI::Shutdown();
}

void sdControllerAPIXInput::Init( const int apiIndex ) {
	if ( !LoadAPI() ) {
		FreeAPI();
		state = CAS_INIT_FAILED;
		return;
	}

	sdControllerManager& manager = sys->GetControllerManager();
	const int oldControllerCount = manager.GetMaxControllers();
	for ( int controllerIndex = 0; controllerIndex < GetMaxControllers(); controllerIndex++ ) {
		sdXInputController* controller = new sdXInputController;
		controller->SetAPITypeIndex( apiIndex );
		controller->SetIndex( controllerIndex );
		if ( controller->Init() ) {
			manager.AddController( *controller );
		} else {
			delete controller;
		}
	}

	const int controllersAdded = manager.GetMaxControllers() - oldControllerCount;
	common->Printf( "...found %d controller port%s\n", controllersAdded, controllersAdded == 1 ? "" : "s" );
	common->Printf( "controllers: %s initialized.\n", GetName() );
	state = CAS_SUPPORTED;
}
