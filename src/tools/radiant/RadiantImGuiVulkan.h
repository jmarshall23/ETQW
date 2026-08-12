// Copyright (C) 2007 Id Software, Inc.
// Dear ImGui renderer used by Darklight2's Radiant shell on ETQW Vulkan.

#ifndef __RADIANT_IMGUI_VULKAN_H__
#define __RADIANT_IMGUI_VULKAN_H__

#include "imgui.h"

enum radiantImGuiViewportType_t {
	RADIANT_IMGUI_VIEW_CAMERA,
	RADIANT_IMGUI_VIEW_XY,
	RADIANT_IMGUI_VIEW_Z,
	RADIANT_IMGUI_VIEW_TEXTURE,
	RADIANT_IMGUI_VIEW_MEDIA
};

bool RadiantImGuiVulkanInit( HWND window );
void RadiantImGuiVulkanShutdown();
void RadiantImGuiVulkanNewFrame();
bool RadiantImGuiVulkanBeginFrame( HWND window, const float clearColor[ 4 ] );
void RadiantImGuiVulkanRenderViewports( ImDrawData* drawData );
void RadiantImGuiVulkanRenderDrawData( ImDrawData* drawData );
void RadiantImGuiVulkanEndFrame();
ImVec2 RadiantImGuiVulkanContentScale();
void RadiantImGuiVulkanGetViewportTimings( double timings[ 5 ] );

void RadiantImGuiVulkanQueueViewport( radiantImGuiViewportType_t type,
	void* view, const void* textureOwner, int width, int height );
void RadiantImGuiVulkanDestroyViewport( const void* textureOwner );
ImVec2 RadiantImGuiVulkanViewportUV1( const void* textureOwner,
	int width, int height );

bool RadiantImGuiVulkanUploadTexture( const void* owner,
	const unsigned char* rgba, int width, int height, bool linear, bool repeat );
void RadiantImGuiVulkanDestroyTexture( const void* owner );
ImTextureRef RadiantImGuiVulkanTexture( const void* owner );

#endif
