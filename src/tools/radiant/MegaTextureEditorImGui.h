#ifndef __MEGATEXTURE_EDITOR_IMGUI_H__
#define __MEGATEXTURE_EDITOR_IMGUI_H__

class CCamWnd;
class idMaterial;
class idRenderModel;

void MegaTextureEditorImGuiShow( const char *project = NULL );
void MegaTextureEditorImGuiHide();
bool MegaTextureEditorImGuiIsOpen();
void MegaTextureEditorImGuiRender();
void MegaTextureEditorImGuiRenderInspector();
void MegaTextureEditorImGuiSetModeActive( bool active );
void MegaTextureEditorImGuiOnMapLoaded();
void MegaTextureEditorImGuiOnBlankMapOverwrite( const char *mapFile );
bool MegaTextureEditorImGuiHandleCameraInput( CCamWnd *view, int x, int y,
	bool hovered, bool leftClicked, bool leftDown, bool leftReleased, bool invert );
bool MegaTextureEditorImGuiHandleKey( int key, bool down, bool control, bool repeat );
bool MegaTextureEditorImGuiDrawLayeredTerrain( idRenderModel *model, const idVec3 &origin, const idMat3 &axis );
void MegaTextureEditorImGuiShutdown();

#endif
