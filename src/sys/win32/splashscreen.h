// Copyright (C) 2007 Id Software, Inc.
//
// Enemy Territory: QUAKE Wars Win32 startup splash screen.

#ifndef __SYS_WIN32_SPLASHSCREEN_H__
#define __SYS_WIN32_SPLASHSCREEN_H__

#include <windows.h>

class sdSplashScreen {
public:
	explicit			sdSplashScreen( HINSTANCE instance );
						~sdSplashScreen();

	bool				SetBitmap( unsigned int bitmapIdentifier, COLORREF maskColor );
	bool				Show();
	void				Hide();

private:
	typedef BOOL ( WINAPI *animateWindow_t )( HWND window, DWORD time, DWORD flags );

	void				DrawWindow( HWND window, HDC dc ) const;
	static HRGN			CreateRgnFromBitmap( HWND window, HBITMAP bitmap, const BITMAP& bitmapInfo, COLORREF maskColor );
	void				Destroy();
	ATOM				RegisterClassA() const;
	HWND				Create( HINSTANCE instance );
	static LRESULT CALLBACK WndProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam );

	HBITMAP				bitmap;
	HRGN				region;
	HWND				hWnd;
	HINSTANCE			hInstance;
	BITMAP				bitmapInfo;
	animateWindow_t		pfnAnimateWindow;
};

#endif // __SYS_WIN32_SPLASHSCREEN_H__
