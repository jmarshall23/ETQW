#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "qe3.h"
#include "MainFrm.h"
#include "LightDlg.h"
#include "RadiantImGui.h"
#include "SurfaceDlg.h"
#include "MegaTextureEditorImGui.h"
#include "RadiantImGuiVulkan.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND window, UINT message, WPARAM wParam, LPARAM lParam );

namespace {

static const char *RADIANT_IMGUI_WINDOW_CLASS = "DarklightRadiantImGui";
static const UINT_PTR RADIANT_IMGUI_FRAME_TIMER = 1;

static void RadiantOpenTracePath( char path[MAX_PATH] ) {
	GetModuleFileNameA( NULL, path, MAX_PATH );
	char *slash = strrchr( path, '\\' );
	if ( slash == NULL ) {
		slash = strrchr( path, '/' );
	}
	if ( slash != NULL ) {
		slash[1] = '\0';
	} else {
		path[0] = '\0';
	}
	idStr::Append( path, MAX_PATH, "radiant_open_trace.log" );
}

static void RadiantOpenTraceWrite( const char *eventName, const char *detail ) {
	char path[MAX_PATH];
	RadiantOpenTracePath( path );
	FILE *trace = fopen( path, "a" );
	if ( trace == NULL ) {
		return;
	}
	SYSTEMTIME now;
	GetLocalTime( &now );
	fprintf( trace, "%02u:%02u:%02u.%03u | %-24s | %s\n", now.wHour, now.wMinute,
		now.wSecond, now.wMilliseconds, eventName != NULL ? eventName : "",
		detail != NULL ? detail : "" );
	fflush( trace );
	fclose( trace );
}

static void RadiantOpenTraceReset() {
	char path[MAX_PATH];
	RadiantOpenTracePath( path );
	FILE *trace = fopen( path, "w" );
	if ( trace != NULL ) {
		fclose( trace );
	}
	RadiantOpenTraceWrite( "launch", "ETQW Radiant open-map trace started" );
}

// Dear ImGui stores the current context globally.  Radiant and the GUI editor
// share this thread, and opening/focusing one window can synchronously enter
// its window procedure while the other editor is still building a frame.
// Restore the caller's context on every exit from those nested operations.
class ScopedImGuiContext {
public:
	explicit ScopedImGuiContext( ImGuiContext *context ) : previous( ImGui::GetCurrentContext() ) {
		ImGui::SetCurrentContext( context );
	}

	~ScopedImGuiContext() {
		ImGui::SetCurrentContext( previous );
	}

private:
	ImGuiContext *previous;
};

idCVar radiant_useImGui( "radiant_useImGui", "1", CVAR_TOOL | CVAR_ARCHIVE,
	"deprecated; Radiant now always uses the Dear ImGui window shell" );
idCVar radiant_imguiQuadLayout( "radiant_imguiQuadLayout", "0", CVAR_TOOL | CVAR_ARCHIVE,
	"use the four-view ImGui Radiant workspace" );
idCVar radiant_imguiShowInspector( "radiant_imguiShowInspector", "1", CVAR_TOOL | CVAR_ARCHIVE,
	"show the ImGui Radiant inspector pane" );
idCVar radiant_imguiShowZ( "radiant_imguiShowZ", "1", CVAR_TOOL | CVAR_ARCHIVE,
	"show the ImGui Radiant Z pane" );

static void RenderImGuiDrawData( ImDrawData *drawData ) {
	RadiantImGuiVulkanRenderDrawData( drawData );
}

class LegacyViewportTexture {
public:
	LegacyViewportTexture() : width( 0 ), height( 0 ) {}

	void Destroy() {
		RadiantImGuiVulkanDestroyViewport( this );
		width = height = 0;
	}

	bool Prepare( int requestedWidth, int requestedHeight ) {
		width = Max( requestedWidth, 1 );
		height = Max( requestedHeight, 1 );
		return true;
	}

	void Queue( radiantImGuiViewportType_t type, void* view ) {
		RadiantImGuiVulkanQueueViewport( type, view, this, width, height );
	}

	ImTextureRef TextureRef() const {
		return RadiantImGuiVulkanTexture( this );
	}

	ImVec2 UV1() const {
		return RadiantImGuiVulkanViewportUV1( this, width, height );
	}

private:
	int width;
	int height;
};

class RadiantImGuiHost {
public:
	RadiantImGuiHost();
	~RadiantImGuiHost();

	bool Create();
	void Destroy();
	void Focus();
	void Frame();
	void ShowInspectorTab( int mode );
	void ShowMegaTextureInspector();
	void ShowLightEditor();
	void RefreshLightEditor();
	void ShowSurfaceInspector();
	void RefreshSurfaceInspector();
	void ShowPatchInspector();
	void RefreshPatchInspector();
	LRESULT WindowProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam );
	HWND Window() const { return hwnd; }

private:
	static LRESULT CALLBACK StaticWindowProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam );
	bool InitializeRenderer();
	void ShutdownRenderer();
	void MakeCurrent();
	void RestoreEngineContext();
	void RenderShell();
	void RenderIconToolbar();
	bool EnsureToolbarAtlas();
	void RenderNativeMenuBar();
	void RenderNativeMenuItems( HMENU menu );
	void RenderXYContextMenu( CXYWnd *view, const ImVec2 &minimum );
	void RenderXYContextMenuItems( CXYWnd *view, HMENU menu );
	void RenderViewportChild( const char *id, const char *title, CXYWnd *view,
		LegacyViewportTexture &surface, const ImVec2 &size, int captureId );
	void RenderCameraPanel( const ImVec2 &size );
	void RenderXYPanel( CXYWnd *view, LegacyViewportTexture &surface, const ImVec2 &size, int captureId );
	void RenderZPanel( const ImVec2 &size );
	void RenderInspectorPanel();
	void RenderTexturePanel( const ImVec2 &size );
	void RenderMediaBrowser();
	void RenderMediaTreeItem( CDialogTextures &browser, HTREEITEM item );
	void RenderMediaPreview( const ImVec2 &size );
	void RenderEntityInspector();
	void BeginOpenMapDialog();
	void RenderOpenMapDialog();
	void BeginSaveMapDialog( bool region );
	void RenderSaveMapDialog();
	void RenderPreferencesWindow();
	void RenderSurfaceInspectorWindow();
	void RenderLightEditorWindow();
	void RenderPatchInspectorWindow();
	void SyncSurfaceInspector();
	void LoadLightEditorSelection();
	void LoadPatchInspectorSelection();
	void DispatchCommand( UINT command );
	void HandleCameraInput( CCamWnd *view, bool hovered, const ImVec2 &minimum );
	void HandleXYInput( CXYWnd *view, bool hovered, const ImVec2 &minimum, int captureId );
	void HandleZInput( CZWnd *view, bool hovered, const ImVec2 &minimum );
	void HandleTextureInput( CNewTexWnd *view, bool hovered, const ImVec2 &minimum );
	void HandleMediaPreviewInput( idGLWidget *view, bool hovered, const ImVec2 &minimum );
	UINT MouseButtons() const;

	HWND hwnd;
	HDC dc;
	ImGuiContext *imguiContext;
	HMENU commandMenu;
	bool rendererReady;
	bool rendering;
	bool quadLayout;
	bool showZView;
	bool showInspector;
	bool showToolbar;
	bool showStatusBar;
	bool showPreferences;
	bool showSurfaceInspector;
	bool showLightEditor;
	bool showPatchInspector;
	bool requestLightFocus;
	bool requestSurfaceRefresh;
	bool requestLightRefresh;
	bool requestPatchRefresh;
	bool requestOpenMapDialog;
	bool requestSaveMapDialog;
	bool saveMapRegion;
	bool saveOverwritePending;
	int requestedInspector;
	bool requestMegaTextureInspector;
	bool cameraInputHovered;
	float uiScale;
	LegacyViewportTexture cameraSurface;
	LegacyViewportTexture xySurface;
	LegacyViewportTexture xzSurface;
	LegacyViewportTexture yzSurface;
	LegacyViewportTexture zSurface;
	LegacyViewportTexture textureSurface;
	LegacyViewportTexture mediaSurface;
	GLuint toolbarAtlas;
	HIMAGELIST toolbarImageList;
	int toolbarAtlasWidth;
	int toolbarAtlasHeight;
	int toolbarIconWidth;
	int toolbarIconHeight;
	int toolbarAtlasColumns;
	int toolbarIconCount;
	int capturedView;
	CXYWnd *contextPressView;
	CXYWnd *contextMenuRequestView;
	int contextPressX;
	int contextPressY;
	bool contextPressMoved;
	char consoleCommand[256];
	char openMapDirectory[MAX_PATH];
	char openMapPath[MAX_PATH];
	char openMapError[256];
	char pendingMapLoad[MAX_PATH];
	char saveMapDirectory[MAX_PATH];
	char saveMapPath[MAX_PATH];
	char saveMapError[256];
	char pendingMapSave[MAX_PATH];
	bool pendingMapSaveRegion;
	bool pendingMapSaveAs;
	int profileFramesRemaining;
	entity_t *inspectedEntity;
	char entityKey[128];
	char entityValue[1024];
	char entityClassFilter[128];
	char surfaceMaterial[256];
	float surfaceScale[2];
	float surfaceShiftStep[2];
	float surfaceRotate;
	float surfaceFit[2];
	CLightInfo lightInfo;
	CLightInfo originalLightInfo;
	int selectedLightCount;
	idList<idStr> lightMaterials;
	char lightMaterial[256];
	patchMesh_t *inspectedPatch;
	int patchRow;
	int patchColumn;
	float patchPosition[3];
	float patchTexcoord[2];
	float patchTransformStep[5];
};

static RadiantImGuiHost *radiantImGuiHost = NULL;

RadiantImGuiHost::RadiantImGuiHost() :
	hwnd( NULL ), dc( NULL ), imguiContext( NULL ), commandMenu( NULL ), rendererReady( false ), rendering( false ),
	quadLayout( false ), showZView( true ), showInspector( true ), showToolbar( true ), showStatusBar( true ),
	showPreferences( false ), showSurfaceInspector( false ), showLightEditor( false ), showPatchInspector( false ),
	requestLightFocus( false ),
	requestSurfaceRefresh( false ), requestLightRefresh( false ), requestPatchRefresh( false ), requestOpenMapDialog( false ),
	requestSaveMapDialog( false ), saveMapRegion( false ), saveOverwritePending( false ),
	requestedInspector( 0 ), requestMegaTextureInspector( false ), cameraInputHovered( false ),
	uiScale( 1.0f ), toolbarAtlas( 0 ), toolbarImageList( NULL ), toolbarAtlasWidth( 0 ), toolbarAtlasHeight( 0 ),
	toolbarIconWidth( 0 ), toolbarIconHeight( 0 ), toolbarAtlasColumns( 0 ), toolbarIconCount( 0 ),
	capturedView( 0 ), contextPressView( NULL ), contextMenuRequestView( NULL ), contextPressX( 0 ), contextPressY( 0 ),
	contextPressMoved( false ), inspectedEntity( NULL ), surfaceRotate( 0.0f ), selectedLightCount( 0 ),
	inspectedPatch( NULL ), patchRow( 0 ), patchColumn( 0 ) {
	consoleCommand[0] = '\0';
	openMapDirectory[0] = '\0';
	openMapPath[0] = '\0';
	openMapError[0] = '\0';
	pendingMapLoad[0] = '\0';
	saveMapDirectory[0] = '\0';
	saveMapPath[0] = '\0';
	saveMapError[0] = '\0';
	pendingMapSave[0] = '\0';
	pendingMapSaveRegion = false;
	pendingMapSaveAs = false;
	profileFramesRemaining = 0;
	entityKey[0] = '\0';
	entityValue[0] = '\0';
	entityClassFilter[0] = '\0';
	surfaceMaterial[0] = '\0';
	lightMaterial[0] = '\0';
	surfaceScale[0] = surfaceScale[1] = 0.5f;
	surfaceShiftStep[0] = surfaceShiftStep[1] = 8.0f;
	surfaceFit[0] = surfaceFit[1] = 1.0f;
	patchPosition[0] = patchPosition[1] = patchPosition[2] = 0.0f;
	patchTexcoord[0] = patchTexcoord[1] = 0.0f;
	patchTransformStep[0] = patchTransformStep[1] = patchTransformStep[3] = patchTransformStep[4] = 0.05f;
	patchTransformStep[2] = 45.0f;
}

RadiantImGuiHost::~RadiantImGuiHost() {
	Destroy();
}

bool RadiantImGuiHost::Create() {
	RadiantOpenTraceReset();
	// The legacy MFC frame already exists at this point. Changing the UI thread
	// to per-monitor DPI awareness here makes GetClientRect return a different
	// coordinate space for the parent and creates an oversized, clipped child.
	// Keep the frame's original DPI context and scale ImGui's style explicitly.
	HINSTANCE instance = win32.hInstance;
	// ETQW Radiant always opens in its primary Camera + XY + Z workspace. The
	// archived Darklight four-view setting must not change the startup layout.
	quadLayout = false;
	radiant_imguiQuadLayout.SetBool( false );
	showInspector = radiant_imguiShowInspector.GetBool();
	showZView = true;
	radiant_imguiShowZ.SetBool( true );

	WNDCLASSEXA windowClass;
	ZeroMemory( &windowClass, sizeof( windowClass ) );
	windowClass.cbSize = sizeof( windowClass );
	windowClass.style = CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = StaticWindowProc;
	windowClass.hInstance = instance;
	windowClass.hCursor = LoadCursor( NULL, IDC_ARROW );
	windowClass.hIcon = LoadIcon( instance, MAKEINTRESOURCE( IDR_MAINFRAME ) );
	windowClass.hbrBackground = NULL;
	windowClass.lpszClassName = RADIANT_IMGUI_WINDOW_CLASS;
	RegisterClassExA( &windowClass );

	HWND parentWindow = g_pParentWnd != NULL ? g_pParentWnd->GetSafeHwnd() : NULL;
	if ( parentWindow == NULL ) {
		return false;
	}
	RECT client;
	GetClientRect( parentWindow, &client );
	hwnd = CreateWindowExA( 0, RADIANT_IMGUI_WINDOW_CLASS, "Radiant ImGui Client",
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		0, 0, Max( 1L, client.right ), Max( 1L, client.bottom ), parentWindow, NULL, instance, this );
	if ( hwnd == NULL ) {
		return false;
	}
	if ( !InitializeRenderer() ) {
		DestroyWindow( hwnd );
		hwnd = NULL;
		return false;
	}
	commandMenu = LoadMenuA( instance, MAKEINTRESOURCEA( IDR_MENU_QUAKE3 ) );
	ShowWindow( hwnd, SW_SHOW );
	UpdateWindow( hwnd );
	SetTimer( hwnd, RADIANT_IMGUI_FRAME_TIMER, 16, NULL );
	return true;
}

void RadiantImGuiHost::Destroy() {
	MegaTextureEditorImGuiShutdown();
	ShutdownRenderer();
	if ( commandMenu != NULL ) {
		DestroyMenu( commandMenu );
		commandMenu = NULL;
	}
	if ( hwnd != NULL ) {
		HWND oldWindow = hwnd;
		hwnd = NULL;
		KillTimer( oldWindow, RADIANT_IMGUI_FRAME_TIMER );
		DestroyWindow( oldWindow );
	}
}

void RadiantImGuiHost::Focus() {
	if ( hwnd == NULL ) {
		return;
	}
	profileFramesRemaining = 8;
	HWND frame = GetAncestor( hwnd, GA_ROOT );
	ShowWindow( frame, SW_RESTORE );
	SetForegroundWindow( frame );
	SetFocus( hwnd );
}

bool RadiantImGuiHost::InitializeRenderer() {
	IMGUI_CHECKVERSION();
	imguiContext = ImGui::CreateContext();
	ScopedImGuiContext scopedContext( imguiContext );
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = "radiant_imgui.ini";
	uiScale = Max( 1.0f, ImGui_ImplWin32_GetDpiScaleForHwnd( hwnd ) );
	ImFontConfig fontConfig;
	fontConfig.SizePixels = 13.0f * uiScale;
	io.Fonts->AddFontDefault( &fontConfig );
	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	ImVec4 *colors = style.Colors;
	colors[ImGuiCol_Text]                  = ImVec4( 0.88f, 0.89f, 0.90f, 1.00f );
	colors[ImGuiCol_TextDisabled]          = ImVec4( 0.50f, 0.52f, 0.54f, 1.00f );
	colors[ImGuiCol_WindowBg]              = ImVec4( 0.115f, 0.120f, 0.125f, 1.00f );
	colors[ImGuiCol_ChildBg]               = ImVec4( 0.105f, 0.110f, 0.115f, 1.00f );
	colors[ImGuiCol_PopupBg]               = ImVec4( 0.135f, 0.140f, 0.145f, 0.98f );
	colors[ImGuiCol_Border]                = ImVec4( 0.30f, 0.31f, 0.32f, 0.75f );
	colors[ImGuiCol_BorderShadow]          = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
	colors[ImGuiCol_FrameBg]               = ImVec4( 0.18f, 0.19f, 0.20f, 1.00f );
	colors[ImGuiCol_FrameBgHovered]        = ImVec4( 0.25f, 0.26f, 0.27f, 1.00f );
	colors[ImGuiCol_FrameBgActive]         = ImVec4( 0.31f, 0.32f, 0.33f, 1.00f );
	colors[ImGuiCol_TitleBg]               = ImVec4( 0.14f, 0.145f, 0.15f, 1.00f );
	colors[ImGuiCol_TitleBgActive]         = ImVec4( 0.20f, 0.21f, 0.22f, 1.00f );
	colors[ImGuiCol_MenuBarBg]             = ImVec4( 0.15f, 0.155f, 0.16f, 1.00f );
	colors[ImGuiCol_ScrollbarBg]           = ImVec4( 0.10f, 0.105f, 0.11f, 1.00f );
	colors[ImGuiCol_ScrollbarGrab]         = ImVec4( 0.30f, 0.31f, 0.32f, 1.00f );
	colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4( 0.39f, 0.40f, 0.41f, 1.00f );
	colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4( 0.47f, 0.48f, 0.49f, 1.00f );
	colors[ImGuiCol_CheckMark]             = ImVec4( 0.72f, 0.76f, 0.80f, 1.00f );
	colors[ImGuiCol_SliderGrab]            = ImVec4( 0.54f, 0.57f, 0.60f, 1.00f );
	colors[ImGuiCol_SliderGrabActive]      = ImVec4( 0.70f, 0.73f, 0.76f, 1.00f );
	colors[ImGuiCol_Button]                = ImVec4( 0.20f, 0.21f, 0.22f, 1.00f );
	colors[ImGuiCol_ButtonHovered]         = ImVec4( 0.29f, 0.30f, 0.31f, 1.00f );
	colors[ImGuiCol_ButtonActive]          = ImVec4( 0.37f, 0.38f, 0.39f, 1.00f );
	colors[ImGuiCol_Header]                = ImVec4( 0.23f, 0.24f, 0.25f, 1.00f );
	colors[ImGuiCol_HeaderHovered]         = ImVec4( 0.31f, 0.32f, 0.33f, 1.00f );
	colors[ImGuiCol_HeaderActive]          = ImVec4( 0.39f, 0.40f, 0.41f, 1.00f );
	colors[ImGuiCol_Separator]             = ImVec4( 0.30f, 0.31f, 0.32f, 0.75f );
	colors[ImGuiCol_ResizeGrip]            = ImVec4( 0.38f, 0.40f, 0.42f, 0.25f );
	colors[ImGuiCol_ResizeGripHovered]     = ImVec4( 0.55f, 0.58f, 0.61f, 0.67f );
	colors[ImGuiCol_ResizeGripActive]      = ImVec4( 0.66f, 0.69f, 0.72f, 0.95f );
	colors[ImGuiCol_Tab]                   = ImVec4( 0.17f, 0.18f, 0.19f, 1.00f );
	colors[ImGuiCol_TabHovered]            = ImVec4( 0.30f, 0.31f, 0.32f, 1.00f );
	colors[ImGuiCol_TabSelected]           = ImVec4( 0.27f, 0.28f, 0.29f, 1.00f );
	colors[ImGuiCol_TabSelectedOverline]   = ImVec4( 0.62f, 0.65f, 0.68f, 1.00f );
	colors[ImGuiCol_TableHeaderBg]         = ImVec4( 0.18f, 0.19f, 0.20f, 1.00f );
	colors[ImGuiCol_TableBorderStrong]     = ImVec4( 0.34f, 0.35f, 0.36f, 1.00f );
	colors[ImGuiCol_TableBorderLight]      = ImVec4( 0.24f, 0.25f, 0.26f, 1.00f );
	colors[ImGuiCol_TextSelectedBg]        = ImVec4( 0.39f, 0.43f, 0.47f, 0.55f );
	colors[ImGuiCol_NavCursor]             = ImVec4( 0.68f, 0.71f, 0.74f, 1.00f );
	style.WindowRounding = 3.0f;
	style.ChildRounding = 3.0f;
	style.FrameRounding = 3.0f;
	style.PopupRounding = 3.0f;
	style.ScrollbarRounding = 4.0f;
	style.ScaleAllSizes( uiScale );
	if ( !RadiantImGuiVulkanInit( hwnd ) ) {
		ShutdownRenderer();
		return false;
	}
	rendererReady = true;
	return true;
}

void RadiantImGuiHost::ShutdownRenderer() {
	if ( rendererReady || imguiContext != NULL ) {
		ImGuiContext *previousContext = ImGui::GetCurrentContext();
		ImGuiContext *destroyedContext = imguiContext;
		cameraSurface.Destroy();
		xySurface.Destroy();
		xzSurface.Destroy();
		yzSurface.Destroy();
		zSurface.Destroy();
		textureSurface.Destroy();
		mediaSurface.Destroy();
		if ( toolbarAtlas != 0 ) {
			RadiantImGuiVulkanDestroyTexture( &toolbarAtlas );
			toolbarAtlas = 0;
		}
		ImGui::SetCurrentContext( imguiContext );
		if ( rendererReady ) {
			RadiantImGuiVulkanShutdown();
		}
		if ( imguiContext != NULL ) {
			ImGui::DestroyContext( imguiContext );
			imguiContext = NULL;
		}
		rendererReady = false;
		ImGui::SetCurrentContext( previousContext == destroyedContext ? NULL : previousContext );
	}
	dc = NULL;
}

void RadiantImGuiHost::MakeCurrent() {
}

void RadiantImGuiHost::RestoreEngineContext() {
}

void RadiantImGuiHost::Frame() {
	if ( !rendererReady || rendering || hwnd == NULL || !IsWindowVisible( hwnd ) || IsIconic( hwnd ) ) {
		if ( pendingMapLoad[0] != '\0' ) {
			RadiantOpenTraceWrite( "frame blocked", va( "ready=%d rendering=%d hwnd=%p visible=%d iconic=%d pending=%s",
				rendererReady, rendering, hwnd, hwnd != NULL ? IsWindowVisible( hwnd ) : 0,
				hwnd != NULL ? IsIconic( hwnd ) : 0, pendingMapLoad ) );
		}
		return;
	}
	ScopedImGuiContext scopedContext( imguiContext );
	if ( pendingMapSave[0] != '\0' ) {
		char savePath[MAX_PATH];
		idStr::Copynz( savePath, pendingMapSave, sizeof( savePath ) );
		const bool saveRegion = pendingMapSaveRegion;
		const bool saveAs = pendingMapSaveAs;
		pendingMapSave[0] = '\0';
		pendingMapSaveRegion = false;
		pendingMapSaveAs = false;

		// Save between ImGui frames. Map_SaveFile drives the legacy progress
		// controller and may pump window messages, so keep the reentrancy guard
		// raised until it has completely finished.
		rendering = true;
		MakeCurrent();
		const bool saved = Map_SaveFile( savePath, saveRegion );
		rendering = false;
		if ( saved && saveAs && !saveRegion ) {
			idStr::Copynz( currentmap, savePath, sizeof( currentmap ) );
			AddNewItem( g_qeglobals.d_lpMruMenu, savePath );
			Sys_SetTitle( savePath );
		}
		if ( saved && g_pParentWnd != NULL ) {
			g_pParentWnd->SetTimer( QE_TIMER1, g_PrefsDlg.m_nAutoSave * 60 * 1000, NULL );
		}
	}
	if ( pendingMapLoad[0] != '\0' ) {
		char mapPath[MAX_PATH];
		idStr::Copynz( mapPath, pendingMapLoad, sizeof( mapPath ) );
		pendingMapLoad[0] = '\0';
		common->Printf( "Radiant Open: loading %s\n", mapPath );
		RadiantOpenTraceWrite( "load begin", mapPath );
		capturedView = 0;
		contextPressView = NULL;
		contextMenuRequestView = NULL;
		// Present the current editor with a small loading overlay before entering
		// the synchronous map builder. Keeping the workspace visible avoids the
		// jarring replacement of the entire editor by a temporary blank window.
		rendering = true;
		MakeCurrent();
		ImGui::SetCurrentContext( imguiContext );
		RadiantImGuiVulkanNewFrame();
		ImGui::NewFrame();
		RenderShell();
		ImGuiIO &loadingIO = ImGui::GetIO();
		ImGui::GetForegroundDrawList()->AddRectFilled( ImVec2( 0, 0 ), loadingIO.DisplaySize,
			IM_COL32( 0, 0, 0, 88 ) );
		const ImVec2 loadingSize( Min( 540.0f * uiScale, loadingIO.DisplaySize.x - 40.0f ),
			148.0f * uiScale );
		ImGui::SetNextWindowPos( ImVec2( loadingIO.DisplaySize.x * 0.5f, loadingIO.DisplaySize.y * 0.5f ),
			ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
		ImGui::SetNextWindowSize( loadingSize );
		ImGui::Begin( "Loading map##RadiantMapLoad", NULL,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs );
		idStr loadingName = mapPath;
		loadingName.StripPath();
		ImGui::TextUnformatted( loadingName.c_str() );
		ImGui::Spacing();
		ImGui::ProgressBar( -1.0f * (float)ImGui::GetTime(), ImVec2( -1.0f, 0 ),
			"Building editor geometry..." );
		ImGui::End();
		ImGui::Render();
		const float loadingClear[ 4 ] = { 0.105f, 0.110f, 0.115f, 1.0f };
		if ( RadiantImGuiVulkanBeginFrame( hwnd, loadingClear ) ) {
			RadiantImGuiVulkanRenderViewports( ImGui::GetDrawData() );
			RenderImGuiDrawData( ImGui::GetDrawData() );
			RadiantImGuiVulkanEndFrame();
		}
		// Loading destroys the old entity list. Clear the inspector's backend
		// pointer before Map_Free() so the first ImGui frame after the load can
		// never dereference the previous map's entity.
		if ( g_Inspectors != NULL ) {
			g_Inspectors->entityDlg.SetEditEntity( NULL );
		}
		inspectedEntity = NULL;
		// Run the synchronous legacy loader between ImGui frames. Keep the
		// reentrancy guard raised until all editor state has been rebuilt.
		// Keep the shared OpenGL context attached to the ImGui child while the
		// legacy map builder creates display lists and loads materials. Moving
		// the context back to Doom's hidden game DC discards the visible child
		// surface and is what left the maximized frame blank after loading.
		MakeCurrent();
		Map_LoadFile( mapPath );
		RadiantOpenTraceWrite( "load returned", currentmap );
		profileFramesRemaining = 8;
		MakeCurrent();
		ImGui::SetCurrentContext( imguiContext );
		// The legacy loader focuses one of its hidden compatibility views and the
		// restricted loading pump intentionally drops input messages. Start the
		// visible shell with clean input and reclaim keyboard focus so modifiers
		// such as Shift reach the XY bridge again after every map load.
		ImGui::GetIO().ClearInputKeys();
		ImGui::GetIO().ClearInputMouse();
		g_PrefsDlg.SavePrefs();
		rendering = false;
		if ( hwnd != NULL ) {
			HWND frame = g_pParentWnd != NULL ? g_pParentWnd->GetSafeHwnd() : NULL;
			if ( frame != NULL ) {
				if ( GetParent( hwnd ) != frame ) {
					SetParent( hwnd, frame );
				}
				ShowWindow( frame, SW_SHOW );
				g_pParentWnd->ResizeImGuiShell();
			}
			ShowWindow( hwnd, SW_SHOW );
			SetFocus( hwnd );
			InvalidateRect( hwnd, NULL, FALSE );
			// Render synchronously now that the map owns valid editor state. Do
			// not depend on MFC/Win32 deciding to deliver another paint message.
			Frame();
		}
		return;
	}
	rendering = true;
	const ULONGLONG frameStart = GetTickCount64();
	MakeCurrent();
	ImGui::SetCurrentContext( imguiContext );

	RadiantImGuiVulkanNewFrame();
	ImGui::NewFrame();
	RenderShell();
	const ULONGLONG shellEnd = GetTickCount64();
	// Legacy editor callbacks can synchronously repaint hidden compatibility controls.
	// Always reclaim the host context before submitting the ImGui frame.
	MakeCurrent();
	ImGui::SetCurrentContext( imguiContext );
	ImGui::Render();

	const float clearColor[ 4 ] = { 0.105f, 0.110f, 0.115f, 1.0f };
	if ( RadiantImGuiVulkanBeginFrame( hwnd, clearColor ) ) {
		RadiantImGuiVulkanRenderViewports( ImGui::GetDrawData() );
		RenderImGuiDrawData( ImGui::GetDrawData() );
		RadiantImGuiVulkanEndFrame();
	}
	const ULONGLONG frameEnd = GetTickCount64();
	if ( profileFramesRemaining > 0 ) {
		double viewTimings[ 5 ];
		RadiantImGuiVulkanGetViewportTimings( viewTimings );
		RadiantOpenTraceWrite( "frame profile", va( "total=%llums shell=%llums submit=%llums camera=%.2fms xy=%.2fms z=%.2fms texture=%.2fms media=%.2fms",
			frameEnd - frameStart, shellEnd - frameStart, frameEnd - shellEnd,
			viewTimings[ RADIANT_IMGUI_VIEW_CAMERA ], viewTimings[ RADIANT_IMGUI_VIEW_XY ],
			viewTimings[ RADIANT_IMGUI_VIEW_Z ], viewTimings[ RADIANT_IMGUI_VIEW_TEXTURE ],
			viewTimings[ RADIANT_IMGUI_VIEW_MEDIA ] ) );
		profileFramesRemaining--;
	}
	rendering = false;
	// Begin a load selected by this frame immediately after its completed ImGui
	// submission. The embedded MFC pump can coalesce the click's invalidation,
	// so waiting for another timer/paint made Open appear to do nothing.
	if ( pendingMapLoad[0] != '\0' ) {
		RadiantOpenTraceWrite( "immediate load frame", pendingMapLoad );
		Frame();
	}
}

void RadiantImGuiHost::ShowInspectorTab( int mode ) {
	showInspector = true;
	radiant_imguiShowInspector.SetBool( true );
	requestedInspector = mode;
	Focus();
}

void RadiantImGuiHost::ShowMegaTextureInspector() {
	showInspector = true;
	radiant_imguiShowInspector.SetBool( true );
	requestMegaTextureInspector = true;
	Focus();
}

void RadiantImGuiHost::SyncSurfaceInspector() {
	g_dlgSurface.SetTexMods();
	idStr::Copynz( surfaceMaterial, (LPCSTR)g_dlgSurface.m_strMaterial, sizeof( surfaceMaterial ) );
	surfaceScale[0] = g_dlgSurface.m_horzScale;
	surfaceScale[1] = g_dlgSurface.m_vertScale;
	surfaceShiftStep[0] = g_dlgSurface.m_horzShift;
	surfaceShiftStep[1] = g_dlgSurface.m_vertShift;
	surfaceRotate = g_dlgSurface.m_rotate;
	surfaceFit[0] = g_dlgSurface.m_fWidth;
	surfaceFit[1] = g_dlgSurface.m_fHeight;
	requestSurfaceRefresh = false;
}

void RadiantImGuiHost::ShowSurfaceInspector() {
	g_dlgSurface.BeginImGuiInspector();
	showSurfaceInspector = true;
	SyncSurfaceInspector();
	Focus();
}

void RadiantImGuiHost::RefreshSurfaceInspector() {
	if ( showSurfaceInspector ) {
		requestSurfaceRefresh = true;
	}
}

void RadiantImGuiHost::LoadLightEditorSelection() {
	selectedLightCount = LightInspector_LoadInfo( lightInfo, originalLightInfo );
	idStr::Copynz( lightMaterial, (LPCSTR)lightInfo.strTexture, sizeof( lightMaterial ) );
	lightMaterials.Clear();
	const int materialCount = declManager->GetNumDecls( DECL_MATERIAL );
	for ( int index = 0; index < materialCount; index++ ) {
		const idMaterial *material = declManager->MaterialByIndex( index, false );
		if ( material == NULL ) {
			continue;
		}
		idStr name = material->GetName();
		idStr lowerName = name;
		lowerName.ToLower();
		if ( !lowerName.Icmpn( "lights/", 7 ) || !lowerName.Icmpn( "fogs/", 5 ) ) {
			lightMaterials.Append( name );
		}
	}
	requestLightRefresh = false;
}

void RadiantImGuiHost::ShowLightEditor() {
	showLightEditor = true;
	requestLightFocus = true;
	LoadLightEditorSelection();
	Focus();
}

void RadiantImGuiHost::RefreshLightEditor() {
	if ( showLightEditor ) {
		requestLightRefresh = true;
	}
}

void RadiantImGuiHost::LoadPatchInspectorSelection() {
	inspectedPatch = SinglePatchSelected();
	if ( inspectedPatch == NULL ) {
		patchRow = patchColumn = 0;
		patchPosition[0] = patchPosition[1] = patchPosition[2] = 0.0f;
		patchTexcoord[0] = patchTexcoord[1] = 0.0f;
		requestPatchRefresh = false;
		return;
	}
	patchRow = Max( 0, Min( inspectedPatch->height - 1, patchRow ) );
	patchColumn = Max( 0, Min( inspectedPatch->width - 1, patchColumn ) );
	const idDrawVert &control = inspectedPatch->ctrl( patchColumn, patchRow );
	for ( int component = 0; component < 3; component++ ) patchPosition[component] = control.xyz[component];
	const idVec2 controlST = control.GetST();
	patchTexcoord[0] = controlST[0];
	patchTexcoord[1] = controlST[1];
	requestPatchRefresh = false;
}

void RadiantImGuiHost::ShowPatchInspector() {
	showPatchInspector = true;
	LoadPatchInspectorSelection();
	Focus();
}

void RadiantImGuiHost::RefreshPatchInspector() {
	if ( showPatchInspector ) {
		requestPatchRefresh = true;
	}
}

void RadiantImGuiHost::RenderShell() {
	ImGuiIO &io = ImGui::GetIO();
	ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
	ImGui::SetNextWindowSize( io.DisplaySize );
	ImGui::Begin( "ETQW Radiant Root", NULL,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	if ( ImGui::BeginMenuBar() ) {
		RenderNativeMenuBar();
		if ( ImGui::BeginMenu( "Editors" ) ) {
			const bool megaTextureOpen = MegaTextureEditorImGuiIsOpen();
			if ( ImGui::MenuItem( "MegaTexture Terrain Editor...", NULL,
				megaTextureOpen ) ) {
				if ( megaTextureOpen ) {
					MegaTextureEditorImGuiHide();
				} else {
					MegaTextureEditorImGuiShow();
					ShowMegaTextureInspector();
				}
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( "Light Editor...", "J" ) ) {
				ShowLightEditor();
			}
			if ( ImGui::MenuItem( "Surface Inspector...", "S" ) ) {
				ShowSurfaceInspector();
			}
			if ( ImGui::MenuItem( "Patch Inspector...", "Shift+S" ) ) {
				ShowPatchInspector();
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Workspace" ) ) {
			if ( ImGui::MenuItem( "Four views", NULL, quadLayout ) ) {
				quadLayout = true;
				radiant_imguiQuadLayout.SetBool( true );
			}
			if ( ImGui::MenuItem( "XY + Camera", NULL, !quadLayout ) ) {
				quadLayout = false;
				radiant_imguiQuadLayout.SetBool( false );
			}
			ImGui::Separator();
			ImGui::MenuItem( "Toolbar", NULL, &showToolbar );
			ImGui::MenuItem( "Status bar", NULL, &showStatusBar );
			if ( ImGui::MenuItem( "Z view", NULL, &showZView ) ) radiant_imguiShowZ.SetBool( showZView );
			if ( ImGui::MenuItem( "Inspector", NULL, &showInspector ) ) radiant_imguiShowInspector.SetBool( showInspector );
			ImGui::Separator();
			const camera_draw_mode cameraMode = g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ?
				g_pParentWnd->GetCamera()->Camera().draw_mode : cd_texture;
			if ( ImGui::MenuItem( "Textured camera", NULL, cameraMode == cd_texture ) ) {
				DispatchCommand( ID_VIEW_BILINEARMIPMAP );
			}
			if ( ImGui::MenuItem( "Flat-shaded camera", NULL, cameraMode == cd_solid ) ) {
				DispatchCommand( ID_TEXTURES_FLATSHADE );
			}
			if ( ImGui::MenuItem( "Wireframe camera", NULL, cameraMode == cd_wire ) ) {
				DispatchCommand( ID_TEXTURES_WIREFRAME );
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
	if ( showToolbar ) RenderIconToolbar();
	ImVec2 available = ImGui::GetContentRegionAvail();
	const float statusHeight = showStatusBar ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
	ImVec2 workspaceSize( available.x, Max( 1.0f, available.y - statusHeight ) );
	if ( quadLayout ) {
		const int columns = 2 + ( showZView ? 1 : 0 ) + ( showInspector ? 1 : 0 );
		if ( ImGui::BeginTable( "RadiantWorkspaceQuad", columns,
			ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings, workspaceSize ) ) {
			ImGui::TableSetupColumn( "Left", ImGuiTableColumnFlags_WidthStretch, 1.0f );
			ImGui::TableSetupColumn( "Right", ImGuiTableColumnFlags_WidthStretch, 1.0f );
			if ( showZView ) ImGui::TableSetupColumn( "Z", ImGuiTableColumnFlags_WidthStretch, 0.24f );
			if ( showInspector ) ImGui::TableSetupColumn( "Inspector", ImGuiTableColumnFlags_WidthStretch, 0.70f );
			ImGui::TableNextColumn();
			float panelHeight = Max( 1.0f, ( ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y ) * 0.5f );
			RenderViewportChild( "CameraViewPanel", "Camera", NULL, cameraSurface, ImVec2( 0, panelHeight ), 4 );
			RenderViewportChild( "XZViewPanel", "XZ (Front)", g_pParentWnd != NULL ? g_pParentWnd->GetXZWnd() : NULL,
				xzSurface, ImVec2( 0, 0 ), 2 );
			ImGui::TableNextColumn();
			panelHeight = Max( 1.0f, ( ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y ) * 0.5f );
			RenderViewportChild( "XYViewPanel", "XY (Top)", g_pParentWnd != NULL ? g_pParentWnd->GetXYWnd() : NULL,
				xySurface, ImVec2( 0, panelHeight ), 1 );
			RenderViewportChild( "YZViewPanel", "YZ (Side)", g_pParentWnd != NULL ? g_pParentWnd->GetYZWnd() : NULL,
				yzSurface, ImVec2( 0, 0 ), 3 );
			if ( showZView ) {
				ImGui::TableNextColumn();
				ImGui::BeginChild( "ZViewPanel", ImVec2( 0, 0 ), true,
					ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
				ImGui::TextUnformatted( "Z" );
				RenderZPanel( ImGui::GetContentRegionAvail() );
				ImGui::EndChild();
			}
			if ( showInspector ) {
				ImGui::TableNextColumn();
				RenderInspectorPanel();
			}
			ImGui::EndTable();
		}
	} else {
		// The primary ETQW layout always includes the dedicated Z window. It may
		// be toggled in the optional four-view workspace, but never omitted here.
		showZView = true;
		const int columns = 3;
		if ( ImGui::BeginTable( "RadiantWorkspaceTwoView", columns,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings, workspaceSize ) ) {
			ImGui::TableSetupColumn( "CameraAndInspector", ImGuiTableColumnFlags_WidthStretch, 0.80f );
			ImGui::TableSetupColumn( "XY", ImGuiTableColumnFlags_WidthStretch, 1.20f );
			ImGui::TableSetupColumn( "Z", ImGuiTableColumnFlags_WidthStretch, 0.22f );
			ImGui::TableNextColumn();
			if ( showInspector ) {
				float cameraHeight = Max( 1.0f, ImGui::GetContentRegionAvail().y * 0.56f );
				RenderViewportChild( "CameraViewPanel", "Camera", NULL, cameraSurface, ImVec2( 0, cameraHeight ), 4 );
				RenderInspectorPanel();
			} else {
				RenderViewportChild( "CameraViewPanel", "Camera", NULL, cameraSurface, ImVec2( 0, 0 ), 4 );
			}
			ImGui::TableNextColumn();
			RenderViewportChild( "XYViewPanel", "XY (Top)", g_pParentWnd != NULL ? g_pParentWnd->GetXYWnd() : NULL,
				xySurface, ImVec2( 0, 0 ), 1 );
			ImGui::TableNextColumn();
			ImGui::BeginChild( "ZViewPanel", ImVec2( 0, 0 ), true,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
			ImGui::TextUnformatted( "Z" );
			RenderZPanel( ImGui::GetContentRegionAvail() );
			ImGui::EndChild();
			ImGui::EndTable();
		}
	}
	if ( showStatusBar && g_pParentWnd != NULL ) {
		ImGui::Separator();
		if ( ImGui::BeginTable( "RadiantStatus", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings ) ) {
			for ( int pane = 0; pane < 5; pane++ ) {
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( g_pParentWnd->GetStatusText( pane ) );
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
	RenderOpenMapDialog();
	RenderSaveMapDialog();
	RenderPreferencesWindow();
	RenderSurfaceInspectorWindow();
	RenderLightEditorWindow();
	RenderPatchInspectorWindow();
	MegaTextureEditorImGuiRender();
}

static void CleanMenuText( const char *source, char *label, int labelSize, char *shortcut, int shortcutSize ) {
	label[0] = '\0';
	shortcut[0] = '\0';
	bool inShortcut = false;
	int labelIndex = 0;
	int shortcutIndex = 0;
	for ( const char *cursor = source; *cursor != '\0'; cursor++ ) {
		if ( *cursor == '\t' ) {
			inShortcut = true;
			continue;
		}
		if ( *cursor == '&' ) {
			if ( cursor[1] == '&' ) {
				cursor++;
			} else {
				continue;
			}
		}
		if ( inShortcut ) {
			if ( shortcutIndex + 1 < shortcutSize ) shortcut[shortcutIndex++] = *cursor;
		} else if ( labelIndex + 1 < labelSize ) {
			label[labelIndex++] = *cursor;
		}
	}
	label[labelIndex] = '\0';
	shortcut[shortcutIndex] = '\0';
}

static bool FindCommandMenuLabel( HMENU menu, UINT command, char *label, int labelSize ) {
	for ( int index = 0; index < GetMenuItemCount( menu ); index++ ) {
		HMENU submenu = GetSubMenu( menu, index );
		if ( submenu != NULL && FindCommandMenuLabel( submenu, command, label, labelSize ) ) return true;
		if ( submenu == NULL && GetMenuItemID( menu, index ) == command ) {
			char source[512];
			char shortcut[128];
			GetMenuStringA( menu, index, source, sizeof( source ), MF_BYPOSITION );
			CleanMenuText( source, label, labelSize, shortcut, sizeof( shortcut ) );
			return label[0] != '\0';
		}
	}
	return false;
}

static bool IsRetiredRadiantCommand( UINT command ) {
	switch ( command ) {
		// These entries were already disconnected in the Doom 3-era MFC frame.
		// Hiding them keeps the ImGui menu honest instead of presenting actions
		// that silently send an unhandled WM_COMMAND to the controller window.
		case ID_BRUSH_SCRIPTS:
		case ID_VIEW_GROUPS:
		case ID_VIEW_SHOWDETAIL:
		case ID_VIEW_ENTITIESAS_BOUNDINGBOX:
		case ID_VIEW_ENTITIESAS_SELECTEDWIREFRAME:
		case ID_VIEW_ENTITIESAS_SELECTEDSKINNED:
		case ID_VIEW_ENTITIESAS_SKINNEDANDBOXED:
		case ID_SELECTION_MAKE_DETAIL:
		case ID_SELECTION_MAKE_STRUCTURAL:
		case ID_MISC_BENCHMARK:
		case ID_PLUGINS_REFRESH:
		case ID_CURVE_PRIMITIVES_SPHERE:
		case 32866: // orphaned "Select Complete Entity" resource entry
		// The ImGui workspace owns its layout; these old commands only hide the
		// invisible MFC child HWNDs. Layout selection lives in Workspace instead.
		case ID_TOGGLECAMERA:
		case ID_TOGGLEVIEW:
		case ID_TOGGLEVIEW_XZ:
		case ID_TOGGLEVIEW_YZ:
			return true;
		default:
			return false;
	}
}

static bool NativeMenuHasVisibleItems( HMENU menu ) {
	for ( int index = 0; index < GetMenuItemCount( menu ); index++ ) {
		const UINT state = GetMenuState( menu, index, MF_BYPOSITION );
		if ( state == (UINT)-1 || ( state & MF_SEPARATOR ) ) continue;
		HMENU submenu = GetSubMenu( menu, index );
		if ( submenu != NULL ) {
			if ( NativeMenuHasVisibleItems( submenu ) ) return true;
			continue;
		}
		const UINT command = GetMenuItemID( menu, index );
		if ( command != (UINT)-1 && !IsRetiredRadiantCommand( command ) ) return true;
	}
	return false;
}

static int ToolbarPowerOfTwo( int value ) {
	int result = 1;
	while ( result < value ) result <<= 1;
	return result;
}

bool RadiantImGuiHost::EnsureToolbarAtlas() {
	if ( g_pParentWnd == NULL ) return false;
	HIMAGELIST images = g_pParentWnd->GetToolbarImageList();
	const int imageCount = images != NULL ? ImageList_GetImageCount( images ) : 0;
	if ( toolbarAtlas != 0 && images == toolbarImageList && imageCount == toolbarIconCount ) return true;
	if ( images == NULL || imageCount <= 0 ) return false;
	int iconWidth = 0;
	int iconHeight = 0;
	if ( !ImageList_GetIconSize( images, &iconWidth, &iconHeight ) || iconWidth <= 0 || iconHeight <= 0 ) return false;

	if ( toolbarAtlas != 0 ) RadiantImGuiVulkanDestroyTexture( &toolbarAtlas );
	toolbarAtlas = 0;
	toolbarImageList = images;
	toolbarIconCount = imageCount;
	toolbarIconWidth = iconWidth;
	toolbarIconHeight = iconHeight;
	toolbarAtlasColumns = Min( imageCount, 16 );
	const int rows = ( imageCount + toolbarAtlasColumns - 1 ) / toolbarAtlasColumns;
	toolbarAtlasWidth = ToolbarPowerOfTwo( toolbarAtlasColumns * iconWidth );
	toolbarAtlasHeight = ToolbarPowerOfTwo( rows * iconHeight );

	BITMAPINFO bitmapInfo;
	ZeroMemory( &bitmapInfo, sizeof( bitmapInfo ) );
	bitmapInfo.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	bitmapInfo.bmiHeader.biWidth = toolbarAtlasWidth;
	bitmapInfo.bmiHeader.biHeight = -toolbarAtlasHeight;
	bitmapInfo.bmiHeader.biPlanes = 1;
	bitmapInfo.bmiHeader.biBitCount = 32;
	bitmapInfo.bmiHeader.biCompression = BI_RGB;
	void *blackBits = NULL;
	void *whiteBits = NULL;
	HBITMAP blackBitmap = CreateDIBSection( NULL, &bitmapInfo, DIB_RGB_COLORS, &blackBits, NULL, 0 );
	HBITMAP whiteBitmap = CreateDIBSection( NULL, &bitmapInfo, DIB_RGB_COLORS, &whiteBits, NULL, 0 );
	HDC blackDC = CreateCompatibleDC( NULL );
	HDC whiteDC = CreateCompatibleDC( NULL );
	if ( blackBitmap == NULL || whiteBitmap == NULL || blackDC == NULL || whiteDC == NULL ) {
		if ( blackDC != NULL ) DeleteDC( blackDC );
		if ( whiteDC != NULL ) DeleteDC( whiteDC );
		if ( blackBitmap != NULL ) DeleteObject( blackBitmap );
		if ( whiteBitmap != NULL ) DeleteObject( whiteBitmap );
		return false;
	}
	HGDIOBJ oldBlack = SelectObject( blackDC, blackBitmap );
	HGDIOBJ oldWhite = SelectObject( whiteDC, whiteBitmap );
	PatBlt( blackDC, 0, 0, toolbarAtlasWidth, toolbarAtlasHeight, BLACKNESS );
	PatBlt( whiteDC, 0, 0, toolbarAtlasWidth, toolbarAtlasHeight, WHITENESS );
	for ( int index = 0; index < imageCount; index++ ) {
		HICON icon = ImageList_GetIcon( images, index, ILD_TRANSPARENT );
		if ( icon == NULL ) continue;
		const int x = ( index % toolbarAtlasColumns ) * iconWidth;
		const int y = ( index / toolbarAtlasColumns ) * iconHeight;
		DrawIconEx( blackDC, x, y, icon, iconWidth, iconHeight, 0, NULL, DI_NORMAL );
		DrawIconEx( whiteDC, x, y, icon, iconWidth, iconHeight, 0, NULL, DI_NORMAL );
		DestroyIcon( icon );
	}

	const int pixelCount = toolbarAtlasWidth * toolbarAtlasHeight;
	unsigned char *rgba = new unsigned char[pixelCount * 4];
	const unsigned char *black = (const unsigned char *)blackBits;
	const unsigned char *white = (const unsigned char *)whiteBits;
	for ( int pixel = 0; pixel < pixelCount; pixel++ ) {
		const int source = pixel * 4;
		int difference = Max( Max( (int)white[source + 0] - black[source + 0],
			(int)white[source + 1] - black[source + 1] ),
			(int)white[source + 2] - black[source + 2] );
		const int alpha = 255 - Max( 0, Min( 255, difference ) );
		const int destination = source;
		rgba[destination + 0] = alpha > 0 ? (unsigned char)Min( 255, (int)black[source + 2] * 255 / alpha ) : 0;
		rgba[destination + 1] = alpha > 0 ? (unsigned char)Min( 255, (int)black[source + 1] * 255 / alpha ) : 0;
		rgba[destination + 2] = alpha > 0 ? (unsigned char)Min( 255, (int)black[source + 0] * 255 / alpha ) : 0;
		rgba[destination + 3] = (unsigned char)alpha;
	}
	SelectObject( blackDC, oldBlack );
	SelectObject( whiteDC, oldWhite );
	DeleteDC( blackDC );
	DeleteDC( whiteDC );
	DeleteObject( blackBitmap );
	DeleteObject( whiteBitmap );

	const bool uploaded = RadiantImGuiVulkanUploadTexture( &toolbarAtlas, rgba,
		toolbarAtlasWidth, toolbarAtlasHeight, true, false );
	delete[] rgba;
	toolbarAtlas = uploaded ? 1 : 0;
	return uploaded;
}

void RadiantImGuiHost::RenderIconToolbar() {
	if ( g_pParentWnd == NULL || !EnsureToolbarAtlas() ) {
		if ( ImGui::Button( "Open" ) ) DispatchCommand( ID_FILE_OPEN );
		ImGui::SameLine();
		if ( ImGui::Button( "Save" ) ) DispatchCommand( ID_FILE_SAVE );
		return;
	}
	const float iconWidth = (float)toolbarIconWidth;
	const float iconHeight = (float)toolbarIconHeight;
	const float barHeight = iconHeight + ImGui::GetStyle().FramePadding.y * 2.0f + ImGui::GetStyle().ScrollbarSize + 3.0f * uiScale;
	ImGui::BeginChild( "RadiantIconToolbar", ImVec2( 0, barHeight ), false,
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	bool hasPrevious = false;
	for ( int index = 0; index < g_pParentWnd->GetToolbarButtonCount(); index++ ) {
		TBBUTTON button;
		if ( !g_pParentWnd->GetToolbarButton( index, button ) || ( button.fsState & TBSTATE_HIDDEN ) ) continue;
		if ( hasPrevious ) ImGui::SameLine();
		if ( button.fsStyle & BTNS_SEP ) {
			ImGui::Dummy( ImVec2( 7.0f * uiScale, iconHeight ) );
			hasPrevious = true;
			continue;
		}
		if ( button.iBitmap < 0 || button.iBitmap >= toolbarIconCount || button.idCommand == 0 ) continue;
		const int column = button.iBitmap % toolbarAtlasColumns;
		const int row = button.iBitmap / toolbarAtlasColumns;
		const ImVec2 uv0( (float)( column * toolbarIconWidth ) / toolbarAtlasWidth,
			(float)( row * toolbarIconHeight ) / toolbarAtlasHeight );
		const ImVec2 uv1( (float)( ( column + 1 ) * toolbarIconWidth ) / toolbarAtlasWidth,
			(float)( ( row + 1 ) * toolbarIconHeight ) / toolbarAtlasHeight );
		const bool enabled = ( button.fsState & TBSTATE_ENABLED ) != 0;
		const bool checked = ( button.fsState & ( TBSTATE_CHECKED | TBSTATE_PRESSED ) ) != 0;
		ImGui::PushID( index );
		if ( !enabled ) ImGui::BeginDisabled();
		if ( checked ) ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_ButtonActive ) );
		if ( ImGui::ImageButton( "##ToolbarCommand", RadiantImGuiVulkanTexture( &toolbarAtlas ),
			ImVec2( iconWidth, iconHeight ), uv0, uv1 ) ) {
			DispatchCommand( button.idCommand );
		}
		if ( checked ) ImGui::PopStyleColor();
		if ( !enabled ) ImGui::EndDisabled();
		if ( ImGui::IsItemHovered() ) {
			char label[256];
			if ( commandMenu != NULL && FindCommandMenuLabel( commandMenu, button.idCommand, label, sizeof( label ) ) ) {
				ImGui::SetTooltip( "%s", label );
			} else {
				ImGui::SetTooltip( "Command %u", button.idCommand );
			}
		}
		ImGui::PopID();
		hasPrevious = true;
	}
	ImGui::EndChild();
}

void RadiantImGuiHost::RenderNativeMenuBar() {
	if ( commandMenu == NULL ) {
		return;
	}
	const int count = GetMenuItemCount( commandMenu );
	for ( int index = 0; index < count; index++ ) {
		char source[512];
		char label[512];
		char shortcut[128];
		GetMenuStringA( commandMenu, index, source, sizeof( source ), MF_BYPOSITION );
		CleanMenuText( source, label, sizeof( label ), shortcut, sizeof( shortcut ) );
		HMENU submenu = GetSubMenu( commandMenu, index );
		if ( submenu != NULL && NativeMenuHasVisibleItems( submenu ) && ImGui::BeginMenu( label ) ) {
			RenderNativeMenuItems( submenu );
			ImGui::EndMenu();
		}
	}
}

void RadiantImGuiHost::RenderNativeMenuItems( HMENU menu ) {
	const int count = GetMenuItemCount( menu );
	bool emittedItem = false;
	bool pendingSeparator = false;
	for ( int index = 0; index < count; index++ ) {
		UINT state = GetMenuState( menu, index, MF_BYPOSITION );
		if ( state == (UINT)-1 ) {
			continue;
		}
		if ( state & MF_SEPARATOR ) {
			if ( emittedItem ) pendingSeparator = true;
			continue;
		}
		char source[512];
		char label[512];
		char shortcut[128];
		GetMenuStringA( menu, index, source, sizeof( source ), MF_BYPOSITION );
		CleanMenuText( source, label, sizeof( label ), shortcut, sizeof( shortcut ) );
		const bool enabled = ( state & ( MF_DISABLED | MF_GRAYED ) ) == 0;
		HMENU submenu = GetSubMenu( menu, index );
		if ( submenu != NULL ) {
			if ( !NativeMenuHasVisibleItems( submenu ) ) continue;
			if ( pendingSeparator ) {
				ImGui::Separator();
				pendingSeparator = false;
			}
			if ( ImGui::BeginMenu( label, enabled ) ) {
				RenderNativeMenuItems( submenu );
				ImGui::EndMenu();
			}
			emittedItem = true;
			continue;
		}
		const UINT command = GetMenuItemID( menu, index );
		if ( command == (UINT)-1 || IsRetiredRadiantCommand( command ) ) continue;
		if ( pendingSeparator ) {
			ImGui::Separator();
			pendingSeparator = false;
		}
		if ( ImGui::MenuItem( label, shortcut[0] != '\0' ? shortcut : NULL, ( state & MF_CHECKED ) != 0, enabled ) &&
			command != (UINT)-1 ) {
			DispatchCommand( command );
		}
		emittedItem = true;
	}
}

void RadiantImGuiHost::RenderXYContextMenu( CXYWnd *view, const ImVec2 &minimum ) {
	// Do not create the popup in the same frame as the button release. Apart
	// from being easier to reason about, this prevents that release from also
	// activating whichever popup item happens to appear beneath the cursor.
	if ( contextMenuRequestView == view && !ImGui::IsMouseReleased( ImGuiMouseButton_Right ) ) {
		ImGui::OpenPopup( "XYContextMenu" );
		contextMenuRequestView = NULL;
	}
	if ( ImGui::BeginPopup( "XYContextMenu" ) ) {
		HMENU menu = view != NULL && !contextPressMoved ?
			view->PrepareContextMenu( contextPressX, contextPressY ) : NULL;
		if ( menu != NULL ) {
			RenderXYContextMenuItems( view, menu );
		} else {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void RadiantImGuiHost::RenderXYContextMenuItems( CXYWnd *view, HMENU menu ) {
	const int count = GetMenuItemCount( menu );
	for ( int index = 0; index < count; index++ ) {
		UINT state = GetMenuState( menu, index, MF_BYPOSITION );
		if ( state == (UINT)-1 ) continue;
		if ( state & MF_SEPARATOR ) {
			ImGui::Separator();
			continue;
		}
		char source[512];
		char label[512];
		char shortcut[128];
		GetMenuStringA( menu, index, source, sizeof( source ), MF_BYPOSITION );
		CleanMenuText( source, label, sizeof( label ), shortcut, sizeof( shortcut ) );
		const bool enabled = ( state & ( MF_DISABLED | MF_GRAYED ) ) == 0;
		HMENU submenu = GetSubMenu( menu, index );
		if ( submenu != NULL ) {
			if ( ImGui::BeginMenu( label, enabled ) ) {
				RenderXYContextMenuItems( view, submenu );
				ImGui::EndMenu();
			}
			continue;
		}
		const UINT command = GetMenuItemID( menu, index );
		if ( ImGui::MenuItem( label, shortcut[0] != '\0' ? shortcut : NULL,
			( state & MF_CHECKED ) != 0, enabled ) && command != (UINT)-1 && view != NULL ) {
			view->ExecuteContextCommand( command );
		}
	}
}

void RadiantImGuiHost::DispatchCommand( UINT command ) {
	if ( command == ID_FILE_OPEN ) {
		if ( ConfirmModified() ) {
			BeginOpenMapDialog();
		}
		return;
	}
	if ( command == ID_FILE_SAVEAS ) {
		BeginSaveMapDialog( false );
		return;
	}
	if ( command == ID_FILE_SAVEREGION ) {
		BeginSaveMapDialog( true );
		return;
	}
	if ( command == ID_FILE_SAVE ) {
		if ( !idStr::Icmp( currentmap, "unnamed.world" ) ) {
			BeginSaveMapDialog( false );
		} else {
			idStr::Copynz( pendingMapSave, currentmap, sizeof( pendingMapSave ) );
			pendingMapSaveRegion = false;
			pendingMapSaveAs = false;
		}
		return;
	}
	if ( command == ID_VIEW_TOOLBAR ) {
		showToolbar = !showToolbar;
		if ( commandMenu != NULL ) {
			CheckMenuItem( commandMenu, ID_VIEW_TOOLBAR,
				MF_BYCOMMAND | ( showToolbar ? MF_CHECKED : MF_UNCHECKED ) );
		}
		return;
	}
	if ( command == ID_VIEW_STATUS_BAR ) {
		showStatusBar = !showStatusBar;
		if ( commandMenu != NULL ) {
			CheckMenuItem( commandMenu, ID_VIEW_STATUS_BAR,
				MF_BYCOMMAND | ( showStatusBar ? MF_CHECKED : MF_UNCHECKED ) );
		}
		return;
	}
	if ( command == ID_TOGGLECONSOLE ) {
		ShowInspectorTab( W_CONSOLE );
		return;
	}
	if ( command == ID_TOGGLEZ ) {
		showZView = !showZView;
		radiant_imguiShowZ.SetBool( showZView );
		return;
	}
	if ( command == ID_PREFS ) {
		showPreferences = true;
		return;
	}
	if ( command == ID_TEXTURES_INSPECTOR ) {
		ShowSurfaceInspector();
		return;
	}
	if ( command == ID_PROJECTED_LIGHT ) {
		ShowLightEditor();
		return;
	}
	if ( command == ID_PATCH_INSPECTOR ) {
		ShowPatchInspector();
		return;
	}
	if ( command >= ID_VIEW_NEAREST && command <= ID_TEXTURES_FLATSHADE ) {
		// Texture_SetMode used to update the MFC menu attached to the frame.
		// The ImGui shell deliberately detaches that menu, so handle these
		// commands directly and let the Workspace menu reflect camera state.
		Texture_SetMode( command );
		return;
	}
	if ( g_pParentWnd != NULL ) {
		// Command handlers can open modal dialogs, rebuild map state, and pump
		// messages. Deliver them after the current ImGui frame has completed.
		g_pParentWnd->PostMessage( WM_COMMAND, MAKEWPARAM( command, 0 ), 0 );
	}
}

void RadiantImGuiHost::RenderViewportChild( const char *id, const char *title, CXYWnd *view,
	LegacyViewportTexture &surface, const ImVec2 &size, int captureId ) {
	ImGui::BeginChild( id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImGui::TextUnformatted( title );
	if ( view != NULL ) {
		RenderXYPanel( view, surface, ImGui::GetContentRegionAvail(), captureId );
	} else if ( !idStr::Icmp( id, "CameraViewPanel" ) ) {
		RenderCameraPanel( ImGui::GetContentRegionAvail() );
	}
	ImGui::EndChild();
}

void RadiantImGuiHost::RenderCameraPanel( const ImVec2 &requestedSize ) {
	if ( g_pParentWnd == NULL || g_pParentWnd->GetCamera() == NULL ) {
		ImGui::TextDisabled( "Camera is not initialized." );
		return;
	}
	RECT client;
	GetClientRect( hwnd, &client );
	int width = Min( Max( 1, (int)requestedSize.x ), Max( 1, (int)client.right ) );
	int height = Min( Max( 1, (int)requestedSize.y ), Max( 1, (int)client.bottom ) );
	cameraSurface.Prepare( width, height );
	cameraSurface.Queue( RADIANT_IMGUI_VIEW_CAMERA, g_pParentWnd->GetCamera() );
	ImGui::Image( cameraSurface.TextureRef(), ImVec2( (float)width, (float)height ),
		ImVec2( 0.0f, 0.0f ), cameraSurface.UV1() );
	HandleCameraInput( g_pParentWnd->GetCamera(), ImGui::IsItemHovered(), ImGui::GetItemRectMin() );
}

void RadiantImGuiHost::RenderXYPanel( CXYWnd *view, LegacyViewportTexture &surface, const ImVec2 &requestedSize, int captureId ) {
	if ( view == NULL ) {
		ImGui::TextDisabled( "Orthographic view is not initialized." );
		return;
	}
	RECT client;
	GetClientRect( hwnd, &client );
	int width = Min( Max( 1, (int)requestedSize.x ), Max( 1, (int)client.right ) );
	int height = Min( Max( 1, (int)requestedSize.y ), Max( 1, (int)client.bottom ) );
	ImGui::PushID( captureId );
	surface.Prepare( width, height );
	surface.Queue( RADIANT_IMGUI_VIEW_XY, view );
	ImGui::Image( surface.TextureRef(), ImVec2( (float)width, (float)height ),
		ImVec2( 0.0f, 0.0f ), surface.UV1() );
	ImGui::PopID();
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 minimum = ImGui::GetItemRectMin();
	HandleXYInput( view, hovered, minimum, captureId );
	RenderXYContextMenu( view, minimum );
}

void RadiantImGuiHost::RenderZPanel( const ImVec2 &requestedSize ) {
	CZWnd *view = g_pParentWnd != NULL ? g_pParentWnd->GetZWnd() : NULL;
	if ( view == NULL ) {
		ImGui::TextDisabled( "Z view is not initialized." );
		return;
	}
	RECT client;
	GetClientRect( hwnd, &client );
	int width = Min( Max( 1, (int)requestedSize.x ), Max( 1, (int)client.right ) );
	int height = Min( Max( 1, (int)requestedSize.y ), Max( 1, (int)client.bottom ) );
	zSurface.Prepare( width, height );
	zSurface.Queue( RADIANT_IMGUI_VIEW_Z, view );
	ImGui::Image( zSurface.TextureRef(), ImVec2( (float)width, (float)height ),
		ImVec2( 0.0f, 0.0f ), zSurface.UV1() );
	HandleZInput( view, ImGui::IsItemHovered(), ImGui::GetItemRectMin() );
}

void RadiantImGuiHost::RenderInspectorPanel() {
	bool megaTextureSelected = false;
	ImGui::BeginChild( "InspectorPanel", ImVec2( 0, 0 ), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	if ( ImGui::BeginTabBar( "InspectorTabs" ) ) {
		if ( ImGui::BeginTabItem( "Textures", NULL,
			requestedInspector == W_TEXTURE ? ImGuiTabItemFlags_SetSelected : 0 ) ) {
			if ( g_Inspectors != NULL && g_Inspectors->GetMode() != W_TEXTURE ) g_Inspectors->SetMode( W_TEXTURE, false );
			int textureScale = g_PrefsDlg.m_nTextureScale;
			if ( ImGui::Button( "All" ) && g_pParentWnd != NULL ) {
				g_pParentWnd->PostMessage( WM_COMMAND, MAKEWPARAM( ID_TEXTURES_SHOWALL, 0 ), 0 );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "In use" ) && g_pParentWnd != NULL ) {
				g_pParentWnd->PostMessage( WM_COMMAND, MAKEWPARAM( ID_TEXTURES_SHOWINUSE, 0 ), 0 );
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth( -1.0f );
			if ( ImGui::SliderInt( "##TextureScale", &textureScale, 10, 200, "%d%%" ) ) {
				g_PrefsDlg.m_nTextureScale = textureScale;
			}
			RenderTexturePanel( ImGui::GetContentRegionAvail() );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Materials", NULL,
			requestedInspector == W_MEDIA ? ImGuiTabItemFlags_SetSelected : 0 ) ) {
			if ( g_Inspectors != NULL && g_Inspectors->GetMode() != W_MEDIA ) g_Inspectors->SetMode( W_MEDIA, false );
			RenderMediaBrowser();
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Console", NULL,
			requestedInspector == W_CONSOLE ? ImGuiTabItemFlags_SetSelected : 0 ) ) {
			if ( g_Inspectors != NULL && g_Inspectors->GetMode() != W_CONSOLE ) g_Inspectors->SetMode( W_CONSOLE, false );
			if ( g_Inspectors != NULL ) {
				CConsoleDlg &console = g_Inspectors->consoleWnd;
				float inputHeight = ImGui::GetFrameHeightWithSpacing();
				ImGui::BeginChild( "ConsoleOutput", ImVec2( 0, -inputHeight ), true, ImGuiWindowFlags_HorizontalScrollbar );
				ImGui::TextUnformatted( console.GetConsoleText() );
				if ( ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f ) ImGui::SetScrollHereY( 1.0f );
				ImGui::EndChild();
				ImGui::SetNextItemWidth( -1.0f );
				if ( ImGui::InputText( "##ConsoleCommand", consoleCommand, sizeof( consoleCommand ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
					console.ExecuteCommand( idStr( consoleCommand ) );
					consoleCommand[0] = '\0';
				}
				if ( ImGui::IsItemActive() && ImGui::IsKeyPressed( ImGuiKey_UpArrow ) ) {
					idStr history = console.NavigateHistory( -1, consoleCommand );
					idStr::Copynz( consoleCommand, history.c_str(), sizeof( consoleCommand ) );
				}
				if ( ImGui::IsItemActive() && ImGui::IsKeyPressed( ImGuiKey_DownArrow ) ) {
					idStr history = console.NavigateHistory( 1, consoleCommand );
					idStr::Copynz( consoleCommand, history.c_str(), sizeof( consoleCommand ) );
				}
			} else {
				ImGui::TextDisabled( "Console is not initialized." );
			}
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Entity", NULL,
			requestedInspector == W_ENTITY ? ImGuiTabItemFlags_SetSelected : 0 ) ) {
			if ( g_Inspectors != NULL && g_Inspectors->GetMode() != W_ENTITY ) g_Inspectors->SetMode( W_ENTITY, false );
			ImGui::BeginChild( "EntityInspectorScroll", ImVec2( 0, 0 ), false );
			RenderEntityInspector();
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "MegaTexture", NULL,
			requestMegaTextureInspector ? ImGuiTabItemFlags_SetSelected : 0 ) ) {
			megaTextureSelected = true;
			MegaTextureEditorImGuiSetModeActive( true );
			MegaTextureEditorImGuiRenderInspector();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
		requestedInspector = 0;
		requestMegaTextureInspector = false;
	}
	if ( !megaTextureSelected ) MegaTextureEditorImGuiSetModeActive( false );
	ImGui::EndChild();
}

void RadiantImGuiHost::RenderMediaBrowser() {
	if ( g_Inspectors == NULL || g_Inspectors->mediaDlg.m_treeTextures.GetSafeHwnd() == NULL ) {
		ImGui::TextDisabled( "Material browser is not initialized." );
		return;
	}
	CDialogTextures &browser = g_Inspectors->mediaDlg;
	if ( ImGui::Button( "Load selected" ) ) browser.LoadMedia();
	ImGui::SameLine();
	if ( ImGui::Button( "Refresh" ) ) browser.RefreshMedia();
	ImVec2 available = ImGui::GetContentRegionAvail();
	const float previewHeight = Max( 90.0f * uiScale, available.y * 0.38f );
	ImGui::BeginChild( "MaterialTree", ImVec2( 0, Max( 1.0f, available.y - previewHeight - ImGui::GetStyle().ItemSpacing.y ) ), true );
	for ( HTREEITEM item = browser.m_treeTextures.GetRootItem(); item != NULL;
		item = browser.m_treeTextures.GetNextSiblingItem( item ) ) {
		RenderMediaTreeItem( browser, item );
	}
	ImGui::EndChild();
	RenderMediaPreview( ImVec2( ImGui::GetContentRegionAvail().x, previewHeight ) );
}

void RadiantImGuiHost::RenderMediaTreeItem( CDialogTextures &browser, HTREEITEM item ) {
	CTreeCtrl &tree = browser.m_treeTextures;
	const bool hasChildren = tree.ItemHasChildren( item ) != FALSE;
	const bool selected = tree.GetSelectedItem() == item;
	CString itemText = tree.GetItemText( item );
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if ( selected ) flags |= ImGuiTreeNodeFlags_Selected;
	if ( !hasChildren ) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	ImGui::PushID( (void *)item );
	const bool open = ImGui::TreeNodeEx( itemText.GetString(), flags );
	if ( ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() ) {
		browser.SelectMediaItem( item );
	}
	if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) {
		browser.ActivateMediaItem( item );
	}
	if ( hasChildren && open ) {
		for ( HTREEITEM child = tree.GetChildItem( item ); child != NULL; child = tree.GetNextSiblingItem( child ) ) {
			RenderMediaTreeItem( browser, child );
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void RadiantImGuiHost::RenderMediaPreview( const ImVec2 &requestedSize ) {
	idGLWidget *view = g_Inspectors != NULL ? &g_Inspectors->mediaDlg.m_wndPreview : NULL;
	if ( view == NULL ) return;
	RECT client;
	GetClientRect( hwnd, &client );
	int width = Min( Max( 1, (int)requestedSize.x ), Max( 1, (int)client.right ) );
	int height = Min( Max( 1, (int)requestedSize.y ), Max( 1, (int)client.bottom ) );
	mediaSurface.Prepare( width, height );
	mediaSurface.Queue( RADIANT_IMGUI_VIEW_MEDIA, view );
	ImGui::Image( mediaSurface.TextureRef(), ImVec2( (float)width, (float)height ),
		ImVec2( 0.0f, 0.0f ), mediaSurface.UV1() );
	HandleMediaPreviewInput( view, ImGui::IsItemHovered(), ImGui::GetItemRectMin() );
}

void RadiantImGuiHost::RenderEntityInspector() {
	if ( g_Inspectors == NULL ) {
		ImGui::TextDisabled( "Entity inspector is not initialized." );
		return;
	}
	CEntityDlg &inspector = g_Inspectors->entityDlg;
	entity_t *entity = inspector.GetEditEntity();
	if ( selected_brushes.next != &selected_brushes && selected_brushes.next->owner != entity ) {
		// Drive the same selection synchronization used by the MFC inspector.
		// This also preserves its multi-entity detection and property metadata.
		inspector.UpdateEntitySel( selected_brushes.next->owner->eclass );
		entity = inspector.GetEditEntity();
	}
	if ( entity != inspectedEntity ) {
		inspectedEntity = entity;
		entityKey[0] = '\0';
		entityValue[0] = '\0';
	}
	eclass_t *selectedClass = inspector.GetSelectedClass();
	const char *classPreview = selectedClass != NULL ? selectedClass->name : "Select entity class";
	const float createWidth = 70.0f * uiScale;
	ImGui::SetNextItemWidth( Max( 1.0f, ImGui::GetContentRegionAvail().x - createWidth - ImGui::GetStyle().ItemSpacing.x ) );
	if ( ImGui::BeginCombo( "##EntityClass", classPreview ) ) {
		ImGui::SetNextItemWidth( -1.0f );
		ImGui::InputTextWithHint( "##EntityClassFilter", "filter classes", entityClassFilter, sizeof( entityClassFilter ) );
		for ( eclass_t *candidate = eclass; candidate != NULL; candidate = candidate->next ) {
			if ( entityClassFilter[0] != '\0' && idStr::FindText( candidate->name, entityClassFilter, false ) < 0 ) {
				continue;
			}
			const bool selected = candidate == selectedClass;
			if ( ImGui::Selectable( candidate->name, selected ) ) {
				inspector.SelectClass( candidate );
				selectedClass = candidate;
			}
			if ( selected ) ImGui::SetItemDefaultFocus();
			if ( candidate->desc.Length() > 0 && ImGui::IsItemHovered() ) {
				ImGui::SetTooltip( "%s", candidate->desc.c_str() );
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Create", ImVec2( createWidth, 0 ) ) ) {
		inspector.CreateEntity();
		entity = inspector.GetEditEntity();
	}
	if ( selectedClass != NULL && selectedClass->desc.Length() > 0 ) {
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
		ImGui::TextWrapped( "%s", selectedClass->desc.c_str() );
		ImGui::PopStyleColor();
	}
	if ( entity == NULL || entity->eclass == NULL ) {
		ImGui::TextDisabled( "No entity is selected." );
		return;
	}
	float editorHeight = 3.0f * ImGui::GetFrameHeightWithSpacing() + 8.0f * uiScale;
	ImGui::BeginChild( "EntityPairs", ImVec2( 0, -editorHeight ), true );
	if ( ImGui::BeginTable( "EntityPairTable", 2,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp ) ) {
		ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthStretch, 0.42f );
		ImGui::TableSetupColumn( "Value", ImGuiTableColumnFlags_WidthStretch, 0.58f );
		for ( int index = 0; index < entity->epairs.GetNumKeyVals(); index++ ) {
			const idKeyValue *keyValue = entity->epairs.GetKeyVal( index );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			ImGui::PushID( index );
			bool selected = idStr::Icmp( entityKey, keyValue->GetKey().c_str() ) == 0;
			if ( ImGui::Selectable( keyValue->GetKey().c_str(), selected, ImGuiSelectableFlags_SpanAllColumns ) ) {
				idStr::Copynz( entityKey, keyValue->GetKey().c_str(), sizeof( entityKey ) );
				idStr::Copynz( entityValue, keyValue->GetValue().c_str(), sizeof( entityValue ) );
			}
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextUnformatted( keyValue->GetValue().c_str() );
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if ( ImGui::CollapsingHeader( "Available properties" ) ) {
		for ( int index = 0; index < entity->eclass->vars.Num(); index++ ) {
			const evar_t &variable = entity->eclass->vars[index];
			ImGui::PushID( index );
			if ( ImGui::Selectable( variable.name.c_str(), false ) ) {
				idStr::Copynz( entityKey, variable.name.c_str(), sizeof( entityKey ) );
				entityValue[0] = '\0';
			}
			if ( variable.desc.Length() > 0 && ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", variable.desc.c_str() );
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputTextWithHint( "##EntityKey", "key", entityKey, sizeof( entityKey ) );
	ImGui::SetNextItemWidth( -1.0f );
	bool submit = ImGui::InputTextWithHint( "##EntityValue", "value", entityValue, sizeof( entityValue ),
		ImGuiInputTextFlags_EnterReturnsTrue );
	if ( ImGui::Button( "Apply" ) || submit ) {
		inspector.ApplyKeyValue( entityKey, entityValue );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Delete" ) ) {
		inspector.DeleteKeyValue( entityKey );
		entityKey[0] = '\0';
		entityValue[0] = '\0';
	}
	if ( ImGui::CollapsingHeader( "Direction", ImGuiTreeNodeFlags_DefaultOpen ) ) {
		static const char *labels[] = { "135", "90", "45", "180", "0", "225", "270", "315", "Up", "Down" };
		static const char *values[] = { "135", "90", "45", "180", "0", "225", "270", "315", "-1", "-2" };
		for ( int index = 0; index < 10; index++ ) {
			if ( index > 0 ) ImGui::SameLine();
			if ( ImGui::SmallButton( labels[index] ) ) inspector.ApplyAngle( values[index] );
		}
	}
}

static bool CheckboxBOOL( const char *label, BOOL &value ) {
	bool checked = value != FALSE;
	if ( ImGui::Checkbox( label, &checked ) ) {
		value = checked ? TRUE : FALSE;
		return true;
	}
	return false;
}

static void RadiantParentDirectory( char *path ) {
	int length = (int)strlen( path );
	while ( length > 3 && ( path[length - 1] == '\\' || path[length - 1] == '/' ) ) {
		path[--length] = '\0';
	}
	char *backslash = strrchr( path, '\\' );
	char *slash = strrchr( path, '/' );
	char *separator = backslash > slash ? backslash : slash;
	if ( separator == NULL ) return;
	if ( separator == path + 2 && path[1] == ':' ) {
		separator[1] = '\0';
	} else if ( separator > path ) {
		*separator = '\0';
	}
}

void RadiantImGuiHost::BeginOpenMapDialog() {
	idStr directory = ValueForKey( g_qeglobals.d_project_entity, "mapspath" );
	if ( directory.IsEmpty() ) {
		directory = ValueForKey( g_qeglobals.d_project_entity, "basepath" );
		directory.AppendPath( "maps" );
	}
	if ( g_PrefsDlg.m_strMaps.GetLength() > 0 ) {
		directory.AppendPath( g_PrefsDlg.m_strMaps.GetString() );
	}
	directory.SlashesToBackSlashes();
	if ( GetFullPathNameA( directory.c_str(), sizeof( openMapDirectory ), openMapDirectory, NULL ) == 0 ) {
		idStr::Copynz( openMapDirectory, directory.c_str(), sizeof( openMapDirectory ) );
	}
	openMapPath[0] = '\0';
	openMapError[0] = '\0';
	RadiantOpenTraceWrite( "dialog opened", openMapDirectory );
	requestOpenMapDialog = true;
}

void RadiantImGuiHost::RenderOpenMapDialog() {
	if ( requestOpenMapDialog ) {
		ImGui::OpenPopup( "Open Map" );
		requestOpenMapDialog = false;
	}
	ImGui::SetNextWindowSize( ImVec2( 680.0f * uiScale, 520.0f * uiScale ), ImGuiCond_Appearing );
	if ( !ImGui::BeginPopupModal( "Open Map", NULL, ImGuiWindowFlags_NoSavedSettings ) ) return;

	if ( ImGui::Button( "Up" ) ) {
		RadiantParentDirectory( openMapDirectory );
		openMapPath[0] = '\0';
		openMapError[0] = '\0';
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputText( "##OpenMapDirectory", openMapDirectory, sizeof( openMapDirectory ),
		ImGuiInputTextFlags_ReadOnly );

	idStrList directories;
	idStrList files;
	Sys_ListFiles( openMapDirectory, "/", directories );
	Sys_ListFiles( openMapDirectory, ".world", files );
	idStrList regionFiles;
	Sys_ListFiles( openMapDirectory, ".reg", regionFiles );
	for ( int index = 0; index < regionFiles.Num(); index++ ) {
		files.Append( regionFiles[index] );
	}
	directories.Sort();
	files.Sort();

	bool openSelected = false;
	ImGui::BeginChild( "OpenMapFiles", ImVec2( 0, 350.0f * uiScale ), true );
	if ( directories.Num() > 0 ) {
		ImGui::TextDisabled( "Folders (click to open)" );
	}
	for ( int index = 0; index < directories.Num(); index++ ) {
		ImGui::PushID( index );
		idStr label = "[";
		label += directories[index];
		label += "]";
		if ( ImGui::Selectable( label.c_str() ) ) {
			idStr next = openMapDirectory;
			next.AppendPath( directories[index] );
			next.SlashesToBackSlashes();
			idStr::Copynz( openMapDirectory, next.c_str(), sizeof( openMapDirectory ) );
			RadiantOpenTraceWrite( "folder selected", openMapDirectory );
			openMapPath[0] = '\0';
			openMapError[0] = '\0';
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	if ( files.Num() > 0 ) {
		ImGui::SeparatorText( "World files" );
	} else if ( directories.Num() > 0 ) {
		ImGui::Spacing();
		ImGui::TextDisabled( "No .world files in this folder. Open a folder above." );
	} else {
		ImGui::TextDisabled( "No .world files or subfolders were found here." );
	}
	for ( int index = 0; index < files.Num(); index++ ) {
		idStr fullPath = openMapDirectory;
		fullPath.AppendPath( files[index] );
		fullPath.SlashesToBackSlashes();
		const bool selected = !idStr::Icmp( openMapPath, fullPath.c_str() );
		if ( ImGui::Selectable( files[index].c_str(), selected ) ) {
			idStr::Copynz( openMapPath, fullPath.c_str(), sizeof( openMapPath ) );
			common->Printf( "Radiant Open: selected %s\n", openMapPath );
			RadiantOpenTraceWrite( "file selected", openMapPath );
			openMapError[0] = '\0';
			// The embedded MFC/ImGui message pump does not reliably preserve a
			// second click or a later button activation. A world-file row is an
			// unambiguous action, so open it on the click that selected it.
			openSelected = true;
		}
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputTextWithHint( "##OpenMapPath", "Click a .world or .reg file to open it", openMapPath,
		sizeof( openMapPath ) );
	if ( openMapError[0] != '\0' ) {
		ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.3f, 1.0f ), "%s", openMapError );
	}
	if ( ImGui::Button( "Open" ) ) {
		common->Printf( "Radiant Open: button path '%s'\n", openMapPath );
		RadiantOpenTraceWrite( "open button", openMapPath );
		openSelected = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Cancel" ) ) {
		ImGui::CloseCurrentPopup();
	}
	if ( openSelected ) {
		idStr selectedPath = openMapPath;
		selectedPath.SlashesToBackSlashes();
		const bool isWorld = idStr::CheckExtension( selectedPath.c_str(), "world" );
		const bool isRegion = idStr::CheckExtension( selectedPath.c_str(), "reg" );
		RadiantOpenTraceWrite( "activation validated", va( "empty=%d world=%d reg=%d path=%s",
			selectedPath.IsEmpty(), isWorld, isRegion, selectedPath.c_str() ) );
		if ( selectedPath.IsEmpty() || ( !isWorld && !isRegion ) ) {
			idStr::Copynz( openMapError, "Choose an existing .world or .reg file.", sizeof( openMapError ) );
			RadiantOpenTraceWrite( "activation rejected", openMapError );
		} else {
			idStr::Copynz( openMapPath, selectedPath.c_str(), sizeof( openMapPath ) );
			AddNewItem( g_qeglobals.d_lpMruMenu, openMapPath );
			// Loading inside RenderShell invalidates editor state while ImGui is
			// still building this frame. Defer it to Frame() before NewFrame().
			idStr::Copynz( pendingMapLoad, selectedPath.c_str(), sizeof( pendingMapLoad ) );
			common->Printf( "Radiant Open: queued %s\n", pendingMapLoad );
			RadiantOpenTraceWrite( "load queued", pendingMapLoad );
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::EndPopup();
}

void RadiantImGuiHost::BeginSaveMapDialog( bool region ) {
	idStr directory = ValueForKey( g_qeglobals.d_project_entity, "mapspath" );
	if ( directory.IsEmpty() ) {
		directory = ValueForKey( g_qeglobals.d_project_entity, "basepath" );
		directory.AppendPath( "maps" );
	}
	if ( g_PrefsDlg.m_strMaps.GetLength() > 0 ) {
		directory.AppendPath( g_PrefsDlg.m_strMaps.GetString() );
	}
	directory.SlashesToBackSlashes();
	if ( GetFullPathNameA( directory.c_str(), sizeof( saveMapDirectory ), saveMapDirectory, NULL ) == 0 ) {
		idStr::Copynz( saveMapDirectory, directory.c_str(), sizeof( saveMapDirectory ) );
	}

	saveMapPath[0] = '\0';
	if ( idStr::Icmp( currentmap, "unnamed.world" ) != 0 ) {
		idStr suggested = currentmap;
		if ( region ) suggested.SetFileExtension( "reg" );
		idStr::Copynz( saveMapPath, suggested.c_str(), sizeof( saveMapPath ) );
	}
	saveMapError[0] = '\0';
	saveMapRegion = region;
	saveOverwritePending = false;
	requestSaveMapDialog = true;
}

void RadiantImGuiHost::RenderSaveMapDialog() {
	if ( requestSaveMapDialog ) {
		ImGui::OpenPopup( "Save Map As" );
		requestSaveMapDialog = false;
	}
	ImGui::SetNextWindowSize( ImVec2( 680.0f * uiScale, 520.0f * uiScale ), ImGuiCond_Appearing );
	if ( !ImGui::BeginPopupModal( "Save Map As", NULL, ImGuiWindowFlags_NoSavedSettings ) ) return;

	if ( ImGui::Button( "Up" ) ) {
		RadiantParentDirectory( saveMapDirectory );
		saveMapPath[0] = '\0';
		saveMapError[0] = '\0';
		saveOverwritePending = false;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputText( "##SaveMapDirectory", saveMapDirectory, sizeof( saveMapDirectory ),
		ImGuiInputTextFlags_ReadOnly );

	idStrList directories;
	idStrList files;
	Sys_ListFiles( saveMapDirectory, "/", directories );
	Sys_ListFiles( saveMapDirectory, saveMapRegion ? ".reg" : ".world", files );
	directories.Sort();
	files.Sort();

	bool saveSelected = false;
	ImGui::BeginChild( "SaveMapFiles", ImVec2( 0, 350.0f * uiScale ), true );
	if ( directories.Num() > 0 ) {
		ImGui::TextDisabled( "Folders (click to open)" );
	}
	for ( int index = 0; index < directories.Num(); index++ ) {
		ImGui::PushID( index );
		idStr label = "[";
		label += directories[index];
		label += "]";
		if ( ImGui::Selectable( label.c_str() ) ) {
			idStr next = saveMapDirectory;
			next.AppendPath( directories[index] );
			next.SlashesToBackSlashes();
			idStr::Copynz( saveMapDirectory, next.c_str(), sizeof( saveMapDirectory ) );
			saveMapPath[0] = '\0';
			saveMapError[0] = '\0';
			saveOverwritePending = false;
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	if ( files.Num() > 0 ) {
		ImGui::SeparatorText( saveMapRegion ? "Region files" : "World files" );
	} else if ( directories.Num() > 0 ) {
		ImGui::Spacing();
		ImGui::TextDisabled( "No matching files in this folder. Open a folder or enter a new name below." );
	}
	for ( int index = 0; index < files.Num(); index++ ) {
		idStr fullPath = saveMapDirectory;
		fullPath.AppendPath( files[index] );
		fullPath.SlashesToBackSlashes();
		const bool selected = !idStr::Icmp( saveMapPath, fullPath.c_str() );
		if ( ImGui::Selectable( files[index].c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick ) ) {
			idStr::Copynz( saveMapPath, fullPath.c_str(), sizeof( saveMapPath ) );
			saveMapError[0] = '\0';
			saveOverwritePending = false;
			saveSelected = ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left );
		}
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth( -1.0f );
	if ( ImGui::InputTextWithHint( "##SaveMapPath", saveMapRegion ? "region name" : "map name",
		saveMapPath, sizeof( saveMapPath ), ImGuiInputTextFlags_EnterReturnsTrue ) ) {
		saveOverwritePending = false;
		saveMapError[0] = '\0';
		saveSelected = true;
	}
	if ( saveMapError[0] != '\0' ) {
		ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.3f, 1.0f ), "%s", saveMapError );
	}
	if ( ImGui::Button( saveOverwritePending ? "Overwrite" : "Save" ) ) saveSelected = true;
	ImGui::SameLine();
	if ( ImGui::Button( "Cancel" ) ) {
		ImGui::CloseCurrentPopup();
	}

	if ( saveSelected ) {
		idStr target = saveMapPath;
		if ( target.IsEmpty() ) {
			idStr::Copynz( saveMapError, "Enter a map filename.", sizeof( saveMapError ) );
		} else {
			const bool absolute = ( target.Length() > 2 && target[1] == ':' ) || target[0] == '/' || target[0] == '\\';
			if ( !absolute ) {
				idStr relative = target;
				target = saveMapDirectory;
				target.AppendPath( relative );
			}
			target.SlashesToBackSlashes();
			if ( !idStr::CheckExtension( target.c_str(), saveMapRegion ? "reg" : "world" ) ) {
				target.SetFileExtension( saveMapRegion ? "reg" : "world" );
			}
			char fullTarget[MAX_PATH];
			if ( GetFullPathNameA( target.c_str(), sizeof( fullTarget ), fullTarget, NULL ) == 0 ) {
				idStr::Copynz( fullTarget, target.c_str(), sizeof( fullTarget ) );
			}
			idStr fullTargetString = fullTarget;
			idStr parent;
			fullTargetString.ExtractFilePath( parent );
			DWORD parentAttributes = GetFileAttributesA( parent.c_str() );
			DWORD targetAttributes = GetFileAttributesA( fullTarget );
			if ( parent.IsEmpty() || parentAttributes == INVALID_FILE_ATTRIBUTES ||
				!( parentAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
				idStr::Copynz( saveMapError, "The destination folder does not exist.", sizeof( saveMapError ) );
				saveOverwritePending = false;
			} else if ( targetAttributes != INVALID_FILE_ATTRIBUTES && ( targetAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
				idStr::Copynz( saveMapError, "Choose a filename, not a folder.", sizeof( saveMapError ) );
				saveOverwritePending = false;
			} else if ( targetAttributes != INVALID_FILE_ATTRIBUTES && !saveOverwritePending ) {
				idStr::Copynz( saveMapPath, fullTarget, sizeof( saveMapPath ) );
				idStr::Copynz( saveMapError, "That file exists. Click Overwrite to replace it.", sizeof( saveMapError ) );
				saveOverwritePending = true;
			} else {
				idStr::Copynz( pendingMapSave, fullTarget, sizeof( pendingMapSave ) );
				pendingMapSaveRegion = saveMapRegion;
				pendingMapSaveAs = !saveMapRegion;
				saveOverwritePending = false;
				ImGui::CloseCurrentPopup();
			}
		}
	}
	ImGui::EndPopup();
}

void RadiantImGuiHost::RenderPreferencesWindow() {
	if ( !showPreferences ) {
		return;
	}
	ImGui::SetNextWindowSize( ImVec2( 620.0f * uiScale, 560.0f * uiScale ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "Radiant Preferences", &showPreferences ) ) {
		ImGui::End();
		return;
	}
	if ( ImGui::BeginTabBar( "PreferenceTabs" ) ) {
		if ( ImGui::BeginTabItem( "Editing" ) ) {
			CheckboxBOOL( "Texture lock", g_PrefsDlg.m_bTextureLock );
			CheckboxBOOL( "Snap texture operations to grid", g_PrefsDlg.m_bSnapTToGrid );
			CheckboxBOOL( "Select whole entities", g_PrefsDlg.m_bSelectWholeEntities );
			CheckboxBOOL( "Select brushes only", g_PrefsDlg.m_selectOnlyBrushes );
			CheckboxBOOL( "Exclude models from selection", g_PrefsDlg.m_selectNoModels );
			CheckboxBOOL( "Select by bounding brush", g_PrefsDlg.m_selectByBoundingBrush );
			CheckboxBOOL( "Clean tiny brushes", g_PrefsDlg.m_bCleanTiny );
			ImGui::InputFloat( "Tiny brush threshold", &g_PrefsDlg.m_fTinySize, 0.1f, 1.0f, "%.3f" );
			ImGui::SliderInt( "Undo levels", &g_PrefsDlg.m_nUndoLevels, 1, 64 );
			ImGui::SliderInt( "Rotation increment", &g_PrefsDlg.m_nRotation, 1, 90 );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Views" ) ) {
			CheckboxBOOL( "Camera updates XY views", g_PrefsDlg.m_bCamXYUpdate );
			CheckboxBOOL( "Chase mouse", g_PrefsDlg.m_bChaseMouse );
			CheckboxBOOL( "Draw brush sizes", g_PrefsDlg.m_bSizePaint );
			CheckboxBOOL( "New light drawing", g_PrefsDlg.m_bNewLightDraw );
			CheckboxBOOL( "OpenGL lighting", g_PrefsDlg.m_bGLLighting );
			CheckboxBOOL( "High color textures", g_PrefsDlg.m_bHiColorTextures );
			CheckboxBOOL( "Bilinear texture clamping", g_PrefsDlg.m_bNoClamp );
			CheckboxBOOL( "Texture browser scrollbar", g_PrefsDlg.m_bTextureScrollbar );
			ImGui::SliderInt( "Texture thumbnail scale", &g_PrefsDlg.m_nTextureScale, 10, 200, "%d%%" );
			ImGui::SliderInt( "Texture quality", &g_PrefsDlg.m_nTextureQuality, 0, MAX_TEXTURE_QUALITY );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Input / Startup" ) ) {
			ImGui::SliderInt( "Camera move speed", &g_PrefsDlg.m_nMoveSpeed, 10, 5000 );
			ImGui::SliderInt( "Camera angle speed", &g_PrefsDlg.m_nAngleSpeed, 5, 2500 );
			CheckboxBOOL( "Load last project", g_PrefsDlg.m_bLoadLast );
			CheckboxBOOL( "Load last map", g_PrefsDlg.m_bLoadLastMap );
			CheckboxBOOL( "Autosave", g_PrefsDlg.m_bAutoSave );
			ImGui::SliderInt( "Autosave interval (minutes)", &g_PrefsDlg.m_nAutoSave, 1, 60 );
			CheckboxBOOL( "Create map snapshots", g_PrefsDlg.m_bSnapShots );
			CheckboxBOOL( "Run game before external tools", g_PrefsDlg.m_bRunBefore );
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	if ( ImGui::Button( "Apply and save" ) ) {
		g_PrefsDlg.SavePrefs();
		Undo_SetMaxSize( g_PrefsDlg.m_nUndoLevels );
		if ( g_pParentWnd != NULL ) g_pParentWnd->SetGridStatus();
		if ( g_Inspectors != NULL ) g_Inspectors->texWnd.UpdatePrefs();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Close" ) ) showPreferences = false;
	ImGui::End();
}

void RadiantImGuiHost::RenderSurfaceInspectorWindow() {
	if ( !showSurfaceInspector ) {
		return;
	}
	if ( requestSurfaceRefresh ) {
		SyncSurfaceInspector();
	}
	ImGui::SetNextWindowSize( ImVec2( 470.0f * uiScale, 570.0f * uiScale ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "Surface Inspector", &showSurfaceInspector ) ) {
		ImGui::End();
		return;
	}
	if ( ImGui::Button( "Reload from selection" ) ) {
		SyncSurfaceInspector();
	}
	ImGui::SeparatorText( "Material" );
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputText( "Material", surfaceMaterial, sizeof( surfaceMaterial ) );
	if ( ImGui::Button( "Apply material" ) ) {
		g_dlgSurface.m_strMaterial = surfaceMaterial;
		Select_UpdateTextureName( surfaceMaterial );
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::SeparatorText( "Transform" );
	CheckboxBOOL( "Absolute scale", g_dlgSurface.m_absolute );
	ImGui::InputFloat2( "Scale", surfaceScale, "%.4f" );
	if ( ImGui::Button( "Apply scale" ) ) {
		g_dlgSurface.m_horzScale = surfaceScale[0];
		g_dlgSurface.m_vertScale = surfaceScale[1];
		Select_ScaleTexture( surfaceScale[0], surfaceScale[1], true, g_dlgSurface.m_absolute != FALSE );
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::InputFloat( "Rotation", &surfaceRotate, 1.0f, 15.0f, "%.2f" );
	if ( ImGui::Button( "Apply rotation" ) ) {
		g_dlgSurface.m_rotate = surfaceRotate;
		Select_RotateTexture( surfaceRotate, true );
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::InputFloat2( "Shift step", surfaceShiftStep, "%.2f" );
	g_dlgSurface.m_horzShift = surfaceShiftStep[0];
	g_dlgSurface.m_vertShift = surfaceShiftStep[1];
	bool textureChanged = false;
	if ( ImGui::Button( "Left" ) ) { Select_ShiftTexture( -surfaceShiftStep[0], 0.0f ); textureChanged = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Right" ) ) { Select_ShiftTexture( surfaceShiftStep[0], 0.0f ); textureChanged = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Up" ) ) { Select_ShiftTexture( 0.0f, surfaceShiftStep[1] ); textureChanged = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Down" ) ) { Select_ShiftTexture( 0.0f, -surfaceShiftStep[1] ); textureChanged = true; }
	ImGui::InputFloat2( "Fit", surfaceFit, "%.2f" );
	if ( ImGui::Button( "Fit selection" ) ) {
		g_dlgSurface.m_fWidth = surfaceFit[0];
		g_dlgSurface.m_fHeight = surfaceFit[1];
		Select_FitTexture( surfaceFit[1], surfaceFit[0] );
		textureChanged = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Flip X" ) ) { Select_FlipTexture( false ); textureChanged = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Flip Y" ) ) { Select_FlipTexture( true ); textureChanged = true; }
	if ( textureChanged ) {
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_ALL );
	}

	ImGui::SeparatorText( "Patch" );
	bool subdivisionsChanged = CheckboxBOOL( "Explicit subdivisions", g_dlgSurface.m_subdivide );
	subdivisionsChanged |= ImGui::InputInt( "Horizontal", &g_dlgSurface.m_nHorz, 1, 4 );
	subdivisionsChanged |= ImGui::InputInt( "Vertical", &g_dlgSurface.m_nVert, 1, 4 );
	g_dlgSurface.m_nHorz = Max( 1, Min( 32, g_dlgSurface.m_nHorz ) );
	g_dlgSurface.m_nVert = Max( 1, Min( 32, g_dlgSurface.m_nVert ) );
	if ( subdivisionsChanged ) {
		Patch_SubdivideSelected( g_dlgSurface.m_subdivide != FALSE, g_dlgSurface.m_nHorz, g_dlgSurface.m_nVert );
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_CAMERA | W_XY );
	}
	if ( ImGui::Button( "Natural patch" ) ) {
		Select_SetTexture( &g_qeglobals.d_texturewin.texdef, &g_qeglobals.d_texturewin.brushprimit_texdef, false );
		Patch_NaturalizeSelected();
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Natural patch (details)" ) ) {
		Patch_NaturalizeSelected( true );
		if ( g_pParentWnd != NULL && g_pParentWnd->GetCamera() != NULL ) g_pParentWnd->GetCamera()->MarkWorldDirty();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::End();
}

void RadiantImGuiHost::RenderLightEditorWindow() {
	if ( !showLightEditor ) {
		return;
	}
	if ( requestLightRefresh ) {
		LoadLightEditorSelection();
	}
	ImGui::SetNextWindowSize( ImVec2( 560.0f * uiScale, 650.0f * uiScale ), ImGuiCond_FirstUseEver );
	if ( requestLightFocus ) {
		ImGui::SetNextWindowCollapsed( false, ImGuiCond_Always );
		ImGui::SetNextWindowFocus();
	}
	bool lightEditorOpen = true;
	if ( !ImGui::Begin( "Light Editor", &lightEditorOpen ) ) {
		showLightEditor = lightEditorOpen;
		requestLightFocus = false;
		ImGui::End();
		return;
	}
	showLightEditor = lightEditorOpen;
	requestLightFocus = false;
	if ( selectedLightCount == 0 ) {
		ImGui::TextDisabled( "No selected light. Select a light entity and reload." );
	} else if ( selectedLightCount == 1 ) {
		ImGui::TextUnformatted( "Editing the selected light." );
	} else {
		ImGui::Text( "Editing %d selected lights. Apply Differences preserves unchanged values.", selectedLightCount );
	}
	if ( ImGui::Button( "Reload selection" ) ) {
		LoadLightEditorSelection();
	}
	ImGui::BeginDisabled( selectedLightCount == 0 );

	ImGui::SeparatorText( "Type and material" );
	if ( ImGui::RadioButton( "Point", lightInfo.pointLight ) ) {
		lightInfo.DefaultPoint();
		lightMaterial[0] = '\0';
	}
	ImGui::SameLine();
	if ( ImGui::RadioButton( "Projected", !lightInfo.pointLight ) ) {
		lightInfo.DefaultProjected();
		lightMaterial[0] = '\0';
	}
	ImGui::SetNextItemWidth( -1.0f );
	ImGui::InputText( "Material", lightMaterial, sizeof( lightMaterial ) );
	if ( ImGui::BeginCombo( "Light material list", lightMaterial[0] != '\0' ? lightMaterial : "<default>" ) ) {
		for ( int index = 0; index < lightMaterials.Num(); index++ ) {
			const bool selected = !idStr::Icmp( lightMaterial, lightMaterials[index].c_str() );
			if ( ImGui::Selectable( lightMaterials[index].c_str(), selected ) ) {
				idStr::Copynz( lightMaterial, lightMaterials[index].c_str(), sizeof( lightMaterial ) );
			}
			if ( selected ) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SeparatorText( "Appearance" );
	float color[3] = { lightInfo.color[0] / 255.0f, lightInfo.color[1] / 255.0f, lightInfo.color[2] / 255.0f };
	if ( ImGui::ColorEdit3( "Color", color ) ) {
		for ( int component = 0; component < 3; component++ ) lightInfo.color[component] = color[component] * 255.0f;
	}
	ImGui::SliderFloat( "Falloff", &lightInfo.fallOff, 0.0f, 1.0f, "%.2f" );
	ImGui::Checkbox( "Cast shadows", &lightInfo.castShadows );
	ImGui::SameLine();
	ImGui::Checkbox( "Diffuse", &lightInfo.castDiffuse );
	ImGui::SameLine();
	ImGui::Checkbox( "Specular", &lightInfo.castSpecular );
	if ( ImGui::Checkbox( "Fog light", &lightInfo.fog ) && lightInfo.fog && lightInfo.fogDensity[3] <= 0.0f ) {
		lightInfo.fogDensity[3] = 255.0f;
	}
	if ( lightInfo.fog ) {
		float fogColor[4] = { lightInfo.fogDensity[0] / 255.0f, lightInfo.fogDensity[1] / 255.0f,
			lightInfo.fogDensity[2] / 255.0f, lightInfo.fogDensity[3] / 255.0f };
		if ( ImGui::ColorEdit4( "Fog color / density", fogColor ) ) {
			for ( int component = 0; component < 4; component++ ) lightInfo.fogDensity[component] = fogColor[component] * 255.0f;
		}
	}

	if ( lightInfo.pointLight ) {
		ImGui::SeparatorText( "Point light volume" );
		ImGui::Checkbox( "Equal radius", &lightInfo.equalRadius );
		if ( lightInfo.equalRadius ) {
			if ( ImGui::InputFloat( "Radius", &lightInfo.lightRadius[0], 1.0f, 16.0f, "%.2f" ) ) {
				lightInfo.lightRadius[1] = lightInfo.lightRadius[2] = lightInfo.lightRadius[0];
			}
		} else {
			ImGui::InputFloat3( "Radius XYZ", lightInfo.lightRadius.ToFloatPtr(), "%.2f" );
		}
		if ( ImGui::Checkbox( "Custom center", &lightInfo.hasCenter ) && lightInfo.hasCenter && lightInfo.lightCenter == vec3_origin ) {
			lightInfo.lightCenter.z = 32.0f;
		}
		if ( lightInfo.hasCenter ) ImGui::InputFloat3( "Center", lightInfo.lightCenter.ToFloatPtr(), "%.2f" );
		ImGui::Checkbox( "Parallel", &lightInfo.isParallel );
	} else {
		ImGui::SeparatorText( "Projected light volume" );
		ImGui::InputFloat3( "Target", lightInfo.lightTarget.ToFloatPtr(), "%.2f" );
		ImGui::InputFloat3( "Right", lightInfo.lightRight.ToFloatPtr(), "%.2f" );
		ImGui::InputFloat3( "Up", lightInfo.lightUp.ToFloatPtr(), "%.2f" );
		ImGui::Checkbox( "Explicit start and end", &lightInfo.explicitStartEnd );
		if ( lightInfo.explicitStartEnd ) {
			ImGui::InputFloat3( "Start", lightInfo.lightStart.ToFloatPtr(), "%.2f" );
			ImGui::InputFloat3( "End", lightInfo.lightEnd.ToFloatPtr(), "%.2f" );
		}
	}

	ImGui::Separator();
	if ( ImGui::Button( "Apply to selected lights" ) ) {
		lightInfo.strTexture = lightMaterial;
		LightInspector_ApplyInfo( lightInfo, &originalLightInfo, false );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Apply differences" ) ) {
		lightInfo.strTexture = lightMaterial;
		LightInspector_ApplyInfo( lightInfo, &originalLightInfo, true );
	}
	ImGui::EndDisabled();
	ImGui::End();
}

void RadiantImGuiHost::RenderPatchInspectorWindow() {
	if ( !showPatchInspector ) {
		return;
	}
	patchMesh_t *selectedPatch = SinglePatchSelected();
	if ( requestPatchRefresh || selectedPatch != inspectedPatch ) {
		LoadPatchInspectorSelection();
	}
	ImGui::SetNextWindowSize( ImVec2( 500.0f * uiScale, 530.0f * uiScale ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "Patch Inspector", &showPatchInspector ) ) {
		ImGui::End();
		return;
	}
	if ( inspectedPatch == NULL ) {
		ImGui::TextDisabled( "Select exactly one patch to inspect its control points." );
		if ( ImGui::Button( "Reload selection" ) ) LoadPatchInspectorSelection();
		ImGui::End();
		return;
	}
	ImGui::Text( "Material: %s", inspectedPatch->d_texture != NULL ? inspectedPatch->d_texture->GetName() : "<none>" );
	ImGui::Text( "Dimensions: %d x %d", inspectedPatch->width, inspectedPatch->height );
	if ( ImGui::Button( "Reload selection" ) ) LoadPatchInspectorSelection();

	ImGui::SeparatorText( "Control point" );
	int oldColumn = patchColumn;
	int oldRow = patchRow;
	ImGui::SliderInt( "Column", &patchColumn, 0, Max( 0, inspectedPatch->width - 1 ) );
	ImGui::SliderInt( "Row", &patchRow, 0, Max( 0, inspectedPatch->height - 1 ) );
	if ( patchColumn != oldColumn || patchRow != oldRow ) LoadPatchInspectorSelection();
	ImGui::InputFloat3( "Position", patchPosition, "%.3f" );
	ImGui::InputFloat2( "Texture coordinate", patchTexcoord, "%.4f" );
	if ( ImGui::Button( "Apply control point" ) ) {
		idDrawVert &control = inspectedPatch->ctrl( patchColumn, patchRow );
		for ( int component = 0; component < 3; component++ ) control.xyz[component] = patchPosition[component];
		control.SetST( patchTexcoord[0], patchTexcoord[1] );
		Patch_MakeDirty( inspectedPatch );
		Sys_UpdateWindows( W_ALL );
	}

	ImGui::SeparatorText( "Patch texturing" );
	ImGui::InputFloat2( "Scale step S / T", &patchTransformStep[0], "%.4f" );
	ImGui::InputFloat( "Rotation step", &patchTransformStep[2], 1.0f, 15.0f, "%.2f" );
	ImGui::InputFloat2( "Shift step S / T", &patchTransformStep[3], "%.4f" );
	texdef_t transform;
	ZeroMemory( &transform, sizeof( transform ) );
	bool applyTransform = false;
	if ( ImGui::Button( "Scale S -" ) ) { transform.scale[0] = 1.0f - patchTransformStep[0]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Scale S +" ) ) { transform.scale[0] = 1.0f + patchTransformStep[0]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Scale T -" ) ) { transform.scale[1] = 1.0f - patchTransformStep[1]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Scale T +" ) ) { transform.scale[1] = 1.0f + patchTransformStep[1]; applyTransform = true; }
	if ( ImGui::Button( "Shift S -" ) ) { transform.shift[0] = -patchTransformStep[3]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Shift S +" ) ) { transform.shift[0] = patchTransformStep[3]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Shift T -" ) ) { transform.shift[1] = -patchTransformStep[4]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Shift T +" ) ) { transform.shift[1] = patchTransformStep[4]; applyTransform = true; }
	if ( ImGui::Button( "Rotate -" ) ) { transform.rotate = -patchTransformStep[2]; applyTransform = true; }
	ImGui::SameLine();
	if ( ImGui::Button( "Rotate +" ) ) { transform.rotate = patchTransformStep[2]; applyTransform = true; }
	if ( applyTransform ) {
		Patch_SetTextureInfo( &transform );
		Sys_UpdateWindows( W_CAMERA | W_XY );
	}
	if ( ImGui::Button( "Fit" ) ) {
		Patch_FitTexturing();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Natural" ) ) {
		Patch_NaturalizeSelected();
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Natural (details)" ) ) {
		Patch_NaturalizeSelected( true );
		Sys_UpdateWindows( W_ALL );
	}
	ImGui::End();
}

void RadiantImGuiHost::RenderTexturePanel( const ImVec2 &requestedSize ) {
	CNewTexWnd *view = g_Inspectors != NULL ? &g_Inspectors->texWnd : NULL;
	if ( view == NULL ) {
		ImGui::TextDisabled( "Texture browser is not initialized." );
		return;
	}
	RECT client;
	GetClientRect( hwnd, &client );
	int width = Min( Max( 1, (int)requestedSize.x ), Max( 1, (int)client.right ) );
	int height = Min( Max( 1, (int)requestedSize.y ), Max( 1, (int)client.bottom ) );
	textureSurface.Prepare( width, height );
	textureSurface.Queue( RADIANT_IMGUI_VIEW_TEXTURE, view );
	ImGui::Image( textureSurface.TextureRef(), ImVec2( (float)width, (float)height ),
		ImVec2( 0.0f, 0.0f ), textureSurface.UV1() );
	HandleTextureInput( view, ImGui::IsItemHovered(), ImGui::GetItemRectMin() );
}

UINT RadiantImGuiHost::MouseButtons() const {
	const ImGuiIO &io = ImGui::GetIO();
	UINT buttons = 0;
	if ( io.MouseDown[ImGuiMouseButton_Left] ) buttons |= MK_LBUTTON;
	if ( io.MouseDown[ImGuiMouseButton_Right] ) buttons |= MK_RBUTTON;
	if ( io.MouseDown[ImGuiMouseButton_Middle] ) buttons |= MK_MBUTTON;
	// Mouse selection should use the physical modifier state. ImGui may not
	// receive the Shift-down event if a hidden legacy view owned focus before
	// the click (notably immediately after Map_LoadFile()).
	if ( io.KeyShift || ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) ) buttons |= MK_SHIFT;
	if ( io.KeyCtrl || ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) ) buttons |= MK_CONTROL;
	return buttons;
}

void RadiantImGuiHost::HandleCameraInput( CCamWnd *view, bool hovered, const ImVec2 &minimum ) {
	const int captureId = 2;
	cameraInputHovered = hovered;
	if ( view == NULL || ( !hovered && capturedView != captureId ) ) {
		return;
	}
	ImGuiIO &io = ImGui::GetIO();
	const ImVec2 renderScale = RadiantImGuiVulkanContentScale();
	int x = idMath::Ftoi( ( io.MousePos.x - minimum.x ) * renderScale.x );
	int y = idMath::Ftoi( ( io.MousePos.y - minimum.y ) * renderScale.y );
	const bool rightClicked = hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right );
	if ( rightClicked ) capturedView = captureId;
	const bool freeFly = capturedView == captureId && io.MouseDown[ImGuiMouseButton_Right];
	const bool pageVertical = ( hovered || capturedView == captureId ) &&
		( ImGui::IsKeyDown( ImGuiKey_PageUp ) || ImGui::IsKeyDown( ImGuiKey_PageDown ) );
	if ( freeFly || pageVertical ) {
		camera_t &camera = view->Camera();
		bool changed = false;
		const float boost = io.KeyShift ? 4.0f : 1.0f;
		if ( freeFly && io.KeyCtrl ) {
			if ( io.MouseDelta.y != 0.0f ) {
				camera.origin[2] -= io.MouseDelta.y * renderScale.y * boost;
				changed = true;
			}
		} else if ( freeFly && ( io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ) ) {
			camera.angles[YAW] -= io.MouseDelta.x * renderScale.x * 0.25f;
			camera.angles[PITCH] = idMath::ClampFloat( -89.0f, 89.0f,
				camera.angles[PITCH] - io.MouseDelta.y * renderScale.y * 0.25f );
			changed = true;
		}

		float forwardInput = 0.0f, strafeInput = 0.0f, verticalInput = 0.0f;
		if ( freeFly ) {
			if ( ImGui::IsKeyDown( ImGuiKey_W ) ) forwardInput += 1.0f;
			if ( ImGui::IsKeyDown( ImGuiKey_S ) ) forwardInput -= 1.0f;
			if ( ImGui::IsKeyDown( ImGuiKey_D ) ) strafeInput += 1.0f;
			if ( ImGui::IsKeyDown( ImGuiKey_A ) ) strafeInput -= 1.0f;
		}
		if ( ImGui::IsKeyDown( ImGuiKey_PageUp ) ) verticalInput += 1.0f;
		if ( ImGui::IsKeyDown( ImGuiKey_PageDown ) ) verticalInput -= 1.0f;
		const float planarLength = idMath::Sqrt( forwardInput * forwardInput + strafeInput * strafeInput );
		if ( planarLength > 1.0f ) { forwardInput /= planarLength; strafeInput /= planarLength; }
		if ( forwardInput != 0.0f || strafeInput != 0.0f || verticalInput != 0.0f ) {
			idVec3 forward, right;
			idAngles( -camera.angles[PITCH], camera.angles[YAW], 0.0f ).ToVectors( &forward, &right, NULL );
			const float frameTime = idMath::ClampFloat( 0.001f, 0.1f, io.DeltaTime );
			const float distance = g_PrefsDlg.m_nMoveSpeed * boost * frameTime;
			camera.origin += forward * ( forwardInput * distance );
			camera.origin += right * ( strafeInput * distance );
			camera.origin[2] += verticalInput * distance;
			changed = true;
		}
		if ( changed ) Sys_UpdateWindows( W_CAMERA | W_XY_OVERLAY | W_Z );
	}
	const bool megaTextureConsumesLeft = MegaTextureEditorImGuiHandleCameraInput( view, x, y, hovered,
		hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ),
		io.MouseDown[ImGuiMouseButton_Left], ImGui::IsMouseReleased( ImGuiMouseButton_Left ), io.KeyCtrl );
	if ( megaTextureConsumesLeft && hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) ) capturedView = captureId;
	for ( int button = 0; button < 3; button++ ) {
		if ( button == ImGuiMouseButton_Left && megaTextureConsumesLeft ) continue;
		if ( button == ImGuiMouseButton_Right ) continue;
		if ( hovered && ImGui::IsMouseClicked( button ) ) {
			capturedView = captureId;
			view->HandleMouseButton( button, true, x, y, MouseButtons() );
		}
	}
	if ( capturedView != captureId ) {
		return;
	}
	UINT legacyButtons = MouseButtons() & ~MK_RBUTTON;
	if ( megaTextureConsumesLeft ) legacyButtons &= ~MK_LBUTTON;
	view->HandleMouseMove( x, y, legacyButtons );
	for ( int button = 0; button < 3; button++ ) {
		if ( button == ImGuiMouseButton_Left && megaTextureConsumesLeft ) continue;
		if ( button == ImGuiMouseButton_Right ) continue;
		if ( ImGui::IsMouseReleased( button ) ) {
			view->HandleMouseButton( button, false, x, y, MouseButtons() );
		}
	}
	if ( !io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] ) {
		capturedView = 0;
	}
}

void RadiantImGuiHost::HandleXYInput( CXYWnd *view, bool hovered, const ImVec2 &minimum, int captureId ) {
	if ( view == NULL || ( !hovered && capturedView != captureId ) ) {
		return;
	}
	ImGuiIO &io = ImGui::GetIO();
	const ImVec2 renderScale = RadiantImGuiVulkanContentScale();
	int x = idMath::Ftoi( ( io.MousePos.x - minimum.x ) * renderScale.x );
	int y = idMath::Ftoi( ( io.MousePos.y - minimum.y ) * renderScale.y );
	if ( hovered && io.MouseWheel != 0.0f ) {
		view->HandleMouseWheel( io.MouseWheel > 0.0f ? WHEEL_DELTA : -WHEEL_DELTA );
	}
	for ( int button = 0; button < 3; button++ ) {
		if ( hovered && ImGui::IsMouseClicked( button ) ) {
			if ( g_pParentWnd != NULL ) {
				g_pParentWnd->SetActiveXY( view );
			}
			if ( button == ImGuiMouseButton_Right ) {
				contextPressView = view;
				contextPressX = x;
				contextPressY = y;
				contextPressMoved = io.KeyAlt || io.KeyCtrl || io.KeyShift;
			}
			capturedView = captureId;
			// A plain right-click belongs to the context menu. Delay the
			// legacy XY pan operation until the pointer actually moves, so a
			// menu click never leaves the old mouse state/cursor capture active.
			if ( button != ImGuiMouseButton_Right || contextPressMoved ) {
				view->HandleMouseButton( button, true, x, y, MouseButtons() );
			}
		}
	}
	if ( capturedView != captureId ) {
		if ( hovered ) view->HandleMouseMove( x, y, MouseButtons() );
		return;
	}
	if ( contextPressView == view && !contextPressMoved && io.MouseDown[ImGuiMouseButton_Right] &&
		( abs( x - contextPressX ) > (int)( 4.0f * uiScale ) ||
		  abs( y - contextPressY ) > (int)( 4.0f * uiScale ) ) ) {
		contextPressMoved = true;
		view->HandleMouseButton( ImGuiMouseButton_Right, true,
			contextPressX, contextPressY, MouseButtons() );
		POINT anchor = {
			(LONG)( minimum.x + contextPressX / Max( renderScale.x, 0.001f ) ),
			(LONG)( minimum.y + contextPressY / Max( renderScale.y, 0.001f ) )
		};
		ClientToScreen( hwnd, &anchor );
		view->SetExternalPanAnchor( anchor.x, anchor.y );
	}
	view->HandleMouseMove( x, y, MouseButtons() );
	for ( int button = 0; button < 3; button++ ) {
		if ( ImGui::IsMouseReleased( button ) ) {
			if ( button != ImGuiMouseButton_Right || contextPressMoved ) {
				view->HandleMouseButton( button, false, x, y, MouseButtons() );
			}
			if ( button == ImGuiMouseButton_Right && contextPressView == view ) {
				if ( !contextPressMoved ) {
					contextMenuRequestView = view;
				}
				contextPressView = NULL;
			}
		}
	}
	if ( !io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] ) {
		capturedView = 0;
	}
}

void RadiantImGuiHost::HandleTextureInput( CNewTexWnd *view, bool hovered, const ImVec2 &minimum ) {
	const int captureId = 10;
	if ( view == NULL || ( !hovered && capturedView != captureId ) ) {
		return;
	}
	ImGuiIO &io = ImGui::GetIO();
	const ImVec2 renderScale = RadiantImGuiVulkanContentScale();
	int x = idMath::Ftoi( ( io.MousePos.x - minimum.x ) * renderScale.x );
	int y = idMath::Ftoi( ( io.MousePos.y - minimum.y ) * renderScale.y );
	if ( hovered && io.MouseWheel != 0.0f ) {
		view->HandleMouseWheel( io.MouseWheel > 0.0f ? WHEEL_DELTA : -WHEEL_DELTA );
	}
	for ( int button = 0; button < 3; button++ ) {
		if ( hovered && ImGui::IsMouseClicked( button ) ) {
			capturedView = captureId;
			view->HandleMouseButton( button, true, x, y, MouseButtons() );
		}
	}
	if ( capturedView == captureId ) {
		view->HandleMouseMove( x, y, MouseButtons() );
		for ( int button = 0; button < 3; button++ ) {
			if ( ImGui::IsMouseReleased( button ) ) {
				view->HandleMouseButton( button, false, x, y, MouseButtons() );
			}
		}
		if ( !io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] ) {
			capturedView = 0;
		}
	}
}

void RadiantImGuiHost::HandleMediaPreviewInput( idGLWidget *view, bool hovered, const ImVec2 &minimum ) {
	const int captureId = 11;
	if ( view == NULL || ( !hovered && capturedView != captureId ) ) {
		return;
	}
	ImGuiIO &io = ImGui::GetIO();
	const ImVec2 renderScale = RadiantImGuiVulkanContentScale();
	float x = ( io.MousePos.x - minimum.x ) * renderScale.x;
	float y = ( io.MousePos.y - minimum.y ) * renderScale.y;
	if ( view->UsesScreenCoordinates() ) {
		POINT point = { (LONG)io.MousePos.x, (LONG)io.MousePos.y };
		ClientToScreen( hwnd, &point );
		x = (float)point.x;
		y = (float)point.y;
	}
	if ( hovered && io.MouseWheel != 0.0f ) {
		view->HandleMouseWheel( io.MouseWheel > 0.0f ? WHEEL_DELTA : -WHEEL_DELTA );
	}
	for ( int button = 0; button < 3; button++ ) {
		if ( hovered && ImGui::IsMouseClicked( button ) ) {
			capturedView = captureId;
			view->HandleMouseButton( button, true, x, y );
		}
	}
	if ( capturedView != captureId ) return;
	view->HandleMouseMove( x, y );
	for ( int button = 0; button < 3; button++ ) {
		if ( ImGui::IsMouseReleased( button ) ) view->HandleMouseButton( button, false, x, y );
	}
	if ( !io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] ) {
		capturedView = 0;
	}
}

void RadiantImGuiHost::HandleZInput( CZWnd *view, bool hovered, const ImVec2 &minimum ) {
	const int captureId = 5;
	if ( view == NULL || ( !hovered && capturedView != captureId ) ) {
		return;
	}
	ImGuiIO &io = ImGui::GetIO();
	const ImVec2 renderScale = RadiantImGuiVulkanContentScale();
	int x = idMath::Ftoi( ( io.MousePos.x - minimum.x ) * renderScale.x );
	int y = idMath::Ftoi( ( io.MousePos.y - minimum.y ) * renderScale.y );
	for ( int button = 0; button < 3; button++ ) {
		if ( hovered && ImGui::IsMouseClicked( button ) ) {
			capturedView = captureId;
			view->HandleMouseButton( button, true, x, y, MouseButtons() );
		}
	}
	if ( capturedView != captureId ) {
		if ( hovered ) view->HandleMouseMove( x, y, MouseButtons() );
		return;
	}
	view->HandleMouseMove( x, y, MouseButtons() );
	for ( int button = 0; button < 3; button++ ) {
		if ( ImGui::IsMouseReleased( button ) ) {
			view->HandleMouseButton( button, false, x, y, MouseButtons() );
		}
	}
	if ( !io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2] ) {
		capturedView = 0;
	}
}

LRESULT RadiantImGuiHost::WindowProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) {
	ScopedImGuiContext scopedContext( imguiContext );
	bool imguiHandled = false;
	if ( imguiContext != NULL ) {
		imguiHandled = rendererReady && ImGui_ImplWin32_WndProcHandler( window, message, wParam, lParam );
	}
	const bool inputMessage =
		( message >= WM_MOUSEFIRST && message <= WM_MOUSELAST ) ||
		message == WM_KEYDOWN || message == WM_KEYUP ||
		message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
		message == WM_CHAR || message == WM_SYSCHAR;
	if ( inputMessage ) {
		// The embedded editor is event-driven rather than a continuously presented
		// game frame. Every input transition must schedule the ImGui frame which
		// consumes it, otherwise menu clicks remain queued but never become visible.
		InvalidateRect( window, NULL, FALSE );
	}
	if ( message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ) {
		if ( GetFocus() != window ) SetFocus( window );
	}
	if ( ( message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ) &&
		g_pParentWnd != NULL && imguiContext != NULL && !ImGui::GetIO().WantCaptureKeyboard ) {
		const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		const bool controlDown = ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) != 0;
		const bool repeat = ( lParam & ( 1u << 30 ) ) != 0;
		if ( MegaTextureEditorImGuiHandleKey( (int)wParam, keyDown, controlDown, repeat ) ) return 0;
		const bool rightMouseDown = ( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) != 0;
		const bool cameraFlyKey = rightMouseDown &&
			( wParam == 'W' || wParam == 'A' || wParam == 'S' || wParam == 'D' );
		const bool cameraVerticalKey = ( cameraInputHovered || capturedView == 2 ) &&
			( wParam == VK_PRIOR || wParam == VK_NEXT );
		if ( !cameraFlyKey && !cameraVerticalKey ) {
			g_pParentWnd->HandleKey( (UINT)wParam, LOWORD( lParam ), HIWORD( lParam ), keyDown );
		}
		return 0;
	}
	if ( imguiHandled ) {
		// The MFC pump may otherwise consume a complete click sequence before the
		// next idle render. Queue a paint for immediate menu hover/open feedback.
		InvalidateRect( window, NULL, FALSE );
		return TRUE;
	}
	switch ( message ) {
		case WM_TIMER:
			if ( wParam == RADIANT_IMGUI_FRAME_TIMER ) {
				Frame();
				return 0;
			}
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT: {
			PAINTSTRUCT paint;
			BeginPaint( window, &paint );
			EndPaint( window, &paint );
			Frame();
			return 0;
		}
		case WM_SIZE:
			if ( wParam != SIZE_MINIMIZED ) {
				InvalidateRect( window, NULL, FALSE );
			}
			return 0;
		case WM_DPICHANGED: {
			const RECT *suggested = (const RECT *)lParam;
			SetWindowPos( window, NULL, suggested->left, suggested->top,
				suggested->right - suggested->left, suggested->bottom - suggested->top,
				SWP_NOACTIVATE | SWP_NOZORDER );
			return 0;
		}
		case WM_CLOSE:
			if ( g_pParentWnd != NULL ) {
				g_pParentWnd->PostMessage( WM_CLOSE );
			}
			return 0;
		case WM_NCDESTROY:
			SetWindowLongPtr( window, GWLP_USERDATA, 0 );
			if ( hwnd == window ) {
				hwnd = NULL;
			}
			return 0;
	}
	return DefWindowProc( window, message, wParam, lParam );
}

LRESULT CALLBACK RadiantImGuiHost::StaticWindowProc( HWND window, UINT message, WPARAM wParam, LPARAM lParam ) {
	RadiantImGuiHost *host = (RadiantImGuiHost *)GetWindowLongPtr( window, GWLP_USERDATA );
	if ( message == WM_NCCREATE ) {
		CREATESTRUCT *create = (CREATESTRUCT *)lParam;
		host = (RadiantImGuiHost *)create->lpCreateParams;
		SetWindowLongPtr( window, GWLP_USERDATA, (LONG_PTR)host );
		host->hwnd = window;
	}
	return host != NULL ? host->WindowProc( window, message, wParam, lParam ) : DefWindowProc( window, message, wParam, lParam );
}

} // namespace

void RadiantImGuiTraceOpen( const char *eventName, const char *detail ) {
	RadiantOpenTraceWrite( eventName, detail );
}

bool RadiantImGuiEnabled() {
	// The native frame is now a hidden compatibility/controller layer only.
	// Do not allow an archived value from an older build to bring that shell
	// back during startup.
	return true;
}

bool RadiantImGuiCreate() {
	if ( radiantImGuiHost != NULL ) {
		return true;
	}
	radiantImGuiHost = new RadiantImGuiHost();
	if ( !radiantImGuiHost->Create() ) {
		delete radiantImGuiHost;
		radiantImGuiHost = NULL;
		return false;
	}
	return true;
}

void RadiantImGuiDestroy() {
	delete radiantImGuiHost;
	radiantImGuiHost = NULL;
}

void RadiantImGuiFocus() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->Focus();
	}
}

void RadiantImGuiFrame() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->Frame();
	}
}

void RadiantImGuiPumpMessages() {
	if ( radiantImGuiHost == NULL || radiantImGuiHost->Window() == NULL ) {
		return;
	}

	// Map loading is intentionally synchronous because almost all of Radiant's
	// map and renderer state is thread-affine. Drain the Win32 queue often enough
	// to prevent ghosting, but dispatch only paint/layout messages: input,
	// commands, timers and close requests must not re-enter the editor midway
	// through Map_LoadFile().
	MSG message;
	while ( PeekMessage( &message, NULL, 0, 0, PM_REMOVE ) ) {
		if ( message.message == WM_QUIT ) {
			PostQuitMessage( (int)message.wParam );
			break;
		}
		switch ( message.message ) {
			case WM_PAINT:
			case WM_NCPAINT:
			case WM_ERASEBKGND:
			case WM_SIZE:
			case WM_MOVE:
			case WM_WINDOWPOSCHANGED:
				TranslateMessage( &message );
				DispatchMessage( &message );
				break;
			default:
				break;
		}
	}
}

HWND RadiantImGuiWindow() {
	return radiantImGuiHost != NULL ? radiantImGuiHost->Window() : NULL;
}

void RadiantImGuiShowInspector( int mode ) {
	if ( radiantImGuiHost != NULL ) {
		if ( g_Inspectors != NULL ) {
			g_Inspectors->SetMode( mode, false );
		}
		radiantImGuiHost->ShowInspectorTab( mode );
	}
}

void RadiantImGuiShowMegaTextureInspector() {
	if ( radiantImGuiHost != NULL ) radiantImGuiHost->ShowMegaTextureInspector();
}

void RadiantImGuiShowLightEditor() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->ShowLightEditor();
	}
}

void RadiantImGuiRefreshLightEditor() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->RefreshLightEditor();
	}
}

void RadiantImGuiShowSurfaceInspector() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->ShowSurfaceInspector();
	}
}

void RadiantImGuiRefreshSurfaceInspector() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->RefreshSurfaceInspector();
	}
}

void RadiantImGuiShowPatchInspector() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->ShowPatchInspector();
	}
}

void RadiantImGuiRefreshPatchInspector() {
	if ( radiantImGuiHost != NULL ) {
		radiantImGuiHost->RefreshPatchInspector();
	}
}

void RadiantImGuiApplyMaterial( const char *materialName ) {
	if ( materialName == NULL || materialName[0] == '\0' ) return;
	const idMaterial *material = declManager->FindMaterial( materialName, false );
	if ( material == NULL ) {
		common->Warning( "Material '%s' was not found.\n", materialName );
		return;
	}
	Select_SetDefaultTexture( material, false, true );
	Sys_UpdateWindows( W_ALL );
}
