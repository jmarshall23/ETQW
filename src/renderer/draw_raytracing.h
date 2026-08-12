// Copyright (C) 2007 Id Software, Inc.
//
// x64 Vulkan ray-query lighting integration.

#ifndef __ETQW_DRAW_RAYTRACING_H__
#define __ETQW_DRAW_RAYTRACING_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

struct viewDef_s;
class idStr;

// Filled before vkCreateDevice.  Keeping the feature chain here lets the
// ray-tracing implementation own all optional Vulkan feature policy.
struct sdRayTracingDeviceConfiguration {
	VkPhysicalDeviceFeatures						coreFeatures;
	VkPhysicalDeviceBufferDeviceAddressFeatures	bufferDeviceAddress;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure;
	VkPhysicalDeviceRayQueryFeaturesKHR			rayQuery;
	const char*								deviceExtensions[ 3 ];
	unsigned int							deviceExtensionCount;

	sdRayTracingDeviceConfiguration();
};

struct sdRayTracingVulkanContext {
	VkPhysicalDevice			physicalDevice;
	VkDevice				device;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkFormat				colorFormat;
	VkFormat				depthFormat;
	unsigned int			frameCount;
	PFN_vkGetDeviceProcAddr	GetDeviceProcAddr;
};

struct sdRayTracingGeometry {
	// Static surfaces use the persistent triangle object as their owner.  A
	// deforming surface uses its render entity instead, so every deforming
	// entity/surface pair receives its own frame-safe BLAS cache entry.
	const void*		geometryOwner;
	int				surfaceId;
	bool			deforming;
	VkBuffer		vertexBuffer;
	VkDeviceSize	vertexOffset;
	unsigned int	vertexCount;
	VkBuffer		indexBuffer;
	VkDeviceSize	indexOffset;
	unsigned int	indexCount;
	float			transform[ 12 ];
	float			normal[ 4 ];
};

struct sdRayTracingViewContext {
	VkCommandBuffer	commandBuffer;
	unsigned int	frameIndex;
	VkImage			colorImage;
	VkImageView	colorImageView;
	VkImage			depthImage;
	VkImageView	depthImageView;
	VkImage			reflectionImage;
	VkImageView	reflectionImageView;
	VkSampler		reflectionSampler;
	VkExtent2D		framebufferExtent;
	VkRect2D		viewport;
};

bool R_RayTracingRequested();
void R_RayTracingResetBackendSelection();
void R_RayTracingSelectStencilFallback( const char* reason );
bool R_RayTracingUsingStencilFallback();

bool R_RayTracingConfigureDevice(
	PFN_vkGetPhysicalDeviceFeatures2 getFeatures,
	PFN_vkEnumerateDeviceExtensionProperties enumerateExtensions,
	VkPhysicalDevice physicalDevice,
	sdRayTracingDeviceConfiguration& configuration,
	idStr& reason );

bool R_RayTracingInit( const sdRayTracingVulkanContext& context );
void R_RayTracingShutdown();
void R_RayTracingPurgeGeometryCache();
bool R_RayTracingIsInitialized();
void R_RayTracingBeginFrame( unsigned int frameIndex );

bool R_RayTracingDrawView( const viewDef_s* view,
	const sdRayTracingGeometry* geometries, int geometryCount,
	const sdRayTracingGeometry* waterGeometries, int waterGeometryCount,
	const sdRayTracingViewContext& context );

#endif /* !__ETQW_DRAW_RAYTRACING_H__ */
