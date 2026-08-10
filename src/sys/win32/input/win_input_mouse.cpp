// Copyright (C) 2007 Id Software, Inc.
//


#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../sys_local.h"
#include "../win_local.h"
#include "win_input_mouse.h"

namespace {

typedef BOOL ( WINAPI* registerRawInputDevices_t )( PCRAWINPUTDEVICE, UINT, UINT );
typedef UINT ( WINAPI* getRawInputData_t )( HRAWINPUT, UINT, LPVOID, PUINT, UINT );

registerRawInputDevices_t registerRawInputDevices;
getRawInputData_t getRawInputData;

idMouseDInput mouseDInput;
idMouseRawInput mouseRawInput;
idMouse* activeMouse;

void HideMouseCursor() {
	for ( int i = 0; i < 10; i++ ) {
		if ( ShowCursor( FALSE ) < 0 ) {
			break;
		}
	}
}

void ShowMouseCursor() {
	for ( int i = 0; i < 10; i++ ) {
		if ( ShowCursor( TRUE ) >= 0 ) {
			break;
		}
	}
}

void ClipMouseToWindow() {
	RECT rect;
	if ( GetClientRect( win32.hWnd, &rect ) ) {
		ClientToScreen( win32.hWnd, reinterpret_cast< POINT* >( &rect.left ) );
		ClientToScreen( win32.hWnd, reinterpret_cast< POINT* >( &rect.right ) );
		ClipCursor( &rect );
	}
}

void SetMouseGrab( bool grab ) {
	win32.mouseReleased = !grab;
	if ( !grab ) {
		IN_Frame();
	}
}

} // namespace

idCVar m_rawInput( "m_rawInput", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_ARCHIVE, "use Raw Input API for mouse input" );

idMouseDInput::idMouseDInput() : active( false ), mouseDevice( NULL ) {
	memset( polled_didod, 0, sizeof( polled_didod ) );
}

idMouseDInput::~idMouseDInput() {
	Shutdown();
}

bool idMouseDInput::Init() {
	if ( win32.g_pdi == NULL ) {
		return false;
	}
	if ( mouseDevice != NULL ) {
		Shutdown();
	}

	if ( FAILED( win32.g_pdi->CreateDevice( GUID_SysMouse, &mouseDevice, NULL ) ) ) {
		common->Printf( "mouse: Couldn't open DI mouse device\n" );
		return false;
	}
	if ( FAILED( mouseDevice->SetDataFormat( &c_dfDIMouse2 ) ) ) {
		common->Printf( "mouse: Couldn't set DI mouse format\n" );
		return false;
	}
	if ( FAILED( mouseDevice->SetCooperativeLevel( win32.hWnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE ) ) ) {
		common->Printf( "mouse: Couldn't set DI coop level\n" );
		return false;
	}

	DIPROPDWORD bufferSize;
	memset( &bufferSize, 0, sizeof( bufferSize ) );
	bufferSize.diph.dwSize = sizeof( bufferSize );
	bufferSize.diph.dwHeaderSize = sizeof( bufferSize.diph );
	bufferSize.diph.dwObj = 0;
	bufferSize.diph.dwHow = DIPH_DEVICE;
	bufferSize.dwData = directInputBufferSize;
	if ( FAILED( mouseDevice->SetProperty( DIPROP_BUFFERSIZE, &bufferSize.diph ) ) ) {
		common->Printf( "mouse: Couldn't set DI buffersize\n" );
		return false;
	}

	Activate();
	PollInputEvents( false );
	common->Printf( "mouse: DirectInput initialized.\n" );
	activeMouse = this;
	return true;
}

void idMouseDInput::Shutdown() {
	Deactivate();
	if ( mouseDevice != NULL ) {
		mouseDevice->Release();
		mouseDevice = NULL;
	}
	activeMouse = NULL;
}

void idMouseDInput::Activate() {
	if ( active || mouseDevice == NULL ) {
		return;
	}
	active = true;
	HideMouseCursor();
	ClipMouseToWindow();
	mouseDevice->SetCooperativeLevel( win32.hWnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE );
	mouseDevice->Acquire();
}

void idMouseDInput::Deactivate() {
	if ( !active ) {
		return;
	}
	if ( mouseDevice != NULL ) {
		mouseDevice->Unacquire();
		ShowMouseCursor();
		ClipCursor( NULL );
		active = false;
	}
}

void idMouseDInput::GrabCursor( bool grab ) {
	SetMouseGrab( grab );
}

int idMouseDInput::ReturnInputEvent( const int n, int& action, int& value ) {
	if ( static_cast< unsigned int >( n ) >= directInputBufferSize ) {
		return 0;
	}

	const DIDEVICEOBJECTDATA& event = polled_didod[ n ];
	if ( event.dwOfs >= DIMOFS_BUTTON0 && event.dwOfs <= DIMOFS_BUTTON7 ) {
		value = ( event.dwData & 0x80 ) != 0;
		action = event.dwOfs - DIMOFS_BUTTON0;
		return 1;
	}

	switch ( event.dwOfs ) {
		case DIMOFS_X:
			value = static_cast< int >( event.dwData );
			action = 8;
			return 1;
		case DIMOFS_Y:
			value = static_cast< int >( event.dwData );
			action = 9;
			return 1;
		case DIMOFS_Z:
			value = static_cast< int >( event.dwData ) / WHEEL_DELTA;
			action = 10;
			return value != 0;
		default:
			return 0;
	}
}

void idMouseDInput::QueueEvents( int numEvents ) {
	DWORD currentSequence = 0;
	bool haveSequence = false;
	int mouseX = 0;
	int mouseY = 0;

	for ( int i = 0; i < numEvents; i++ ) {
		const DIDEVICEOBJECTDATA& event = polled_didod[ i ];
		if ( !haveSequence || static_cast< int >( event.dwSequence - currentSequence ) > 0 ) {
			if ( mouseX != 0 || mouseY != 0 ) {
				sys->QueEvent( SE_MOUSE, mouseX, mouseY, 0, NULL );
			}
			mouseX = mouseY = 0;
			currentSequence = event.dwSequence;
			haveSequence = true;
		}

		if ( event.dwOfs >= DIMOFS_BUTTON0 && event.dwOfs <= DIMOFS_BUTTON7 ) {
			sys->QueEvent( SE_MOUSE_BUTTON, event.dwOfs - DIMOFS_BUTTON0 + M_MOUSE1, ( event.dwData & 0x80 ) != 0, 0, NULL );
		} else if ( event.dwOfs == DIMOFS_X ) {
			mouseX = static_cast< int >( event.dwData );
		} else if ( event.dwOfs == DIMOFS_Y ) {
			mouseY = static_cast< int >( event.dwData );
		} else if ( event.dwOfs == DIMOFS_Z ) {
			const int wheelDelta = static_cast< int >( event.dwData ) / WHEEL_DELTA;
			const mouseButton_t wheelButton = wheelDelta >= 0 ? M_MWHEELUP : M_MWHEELDOWN;
			for ( int step = abs( wheelDelta ); step > 0; step-- ) {
				sys->QueEvent( SE_MOUSE_BUTTON, wheelButton, 1, 0, NULL );
				sys->QueEvent( SE_MOUSE_BUTTON, wheelButton, 0, 0, NULL );
			}
		}
	}

	if ( mouseX != 0 || mouseY != 0 ) {
		sys->QueEvent( SE_MOUSE, mouseX, mouseY, 0, NULL );
	}
}

int idMouseDInput::PollInputEvents( bool postEvents ) {
	if ( mouseDevice == NULL || !active ) {
		return 0;
	}

	DWORD numEvents = directInputBufferSize;
	HRESULT result = mouseDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), polled_didod, &numEvents, 0 );
	if ( FAILED( result ) ) {
		result = mouseDevice->Acquire();
		if ( FAILED( result ) ) {
			return 0;
		}
		numEvents = directInputBufferSize;
		result = mouseDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), polled_didod, &numEvents, 0 );
		if ( FAILED( result ) ) {
			return 0;
		}
	}

	if ( postEvents ) {
		QueueEvents( numEvents );
	}
	return numEvents;
}

idMouseRawInput::idMouseRawInput() : initialized( false ), active( false ), user32Dll( NULL ) {
}

idMouseRawInput::~idMouseRawInput() {
	Shutdown();
}

bool idMouseRawInput::LoadAPI() {
	common->Printf( "...calling LoadLibrary( '%s' ): ", "user32.dll" );
	user32Dll = LoadLibraryA( "user32.dll" );
	if ( user32Dll == NULL ) {
		common->Printf( "failed\n" );
		return false;
	}
	common->Printf( "succeeded\n" );
	common->Printf( "...initializing Raw Input\n" );

	registerRawInputDevices = reinterpret_cast< registerRawInputDevices_t >( GetProcAddress( user32Dll, "RegisterRawInputDevices" ) );
	if ( registerRawInputDevices == NULL ) {
		common->Warning( "Couldn't find proc address for: RegisterRawInputDevices\n" );
		return false;
	}
	getRawInputData = reinterpret_cast< getRawInputData_t >( GetProcAddress( user32Dll, "GetRawInputData" ) );
	if ( getRawInputData == NULL ) {
		common->Warning( "Couldn't find proc address for: GetRawInputData\n" );
		return false;
	}
	return true;
}

void idMouseRawInput::FreeAPI() {
	if ( user32Dll != NULL ) {
		common->Printf( "...unloading Raw Input DLL\n" );
		FreeLibrary( user32Dll );
		user32Dll = NULL;
	}
	registerRawInputDevices = NULL;
	getRawInputData = NULL;
}

bool idMouseRawInput::Init() {
	if ( initialized ) {
		Shutdown();
	}
	if ( !LoadAPI() ) {
		FreeAPI();
		return false;
	}

	RAWINPUTDEVICE device;
	device.usUsagePage = 1;
	device.usUsage = 2;
	device.dwFlags = RIDEV_NOLEGACY;
	device.hwndTarget = win32.hWnd;
	if ( !registerRawInputDevices( &device, 1, sizeof( device ) ) ) {
		common->Printf( "mouse: Failed to register raw input device\n" );
		FreeAPI();
		return false;
	}

	initialized = true;
	common->Printf( "mouse: Raw Input initialized.\n" );
	activeMouse = this;
	return true;
}

void idMouseRawInput::Shutdown() {
	if ( !initialized ) {
		return;
	}
	Deactivate();

	RAWINPUTDEVICE device;
	device.usUsagePage = 1;
	device.usUsage = 2;
	device.dwFlags = RIDEV_REMOVE;
	device.hwndTarget = NULL;
	if ( registerRawInputDevices != NULL && !registerRawInputDevices( &device, 1, sizeof( device ) ) ) {
		common->Printf( "mouse: Failed to remove raw input device\n" );
	}

	FreeAPI();
	initialized = false;
	activeMouse = NULL;
}

void idMouseRawInput::Activate() {
	if ( active || !initialized ) {
		return;
	}
	active = true;
	HideMouseCursor();
	ClipMouseToWindow();
}

void idMouseRawInput::Deactivate() {
	if ( !active || !initialized ) {
		return;
	}
	ShowMouseCursor();
	ClipCursor( NULL );
	active = false;
}

void idMouseRawInput::GrabCursor( bool grab ) {
	SetMouseGrab( grab );
}

idMouse& idSysLocal::Mouse() {
	if ( activeMouse != NULL ) {
		return *activeMouse;
	}
	if ( !cvarSystem->IsInitialized() || m_rawInput.GetBool() ) {
		return mouseRawInput;
	}
	return mouseDInput;
}

