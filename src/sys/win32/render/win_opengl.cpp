// Copyright (C) 2007 Id Software, Inc.

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "win_opengl.h"
#include "../../sys_public.h"
#include "../../sys_local.h"
#include "../win_local.h"

#include <GL/gl.h>
#include <SDL_syswm.h>

// The retail renderer exports the legacy QGL entry points as writable
// function pointers.  The reconstructed WGL backend links to opengl32
// directly, so initialize the core entry points used outside the renderer to
// their system implementations.
void ( APIENTRY *qglBegin )( GLenum mode ) = glBegin;
void ( APIENTRY *qglEnd )( void ) = glEnd;
void ( APIENTRY *qglNormal3fv )( const GLfloat* values ) = glNormal3fv;
void ( APIENTRY *qglVertex3fv )( const GLfloat* values ) = glVertex3fv;

namespace {

const char* ETQW_WINDOW_CLASS = "ETQW";

void QueueMouseWheel( int delta ) {
	const int steps = delta / WHEEL_DELTA;
	const mouseButton_t button = steps >= 0 ? M_MWHEELUP : M_MWHEELDOWN;
	for ( int i = abs( steps ); i > 0; i-- ) {
		sys->QueEvent( SE_MOUSE_BUTTON, button, 1, 0, NULL );
		sys->QueEvent( SE_MOUSE_BUTTON, button, 0, 0, NULL );
	}
}

void QueueRawMouseInput( LPARAM inputHandle ) {
	UINT size = 0;
	if ( GetRawInputData( reinterpret_cast< HRAWINPUT >( inputHandle ), RID_INPUT, NULL, &size, sizeof( RAWINPUTHEADER ) ) != 0 || size < sizeof( RAWINPUTHEADER ) ) {
		return;
	}

	RAWINPUT* input = reinterpret_cast< RAWINPUT* >( _alloca( size ) );
	if ( GetRawInputData( reinterpret_cast< HRAWINPUT >( inputHandle ), RID_INPUT, input, &size, sizeof( RAWINPUTHEADER ) ) != size ||
		input->header.dwType != RIM_TYPEMOUSE || !sys->Mouse().IsActive() ) {
		return;
	}

	const bool swappedButtons = GetSystemMetrics( SM_SWAPBUTTON ) != 0;
	const USHORT buttonFlags = input->data.mouse.usButtonFlags;
	for ( int physicalButton = 0; physicalButton < 5; physicalButton++ ) {
		int logicalButton = physicalButton;
		if ( swappedButtons && physicalButton < 2 ) {
			logicalButton = 1 - physicalButton;
		}
		const USHORT downFlag = static_cast< USHORT >( 1 << ( physicalButton * 2 ) );
		const USHORT upFlag = static_cast< USHORT >( 1 << ( physicalButton * 2 + 1 ) );
		if ( buttonFlags & downFlag ) {
			sys->QueEvent( SE_MOUSE_BUTTON, M_MOUSE1 + logicalButton, 1, 0, NULL );
		} else if ( buttonFlags & upFlag ) {
			sys->QueEvent( SE_MOUSE_BUTTON, M_MOUSE1 + logicalButton, 0, 0, NULL );
		}
	}

	if ( buttonFlags & RI_MOUSE_WHEEL ) {
		QueueMouseWheel( static_cast< short >( input->data.mouse.usButtonData ) );
	}
	if ( ( input->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) == 0 &&
		( input->data.mouse.lLastX != 0 || input->data.mouse.lLastY != 0 ) ) {
		sys->QueEvent( SE_MOUSE, input->data.mouse.lLastX, input->data.mouse.lLastY, 0, NULL );
	}
}

void QueueKeyboardEvent( UINT message, WPARAM wParam, LPARAM lParam ) {
	const unsigned int scanCode = ( static_cast< unsigned int >( lParam ) >> 16 ) & 0xff;
	const bool extended = ( static_cast< unsigned int >( lParam ) & ( 1u << 24 ) ) != 0;
	const bool isDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	const bool isRepeat = isDown && ( static_cast< unsigned int >( lParam ) & ( 1u << 30 ) ) != 0;
	keyNum_t key = scanCode == 41 ? K_CONSOLE : sys->Keyboard().ConvertScanToKey( scanCode | ( extended ? 0xe000 : 0 ) );
	if ( key == K_INVALID || key == K_PRINT_SCR ) {
		return;
	}
	sys->QueEvent( SE_KEY, SE_KEY_VALUE( key, scanCode ), SE_KEY_VALUE2( isDown, isRepeat ), 0, NULL );
}

bool IsCursorInClientArea( HWND window ) {
	POINT cursor;
	RECT client;
	if ( !GetCursorPos( &cursor ) || !GetClientRect( window, &client ) ) {
		return true;
	}

	POINT topLeft = { client.left, client.top };
	POINT bottomRight = { client.right, client.bottom };
	if ( !ClientToScreen( window, &topLeft ) || !ClientToScreen( window, &bottomRight ) ) {
		return true;
	}

	client.left = topLeft.x;
	client.top = topLeft.y;
	client.right = bottomRight.x;
	client.bottom = bottomRight.y;
	return PtInRect( &client, cursor ) != FALSE;
}

LRESULT CALLBACK ETQWWindowProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) {
	switch ( message ) {
		case WM_CREATE:
			win32.hWnd = window;
			win32.activeApp = true;
			return 0;

		case WM_ACTIVATE:
			win32.activeApp = LOWORD( wParam ) != WA_INACTIVE;
			if ( win32.activeApp ) {
				// A click on the title bar activates the window before DefWindowProc
				// enters its move loop.  Recapturing here clips the cursor back to the
				// client area and prevents that drag from starting.  Also preserve an
				// explicit release made by the console or a long-running operation.
				const bool nonClientClick = LOWORD( wParam ) == WA_CLICKACTIVE &&
					!IsCursorInClientArea( window );
				if ( !win32.mouseReleased && !nonClientClick ) {
					sys->Mouse().GrabCursor( true );
				}
				sys->Keyboard().Activate();
			} else {
				sys->Keyboard().Deactivate();
				win32.movingWindow = false;
			}
			break;

		case WM_SETFOCUS:
			win32.activeApp = true;
			sys->Keyboard().Activate();
			break;

		case WM_KILLFOCUS:
			win32.activeApp = false;
			break;

		case WM_SETCURSOR:
			if ( LOWORD( lParam ) == HTCLIENT ) {
				// ETQW draws its own cursor while the mouse is captured.  When
				// the console releases the mouse, expose a normal Windows arrow.
				// Handling this explicitly also replaces an IDC_WAIT cursor that
				// Visual Studio may leave selected while launching under F5.
				SetCursor( sys->Mouse().IsActive() ? NULL : LoadCursorA( NULL, IDC_ARROW ) );
				return TRUE;
			}
			break;

		case WM_NCLBUTTONDOWN:
			if ( wParam == HTCAPTION || ( wParam >= HTLEFT && wParam <= HTBOTTOMRIGHT ) ) {
				// Raw-input capture and ClipCursor must be released before
				// DefWindowProc starts the modal title-bar move/resize loop.
				win32.movingWindow = true;
				sys->Mouse().Deactivate();
				ReleaseCapture();
			}
			break;

		case WM_ENTERSIZEMOVE:
			win32.movingWindow = true;
			// The system move/resize loop is modal, so IN_Frame will not get an
			// opportunity to apply movingWindow until the drag has ended.
			sys->Mouse().Deactivate();
			break;

		case WM_EXITSIZEMOVE:
			win32.movingWindow = false;
			break;

		case WM_INPUT:
			QueueRawMouseInput( lParam );
			return 0;

		case WM_MOUSEWHEEL:
			QueueMouseWheel( GET_WHEEL_DELTA_WPARAM( wParam ) );
			return 0;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			QueueKeyboardEvent( message, wParam, lParam );
			break;

		case WM_CHAR: {
			const unsigned int scanCode = ( static_cast< unsigned int >( lParam ) >> 16 ) & 0xff;
			sys->QueEvent( SE_CHAR, scanCode, static_cast< int >( wParam ), 0, NULL );
			break;
		}

		case WM_INPUTLANGCHANGE:
			common->Printf( "Changing input language to 0X%X, sub language 0X%X\n", LOWORD( lParam ), HIWORD( lParam ) );
			idKeyInput::Shutdown();
			idKeyInput::Init();
			win32.languageChanged = true;
			break;

		case WM_CLOSE:
			Sys_Quit();
			return 0;

		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;

		case WM_SIZE:
			if ( sys3D != NULL && wParam != SIZE_MINIMIZED ) {
				sys3D->WindowSizeDragged( LOWORD( lParam ), HIWORD( lParam ) );
			}
			break;

		case WM_ERASEBKGND:
			// OpenGL owns the complete client area.
			return 1;
	}

	return DefWindowProcA( window, message, wParam, lParam );
}

BOOL CALLBACK MonitorEnumProc( HMONITOR monitor, HDC, LPRECT, LPARAM data ) {
	idList< monitorInfo_t >* output = reinterpret_cast< idList< monitorInfo_t >* >( data );
	MONITORINFO info;
	memset( &info, 0, sizeof( info ) );
	info.cbSize = sizeof( info );
	if ( !GetMonitorInfoA( monitor, &info ) ) {
		return TRUE;
	}

	monitorInfo_t item;
	item.monitor.x = info.rcMonitor.left;
	item.monitor.y = info.rcMonitor.top;
	item.monitor.w = info.rcMonitor.right - info.rcMonitor.left;
	item.monitor.h = info.rcMonitor.bottom - info.rcMonitor.top;
	item.workArea.x = info.rcWork.left;
	item.workArea.y = info.rcWork.top;
	item.workArea.w = info.rcWork.right - info.rcWork.left;
	item.workArea.h = info.rcWork.bottom - info.rcWork.top;
	item.primary = ( info.dwFlags & MONITORINFOF_PRIMARY ) != 0;
	output->Append( item );
	return TRUE;
}

}

idRenderContextWGL::idRenderContextWGL() :
	window( NULL ),
	glContext( NULL ),
	defaultDC( NULL ),
	currentDC( NULL ),
	pixelFormat( 0 ) {
}

idRenderContextWGL::~idRenderContextWGL() {
	Destroy();
}

bool idRenderContextWGL::Create( const idRenderContextParms& contextParms ) {
	Destroy();
	parms = contextParms;
	window = win32.sdlWindow;
	defaultDC = contextParms.windowDC;
	if ( window == NULL || defaultDC == NULL || contextParms.offscreen ) {
		return false;
	}

	pixelFormat = GetPixelFormat( defaultDC );
	glContext = SDL_GL_CreateContext( window );
	if ( glContext == NULL || !MakeCurrent() ) {
		common->Warning( "SDL_GL_CreateContext failed: %s", SDL_GetError() );
		Destroy();
		return false;
	}
	SetAdditionalDefaultState();
	return true;
}

void idRenderContextWGL::Destroy() {
	if ( glContext != NULL ) {
		if ( SDL_GL_GetCurrentContext() == glContext ) {
			SDL_GL_MakeCurrent( window, NULL );
		}
		SDL_GL_DeleteContext( glContext );
	}
	window = NULL;
	glContext = NULL;
	defaultDC = NULL;
	currentDC = NULL;
	pixelFormat = 0;
}

bool idRenderContextWGL::MakeCurrent( dcHandle_t handle ) {
	if ( window == NULL || glContext == NULL ) {
		return false;
	}
	currentDC = handle != NULL ? handle : defaultDC;
	return currentDC != NULL && SDL_GL_MakeCurrent( window, glContext ) == 0;
}

bool idRenderContextWGL::ReleaseCurrent( dcHandle_t ) {
	if ( glContext == NULL || SDL_GL_GetCurrentContext() != glContext ) {
		return true;
	}
	const bool released = SDL_GL_MakeCurrent( window, NULL ) == 0;
	if ( released ) {
		currentDC = NULL;
	}
	return released;
}

bool idRenderContextWGL::IsValid() const {
	return window != NULL && glContext != NULL && defaultDC != NULL;
}

void idRenderContextWGL::SetAdditionalDefaultState() {
	if ( IsValid() ) {
		glDisable( GL_DITHER );
		glClearDepth( 1.0 );
	}
}

void idRenderContextWGL::ShowContext() const {
}

id3DContextWinGL sys3DLocal;
id3DContext* sys3D = &sys3DLocal;

id3DContextWinGL::id3DContextWinGL() :
	instance( GetModuleHandleA( NULL ) ),
	gameWindow( NULL ),
	gameDC( NULL ),
	openGLLibrary( NULL ),
	currentContext( NULL ) {
	memset( &gameWindowParms, 0, sizeof( gameWindowParms ) );
	gameWindowParms.width = 1280;
	gameWindowParms.height = 720;
	gameWindowParms.pixelAspect = 1.0f;
	monitors.SetGranularity( 1 );
	EnumerateMonitors();
}

id3DContextWinGL::~id3DContextWinGL() {
	Shutdown();
}

bool id3DContextWinGL::CreateGameWindow() {
	if ( ( SDL_WasInit( SDL_INIT_VIDEO ) & SDL_INIT_VIDEO ) == 0 ) {
		common->Warning( "Cannot create the game window before SDL2 video initialization" );
		return false;
	}

	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_STEREO, gameWindowParms.stereo ? 1 : 0 );
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, gameWindowParms.multiSamples.multi > 1 ? 1 : 0 );
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, gameWindowParms.multiSamples.multi > 1 ? gameWindowParms.multiSamples.multi : 0 );

	Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
		SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
	if ( gameWindowParms.fullScreen ) {
		windowFlags |= SDL_WINDOW_FULLSCREEN;
	}

	win32.sdlWindow = SDL_CreateWindow( GAME_NAME,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		gameWindowParms.width, gameWindowParms.height, windowFlags );
	if ( win32.sdlWindow == NULL ) {
		common->Warning( "SDL_CreateWindow failed: %s", SDL_GetError() );
		return false;
	}

	SDL_SysWMinfo windowInfo;
	SDL_VERSION( &windowInfo.version );
	if ( SDL_GetWindowWMInfo( win32.sdlWindow, &windowInfo ) != SDL_TRUE ||
		windowInfo.subsystem != SDL_SYSWM_WINDOWS ) {
		common->Warning( "SDL2 did not expose a Win32 window: %s", SDL_GetError() );
		SDL_DestroyWindow( win32.sdlWindow );
		win32.sdlWindow = NULL;
		return false;
	}

	gameWindow = windowInfo.info.win.window;
	win32.hWnd = gameWindow;
	gameDC = GetDC( gameWindow );
	if ( gameDC == NULL ) {
		SDL_DestroyWindow( win32.sdlWindow );
		win32.sdlWindow = NULL;
		gameWindow = NULL;
		win32.hWnd = NULL;
		return false;
	}
	win32.activeApp = true;
	return true;
}

void id3DContextWinGL::DestroyGameWindow() {
	currentContext = NULL;
	gameContext.Destroy();
	if ( gameWindow != NULL && gameDC != NULL ) {
		ReleaseDC( gameWindow, gameDC );
	}
	gameDC = NULL;
	if ( win32.sdlWindow != NULL ) {
		SDL_DestroyWindow( win32.sdlWindow );
	}
	win32.sdlWindow = NULL;
	gameWindow = NULL;
	win32.hWnd = NULL;
}

void id3DContextWinGL::InitContext( const glimpParms_t& contextParms ) {
	Shutdown();
	gameWindowParms = contextParms;
	if ( gameWindowParms.width <= 0 ) {
		gameWindowParms.width = 1280;
	}
	if ( gameWindowParms.height <= 0 ) {
		gameWindowParms.height = 720;
	}
	if ( gameWindowParms.pixelAspect <= 0.0f ) {
		gameWindowParms.pixelAspect = 1.0f;
	}

	openGLLibrary = LoadLibraryA( "opengl32.dll" );
	if ( openGLLibrary == NULL || !CreateGameWindow() ) {
		Shutdown();
		return;
	}

	idRenderContextParms renderParms(
		gameWindowParms.width,
		gameWindowParms.height,
		8, 8, 8, 8,
		24, 8,
		gameWindowParms.multiSamples,
		false,
		false,
		true,
		false,
		false,
		false,
		true
	);
	renderParms.windowDC = gameDC;
	if ( !gameContext.Create( renderParms ) ) {
		Shutdown();
		return;
	}
	currentContext = &gameContext;
}

void id3DContextWinGL::Shutdown() {
	DestroyGameWindow();
	if ( openGLLibrary != NULL ) {
		FreeLibrary( openGLLibrary );
		openGLLibrary = NULL;
	}
}

void id3DContextWinGL::RecreateContext( const glimpParms_t& parms ) {
	InitContext( parms );
	ShowGameWindow();
}

dcHandle_t id3DContextWinGL::GetGameWindow() {
	return gameDC;
}

wndHandle_t id3DContextWinGL::GetGameWindowHandle() {
	return gameWindow;
}

idRenderContext* id3DContextWinGL::GetGameRenderContext() {
	return gameContext.IsValid() ? &gameContext : NULL;
}

bool id3DContextWinGL::SetGameWindowParms( const glimpParms_t& parms ) {
	const bool wasVisible = gameWindow != NULL && IsWindowVisible( gameWindow ) != FALSE;
	RecreateContext( parms );
	if ( wasVisible ) {
		ShowGameWindow();
	}
	return gameContext.IsValid();
}

idRenderContext* id3DContextWinGL::GetCurrentRenderContext() {
	return currentContext;
}

const glimpParms_t& id3DContextWinGL::GetGameWindowParms() const {
	return gameWindowParms;
}

void id3DContextWinGL::SetPixelFormat( dcHandle_t windowDC ) {
	if ( windowDC == NULL || gameDC == NULL ) {
		return;
	}
	const int format = GetPixelFormat( gameDC );
	PIXELFORMATDESCRIPTOR descriptor;
	memset( &descriptor, 0, sizeof( descriptor ) );
	descriptor.nSize = sizeof( descriptor );
	DescribePixelFormat( gameDC, format, sizeof( descriptor ), &descriptor );
	::SetPixelFormat( windowDC, format, &descriptor );
}

GLExtension_t id3DContextWinGL::ExtensionPointer( const char* name ) {
	if ( name == NULL ) {
		return NULL;
	}
	PROC proc = reinterpret_cast< PROC >( SDL_GL_GetProcAddress( name ) );
	if ( proc == NULL && openGLLibrary != NULL ) {
		proc = GetProcAddress( openGLLibrary, name );
	}
	return reinterpret_cast< GLExtension_t >( proc );
}

void id3DContextWinGL::ShowGameWindow() {
	if ( win32.sdlWindow != NULL ) {
		SDL_ShowWindow( win32.sdlWindow );
		SDL_RaiseWindow( win32.sdlWindow );
	}
}

void id3DContextWinGL::HideGameWindow() {
	if ( win32.sdlWindow != NULL ) {
		SDL_HideWindow( win32.sdlWindow );
	}
}

bool id3DContextWinGL::IsFullscreen() {
	return gameWindowParms.fullScreen;
}

bool id3DContextWinGL::IsMinimized() {
	return win32.sdlWindow == NULL ||
		( SDL_GetWindowFlags( win32.sdlWindow ) & SDL_WINDOW_MINIMIZED ) != 0;
}

void id3DContextWinGL::WindowSizeDragged( int width, int height ) {
	if ( width > 0 ) {
		gameWindowParms.width = width;
	}
	if ( height > 0 ) {
		gameWindowParms.height = height;
	}
}

bool id3DContextWinGL::MakeCurrent( dcHandle_t windowDC ) {
	const bool result = gameContext.MakeCurrent( windowDC );
	if ( result ) {
		currentContext = &gameContext;
	}
	return result;
}

bool id3DContextWinGL::ReleaseCurrent( dcHandle_t windowDC ) {
	const bool result = gameContext.ReleaseCurrent( windowDC );
	if ( result ) {
		currentContext = NULL;
	}
	return result;
}

void id3DContextWinGL::ReleaseContext( dcHandle_t windowDC ) {
	ReleaseCurrent( windowDC );
}

void id3DContextWinGL::SwapBuffers() {
	if ( win32.sdlWindow != NULL && gameContext.IsValid() ) {
		SDL_GL_SwapWindow( win32.sdlWindow );
	}
}

void id3DContextWinGL::SetGamma( unsigned short red[ 256 ], unsigned short green[ 256 ], unsigned short blue[ 256 ] ) {
	if ( gameDC == NULL || red == NULL || green == NULL || blue == NULL ) {
		return;
	}
	unsigned short ramp[ 3 ][ 256 ];
	memcpy( ramp[ 0 ], red, sizeof( ramp[ 0 ] ) );
	memcpy( ramp[ 1 ], green, sizeof( ramp[ 1 ] ) );
	memcpy( ramp[ 2 ], blue, sizeof( ramp[ 2 ] ) );
	SDL_SetWindowGammaRamp( win32.sdlWindow, ramp[ 0 ], ramp[ 1 ], ramp[ 2 ] );
}

bool id3DContextWinGL::IsDisplayModeAvailable( int width, int height ) {
	DEVMODEA mode;
	memset( &mode, 0, sizeof( mode ) );
	mode.dmSize = sizeof( mode );
	for ( DWORD index = 0; EnumDisplaySettingsA( NULL, index, &mode ); index++ ) {
		if ( static_cast< int >( mode.dmPelsWidth ) == width && static_cast< int >( mode.dmPelsHeight ) == height ) {
			return true;
		}
	}
	return false;
}

int id3DContextWinGL::GetNumMSAAModes() const {
	return 1;
}

const char* id3DContextWinGL::GetMSAAMode( int idx, int& val ) const {
	if ( idx != 0 ) {
		val = 0;
		return "";
	}
	val = 0;
	return "Off";
}

bool id3DContextWinGL::IsMSAACountAvailable( int msaa ) const {
	return msaa == 0;
}

int id3DContextWinGL::GetNumMonitors() const {
	return monitors.Num();
}

const monitorInfo_t& id3DContextWinGL::GetMonitor( int index ) const {
	if ( index < 0 || index >= monitors.Num() ) {
		return GetPrimaryMonitor();
	}
	return monitors[ index ];
}

const monitorInfo_t& id3DContextWinGL::GetPrimaryMonitor() const {
	for ( int index = 0; index < monitors.Num(); index++ ) {
		if ( monitors[ index ].primary ) {
			return monitors[ index ];
		}
	}
	static monitorInfo_t fallback;
	if ( monitors.Num() > 0 ) {
		return monitors[ 0 ];
	}
	fallback.monitor.x = 0;
	fallback.monitor.y = 0;
	fallback.monitor.w = GetSystemMetrics( SM_CXSCREEN );
	fallback.monitor.h = GetSystemMetrics( SM_CYSCREEN );
	fallback.workArea = fallback.monitor;
	fallback.primary = true;
	return fallback;
}

void id3DContextWinGL::ConstrainToPrimaryMonitor( int& width, int& height ) {
	const monitorInfo_t& monitor = GetPrimaryMonitor();
	if ( width <= 0 || height <= 0 || monitor.workArea.w <= 0 || monitor.workArea.h <= 0 ) {
		return;
	}
	const float aspect = static_cast< float >( width ) / static_cast< float >( height );
	if ( width > monitor.workArea.w ) {
		width = monitor.workArea.w;
		height = static_cast< int >( width / aspect );
	}
	if ( height > monitor.workArea.h ) {
		height = monitor.workArea.h;
		width = static_cast< int >( height * aspect );
	}
}

void id3DContextWinGL::EnumerateMonitors() {
	monitors.Clear();
	EnumDisplayMonitors( NULL, NULL, MonitorEnumProc, reinterpret_cast< LPARAM >( &monitors ) );
}
