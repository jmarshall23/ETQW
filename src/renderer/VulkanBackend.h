// Copyright (C) 2007 Id Software, Inc.

#ifndef __RENDERER_VULKANBACKEND_H__
#define __RENDERER_VULKANBACKEND_H__

struct sdVulkanBackendState;
struct viewDef_s;

struct sdVulkanGuiVertex {
	float x;
	float y;
	float s;
	float t;
};

struct sdVulkanToolVertex {
	float x;
	float y;
	float z;
	float s;
	float t;
	float r;
	float g;
	float b;
	float a;
};

class sdVulkanBackend {
public:
	sdVulkanBackend();
	~sdVulkanBackend();

	bool Init( void* nativeWindow, int width, int height );
	void Shutdown();

	bool BeginFrame( int width, int height );
	void EndFrame( bool present );
	void WaitIdle();

	// Renderer resource mirrors.  The owner is the stable idImage/vertex-cache
	// object address; Vulkan handles stay private to this backend.
	bool UploadImage2D( const void* owner, const unsigned char* rgba,
		int width, int height, int mipLevels, bool linearFilter, bool repeat );
	bool UploadImageCube( const void* owner, const unsigned char* const rgba[ 6 ],
		int size, bool linearFilter );
	bool UpdateImage2D( const void* owner, int mipLevel, int x, int y,
		int width, int height, const unsigned char* rgba );
	void DestroyImage( const void* owner );
	bool UploadBuffer( const void* owner, const void* data, int bytes,
		bool indexBuffer );
	bool UpdateBuffer( const void* owner, int offset, const void* data, int bytes );
	void DestroyBuffer( const void* owner );
	bool DrawGuiFan( const void* imageOwner, const sdVulkanGuiVertex* vertices,
		int vertexCount, const float* color, int drawStateBits );
	void DrawView( const viewDef_s* view );

	// Native editor windows use their own presentation surfaces.  Radiant's
	// fixed-function compatibility layer submits pre-transformed triangle lists
	// here, keeping tool rendering independent from the game swapchain.
	bool BeginToolWindow( void* nativeWindow, int width, int height,
		const float clearColor[ 4 ] );
	// Legacy editor views render into sampled Vulkan images.  The Dear ImGui
	// shell only composites those images; it never owns or expands map geometry.
	bool BeginToolRenderTarget( const void* owner, int width, int height,
		const float clearColor[ 4 ] );
	void EndToolRenderTarget();
	void DestroyToolRenderTarget( const void* owner );
	bool GetToolRenderTargetSize( const void* owner, int& width, int& height,
		int& textureWidth, int& textureHeight ) const;
	bool DrawToolTriangles( const sdVulkanToolVertex* vertices, int vertexCount,
		bool depthTest, bool blend );
	void SetToolScissor( int x, int y, int width, int height );
	void SetToolImage( const void* imageOwner );
	void ClearToolRegion( const float color[ 4 ], bool clearColor, bool clearDepth );
	bool GetActiveToolWindowExtent( int& width, int& height ) const;
	void EndToolWindow();
	bool IsToolWindowActive() const;

	bool IsInitialized() const;
	bool IsFrameActive() const;
	const char* GetDeviceName() const;

private:
	sdVulkanBackendState*	state;
};

extern sdVulkanBackend vulkanBackend;

bool R_UseVulkanBackend();

#endif /* !__RENDERER_VULKANBACKEND_H__ */
