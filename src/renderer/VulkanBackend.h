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

	bool IsInitialized() const;
	bool IsFrameActive() const;
	const char* GetDeviceName() const;

private:
	sdVulkanBackendState*	state;
};

extern sdVulkanBackend vulkanBackend;

bool R_UseVulkanBackend();

#endif /* !__RENDERER_VULKANBACKEND_H__ */
