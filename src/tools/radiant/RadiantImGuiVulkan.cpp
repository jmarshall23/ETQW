// Copyright (C) 2007 Id Software, Inc.

#include "RadiantPch.h"
#pragma hdrstop

#include "RadiantImGuiVulkan.h"
#include "qe3.h"
#include "MainFrm.h"
#include "GLWidget.h"
#include "../../renderer/VulkanBackend.h"
#include "backends/imgui_impl_win32.h"

namespace {

struct radiantImGuiViewportRequest_t {
	radiantImGuiViewportType_t type;
	void* view;
	int x;
	int y;
	int width;
	int height;
};

const void* fontTextureOwner = NULL;
ImVec2 contentScale( 1.0f, 1.0f );
double viewportTimings[ 5 ] = { 0.0, 0.0, 0.0, 0.0, 0.0 };

double MillisecondsNow() {
	static LARGE_INTEGER frequency = { 0 };
	if ( frequency.QuadPart == 0 ) {
		QueryPerformanceFrequency( &frequency );
	}
	LARGE_INTEGER counter;
	QueryPerformanceCounter( &counter );
	return counter.QuadPart * 1000.0 / frequency.QuadPart;
}

void DrawDirectVulkanViewport( const ImDrawList*, const ImDrawCmd* command ) {
	const radiantImGuiViewportRequest_t* request =
		static_cast< const radiantImGuiViewportRequest_t* >( command->UserCallbackData );
	if ( request == NULL || request->view == NULL || request->width <= 0 ||
		request->height <= 0 ) {
		return;
	}
	const int x = idMath::Ftoi( request->x * contentScale.x );
	const int y = idMath::Ftoi( request->y * contentScale.y );
	const int width = Max( idMath::Ftoi( request->width * contentScale.x ), 1 );
	const int height = Max( idMath::Ftoi( request->height * contentScale.y ), 1 );
	const double start = MillisecondsNow();
	RadiantVulkanSetEmbeddedRegion( x, y, width, height );
	switch ( request->type ) {
		case RADIANT_IMGUI_VIEW_CAMERA:
			static_cast< CCamWnd* >( request->view )->DrawToCurrentContext(
				width, height );
			break;
		case RADIANT_IMGUI_VIEW_XY:
			static_cast< CXYWnd* >( request->view )->DrawToCurrentContext(
				width, height );
			break;
		case RADIANT_IMGUI_VIEW_Z:
			static_cast< CZWnd* >( request->view )->DrawToCurrentContext(
				width, height );
			break;
		case RADIANT_IMGUI_VIEW_TEXTURE:
			static_cast< CNewTexWnd* >( request->view )->DrawToCurrentContext(
				width, height );
			break;
		case RADIANT_IMGUI_VIEW_MEDIA:
			static_cast< idGLWidget* >( request->view )->DrawToCurrentContext(
				width, height );
			break;
	}
	RadiantVulkanEndEmbeddedRegion();
	viewportTimings[ request->type ] += MillisecondsNow() - start;
}

void UpdateContentScale( const ImDrawData* drawData ) {
	if ( drawData == NULL ) {
		contentScale = ImVec2( 1.0f, 1.0f );
		return;
	}
	int framebufferWidth = 0;
	int framebufferHeight = 0;
	if ( vulkanBackend.GetActiveToolWindowExtent( framebufferWidth,
		framebufferHeight ) ) {
		contentScale.x = framebufferWidth /
			Max( drawData->DisplaySize.x, 1.0f );
		contentScale.y = framebufferHeight /
			Max( drawData->DisplaySize.y, 1.0f );
	} else {
		contentScale = drawData->FramebufferScale;
	}
}

void SubmitImGuiCommand( const ImDrawList* commandList,
	const ImDrawCmd& command, const ImDrawData& drawData ) {
	const ImVec2 displayPosition = drawData.DisplayPos;
	const float framebufferWidth = drawData.DisplaySize.x * contentScale.x;
	const float framebufferHeight = drawData.DisplaySize.y * contentScale.y;
	const float clipLeft = ( command.ClipRect.x - displayPosition.x ) * contentScale.x;
	const float clipTop = ( command.ClipRect.y - displayPosition.y ) * contentScale.y;
	const float clipRight = ( command.ClipRect.z - displayPosition.x ) * contentScale.x;
	const float clipBottom = ( command.ClipRect.w - displayPosition.y ) * contentScale.y;
	if ( clipRight <= clipLeft || clipBottom <= clipTop ||
		clipLeft >= framebufferWidth || clipTop >= framebufferHeight ) {
		return;
	}

	vulkanBackend.SetToolScissor( idMath::Ftoi( clipLeft ),
		idMath::Ftoi( framebufferHeight - clipBottom ),
		idMath::Ftoi( clipRight - clipLeft ), idMath::Ftoi( clipBottom - clipTop ) );

	idList< sdVulkanToolVertex > triangles;
	triangles.SetNum( command.ElemCount );
	for ( unsigned int index = 0; index < command.ElemCount; ++index ) {
		const ImDrawIdx sourceIndex = commandList->IdxBuffer[
			command.IdxOffset + index ];
		const ImDrawVert& source = commandList->VtxBuffer[
			command.VtxOffset + sourceIndex ];
		sdVulkanToolVertex& vertex = triangles[ index ];
		vertex.x = ( ( source.pos.x - displayPosition.x ) /
			Max( drawData.DisplaySize.x, 1.0f ) ) * 2.0f - 1.0f;
		vertex.y = 1.0f - ( ( source.pos.y - displayPosition.y ) /
			Max( drawData.DisplaySize.y, 1.0f ) ) * 2.0f;
		vertex.z = 0.0f;
		vertex.w = 1.0f;
		vertex.s = source.uv.x;
		vertex.t = source.uv.y;
		vertex.r = ( ( source.col >> IM_COL32_R_SHIFT ) & 0xff ) / 255.0f;
		vertex.g = ( ( source.col >> IM_COL32_G_SHIFT ) & 0xff ) / 255.0f;
		vertex.b = ( ( source.col >> IM_COL32_B_SHIFT ) & 0xff ) / 255.0f;
		vertex.a = ( ( source.col >> IM_COL32_A_SHIFT ) & 0xff ) / 255.0f;
	}

	const void* textureOwner = reinterpret_cast< const void* >(
		static_cast< uintptr_t >( command.GetTexID() ) );
	vulkanBackend.SetToolImage( textureOwner );
	vulkanBackend.DrawToolTriangles( triangles.Begin(), triangles.Num(), false, true );
}

} // namespace

bool RadiantImGuiVulkanInit( HWND window ) {
	if ( window == NULL || !R_UseVulkanBackend() || !vulkanBackend.IsInitialized() ||
		!ImGui_ImplWin32_Init( window ) ) {
		return false;
	}
	unsigned char* pixels = NULL;
	int width = 0;
	int height = 0;
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );
	fontTextureOwner = io.Fonts;
	if ( pixels == NULL || width <= 0 || height <= 0 ||
		!RadiantImGuiVulkanUploadTexture( fontTextureOwner, pixels, width, height,
			true, false ) ) {
		ImGui_ImplWin32_Shutdown();
		fontTextureOwner = NULL;
		return false;
	}
	io.Fonts->SetTexID( static_cast< ImTextureID >(
		reinterpret_cast< uintptr_t >( fontTextureOwner ) ) );
	return true;
}

void RadiantImGuiVulkanShutdown() {
	if ( fontTextureOwner != NULL ) {
		RadiantImGuiVulkanDestroyTexture( fontTextureOwner );
		fontTextureOwner = NULL;
	}
	ImGui_ImplWin32_Shutdown();
}

void RadiantImGuiVulkanNewFrame() {
	memset( viewportTimings, 0, sizeof( viewportTimings ) );
	ImGui_ImplWin32_NewFrame();
}

bool RadiantImGuiVulkanBeginFrame( HWND window, const float clearColor[ 4 ] ) {
	return RadiantVulkanBeginFrame( window, clearColor );
}

void RadiantImGuiVulkanRenderDrawData( ImDrawData* drawData ) {
	if ( drawData == NULL || drawData->DisplaySize.x <= 0.0f ||
		drawData->DisplaySize.y <= 0.0f ) {
		return;
	}
	UpdateContentScale( drawData );
	for ( int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex ) {
		const ImDrawList* commandList = drawData->CmdLists[ listIndex ];
		for ( int commandIndex = 0; commandIndex < commandList->CmdBuffer.Size;
			++commandIndex ) {
			const ImDrawCmd& command = commandList->CmdBuffer[ commandIndex ];
			if ( command.UserCallback != NULL ) {
				if ( command.UserCallback != ImDrawCallback_ResetRenderState ) {
					command.UserCallback( commandList, &command );
				}
			} else if ( command.ElemCount != 0 ) {
				SubmitImGuiCommand( commandList, command, *drawData );
			}
		}
	}
}

void RadiantImGuiVulkanEndFrame() {
	RadiantVulkanEndFrame();
}

ImVec2 RadiantImGuiVulkanContentScale() {
	return contentScale;
}

void RadiantImGuiVulkanGetViewportTimings( double timings[ 5 ] ) {
	if ( timings != NULL ) {
		memcpy( timings, viewportTimings, sizeof( viewportTimings ) );
	}
}

void RadiantImGuiVulkanAddViewport( ImDrawList* drawList,
	radiantImGuiViewportType_t type, void* view, const ImVec2& minimum,
	const ImVec2& size ) {
	if ( drawList == NULL || view == NULL || size.x < 1.0f || size.y < 1.0f ) {
		return;
	}
	radiantImGuiViewportRequest_t request;
	request.type = type;
	request.view = view;
	request.x = idMath::Ftoi( minimum.x );
	request.y = idMath::Ftoi( minimum.y );
	request.width = Max( idMath::Ftoi( size.x ), 1 );
	request.height = Max( idMath::Ftoi( size.y ), 1 );
	// This callback is an ordering marker only. The viewport draws native
	// Radiant vertex/index batches directly through the Vulkan backend.
	drawList->AddCallback( DrawDirectVulkanViewport, &request, sizeof( request ) );
}

bool RadiantImGuiVulkanUploadTexture( const void* owner,
	const unsigned char* rgba, int width, int height, bool linear, bool repeat ) {
	return owner != NULL && vulkanBackend.UploadImage2D( owner, rgba, width,
		height, 1, linear, repeat );
}

void RadiantImGuiVulkanDestroyTexture( const void* owner ) {
	if ( owner != NULL ) {
		vulkanBackend.DestroyImage( owner );
	}
}

ImTextureRef RadiantImGuiVulkanTexture( const void* owner ) {
	return ImTextureRef( static_cast< ImTextureID >(
		reinterpret_cast< uintptr_t >( owner ) ) );
}
