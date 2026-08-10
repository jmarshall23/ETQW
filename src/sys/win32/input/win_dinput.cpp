// Copyright (C) 2007 Id Software, Inc.
//


#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../sys_local.h"
#include "../win_local.h"
#include "win_dinput.h"
#include "win_xinput.h"

#include <wbemidl.h>
#include <oleauto.h>

extern int com_frameTime;

namespace {

const CLSID ETQW_CLSID_WbemLocator = {
	0x4590f811, 0x1d3a, 0x11d0, { 0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 }
};

const IID ETQW_IID_IWbemLocator = {
	0xdc12a687, 0x737f, 0x11cf, { 0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 }
};

int AxisForObject( const GUID& type, const int* axes, int numAxes ) {
	if ( IsEqualGUID( type, GUID_XAxis ) ) return 0;
	if ( IsEqualGUID( type, GUID_YAxis ) ) return 1;
	if ( IsEqualGUID( type, GUID_ZAxis ) ) return 2;
	if ( IsEqualGUID( type, GUID_RxAxis ) ) return 3;
	if ( IsEqualGUID( type, GUID_RyAxis ) ) return 4;
	if ( IsEqualGUID( type, GUID_RzAxis ) ) return 5;
	if ( IsEqualGUID( type, GUID_Slider ) ) {
		int numSliders = 0;
		for ( int i = 0; i < numAxes; i++ ) {
			if ( axes[ i ] >= 6 ) {
				numSliders++;
			}
		}
		if ( numSliders < 2 ) {
			return 6 + numSliders;
		}
	}
	return -1;
}

void DecodePOV( DWORD pov, int& up, int& down, int& left, int& right ) {
	up = down = left = right = 0;
	if ( LOWORD( pov ) == 0xffff ) {
		return;
	}

	if ( pov <= 2250 || pov > 33750 ) up = 1;
	if ( pov > 2250 && pov <= 6750 ) { up = 1; right = 1; }
	if ( pov > 6750 && pov <= 11250 ) right = 1;
	if ( pov > 11250 && pov <= 15750 ) { down = 1; right = 1; }
	if ( pov > 15750 && pov <= 20250 ) down = 1;
	if ( pov > 20250 && pov <= 24750 ) { down = 1; left = 1; }
	if ( pov > 24750 && pov <= 29250 ) left = 1;
	if ( pov > 29250 && pov <= 33750 ) { up = 1; left = 1; }
}

} // namespace

sdDInputController::sdDInputController() :
	numEvents( 0 ),
	numAxes( 0 ),
	lastFrameTime( 0 ),
	controllerDevice( NULL ) {
	memset( events, 0, sizeof( events ) );
	for ( int i = 0; i < MAX_CONTROLLER_AXES; i++ ) {
		axes[ i ] = -1;
	}
	memset( &oldInputState, 0, sizeof( oldInputState ) );
	state = CS_NOT_CONNECTED;
}

BOOL CALLBACK sdDInputController::EnumObjects( const DIDEVICEOBJECTINSTANCEA* object, void* context ) {
	sdDInputController* controller = static_cast< sdDInputController* >( context );
	if ( ( object->dwType & DIDFT_AXIS ) == 0 ) {
		return DIENUM_CONTINUE;
	}

	if ( object->dwFlags == DIDOI_ASPECTPOSITION && controller->numAxes < MAX_CONTROLLER_AXES ) {
		const int axis = AxisForObject( object->guidType, controller->axes, controller->numAxes );
		if ( axis != -1 ) {
			controller->axes[ controller->numAxes++ ] = axis;
		}
	}

	DIPROPRANGE range;
	memset( &range, 0, sizeof( range ) );
	range.diph.dwSize = sizeof( range );
	range.diph.dwHeaderSize = sizeof( range.diph );
	range.diph.dwObj = object->dwType;
	range.diph.dwHow = DIPH_BYID;
	range.lMin = -32768;
	range.lMax = 32768;
	if ( FAILED( controller->controllerDevice->SetProperty( DIPROP_RANGE, &range.diph ) ) ) {
		common->Printf( "controller: Couldn't set DI axis range\n" );
		return DIENUM_STOP;
	}

	DIPROPDWORD deadZone;
	memset( &deadZone, 0, sizeof( deadZone ) );
	deadZone.diph.dwSize = sizeof( deadZone );
	deadZone.diph.dwHeaderSize = sizeof( deadZone.diph );
	deadZone.diph.dwObj = object->dwType;
	deadZone.diph.dwHow = DIPH_BYID;
	deadZone.dwData = 2500;
	if ( FAILED( controller->controllerDevice->SetProperty( DIPROP_DEADZONE, &deadZone.diph ) ) ) {
		common->Printf( "controller: Couldn't set DI axis deadzone\n" );
		return DIENUM_STOP;
	}

	return DIENUM_CONTINUE;
}

void sdDInputController::UpdateState() {
	state = CS_OK;
}

bool sdDInputController::Init( IDirectInputDevice8A*& device ) {
	controllerDevice = device;

	DIDEVICEINSTANCEA deviceInfo;
	memset( &deviceInfo, 0, sizeof( deviceInfo ) );
	deviceInfo.dwSize = sizeof( deviceInfo );
	if ( FAILED( controllerDevice->GetDeviceInfo( &deviceInfo ) ) ) {
		common->Printf( "controller: Couldn't obtain device info\n" );
		return false;
	}

	idStr::Copynz( name, deviceInfo.tszInstanceName, sizeof( name ) );
	int guidMiddle;
	unsigned short guidTail;
	memcpy( &guidMiddle, &deviceInfo.guidInstance.Data2, sizeof( guidMiddle ) );
	memcpy( &guidTail, deviceInfo.guidInstance.Data4, sizeof( guidTail ) );
	hash = idStr::Hash( va( "%s_%i%i", name, guidMiddle, guidTail ) ) & 0x7fffffff;
	common->Printf( "...found '%s': hash = %i\n", name, hash );

	if ( FAILED( controllerDevice->SetDataFormat( &c_dfDIJoystick2 ) ) ) {
		common->Printf( "controller: Couldn't set DI controller format\n" );
		return false;
	}
	if ( FAILED( controllerDevice->SetCooperativeLevel( win32.hWnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND ) ) ) {
		common->Printf( "controller: Couldn't set DI coop level\n" );
		return false;
	}

	numAxes = 0;
	for ( int i = 0; i < MAX_CONTROLLER_AXES; i++ ) {
		axes[ i ] = -1;
	}
	lastFrameTime = com_frameTime;
	return SUCCEEDED( controllerDevice->EnumObjects( EnumObjects, this, DIDFT_ALL ) );
}

int sdDInputController::ReturnInputEvent( const int n, int& action, int& value ) {
	if ( static_cast< unsigned int >( n ) >= MAX_CONTROLLER_EVENTS ) {
		return 0;
	}
	action = events[ n ].event;
	value = events[ n ].value;
	return 1;
}

void sdDInputController::PostInputEvent( int event, int value ) {
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

int sdDInputController::PollInputEvents() {
	numEvents = 0;
	DIJOYSTATE2 inputState;
	memset( &inputState, 0, sizeof( inputState ) );

	HRESULT result = controllerDevice->Poll();
	if ( FAILED( result ) ) {
		do {
			result = controllerDevice->Acquire();
		} while ( result == DIERR_INPUTLOST );
		result = DIERR_NOTACQUIRED;
	} else {
		result = controllerDevice->GetDeviceState( sizeof( inputState ), &inputState );
	}

	if ( FAILED( result ) ) {
		if ( state == CS_OK ) {
			for ( int event = C_BUTTON1; event <= C_AXIS_MAX; event++ ) {
				PostInputEvent( event, 0 );
			}
		}
		state = CS_NOT_CONNECTED;
		return numEvents;
	}

	state = CS_OK;
	const float frameSeconds = static_cast< float >( com_frameTime - lastFrameTime ) * 0.001f;
	const int mouseX = static_cast< int >( static_cast< float >( inputState.lX ) * frameSeconds / 100.0f );
	const int mouseY = static_cast< int >( static_cast< float >( inputState.lY ) * frameSeconds / 100.0f );
	if ( mouseX != 0 || mouseY != 0 ) {
		sys->QueEvent( SE_CONTROLLER_MOUSE, mouseX, mouseY, 0, NULL );
	}
	lastFrameTime = com_frameTime;

	for ( int button = 0; button < 32; button++ ) {
		if ( inputState.rgbButtons[ button ] != oldInputState.rgbButtons[ button ] ) {
			PostInputEvent( C_BUTTON1 + button, ( inputState.rgbButtons[ button ] & 0x80 ) != 0 );
		}
	}

	if ( inputState.rgdwPOV[ 0 ] != oldInputState.rgdwPOV[ 0 ] ) {
		int up, down, left, right;
		DecodePOV( inputState.rgdwPOV[ 0 ], up, down, left, right );
		PostInputEvent( C_DPAD_UP, up );
		PostInputEvent( C_DPAD_DOWN, down );
		PostInputEvent( C_DPAD_LEFT, left );
		PostInputEvent( C_DPAD_RIGHT, right );
	}

	const LONG* newAxes = &inputState.lX;
	const LONG* oldAxes = &oldInputState.lX;
	for ( int axis = 0; axis < numAxes; axis++ ) {
		const int sourceAxis = axes[ axis ];
		if ( newAxes[ sourceAxis ] != oldAxes[ sourceAxis ] ) {
			PostInputEvent( C_AXIS1 + axis, newAxes[ sourceAxis ] );
		}
	}

	oldInputState = inputState;
	return numEvents;
}

bool sdControllerAPIDInput::IsXInputDevice( const GUID* productGuid ) {
	if ( sys->GetControllerManager().GetAPIState( "XInput" ) != CAS_SUPPORTED ) {
		return false;
	}

	IWbemLocator* locator = NULL;
	IWbemServices* services = NULL;
	IEnumWbemClassObject* enumerator = NULL;
	BSTR nameSpace = NULL;
	BSTR className = NULL;
	BSTR deviceIDName = NULL;
	bool found = false;

	if ( FAILED( CoCreateInstance( ETQW_CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, ETQW_IID_IWbemLocator, reinterpret_cast< void** >( &locator ) ) ) || locator == NULL ) {
		goto cleanup;
	}

	nameSpace = SysAllocString( L"\\\\.\\root\\cimv2" );
	className = SysAllocString( L"Win32_PNPEntity" );
	deviceIDName = SysAllocString( L"DeviceID" );
	if ( nameSpace == NULL || className == NULL || deviceIDName == NULL ) {
		goto cleanup;
	}

	if ( FAILED( locator->ConnectServer( nameSpace, NULL, NULL, NULL, 0, NULL, NULL, &services ) ) || services == NULL ) {
		goto cleanup;
	}
	if ( FAILED( CoSetProxyBlanket( services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE ) ) ) {
		goto cleanup;
	}
	if ( FAILED( services->CreateInstanceEnum( className, 0, NULL, &enumerator ) ) || enumerator == NULL ) {
		goto cleanup;
	}

	for ( ;; ) {
		IWbemClassObject* devices[ 20 ] = {};
		ULONG numDevices = 0;
		const HRESULT nextResult = enumerator->Next( 10000, 20, devices, &numDevices );
		if ( FAILED( nextResult ) || numDevices == 0 ) {
			break;
		}

		for ( ULONG i = 0; i < numDevices; i++ ) {
			VARIANT value;
			VariantInit( &value );
			if ( SUCCEEDED( devices[ i ]->Get( deviceIDName, 0, &value, NULL, NULL ) ) &&
				value.vt == VT_BSTR && value.bstrVal != NULL && wcsstr( value.bstrVal, L"IG_" ) != NULL ) {
				unsigned int vendor = 0;
				unsigned int product = 0;
				const wchar_t* vendorText = wcsstr( value.bstrVal, L"VID_" );
				const wchar_t* productText = wcsstr( value.bstrVal, L"PID_" );
				if ( vendorText != NULL && productText != NULL &&
					swscanf( vendorText, L"VID_%4X", &vendor ) == 1 &&
					swscanf( productText, L"PID_%4X", &product ) == 1 &&
					MAKELONG( vendor, product ) == productGuid->Data1 ) {
					found = true;
				}
			}
			VariantClear( &value );
			devices[ i ]->Release();
			if ( found ) {
				for ( ULONG j = i + 1; j < numDevices; j++ ) {
					devices[ j ]->Release();
				}
				break;
			}
		}
		if ( found ) {
			break;
		}
	}

cleanup:
	if ( enumerator != NULL ) enumerator->Release();
	if ( services != NULL ) services->Release();
	if ( locator != NULL ) locator->Release();
	if ( nameSpace != NULL ) SysFreeString( nameSpace );
	if ( className != NULL ) SysFreeString( className );
	if ( deviceIDName != NULL ) SysFreeString( deviceIDName );
	return found;
}

BOOL CALLBACK sdControllerAPIDInput::EnumControllers( const DIDEVICEINSTANCEA* deviceInfo, void* context ) {
	if ( IsXInputDevice( &deviceInfo->guidProduct ) ) {
		return DIENUM_CONTINUE;
	}

	IDirectInputDevice8A* device = NULL;
	if ( win32.g_pdi == NULL || FAILED( win32.g_pdi->CreateDevice( deviceInfo->guidInstance, &device, NULL ) ) ) {
		return DIENUM_CONTINUE;
	}

	sdDInputController* controller = new sdDInputController;
	if ( controller->Init( device ) ) {
		sys->GetControllerManager().AddController( *controller );
	} else {
		device->Release();
		delete controller;
	}
	return DIENUM_CONTINUE;
}

void sdControllerAPIDInput::Init( const int apiIndex ) {
	if ( win32.g_pdi == NULL ) {
		state = CAS_INIT_FAILED;
		return;
	}

	common->Printf( "...initializing %s\n", GetName() );
	sdControllerManager& manager = sys->GetControllerManager();
	const int oldControllerCount = manager.GetMaxControllers();
	win32.g_pdi->EnumDevices( DI8DEVCLASS_GAMECTRL, EnumControllers, NULL, DIEDFL_ATTACHEDONLY );

	int controllerIndex = 0;
	for ( int i = oldControllerCount; i < manager.GetMaxControllers(); i++ ) {
		sdController& controller = manager.GetController( i );
		controller.SetAPITypeIndex( apiIndex );
		controller.SetIndex( controllerIndex++ );
	}

	const int controllersAdded = manager.GetMaxControllers() - oldControllerCount;
	common->Printf( "...found %d controller%s\n", controllersAdded, controllersAdded == 1 ? "" : "s" );
	common->Printf( "controllers: %s initialized.\n", GetName() );
	state = CAS_SUPPORTED;
}

void sdControllerAPIDInput::Shutdown() {
	sdControllerAPI::Shutdown();
}
