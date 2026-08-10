// Copyright (C) 2007 Id Software, Inc.
//
// Win32 OpenGL context boundary reconstructed at the source paths recorded in
// the Microsoft ETQW PDB.  This intentionally implements the public sys_render
// contract first; extension discovery and the full retail renderer backend can
// be layered on top without changing Common or Session.

#ifndef __SYS_WIN32_RENDER_WIN_OPENGL_H__
#define __SYS_WIN32_RENDER_WIN_OPENGL_H__

#include "../../sys_render.h"

class idRenderContextWGL : public idRenderContext {
public:
							idRenderContextWGL();
	virtual					~idRenderContextWGL();

	virtual bool			Create( const idRenderContextParms& parms );
	virtual void			Destroy();
	virtual bool			MakeCurrent( dcHandle_t handle = NULL );
	virtual bool			IsValid() const;
	virtual void			SetAdditionalDefaultState();
	virtual void			ShowContext() const;

	bool					ReleaseCurrent( dcHandle_t handle = NULL );

private:
	HGLRC					glContext;
	HDC						defaultDC;
	HDC						currentDC;
	int						pixelFormat;
};

class id3DContextWinGL : public id3DContext {
public:
							id3DContextWinGL();
	virtual					~id3DContextWinGL();

	virtual void			InitContext( const glimpParms_t& parms );
	virtual void			Shutdown();
	virtual void			RecreateContext( const glimpParms_t& parms );
	virtual dcHandle_t		GetGameWindow();
	virtual wndHandle_t		GetGameWindowHandle();
	virtual idRenderContext*	GetGameRenderContext();
	virtual bool			SetGameWindowParms( const glimpParms_t& parms );
	virtual idRenderContext*	GetCurrentRenderContext();
	virtual const glimpParms_t& GetGameWindowParms() const;
	virtual void			SetPixelFormat( dcHandle_t windowDC );
	virtual GLExtension_t	ExtensionPointer( const char* name );
	virtual void			ShowGameWindow();
	virtual void			HideGameWindow();
	virtual bool			IsFullscreen();
	virtual bool			IsMinimized();
	virtual void			WindowSizeDragged( int width, int height );
	virtual bool			MakeCurrent( dcHandle_t windowDC );
	virtual bool			ReleaseCurrent( dcHandle_t windowDC );
	virtual void			ReleaseContext( dcHandle_t windowDC );
	virtual void			SwapBuffers();
	virtual void			SetGamma( unsigned short red[ 256 ], unsigned short green[ 256 ], unsigned short blue[ 256 ] );
	virtual bool			IsDisplayModeAvailable( int width, int height );
	virtual int				GetNumMSAAModes() const;
	virtual const char*		GetMSAAMode( int idx, int& val ) const;
	virtual bool			IsMSAACountAvailable( int msaa ) const;
	virtual int				GetNumMonitors() const;
	virtual const monitorInfo_t& GetMonitor( int index ) const;
	virtual const monitorInfo_t& GetPrimaryMonitor() const;
	virtual void			ConstrainToPrimaryMonitor( int& width, int& height );
	virtual void			EnumerateMonitors();

private:
	bool					CreateGameWindow();
	void					DestroyGameWindow();

	HINSTANCE				instance;
	HWND					gameWindow;
	HDC						gameDC;
	HMODULE					openGLLibrary;
	idRenderContextWGL		gameContext;
	idRenderContext*		currentContext;
	glimpParms_t			gameWindowParms;
	idList< monitorInfo_t >	monitors;
};

extern id3DContextWinGL sys3DLocal;

#endif /* !__SYS_WIN32_RENDER_WIN_OPENGL_H__ */
