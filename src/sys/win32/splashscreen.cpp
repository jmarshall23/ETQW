// Copyright (C) 2007 Id Software, Inc.
//
// Enemy Territory: QUAKE Wars Win32 startup splash screen, reconstructed
// from the retail symbols and implementation.  Window userdata uses the
// pointer-width-safe Win32 API so this source works in both x86 and x64.

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "splashscreen.h"
#include "rc/etqw_resource.h"

namespace {

const char* const SPLASH_WINDOW_CLASS = "SDSPLASHSCREEN";
sdSplashScreen* splashScreen = NULL;

} // namespace

sdSplashScreen::sdSplashScreen( HINSTANCE instance ) :
	bitmap( NULL ),
	region( NULL ),
	hWnd( NULL ),
	hInstance( instance ),
	pfnAnimateWindow( NULL ) {
	memset( &bitmapInfo, 0, sizeof( bitmapInfo ) );

	HMODULE user32 = GetModuleHandleA( "USER32.DLL" );
	if ( user32 != NULL ) {
		pfnAnimateWindow = reinterpret_cast< animateWindow_t >( GetProcAddress( user32, "AnimateWindow" ) );
	}

	if ( hInstance != NULL ) {
		Create( hInstance );
	}
}

sdSplashScreen::~sdSplashScreen() {
	if ( hWnd != NULL ) {
		HWND window = hWnd;
		hWnd = NULL;
		SetWindowLongPtrA( window, GWLP_USERDATA, 0 );
		DestroyWindow( window );
	}
	Destroy();
}

void sdSplashScreen::DrawWindow( HWND, HDC dc ) const {
	if ( bitmap == NULL || dc == NULL ) {
		return;
	}

	HDC bitmapDC = CreateCompatibleDC( dc );
	if ( bitmapDC == NULL ) {
		return;
	}

	HGDIOBJ oldBitmap = SelectObject( bitmapDC, bitmap );
	BitBlt( dc, 0, 0, bitmapInfo.bmWidth, bitmapInfo.bmHeight, bitmapDC, 0, 0, SRCCOPY );
	SelectObject( bitmapDC, oldBitmap );
	DeleteDC( bitmapDC );
}

HRGN sdSplashScreen::CreateRgnFromBitmap( HWND window, HBITMAP sourceBitmap, const BITMAP& sourceInfo, COLORREF maskColor ) {
	if ( sourceBitmap == NULL ) {
		return NULL;
	}

	HDC windowDC = GetDC( window );
	if ( windowDC == NULL ) {
		return NULL;
	}
	HDC bitmapDC = CreateCompatibleDC( windowDC );
	if ( bitmapDC == NULL ) {
		ReleaseDC( window, windowDC );
		return NULL;
	}

	HGDIOBJ oldBitmap = SelectObject( bitmapDC, sourceBitmap );
	HRGN result = CreateRectRgn( 0, 0, 0, 0 );
	if ( result != NULL ) {
		for ( int y = sourceInfo.bmHeight - 1; y >= 0; --y ) {
			int x = 0;
			while ( x < sourceInfo.bmWidth ) {
				while ( x < sourceInfo.bmWidth && GetPixel( bitmapDC, x, y ) == maskColor ) {
					++x;
				}
				const int first = x;
				while ( x < sourceInfo.bmWidth && GetPixel( bitmapDC, x, y ) != maskColor ) {
					++x;
				}
				if ( first < x ) {
					HRGN run = CreateRectRgn( first, y, x, y + 1 );
					if ( run != NULL ) {
						CombineRgn( result, result, run, RGN_OR );
						DeleteObject( run );
					}
				}
			}
		}
	}

	SelectObject( bitmapDC, oldBitmap );
	DeleteDC( bitmapDC );
	ReleaseDC( window, windowDC );
	return result;
}

bool sdSplashScreen::SetBitmap( unsigned int bitmapIdentifier, COLORREF maskColor ) {
	if ( bitmap != NULL ) {
		DeleteObject( bitmap );
		bitmap = NULL;
	}
	if ( region != NULL ) {
		DeleteObject( region );
		region = NULL;
	}

	bitmap = LoadBitmapA( hInstance, MAKEINTRESOURCEA( bitmapIdentifier ) );
	if ( bitmap == NULL || hWnd == NULL ) {
		return false;
	}

	if ( GetObjectA( bitmap, sizeof( bitmapInfo ), &bitmapInfo ) == 0 ) {
		DeleteObject( bitmap );
		bitmap = NULL;
		return false;
	}

	// The retail code used SM_CXFULLSCREEN/SM_CYFULLSCREEN and implicitly
	// assumed that the primary monitor began at ( 0, 0 ).  Keep the same
	// primary-monitor placement while using its real desktop coordinates; this
	// remains centered with DPI scaling and non-trivial monitor arrangements.
	RECT monitorRect;
	SetRect( &monitorRect, 0, 0, GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) );
	POINT primaryOrigin = { 0, 0 };
	MONITORINFO monitorInfo;
	memset( &monitorInfo, 0, sizeof( monitorInfo ) );
	monitorInfo.cbSize = sizeof( monitorInfo );
	HMONITOR monitor = MonitorFromPoint( primaryOrigin, MONITOR_DEFAULTTOPRIMARY );
	if ( monitor != NULL && GetMonitorInfoA( monitor, &monitorInfo ) != FALSE ) {
		monitorRect = monitorInfo.rcMonitor;
	}
	const int x = monitorRect.left + ( ( monitorRect.right - monitorRect.left ) - bitmapInfo.bmWidth ) / 2;
	const int y = monitorRect.top + ( ( monitorRect.bottom - monitorRect.top ) - bitmapInfo.bmHeight ) / 2;
	MoveWindow( hWnd, x, y, bitmapInfo.bmWidth, bitmapInfo.bmHeight, TRUE );

	region = CreateRgnFromBitmap( hWnd, bitmap, bitmapInfo, maskColor );
	if ( region != NULL && SetWindowRgn( hWnd, region, TRUE ) != 0 ) {
		// SetWindowRgn transfers ownership of the region to the window manager.
		region = NULL;
	}
	return true;
}

bool sdSplashScreen::Show() {
	if ( hWnd == NULL || bitmap == NULL ) {
		return false;
	}

	if ( pfnAnimateWindow != NULL ) {
		pfnAnimateWindow( hWnd, 500, AW_BLEND );
	} else {
		ShowWindow( hWnd, SW_SHOWNORMAL );
	}
	UpdateWindow( hWnd );
	return true;
}

void sdSplashScreen::Hide() {
	if ( hWnd == NULL ) {
		return;
	}
	if ( pfnAnimateWindow != NULL && IsWindowVisible( hWnd ) != FALSE ) {
		pfnAnimateWindow( hWnd, 300, AW_HIDE | AW_BLEND );
	} else {
		ShowWindow( hWnd, SW_HIDE );
	}
}

void sdSplashScreen::Destroy() {
	if ( bitmap != NULL ) {
		DeleteObject( bitmap );
		bitmap = NULL;
	}
	if ( region != NULL ) {
		DeleteObject( region );
		region = NULL;
	}
	memset( &bitmapInfo, 0, sizeof( bitmapInfo ) );
}

ATOM sdSplashScreen::RegisterClassA() const {
	WNDCLASSEXA windowClass;
	memset( &windowClass, 0, sizeof( windowClass ) );
	windowClass.cbSize = sizeof( windowClass );
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WndProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursorA( NULL, IDC_ARROW );
	windowClass.hbrBackground = reinterpret_cast< HBRUSH >( COLOR_WINDOW + 1 );
	windowClass.lpszClassName = SPLASH_WINDOW_CLASS;
	return ::RegisterClassExA( &windowClass );
}

HWND sdSplashScreen::Create( HINSTANCE instance ) {
	hInstance = instance;
	if ( RegisterClassA() == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS ) {
		return NULL;
	}

	hWnd = CreateWindowExA(
		WS_EX_TOOLWINDOW,
		SPLASH_WINDOW_CLASS,
		"",
		WS_POPUP,
		CW_USEDEFAULT,
		0,
		CW_USEDEFAULT,
		0,
		NULL,
		NULL,
		hInstance,
		this
	);
	if ( hWnd != NULL ) {
		SetWindowTextA( hWnd, "ETQW" );
	}
	return hWnd;
}

LRESULT CALLBACK sdSplashScreen::WndProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) {
	sdSplashScreen* splash = reinterpret_cast< sdSplashScreen* >( GetWindowLongPtrA( window, GWLP_USERDATA ) );
	if ( message == WM_NCCREATE ) {
		const CREATESTRUCTA* createInfo = reinterpret_cast< const CREATESTRUCTA* >( lParam );
		splash = static_cast< sdSplashScreen* >( createInfo->lpCreateParams );
		SetWindowLongPtrA( window, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( splash ) );
	}

	switch ( message ) {
		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT: {
			PAINTSTRUCT paint;
			HDC dc = BeginPaint( window, &paint );
			if ( splash != NULL ) {
				splash->DrawWindow( window, dc );
			}
			EndPaint( window, &paint );
			return 0;
		}

		case WM_PRINTCLIENT:
			if ( splash != NULL ) {
				splash->DrawWindow( window, reinterpret_cast< HDC >( wParam ) );
			}
			return 1;

		case WM_NCDESTROY:
			SetWindowLongPtrA( window, GWLP_USERDATA, 0 );
			if ( splash != NULL && splash->hWnd == window ) {
				splash->hWnd = NULL;
			}
			break;
	}

	return DefWindowProcA( window, message, wParam, lParam );
}

void Sys_ShowSplashScreen( bool show ) {
	if ( show ) {
		if ( splashScreen != NULL ) {
			delete splashScreen;
			splashScreen = NULL;
		}

		splashScreen = new sdSplashScreen( GetModuleHandleA( NULL ) );
		if ( !splashScreen->SetBitmap( IDB_ETQW_SPLASH, RGB( 255, 0, 255 ) ) || !splashScreen->Show() ) {
			delete splashScreen;
			splashScreen = NULL;
		}
		return;
	}

	if ( splashScreen != NULL ) {
		splashScreen->Hide();
		delete splashScreen;
		splashScreen = NULL;
	}
}
