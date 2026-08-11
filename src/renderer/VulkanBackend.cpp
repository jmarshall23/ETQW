// Copyright (C) 2007 Id Software, Inc.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "VulkanBackend.h"
#include "RuntimeSpirvCompiler.h"
#include "tr_render.h"
#include "draw_local.h"
#include "Material.h"
#include "Image.h"
#include "VertexCache.h"
#include "renderbindings.h"
#include "megatexture/MegaTexture.h"
#include "../decllib/declAtmosphere.h"
#include "../decllib/declAmbientCubeMap.h"
#include "../decllib/declRenderBinding.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <windows.h>

namespace {

const int NUM_VULKAN_FRAMES = 2;
const VkDeviceSize VULKAN_GUI_VERTEX_BYTES = 4 * 1024 * 1024;
const int VULKAN_MATERIAL_TEXTURES = 5;

idCVar r_vkValidation(
	"r_vkValidation",
	"1",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT,
	"enable the Khronos Vulkan validation layer when it is installed"
);

idCVar r_vkSkipGui(
	"r_vkSkipGui",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"skip Vulkan GUI draws for renderer diagnostics"
);

idCVar r_vkDebugMaterials(
	"r_vkDebugMaterials",
	"0",
	CVAR_RENDERER | CVAR_BOOL,
	"print the first Vulkan world materials and their resolved textures"
);

struct sdVulkanFrame {
	VkCommandPool		commandPool;
	VkCommandBuffer	commandBuffer;
	VkSemaphore		imageAvailable;
	VkSemaphore		renderComplete;
	VkFence			fence;
	VkBuffer			guiVertexBuffer;
	VkDeviceMemory	guiVertexMemory;
	void*				guiVertexMapped;
	VkDeviceSize		guiVertexOffset;
};

struct sdVulkanImageResource {
	const void*		owner;
	VkImage			image;
	VkDeviceMemory	memory;
	VkImageView		view;
	VkSampler		sampler;
	VkDescriptorSet	descriptorSet;
	int				width;
	int				height;
	int				mipLevels;
};

struct sdVulkanBufferResource {
	const void*		owner;
	VkBuffer			buffer;
	VkDeviceMemory	memory;
	VkDeviceSize		bytes;
	bool				indexBuffer;
};

struct sdVulkanMaterialDescriptor {
	const materialStage_t*	owner;
	const void*				imageOwners[ VULKAN_MATERIAL_TEXTURES ];
	VkImageView				imageViews[ VULKAN_MATERIAL_TEXTURES ];
	VkDescriptorSet			descriptorSet;
};

const char* VkResultName( VkResult result ) {
	switch ( result ) {
		case VK_SUCCESS: return "VK_SUCCESS";
		case VK_NOT_READY: return "VK_NOT_READY";
		case VK_TIMEOUT: return "VK_TIMEOUT";
		case VK_EVENT_SET: return "VK_EVENT_SET";
		case VK_EVENT_RESET: return "VK_EVENT_RESET";
		case VK_INCOMPLETE: return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
		case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
		default: return "unknown VkResult";
	}
}

bool CheckVulkanResult( VkResult result, const char* operation ) {
	if ( result == VK_SUCCESS ) {
		return true;
	}
	common->Warning( "%s failed: %s (%d)", operation, VkResultName( result ),
		static_cast< int >( result ) );
	return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* ) {
	const char* message = callbackData != NULL && callbackData->pMessage != NULL ?
		callbackData->pMessage : "<no validation message>";
	if ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		common->Warning( "Vulkan validation error: %s", message );
	} else if ( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
		common->Warning( "Vulkan validation warning: %s", message );
	} else {
		common->DPrintf( "Vulkan validation: %s\n", message );
	}
	return VK_FALSE;
}

void InitDebugMessengerCreateInfo( VkDebugUtilsMessengerCreateInfoEXT& createInfo ) {
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = VulkanDebugCallback;
}

template< typename T >
bool LoadGlobalFunction( PFN_vkGetInstanceProcAddr getProcAddress,
	const char* name, T& function ) {
	function = reinterpret_cast< T >( getProcAddress( VK_NULL_HANDLE, name ) );
	if ( function == NULL ) {
		common->Warning( "Vulkan loader is missing %s", name );
		return false;
	}
	return true;
}

template< typename T >
bool LoadInstanceFunction( PFN_vkGetInstanceProcAddr getProcAddress,
	VkInstance instance, const char* name, T& function ) {
	function = reinterpret_cast< T >( getProcAddress( instance, name ) );
	if ( function == NULL ) {
		common->Warning( "Vulkan instance is missing %s", name );
		return false;
	}
	return true;
}

template< typename T >
bool LoadDeviceFunction( PFN_vkGetDeviceProcAddr getProcAddress,
	VkDevice device, const char* name, T& function ) {
	function = reinterpret_cast< T >( getProcAddress( device, name ) );
	if ( function == NULL ) {
		common->Warning( "Vulkan device is missing %s", name );
		return false;
	}
	return true;
}

bool HasExtension( const idList< VkExtensionProperties >& extensions,
	const char* name ) {
	for ( int i = 0; i < extensions.Num(); ++i ) {
		if ( idStr::Cmp( extensions[ i ].extensionName, name ) == 0 ) {
			return true;
		}
	}
	return false;
}

bool HasLayer( const idList< VkLayerProperties >& layers, const char* name ) {
	for ( int i = 0; i < layers.Num(); ++i ) {
		if ( idStr::Cmp( layers[ i ].layerName, name ) == 0 ) {
			return true;
		}
	}
	return false;
}

unsigned int ClampUnsigned( unsigned int value, unsigned int minimum,
	unsigned int maximum ) {
	if ( value < minimum ) {
		return minimum;
	}
	if ( maximum != 0 && value > maximum ) {
		return maximum;
	}
	return value;
}

} // namespace

struct sdVulkanBackendState {
	HMODULE					vulkanLibrary;
	HWND					nativeWindow;

	VkInstance				instance;
	VkDebugUtilsMessengerEXT	debugMessenger;
	VkSurfaceKHR				surface;
	VkPhysicalDevice			physicalDevice;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkDevice				device;
	VkQueue					graphicsQueue;
	unsigned int				queueFamilyIndex;
	char					deviceName[ VK_MAX_PHYSICAL_DEVICE_NAME_SIZE ];

	VkSwapchainKHR				swapchain;
	VkFormat				swapchainFormat;
	VkColorSpaceKHR			colorSpace;
	VkExtent2D				extent;
	idList< VkImage >			swapchainImages;
	idList< VkImageView >		swapchainViews;
	idList< byte >			imageInitialized;
	idList< VkImage >			depthImages;
	idList< VkDeviceMemory >	depthMemory;
	idList< VkImageView >		depthViews;
	idList< byte >			depthInitialized;
	bool					swapchainDirty;
	bool					swapchainTransferSource;
	int					swapInterval;
	sdVulkanImageResource	currentRenderResource;
	bool					currentRenderInitialized;

	sdVulkanFrame			frames[ NUM_VULKAN_FRAMES ];
	unsigned int				frameIndex;
	unsigned int				imageIndex;
	bool					frameActive;
	unsigned int				framesPresented;
	unsigned int				guiDrawCalls;
	unsigned int				worldDrawCalls;
	unsigned int				worldDepthDrawCalls;
	unsigned int				worldViewAttempts;
	unsigned int				worldViews;
	unsigned int				worldSurfaceCandidates;
	unsigned int				worldCacheReady;
	unsigned int				worldMaterialReady;
	unsigned int				worldResourceReady;
	unsigned int				worldMissingGeometry;
	unsigned int				worldMissingCache;
	unsigned int				worldMissingMaterial;
	unsigned int				worldMissingResource;
	unsigned int				worldMegaDrawCalls;
	unsigned int				worldSkinnedDrawCalls;
	unsigned int				worldSkyDrawCalls;
	unsigned int				worldStageDrawCalls;
	unsigned int				worldWaterDrawCalls;
	unsigned int				worldStuffDrawCalls;
	unsigned int				worldWaterDescriptorMisses;
	idList< const idMaterial* > reportedMissingMaterials;
	idList< const idMaterial* > reportedDrawMaterials;
	idList< sdVulkanImageResource > imageResources;
	idList< sdVulkanBufferResource > bufferResources;
	idList< sdVulkanMaterialDescriptor > materialDescriptors;
	// Resources removed while a frame is being recorded must remain alive until
	// that command buffer has been submitted and completed.  ETQW retires vertex
	// cache blocks in idRenderSystemLocal::EndFrame before the Vulkan backend
	// submits its frame, so destroying them immediately invalidates recorded
	// bindings and can asynchronously lose the device.
	idList< sdVulkanImageResource > retiredImageResources;
	idList< sdVulkanBufferResource > retiredBufferResources;
	VkDescriptorSetLayout		guiDescriptorSetLayout;
	VkDescriptorPool			guiDescriptorPool;
	VkPipelineLayout			guiPipelineLayout;
	VkPipeline				guiPipeline;
	VkPipeline				guiOpaquePipeline;
	VkPipeline				guiAddPipeline;
	VkPipeline				guiAlphaAddPipeline;
	VkPipeline				guiMultiplyPipeline;
	VkPipeline				worldPipeline;
	VkPipeline				worldDepthPipeline;
	VkPipeline				worldAlphaPipeline;
	VkPipeline				worldAddPipeline;
	VkPipeline				worldAlphaAddPipeline;
	VkPipeline				worldMultiplyPipeline;
	VkPipeline				worldMegaPipeline;
	VkPipeline				worldAtmospherePipeline;
	VkPipeline				worldMaterialPipeline;
	VkPipeline				worldMaterialAlphaPipeline;
	VkPipeline				worldMaterialAddPipeline;
	VkPipeline				worldWaterPipeline;
	VkPipeline				worldHeatHazePipeline;
	VkPipeline				skyPipeline;
	VkFormat					guiPipelineFormat;

	PFN_vkGetInstanceProcAddr			GetInstanceProcAddr;
	PFN_vkEnumerateInstanceVersion		EnumerateInstanceVersion;
	PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProperties;
	PFN_vkEnumerateInstanceLayerProperties	EnumerateInstanceLayerProperties;
	PFN_vkCreateInstance				CreateInstance;
	PFN_vkDestroyInstance				DestroyInstance;
	PFN_vkCreateDebugUtilsMessengerEXT	CreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT	DestroyDebugUtilsMessengerEXT;
	PFN_vkCreateWin32SurfaceKHR			CreateWin32SurfaceKHR;
	PFN_vkDestroySurfaceKHR			DestroySurfaceKHR;
	PFN_vkEnumeratePhysicalDevices		EnumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties		GetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceFeatures2		GetPhysicalDeviceFeatures2;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
	PFN_vkEnumerateDeviceExtensionProperties	EnumerateDeviceExtensionProperties;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR	GetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR	GetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateDevice				CreateDevice;
	PFN_vkGetDeviceProcAddr				GetDeviceProcAddr;

	PFN_vkDestroyDevice				DestroyDevice;
	PFN_vkGetDeviceQueue				GetDeviceQueue;
	PFN_vkDeviceWaitIdle				DeviceWaitIdle;
	PFN_vkQueueWaitIdle				QueueWaitIdle;
	PFN_vkCreateSwapchainKHR			CreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR			DestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR		GetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR			AcquireNextImageKHR;
	PFN_vkQueuePresentKHR				QueuePresentKHR;
	PFN_vkCreateImageView				CreateImageView;
	PFN_vkDestroyImageView				DestroyImageView;
	PFN_vkCreateImage					CreateImage;
	PFN_vkDestroyImage				DestroyImage;
	PFN_vkGetImageMemoryRequirements	GetImageMemoryRequirements;
	PFN_vkAllocateMemory				AllocateMemory;
	PFN_vkFreeMemory					FreeMemory;
	PFN_vkBindImageMemory				BindImageMemory;
	PFN_vkCreateSampler				CreateSampler;
	PFN_vkDestroySampler				DestroySampler;
	PFN_vkCreateBuffer				CreateBuffer;
	PFN_vkDestroyBuffer				DestroyBuffer;
	PFN_vkGetBufferMemoryRequirements	GetBufferMemoryRequirements;
	PFN_vkBindBufferMemory				BindBufferMemory;
	PFN_vkMapMemory					MapMemory;
	PFN_vkUnmapMemory					UnmapMemory;
	PFN_vkCreateShaderModule			CreateShaderModule;
	PFN_vkDestroyShaderModule			DestroyShaderModule;
	PFN_vkCreateDescriptorSetLayout	CreateDescriptorSetLayout;
	PFN_vkDestroyDescriptorSetLayout	DestroyDescriptorSetLayout;
	PFN_vkCreateDescriptorPool			CreateDescriptorPool;
	PFN_vkDestroyDescriptorPool		DestroyDescriptorPool;
	PFN_vkAllocateDescriptorSets		AllocateDescriptorSets;
	PFN_vkFreeDescriptorSets			FreeDescriptorSets;
	PFN_vkUpdateDescriptorSets			UpdateDescriptorSets;
	PFN_vkCreatePipelineLayout			CreatePipelineLayout;
	PFN_vkDestroyPipelineLayout		DestroyPipelineLayout;
	PFN_vkCreateGraphicsPipelines		CreateGraphicsPipelines;
	PFN_vkDestroyPipeline				DestroyPipeline;
	PFN_vkCreateCommandPool			CreateCommandPool;
	PFN_vkDestroyCommandPool			DestroyCommandPool;
	PFN_vkResetCommandPool				ResetCommandPool;
	PFN_vkAllocateCommandBuffers		AllocateCommandBuffers;
	PFN_vkBeginCommandBuffer			BeginCommandBuffer;
	PFN_vkEndCommandBuffer				EndCommandBuffer;
	PFN_vkCreateSemaphore				CreateSemaphore;
	PFN_vkDestroySemaphore				DestroySemaphore;
	PFN_vkCreateFence				CreateFence;
	PFN_vkDestroyFence				DestroyFence;
	PFN_vkWaitForFences				WaitForFences;
	PFN_vkResetFences				ResetFences;
	PFN_vkQueueSubmit2				QueueSubmit2;
	PFN_vkCmdPipelineBarrier2			CmdPipelineBarrier2;
	PFN_vkCmdCopyBufferToImage			CmdCopyBufferToImage;
	PFN_vkCmdCopyBuffer				CmdCopyBuffer;
	PFN_vkCmdCopyImage				CmdCopyImage;
	PFN_vkCmdBlitImage				CmdBlitImage;
	PFN_vkCmdBeginRendering			CmdBeginRendering;
	PFN_vkCmdEndRendering				CmdEndRendering;
	PFN_vkCmdSetViewport				CmdSetViewport;
	PFN_vkCmdSetScissor				CmdSetScissor;
	PFN_vkCmdBindPipeline				CmdBindPipeline;
	PFN_vkCmdBindDescriptorSets		CmdBindDescriptorSets;
	PFN_vkCmdPushConstants				CmdPushConstants;
	PFN_vkCmdBindVertexBuffers		CmdBindVertexBuffers;
	PFN_vkCmdBindIndexBuffer			CmdBindIndexBuffer;
	PFN_vkCmdDraw					CmdDraw;
	PFN_vkCmdDrawIndexed				CmdDrawIndexed;

	sdVulkanBackendState() {
		vulkanLibrary = NULL;
		nativeWindow = NULL;
		instance = VK_NULL_HANDLE;
		debugMessenger = VK_NULL_HANDLE;
		surface = VK_NULL_HANDLE;
		physicalDevice = VK_NULL_HANDLE;
		memset( &memoryProperties, 0, sizeof( memoryProperties ) );
		device = VK_NULL_HANDLE;
		graphicsQueue = VK_NULL_HANDLE;
		queueFamilyIndex = UINT_MAX;
		deviceName[ 0 ] = '\0';
		swapchain = VK_NULL_HANDLE;
		swapchainFormat = VK_FORMAT_UNDEFINED;
		colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		extent.width = 0;
		extent.height = 0;
		swapchainDirty = false;
		swapchainTransferSource = false;
		swapInterval = 0;
		memset( &currentRenderResource, 0, sizeof( currentRenderResource ) );
		currentRenderInitialized = false;
		memset( frames, 0, sizeof( frames ) );
		frameIndex = 0;
		imageIndex = 0;
		frameActive = false;
		framesPresented = 0;
		guiDrawCalls = 0;
		worldDrawCalls = 0;
		worldDepthDrawCalls = 0;
		worldViewAttempts = 0;
		worldViews = 0;
		worldSurfaceCandidates = 0;
		worldCacheReady = 0;
		worldMaterialReady = 0;
		worldResourceReady = 0;
		worldMissingGeometry = 0;
		worldMissingCache = 0;
		worldMissingMaterial = 0;
		worldMissingResource = 0;
		worldMegaDrawCalls = 0;
		worldSkinnedDrawCalls = 0;
		worldSkyDrawCalls = 0;
		worldStageDrawCalls = 0;
		worldWaterDrawCalls = 0;
		worldStuffDrawCalls = 0;
		worldWaterDescriptorMisses = 0;
		guiDescriptorSetLayout = VK_NULL_HANDLE;
		guiDescriptorPool = VK_NULL_HANDLE;
		guiPipelineLayout = VK_NULL_HANDLE;
		guiPipeline = VK_NULL_HANDLE;
		guiOpaquePipeline = VK_NULL_HANDLE;
		guiAddPipeline = VK_NULL_HANDLE;
		guiAlphaAddPipeline = VK_NULL_HANDLE;
		guiMultiplyPipeline = VK_NULL_HANDLE;
		worldPipeline = VK_NULL_HANDLE;
		worldDepthPipeline = VK_NULL_HANDLE;
		worldAlphaPipeline = VK_NULL_HANDLE;
		worldAddPipeline = VK_NULL_HANDLE;
		worldAlphaAddPipeline = VK_NULL_HANDLE;
		worldMultiplyPipeline = VK_NULL_HANDLE;
		worldMegaPipeline = VK_NULL_HANDLE;
		worldAtmospherePipeline = VK_NULL_HANDLE;
		worldMaterialPipeline = VK_NULL_HANDLE;
		worldMaterialAlphaPipeline = VK_NULL_HANDLE;
		worldMaterialAddPipeline = VK_NULL_HANDLE;
		worldWaterPipeline = VK_NULL_HANDLE;
		worldHeatHazePipeline = VK_NULL_HANDLE;
		skyPipeline = VK_NULL_HANDLE;
		guiPipelineFormat = VK_FORMAT_UNDEFINED;

#define CLEAR_VK_FUNCTION( name ) name = NULL
		CLEAR_VK_FUNCTION( GetInstanceProcAddr );
		CLEAR_VK_FUNCTION( EnumerateInstanceVersion );
		CLEAR_VK_FUNCTION( EnumerateInstanceExtensionProperties );
		CLEAR_VK_FUNCTION( EnumerateInstanceLayerProperties );
		CLEAR_VK_FUNCTION( CreateInstance );
		CLEAR_VK_FUNCTION( DestroyInstance );
		CLEAR_VK_FUNCTION( CreateDebugUtilsMessengerEXT );
		CLEAR_VK_FUNCTION( DestroyDebugUtilsMessengerEXT );
		CLEAR_VK_FUNCTION( CreateWin32SurfaceKHR );
		CLEAR_VK_FUNCTION( DestroySurfaceKHR );
		CLEAR_VK_FUNCTION( EnumeratePhysicalDevices );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceProperties );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceMemoryProperties );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceFeatures2 );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceQueueFamilyProperties );
		CLEAR_VK_FUNCTION( EnumerateDeviceExtensionProperties );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceSurfaceSupportKHR );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceSurfaceCapabilitiesKHR );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceSurfaceFormatsKHR );
		CLEAR_VK_FUNCTION( GetPhysicalDeviceSurfacePresentModesKHR );
		CLEAR_VK_FUNCTION( CreateDevice );
		CLEAR_VK_FUNCTION( GetDeviceProcAddr );
		CLEAR_VK_FUNCTION( DestroyDevice );
		CLEAR_VK_FUNCTION( GetDeviceQueue );
		CLEAR_VK_FUNCTION( DeviceWaitIdle );
		CLEAR_VK_FUNCTION( QueueWaitIdle );
		CLEAR_VK_FUNCTION( CreateSwapchainKHR );
		CLEAR_VK_FUNCTION( DestroySwapchainKHR );
		CLEAR_VK_FUNCTION( GetSwapchainImagesKHR );
		CLEAR_VK_FUNCTION( AcquireNextImageKHR );
		CLEAR_VK_FUNCTION( QueuePresentKHR );
		CLEAR_VK_FUNCTION( CreateImageView );
		CLEAR_VK_FUNCTION( DestroyImageView );
		CLEAR_VK_FUNCTION( CreateImage );
		CLEAR_VK_FUNCTION( DestroyImage );
		CLEAR_VK_FUNCTION( GetImageMemoryRequirements );
		CLEAR_VK_FUNCTION( AllocateMemory );
		CLEAR_VK_FUNCTION( FreeMemory );
		CLEAR_VK_FUNCTION( BindImageMemory );
		CLEAR_VK_FUNCTION( CreateSampler );
		CLEAR_VK_FUNCTION( DestroySampler );
		CLEAR_VK_FUNCTION( CreateBuffer );
		CLEAR_VK_FUNCTION( DestroyBuffer );
		CLEAR_VK_FUNCTION( GetBufferMemoryRequirements );
		CLEAR_VK_FUNCTION( BindBufferMemory );
		CLEAR_VK_FUNCTION( MapMemory );
		CLEAR_VK_FUNCTION( UnmapMemory );
		CLEAR_VK_FUNCTION( CreateShaderModule );
		CLEAR_VK_FUNCTION( DestroyShaderModule );
		CLEAR_VK_FUNCTION( CreateDescriptorSetLayout );
		CLEAR_VK_FUNCTION( DestroyDescriptorSetLayout );
		CLEAR_VK_FUNCTION( CreateDescriptorPool );
		CLEAR_VK_FUNCTION( DestroyDescriptorPool );
		CLEAR_VK_FUNCTION( AllocateDescriptorSets );
		CLEAR_VK_FUNCTION( FreeDescriptorSets );
		CLEAR_VK_FUNCTION( UpdateDescriptorSets );
		CLEAR_VK_FUNCTION( CreatePipelineLayout );
		CLEAR_VK_FUNCTION( DestroyPipelineLayout );
		CLEAR_VK_FUNCTION( CreateGraphicsPipelines );
		CLEAR_VK_FUNCTION( DestroyPipeline );
		CLEAR_VK_FUNCTION( CreateCommandPool );
		CLEAR_VK_FUNCTION( DestroyCommandPool );
		CLEAR_VK_FUNCTION( ResetCommandPool );
		CLEAR_VK_FUNCTION( AllocateCommandBuffers );
		CLEAR_VK_FUNCTION( BeginCommandBuffer );
		CLEAR_VK_FUNCTION( EndCommandBuffer );
		CLEAR_VK_FUNCTION( CreateSemaphore );
		CLEAR_VK_FUNCTION( DestroySemaphore );
		CLEAR_VK_FUNCTION( CreateFence );
		CLEAR_VK_FUNCTION( DestroyFence );
		CLEAR_VK_FUNCTION( WaitForFences );
		CLEAR_VK_FUNCTION( ResetFences );
		CLEAR_VK_FUNCTION( QueueSubmit2 );
		CLEAR_VK_FUNCTION( CmdPipelineBarrier2 );
		CLEAR_VK_FUNCTION( CmdCopyBufferToImage );
		CLEAR_VK_FUNCTION( CmdCopyBuffer );
		CLEAR_VK_FUNCTION( CmdCopyImage );
		CLEAR_VK_FUNCTION( CmdBlitImage );
		CLEAR_VK_FUNCTION( CmdBeginRendering );
		CLEAR_VK_FUNCTION( CmdEndRendering );
		CLEAR_VK_FUNCTION( CmdSetViewport );
		CLEAR_VK_FUNCTION( CmdSetScissor );
		CLEAR_VK_FUNCTION( CmdBindPipeline );
		CLEAR_VK_FUNCTION( CmdBindDescriptorSets );
		CLEAR_VK_FUNCTION( CmdPushConstants );
		CLEAR_VK_FUNCTION( CmdBindVertexBuffers );
		CLEAR_VK_FUNCTION( CmdBindIndexBuffer );
		CLEAR_VK_FUNCTION( CmdDraw );
		CLEAR_VK_FUNCTION( CmdDrawIndexed );
#undef CLEAR_VK_FUNCTION
	}
};

namespace {

void EncodeOctahedralDirection( idVec3 direction, float& encodedX,
	float& encodedY ) {
	if ( direction.Normalize() == 0.0f ) {
		direction.Set( 0.0f, 0.0f, 1.0f );
	}
	const float denominator = idMath::Fabs( direction.x ) +
		idMath::Fabs( direction.y ) + idMath::Fabs( direction.z );
	encodedX = direction.x / denominator;
	encodedY = direction.y / denominator;
	if ( direction.z < 0.0f ) {
		const float oldX = encodedX;
		const float oldY = encodedY;
		encodedX = ( 1.0f - idMath::Fabs( oldY ) ) *
			( oldX < 0.0f ? -1.0f : 1.0f );
		encodedY = ( 1.0f - idMath::Fabs( oldX ) ) *
			( oldY < 0.0f ? -1.0f : 1.0f );
	}
}

float PackSnorm2x16AsFloat( float x, float y ) {
	const short packedX = static_cast< short >( idMath::Ftoi(
		idMath::ClampFloat( -1.0f, 1.0f, x ) * 32767.0f ) );
	const short packedY = static_cast< short >( idMath::Ftoi(
		idMath::ClampFloat( -1.0f, 1.0f, y ) * 32767.0f ) );
	const unsigned int packed =
		static_cast< unsigned short >( packedX ) |
		( static_cast< unsigned int >(
			static_cast< unsigned short >( packedY ) ) << 16 );
	float encoded;
	memcpy( &encoded, &packed, sizeof( encoded ) );
	return encoded;
}

void EncodeModelRotation( const float modelMatrix[ 16 ], float& encodedXY,
	float& encodedZW ) {
	idMat3 modelAxis;
	modelAxis[ 0 ].Set( modelMatrix[ 0 ], modelMatrix[ 1 ], modelMatrix[ 2 ] );
	modelAxis[ 1 ].Set( modelMatrix[ 4 ], modelMatrix[ 5 ], modelMatrix[ 6 ] );
	modelAxis[ 2 ].Set( modelMatrix[ 8 ], modelMatrix[ 9 ], modelMatrix[ 10 ] );
	idQuat modelRotation = modelAxis.ToQuat();
	modelRotation.Normalize();
	encodedXY = PackSnorm2x16AsFloat( modelRotation.x, modelRotation.y );
	encodedZW = PackSnorm2x16AsFloat( modelRotation.z, modelRotation.w );
}

void ReportMissingVulkanMaterial( sdVulkanBackendState& state,
	const drawSurf_s& surface ) {
	if ( state.reportedMissingMaterials.Num() >= 24 ) {
		return;
	}
	for ( int i = 0; i < state.reportedMissingMaterials.Num(); ++i ) {
		if ( state.reportedMissingMaterials[ i ] == surface.material ) {
			return;
		}
	}
	state.reportedMissingMaterials.Append( surface.material );
	common->Printf( "Vulkan material fallback missing: '%s' (%d stages)\n",
		surface.material->GetName(), surface.material->GetNumStages() );
	for ( int stageIndex = 0; stageIndex < surface.material->GetNumStages(); ++stageIndex ) {
		const materialStage_t* stage = surface.material->GetStage( stageIndex );
		const bool enabled = surface.materialRegisters != NULL &&
			surface.materialRegisters[ stage->conditionRegister ] != 0.0f;
		common->Printf( "  stage %d: %s, %d textures, mega=%s, sequence=%s, program=%s\n",
			stageIndex, enabled ? "enabled" : "disabled", stage->numTextures,
			stage->megaTexture != NULL ? "yes" : "no",
			stage->imgSequence != NULL ? "yes" : "no",
			stage->renderProgram != NULL ? stage->renderProgram->GetName() : "<none>" );
		for ( int textureIndex = 0; textureIndex < stage->numTextures; ++textureIndex ) {
			const stageTexture_t& texture = stage->textures[ textureIndex ];
			common->Printf( "    texture %d: %s%s\n", textureIndex,
				texture.image != NULL ? texture.image->imgName.c_str() : "<null>",
				texture.image != NULL && texture.image->defaulted ? " (defaulted)" : "" );
		}
	}
}

void ReportVulkanDrawMaterial( sdVulkanBackendState& state,
	const drawSurf_s& surface ) {
	const char* materialName = surface.material->GetName();
	const bool importantEffect =
		idStr::FindText( materialName, "muzzle", false ) >= 0 ||
		idStr::FindText( materialName, "flash", false ) >= 0 ||
		idStr::FindText( materialName, "smoke", false ) >= 0 ||
		idStr::FindText( materialName, "explode", false ) >= 0 ||
		idStr::FindText( materialName, "particle", false ) >= 0 ||
		idStr::FindText( materialName, "heatHaze", false ) >= 0 ||
		surface.space->weaponDepthHack;
	if ( state.reportedDrawMaterials.Num() >= 16 && !importantEffect ) {
		return;
	}
	for ( int i = 0; i < state.reportedDrawMaterials.Num(); ++i ) {
		if ( state.reportedDrawMaterials[ i ] == surface.material ) {
			return;
		}
	}
	state.reportedDrawMaterials.Append( surface.material );
	common->Printf( "Vulkan material draw: '%s' (%d stages, coverage=%d, sort=%g)\n",
		materialName, surface.material->GetNumStages(),
		static_cast< int >( surface.material->Coverage() ),
		surface.material->GetSort() );
	if ( importantEffect && surface.geo != NULL && surface.space != NULL ) {
		common->Printf( "  effect geometry: model=%s, %d verts, %d indexes, bounds "
			"(%g %g %g)-(%g %g %g), scissor (%d %d)-(%d %d), "
			"weaponHack=%d fov=(%g %g) modelHack=%g\n",
			surface.space->model != NULL ? surface.space->model->Name() : "<null>",
			surface.geo->numVerts, surface.geo->numIndexes,
			surface.geo->bounds[ 0 ].x, surface.geo->bounds[ 0 ].y,
			surface.geo->bounds[ 0 ].z, surface.geo->bounds[ 1 ].x,
			surface.geo->bounds[ 1 ].y, surface.geo->bounds[ 1 ].z,
			surface.scissorRect.x1, surface.scissorRect.y1,
			surface.scissorRect.x2, surface.scissorRect.y2,
			surface.space->weaponDepthHack ? 1 : 0,
			surface.space->weaponDepthHackFOV_x,
			surface.space->weaponDepthHackFOV_y,
			surface.space->modelDepthHack );
	}
	for ( int stageIndex = 0; stageIndex < surface.material->GetNumStages(); ++stageIndex ) {
		const materialStage_t* stage = surface.material->GetStage( stageIndex );
		const bool enabled = surface.materialRegisters != NULL &&
			surface.materialRegisters[ stage->conditionRegister ] != 0.0f;
		common->Printf( "  stage %d: %s, state=0x%08x, %d textures, program=%s\n",
			stageIndex, enabled ? "enabled" : "disabled", stage->drawStateBits,
			stage->numTextures, stage->renderProgram != NULL ?
			stage->renderProgram->GetName() : "<none>" );
		for ( int textureIndex = 0; textureIndex < stage->numTextures; ++textureIndex ) {
			const stageTexture_t& texture = stage->textures[ textureIndex ];
			common->Printf( "    %s = %s%s (%dx%d, type %d)\n",
				texture.renderBinding != NULL ? texture.renderBinding->GetName() : "<binding>",
				texture.image != NULL ? texture.image->imgName.c_str() : "<null>",
				texture.image != NULL && texture.image->defaulted ? " (defaulted)" : "",
				texture.image != NULL ? texture.image->uploadWidth : 0,
				texture.image != NULL ? texture.image->uploadHeight : 0,
				texture.image != NULL ? texture.image->type : -1 );
		}
	}
}

bool LoadVulkanLibrary( sdVulkanBackendState& state ) {
	state.vulkanLibrary = LoadLibraryA( "vulkan-1.dll" );
	if ( state.vulkanLibrary == NULL ) {
		common->Warning( "Could not load the 32-bit Vulkan loader (vulkan-1.dll)" );
		return false;
	}
	state.GetInstanceProcAddr = reinterpret_cast< PFN_vkGetInstanceProcAddr >(
		GetProcAddress( state.vulkanLibrary, "vkGetInstanceProcAddr" ) );
	if ( state.GetInstanceProcAddr == NULL ) {
		common->Warning( "vulkan-1.dll does not export vkGetInstanceProcAddr" );
		return false;
	}

	state.EnumerateInstanceVersion = reinterpret_cast< PFN_vkEnumerateInstanceVersion >(
		state.GetInstanceProcAddr( VK_NULL_HANDLE, "vkEnumerateInstanceVersion" ) );
	return
		LoadGlobalFunction( state.GetInstanceProcAddr,
			"vkEnumerateInstanceExtensionProperties", state.EnumerateInstanceExtensionProperties ) &&
		LoadGlobalFunction( state.GetInstanceProcAddr,
			"vkEnumerateInstanceLayerProperties", state.EnumerateInstanceLayerProperties ) &&
		LoadGlobalFunction( state.GetInstanceProcAddr,
			"vkCreateInstance", state.CreateInstance );
}

bool LoadInstanceFunctions( sdVulkanBackendState& state ) {
	bool loaded = true;
#define LOAD_INSTANCE_FUNCTION( name ) loaded = LoadInstanceFunction( state.GetInstanceProcAddr, state.instance, "vk" #name, state.name ) && loaded
	LOAD_INSTANCE_FUNCTION( DestroyInstance );
	LOAD_INSTANCE_FUNCTION( CreateWin32SurfaceKHR );
	LOAD_INSTANCE_FUNCTION( DestroySurfaceKHR );
	LOAD_INSTANCE_FUNCTION( EnumeratePhysicalDevices );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceProperties );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceMemoryProperties );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceFeatures2 );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceQueueFamilyProperties );
	LOAD_INSTANCE_FUNCTION( EnumerateDeviceExtensionProperties );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceSurfaceSupportKHR );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceSurfaceCapabilitiesKHR );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceSurfaceFormatsKHR );
	LOAD_INSTANCE_FUNCTION( GetPhysicalDeviceSurfacePresentModesKHR );
	LOAD_INSTANCE_FUNCTION( CreateDevice );
	LOAD_INSTANCE_FUNCTION( GetDeviceProcAddr );
#undef LOAD_INSTANCE_FUNCTION
	state.CreateDebugUtilsMessengerEXT = reinterpret_cast< PFN_vkCreateDebugUtilsMessengerEXT >(
		state.GetInstanceProcAddr( state.instance, "vkCreateDebugUtilsMessengerEXT" ) );
	state.DestroyDebugUtilsMessengerEXT = reinterpret_cast< PFN_vkDestroyDebugUtilsMessengerEXT >(
		state.GetInstanceProcAddr( state.instance, "vkDestroyDebugUtilsMessengerEXT" ) );
	return loaded;
}

bool LoadDeviceFunctions( sdVulkanBackendState& state ) {
	bool loaded = true;
#define LOAD_DEVICE_FUNCTION( name ) loaded = LoadDeviceFunction( state.GetDeviceProcAddr, state.device, "vk" #name, state.name ) && loaded
	LOAD_DEVICE_FUNCTION( DestroyDevice );
	LOAD_DEVICE_FUNCTION( GetDeviceQueue );
	LOAD_DEVICE_FUNCTION( DeviceWaitIdle );
	LOAD_DEVICE_FUNCTION( QueueWaitIdle );
	LOAD_DEVICE_FUNCTION( CreateSwapchainKHR );
	LOAD_DEVICE_FUNCTION( DestroySwapchainKHR );
	LOAD_DEVICE_FUNCTION( GetSwapchainImagesKHR );
	LOAD_DEVICE_FUNCTION( AcquireNextImageKHR );
	LOAD_DEVICE_FUNCTION( QueuePresentKHR );
	LOAD_DEVICE_FUNCTION( CreateImageView );
	LOAD_DEVICE_FUNCTION( DestroyImageView );
	LOAD_DEVICE_FUNCTION( CreateImage );
	LOAD_DEVICE_FUNCTION( DestroyImage );
	LOAD_DEVICE_FUNCTION( GetImageMemoryRequirements );
	LOAD_DEVICE_FUNCTION( AllocateMemory );
	LOAD_DEVICE_FUNCTION( FreeMemory );
	LOAD_DEVICE_FUNCTION( BindImageMemory );
	LOAD_DEVICE_FUNCTION( CreateSampler );
	LOAD_DEVICE_FUNCTION( DestroySampler );
	LOAD_DEVICE_FUNCTION( CreateBuffer );
	LOAD_DEVICE_FUNCTION( DestroyBuffer );
	LOAD_DEVICE_FUNCTION( GetBufferMemoryRequirements );
	LOAD_DEVICE_FUNCTION( BindBufferMemory );
	LOAD_DEVICE_FUNCTION( MapMemory );
	LOAD_DEVICE_FUNCTION( UnmapMemory );
	LOAD_DEVICE_FUNCTION( CreateShaderModule );
	LOAD_DEVICE_FUNCTION( DestroyShaderModule );
	LOAD_DEVICE_FUNCTION( CreateDescriptorSetLayout );
	LOAD_DEVICE_FUNCTION( DestroyDescriptorSetLayout );
	LOAD_DEVICE_FUNCTION( CreateDescriptorPool );
	LOAD_DEVICE_FUNCTION( DestroyDescriptorPool );
	LOAD_DEVICE_FUNCTION( AllocateDescriptorSets );
	LOAD_DEVICE_FUNCTION( FreeDescriptorSets );
	LOAD_DEVICE_FUNCTION( UpdateDescriptorSets );
	LOAD_DEVICE_FUNCTION( CreatePipelineLayout );
	LOAD_DEVICE_FUNCTION( DestroyPipelineLayout );
	LOAD_DEVICE_FUNCTION( CreateGraphicsPipelines );
	LOAD_DEVICE_FUNCTION( DestroyPipeline );
	LOAD_DEVICE_FUNCTION( CreateCommandPool );
	LOAD_DEVICE_FUNCTION( DestroyCommandPool );
	LOAD_DEVICE_FUNCTION( ResetCommandPool );
	LOAD_DEVICE_FUNCTION( AllocateCommandBuffers );
	LOAD_DEVICE_FUNCTION( BeginCommandBuffer );
	LOAD_DEVICE_FUNCTION( EndCommandBuffer );
	LOAD_DEVICE_FUNCTION( CreateSemaphore );
	LOAD_DEVICE_FUNCTION( DestroySemaphore );
	LOAD_DEVICE_FUNCTION( CreateFence );
	LOAD_DEVICE_FUNCTION( DestroyFence );
	LOAD_DEVICE_FUNCTION( WaitForFences );
	LOAD_DEVICE_FUNCTION( ResetFences );
	LOAD_DEVICE_FUNCTION( QueueSubmit2 );
	LOAD_DEVICE_FUNCTION( CmdPipelineBarrier2 );
	LOAD_DEVICE_FUNCTION( CmdCopyBufferToImage );
	LOAD_DEVICE_FUNCTION( CmdCopyBuffer );
	LOAD_DEVICE_FUNCTION( CmdCopyImage );
	LOAD_DEVICE_FUNCTION( CmdBlitImage );
	LOAD_DEVICE_FUNCTION( CmdBeginRendering );
	LOAD_DEVICE_FUNCTION( CmdEndRendering );
	LOAD_DEVICE_FUNCTION( CmdSetViewport );
	LOAD_DEVICE_FUNCTION( CmdSetScissor );
	LOAD_DEVICE_FUNCTION( CmdBindPipeline );
	LOAD_DEVICE_FUNCTION( CmdBindDescriptorSets );
	LOAD_DEVICE_FUNCTION( CmdPushConstants );
	LOAD_DEVICE_FUNCTION( CmdBindVertexBuffers );
	LOAD_DEVICE_FUNCTION( CmdBindIndexBuffer );
	LOAD_DEVICE_FUNCTION( CmdDraw );
	LOAD_DEVICE_FUNCTION( CmdDrawIndexed );
#undef LOAD_DEVICE_FUNCTION
	return loaded;
}

bool CreateVulkanInstance( sdVulkanBackendState& state ) {
	unsigned int loaderVersion = VK_API_VERSION_1_0;
	if ( state.EnumerateInstanceVersion != NULL ) {
		state.EnumerateInstanceVersion( &loaderVersion );
	}
	if ( loaderVersion < VK_API_VERSION_1_3 ) {
		common->Warning( "Vulkan 1.3 is required; the loader only exposes %u.%u.%u",
			VK_API_VERSION_MAJOR( loaderVersion ), VK_API_VERSION_MINOR( loaderVersion ),
			VK_API_VERSION_PATCH( loaderVersion ) );
		return false;
	}

	unsigned int extensionCount = 0;
	if ( !CheckVulkanResult( state.EnumerateInstanceExtensionProperties(
		NULL, &extensionCount, NULL ), "vkEnumerateInstanceExtensionProperties" ) ) {
		return false;
	}
	idList< VkExtensionProperties > extensions;
	extensions.SetNum( extensionCount );
	if ( extensionCount != 0 && !CheckVulkanResult(
		state.EnumerateInstanceExtensionProperties( NULL, &extensionCount, extensions.Begin() ),
		"vkEnumerateInstanceExtensionProperties" ) ) {
		return false;
	}
	if ( !HasExtension( extensions, VK_KHR_SURFACE_EXTENSION_NAME ) ||
		!HasExtension( extensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) ) {
		common->Warning( "The Vulkan loader does not expose the required Win32 surface extensions" );
		return false;
	}

	unsigned int layerCount = 0;
	state.EnumerateInstanceLayerProperties( &layerCount, NULL );
	idList< VkLayerProperties > layers;
	layers.SetNum( layerCount );
	if ( layerCount != 0 ) {
		state.EnumerateInstanceLayerProperties( &layerCount, layers.Begin() );
	}
	const bool enableValidation = r_vkValidation.GetBool() &&
		HasLayer( layers, "VK_LAYER_KHRONOS_validation" );
	if ( r_vkValidation.GetBool() && !enableValidation ) {
		common->Warning( "VK_LAYER_KHRONOS_validation is not installed" );
	}
	const bool enableDebugUtils = enableValidation &&
		HasExtension( extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

	const char* enabledExtensions[ 3 ];
	unsigned int enabledExtensionCount = 0;
	enabledExtensions[ enabledExtensionCount++ ] = VK_KHR_SURFACE_EXTENSION_NAME;
	enabledExtensions[ enabledExtensionCount++ ] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
	if ( enableDebugUtils ) {
		enabledExtensions[ enabledExtensionCount++ ] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}
	const char* enabledLayers[ 1 ];
	unsigned int enabledLayerCount = 0;
	if ( enableValidation ) {
		enabledLayers[ enabledLayerCount++ ] = "VK_LAYER_KHRONOS_validation";
	}

	VkApplicationInfo applicationInfo;
	memset( &applicationInfo, 0, sizeof( applicationInfo ) );
	applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	applicationInfo.pApplicationName = GAME_NAME;
	applicationInfo.applicationVersion = VK_MAKE_VERSION( 1, 5, 0 );
	applicationInfo.pEngineName = "ETQW Vulkan";
	applicationInfo.engineVersion = VK_MAKE_VERSION( 1, 0, 0 );
	applicationInfo.apiVersion = VK_API_VERSION_1_3;

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
	InitDebugMessengerCreateInfo( debugCreateInfo );
	VkInstanceCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pNext = enableDebugUtils ? &debugCreateInfo : NULL;
	createInfo.pApplicationInfo = &applicationInfo;
	createInfo.enabledExtensionCount = enabledExtensionCount;
	createInfo.ppEnabledExtensionNames = enabledExtensions;
	createInfo.enabledLayerCount = enabledLayerCount;
	createInfo.ppEnabledLayerNames = enabledLayers;

	if ( !CheckVulkanResult( state.CreateInstance( &createInfo, NULL, &state.instance ),
		"vkCreateInstance" ) ) {
		return false;
	}
	if ( !LoadInstanceFunctions( state ) ) {
		return false;
	}
	if ( enableDebugUtils && state.CreateDebugUtilsMessengerEXT != NULL ) {
		if ( !CheckVulkanResult( state.CreateDebugUtilsMessengerEXT( state.instance,
			&debugCreateInfo, NULL, &state.debugMessenger ),
			"vkCreateDebugUtilsMessengerEXT" ) ) {
			return false;
		}
	}
	return true;
}

bool CreateVulkanSurface( sdVulkanBackendState& state ) {
	VkWin32SurfaceCreateInfoKHR createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hinstance = GetModuleHandleA( NULL );
	createInfo.hwnd = state.nativeWindow;
	return CheckVulkanResult( state.CreateWin32SurfaceKHR( state.instance,
		&createInfo, NULL, &state.surface ), "vkCreateWin32SurfaceKHR" );
}

bool DeviceHasExtension( sdVulkanBackendState& state, VkPhysicalDevice device,
	const char* extensionName ) {
	unsigned int count = 0;
	if ( state.EnumerateDeviceExtensionProperties( device, NULL, &count, NULL ) != VK_SUCCESS ) {
		return false;
	}
	idList< VkExtensionProperties > extensions;
	extensions.SetNum( count );
	if ( count != 0 && state.EnumerateDeviceExtensionProperties(
		device, NULL, &count, extensions.Begin() ) != VK_SUCCESS ) {
		return false;
	}
	return HasExtension( extensions, extensionName );
}

bool FindGraphicsPresentQueue( sdVulkanBackendState& state,
	VkPhysicalDevice device, unsigned int& queueFamilyIndex ) {
	unsigned int count = 0;
	state.GetPhysicalDeviceQueueFamilyProperties( device, &count, NULL );
	idList< VkQueueFamilyProperties > families;
	families.SetNum( count );
	if ( count != 0 ) {
		state.GetPhysicalDeviceQueueFamilyProperties( device, &count, families.Begin() );
	}
	for ( unsigned int i = 0; i < count; ++i ) {
		VkBool32 presentSupported = VK_FALSE;
		state.GetPhysicalDeviceSurfaceSupportKHR( device, i, state.surface,
			&presentSupported );
		if ( families[ i ].queueCount != 0 &&
			( families[ i ].queueFlags & VK_QUEUE_GRAPHICS_BIT ) != 0 &&
			presentSupported ) {
			queueFamilyIndex = i;
			return true;
		}
	}
	return false;
}

bool SurfaceIsUsable( sdVulkanBackendState& state, VkPhysicalDevice device ) {
	unsigned int formatCount = 0;
	unsigned int presentModeCount = 0;
	return state.GetPhysicalDeviceSurfaceFormatsKHR( device, state.surface,
		&formatCount, NULL ) == VK_SUCCESS && formatCount != 0 &&
		state.GetPhysicalDeviceSurfacePresentModesKHR( device, state.surface,
		&presentModeCount, NULL ) == VK_SUCCESS && presentModeCount != 0;
}

bool SelectPhysicalDevice( sdVulkanBackendState& state ) {
	unsigned int count = 0;
	if ( !CheckVulkanResult( state.EnumeratePhysicalDevices( state.instance,
		&count, NULL ), "vkEnumeratePhysicalDevices" ) || count == 0 ) {
		common->Warning( "No Vulkan physical devices were found" );
		return false;
	}
	idList< VkPhysicalDevice > devices;
	devices.SetNum( count );
	if ( !CheckVulkanResult( state.EnumeratePhysicalDevices( state.instance,
		&count, devices.Begin() ), "vkEnumeratePhysicalDevices" ) ) {
		return false;
	}

	int bestScore = -1;
	for ( unsigned int i = 0; i < count; ++i ) {
		VkPhysicalDeviceProperties properties;
		state.GetPhysicalDeviceProperties( devices[ i ], &properties );
		if ( properties.apiVersion < VK_API_VERSION_1_3 ||
			!DeviceHasExtension( state, devices[ i ], VK_KHR_SWAPCHAIN_EXTENSION_NAME ) ||
			!SurfaceIsUsable( state, devices[ i ] ) ) {
			continue;
		}

		unsigned int queueFamilyIndex = UINT_MAX;
		if ( !FindGraphicsPresentQueue( state, devices[ i ], queueFamilyIndex ) ) {
			continue;
		}

		VkPhysicalDeviceVulkan13Features features13;
		memset( &features13, 0, sizeof( features13 ) );
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		VkPhysicalDeviceFeatures2 features2;
		memset( &features2, 0, sizeof( features2 ) );
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &features13;
		state.GetPhysicalDeviceFeatures2( devices[ i ], &features2 );
		if ( !features13.dynamicRendering || !features13.synchronization2 ) {
			continue;
		}

		int score = 0;
		if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
			score += 1000;
		} else if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) {
			score += 500;
		}
		score += static_cast< int >( properties.limits.maxImageDimension2D / 1024 );
		if ( score > bestScore ) {
			bestScore = score;
			state.physicalDevice = devices[ i ];
			state.queueFamilyIndex = queueFamilyIndex;
			idStr::Copynz( state.deviceName, properties.deviceName,
				sizeof( state.deviceName ) );
		}
	}

	if ( state.physicalDevice == VK_NULL_HANDLE ) {
		common->Warning( "No Vulkan 1.3 device supports swapchain, dynamic rendering, and synchronization2" );
		return false;
	}
	state.GetPhysicalDeviceMemoryProperties( state.physicalDevice,
		&state.memoryProperties );
	return true;
}

bool CreateLogicalDevice( sdVulkanBackendState& state ) {
	const float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfo;
	memset( &queueCreateInfo, 0, sizeof( queueCreateInfo ) );
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = state.queueFamilyIndex;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	VkPhysicalDeviceVulkan13Features features13;
	memset( &features13, 0, sizeof( features13 ) );
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &features13;
	createInfo.queueCreateInfoCount = 1;
	createInfo.pQueueCreateInfos = &queueCreateInfo;
	createInfo.enabledExtensionCount = 1;
	createInfo.ppEnabledExtensionNames = extensions;
	if ( !CheckVulkanResult( state.CreateDevice( state.physicalDevice,
		&createInfo, NULL, &state.device ), "vkCreateDevice" ) ) {
		return false;
	}
	if ( !LoadDeviceFunctions( state ) ) {
		return false;
	}
	state.GetDeviceQueue( state.device, state.queueFamilyIndex, 0,
		&state.graphicsQueue );
	return state.graphicsQueue != VK_NULL_HANDLE;
}

VkSurfaceFormatKHR ChooseSurfaceFormat(
	const idList< VkSurfaceFormatKHR >& formats ) {
	// ETQW's shaders, textures, and legacy blend equations operate in the
	// gamma-encoded framebuffer domain.  An sRGB swapchain silently linearizes
	// blend operations, which makes low-valued additive GUI art (notably the
	// main-menu scanlines) much brighter than the OpenGL result.
	for ( int i = 0; i < formats.Num(); ++i ) {
		if ( formats[ i ].format == VK_FORMAT_B8G8R8A8_UNORM &&
			formats[ i ].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
			return formats[ i ];
		}
	}
	for ( int i = 0; i < formats.Num(); ++i ) {
		if ( formats[ i ].format == VK_FORMAT_B8G8R8A8_SRGB ) {
			return formats[ i ];
		}
	}
	return formats[ 0 ];
}

VkPresentModeKHR ChoosePresentMode(
	const idList< VkPresentModeKHR >& presentModes, int swapInterval ) {
	if ( swapInterval <= 0 ) {
		for ( int i = 0; i < presentModes.Num(); ++i ) {
			if ( presentModes[ i ] == VK_PRESENT_MODE_MAILBOX_KHR ) {
				return presentModes[ i ];
			}
		}
		for ( int i = 0; i < presentModes.Num(); ++i ) {
			if ( presentModes[ i ] == VK_PRESENT_MODE_IMMEDIATE_KHR ) {
				return presentModes[ i ];
			}
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(
	VkCompositeAlphaFlagsKHR supported ) {
	const VkCompositeAlphaFlagBitsKHR choices[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};
	for ( int i = 0; i < 4; ++i ) {
		if ( ( supported & choices[ i ] ) != 0 ) {
			return choices[ i ];
		}
	}
	return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

bool FindMemoryType( const sdVulkanBackendState& state, unsigned int typeBits,
	VkMemoryPropertyFlags required, unsigned int& typeIndex );

void DestroyCurrentRenderResource( sdVulkanBackendState& state,
	sdVulkanImageResource& resource ) {
	if ( state.device != VK_NULL_HANDLE ) {
		if ( resource.sampler != VK_NULL_HANDLE ) {
			state.DestroySampler( state.device, resource.sampler, NULL );
		}
		if ( resource.view != VK_NULL_HANDLE ) {
			state.DestroyImageView( state.device, resource.view, NULL );
		}
		if ( resource.image != VK_NULL_HANDLE ) {
			state.DestroyImage( state.device, resource.image, NULL );
		}
		if ( resource.memory != VK_NULL_HANDLE ) {
			state.FreeMemory( state.device, resource.memory, NULL );
		}
	}
	memset( &resource, 0, sizeof( resource ) );
}

bool CreateCurrentRenderResource( sdVulkanBackendState& state,
	VkFormat format, VkExtent2D extent, sdVulkanImageResource& resource ) {
	memset( &resource, 0, sizeof( resource ) );
	resource.width = static_cast< int >( extent.width );
	resource.height = static_cast< int >( extent.height );
	resource.mipLevels = 1;

	VkImageCreateInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = extent.width;
	imageInfo.extent.height = extent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( !CheckVulkanResult( state.CreateImage( state.device, &imageInfo,
		NULL, &resource.image ), "vkCreateImage(current render)" ) ) {
		return false;
	}

	VkMemoryRequirements requirements;
	state.GetImageMemoryRequirements( state.device, resource.image, &requirements );
	unsigned int memoryType = UINT_MAX;
	if ( !FindMemoryType( state, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType ) ) {
		DestroyCurrentRenderResource( state, resource );
		return false;
	}
	VkMemoryAllocateInfo allocateInfo;
	memset( &allocateInfo, 0, sizeof( allocateInfo ) );
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = memoryType;
	if ( !CheckVulkanResult( state.AllocateMemory( state.device, &allocateInfo,
		NULL, &resource.memory ), "vkAllocateMemory(current render)" ) ||
		!CheckVulkanResult( state.BindImageMemory( state.device, resource.image,
			resource.memory, 0 ), "vkBindImageMemory(current render)" ) ) {
		DestroyCurrentRenderResource( state, resource );
		return false;
	}

	VkImageViewCreateInfo viewInfo;
	memset( &viewInfo, 0, sizeof( viewInfo ) );
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = resource.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	if ( !CheckVulkanResult( state.CreateImageView( state.device, &viewInfo,
		NULL, &resource.view ), "vkCreateImageView(current render)" ) ) {
		DestroyCurrentRenderResource( state, resource );
		return false;
	}

	VkSamplerCreateInfo samplerInfo;
	memset( &samplerInfo, 0, sizeof( samplerInfo ) );
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 0.0f;
	if ( !CheckVulkanResult( state.CreateSampler( state.device, &samplerInfo,
		NULL, &resource.sampler ), "vkCreateSampler(current render)" ) ) {
		DestroyCurrentRenderResource( state, resource );
		return false;
	}
	return true;
}

void DestroyDepthResources( sdVulkanBackendState& state ) {
	if ( state.device != VK_NULL_HANDLE ) {
		for ( int i = 0; i < state.depthViews.Num(); ++i ) {
			if ( state.depthViews[ i ] != VK_NULL_HANDLE ) {
				state.DestroyImageView( state.device, state.depthViews[ i ], NULL );
			}
		}
		for ( int i = 0; i < state.depthImages.Num(); ++i ) {
			if ( state.depthImages[ i ] != VK_NULL_HANDLE ) {
				state.DestroyImage( state.device, state.depthImages[ i ], NULL );
			}
			if ( state.depthMemory[ i ] != VK_NULL_HANDLE ) {
				state.FreeMemory( state.device, state.depthMemory[ i ], NULL );
			}
		}
	}
	state.depthImages.Clear();
	state.depthMemory.Clear();
	state.depthViews.Clear();
	state.depthInitialized.Clear();
}

void DestroySwapchain( sdVulkanBackendState& state ) {
	DestroyCurrentRenderResource( state, state.currentRenderResource );
	state.currentRenderInitialized = false;
	state.swapchainTransferSource = false;
	DestroyDepthResources( state );
	if ( state.device != VK_NULL_HANDLE ) {
		for ( int i = 0; state.DestroyImageView != NULL &&
			i < state.swapchainViews.Num(); ++i ) {
			if ( state.swapchainViews[ i ] != VK_NULL_HANDLE ) {
				state.DestroyImageView( state.device, state.swapchainViews[ i ], NULL );
			}
		}
		if ( state.swapchain != VK_NULL_HANDLE && state.DestroySwapchainKHR != NULL ) {
			state.DestroySwapchainKHR( state.device, state.swapchain, NULL );
		}
	}
	state.swapchain = VK_NULL_HANDLE;
	state.swapchainImages.Clear();
	state.swapchainViews.Clear();
	state.imageInitialized.Clear();
	state.extent.width = 0;
	state.extent.height = 0;
}

bool CreateDepthResources( sdVulkanBackendState& state, unsigned int count,
	VkExtent2D extent, idList< VkImage >& images,
	idList< VkDeviceMemory >& memory, idList< VkImageView >& views ) {
	images.SetNum( count );
	memory.SetNum( count );
	views.SetNum( count );
	memset( images.Begin(), 0, count * sizeof( VkImage ) );
	memset( memory.Begin(), 0, count * sizeof( VkDeviceMemory ) );
	memset( views.Begin(), 0, count * sizeof( VkImageView ) );
	for ( unsigned int i = 0; i < count; ++i ) {
		VkImageCreateInfo imageInfo;
		memset( &imageInfo, 0, sizeof( imageInfo ) );
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_D32_SFLOAT;
		imageInfo.extent.width = extent.width;
		imageInfo.extent.height = extent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if ( !CheckVulkanResult( state.CreateImage( state.device, &imageInfo,
			NULL, &images[ i ] ), "vkCreateImage(depth)" ) ) {
			break;
		}
		VkMemoryRequirements requirements;
		state.GetImageMemoryRequirements( state.device, images[ i ], &requirements );
		unsigned int memoryType = UINT_MAX;
		if ( !FindMemoryType( state, requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType ) ) {
			break;
		}
		VkMemoryAllocateInfo allocateInfo;
		memset( &allocateInfo, 0, sizeof( allocateInfo ) );
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = memoryType;
		if ( !CheckVulkanResult( state.AllocateMemory( state.device,
			&allocateInfo, NULL, &memory[ i ] ), "vkAllocateMemory(depth)" ) ||
			!CheckVulkanResult( state.BindImageMemory( state.device, images[ i ],
				memory[ i ], 0 ), "vkBindImageMemory(depth)" ) ) {
			break;
		}
		VkImageViewCreateInfo viewInfo;
		memset( &viewInfo, 0, sizeof( viewInfo ) );
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = images[ i ];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_D32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		if ( !CheckVulkanResult( state.CreateImageView( state.device,
			&viewInfo, NULL, &views[ i ] ), "vkCreateImageView(depth)" ) ) {
			break;
		}
	}
	for ( unsigned int i = 0; i < count; ++i ) {
		if ( views[ i ] == VK_NULL_HANDLE ) {
			for ( unsigned int j = 0; j < count; ++j ) {
				if ( views[ j ] != VK_NULL_HANDLE ) {
					state.DestroyImageView( state.device, views[ j ], NULL );
				}
				if ( images[ j ] != VK_NULL_HANDLE ) {
					state.DestroyImage( state.device, images[ j ], NULL );
				}
				if ( memory[ j ] != VK_NULL_HANDLE ) {
					state.FreeMemory( state.device, memory[ j ], NULL );
				}
			}
			images.Clear();
			memory.Clear();
			views.Clear();
			return false;
		}
	}
	return true;
}

bool CreateSwapchain( sdVulkanBackendState& state, int requestedWidth,
	int requestedHeight ) {
	if ( requestedWidth <= 0 || requestedHeight <= 0 ) {
		return false;
	}
	VkSurfaceCapabilitiesKHR capabilities;
	if ( !CheckVulkanResult( state.GetPhysicalDeviceSurfaceCapabilitiesKHR(
		state.physicalDevice, state.surface, &capabilities ),
		"vkGetPhysicalDeviceSurfaceCapabilitiesKHR" ) ) {
		return false;
	}

	unsigned int formatCount = 0;
	state.GetPhysicalDeviceSurfaceFormatsKHR( state.physicalDevice, state.surface,
		&formatCount, NULL );
	idList< VkSurfaceFormatKHR > formats;
	formats.SetNum( formatCount );
	state.GetPhysicalDeviceSurfaceFormatsKHR( state.physicalDevice, state.surface,
		&formatCount, formats.Begin() );
	unsigned int presentModeCount = 0;
	state.GetPhysicalDeviceSurfacePresentModesKHR( state.physicalDevice, state.surface,
		&presentModeCount, NULL );
	idList< VkPresentModeKHR > presentModes;
	presentModes.SetNum( presentModeCount );
	state.GetPhysicalDeviceSurfacePresentModesKHR( state.physicalDevice, state.surface,
		&presentModeCount, presentModes.Begin() );
	if ( formats.Num() == 0 || presentModes.Num() == 0 ) {
		return false;
	}

	const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat( formats );
	state.swapInterval = cvarSystem->GetCVarInteger( "r_swapInterval" );
	const VkPresentModeKHR presentMode = ChoosePresentMode( presentModes,
		state.swapInterval );
	VkExtent2D extent;
	if ( capabilities.currentExtent.width != UINT_MAX ) {
		extent = capabilities.currentExtent;
	} else {
		extent.width = ClampUnsigned( static_cast< unsigned int >( requestedWidth ),
			capabilities.minImageExtent.width, capabilities.maxImageExtent.width );
		extent.height = ClampUnsigned( static_cast< unsigned int >( requestedHeight ),
			capabilities.minImageExtent.height, capabilities.maxImageExtent.height );
	}
	if ( extent.width == 0 || extent.height == 0 ) {
		return false;
	}

	unsigned int imageCount = capabilities.minImageCount + 1;
	if ( capabilities.maxImageCount != 0 && imageCount > capabilities.maxImageCount ) {
		imageCount = capabilities.maxImageCount;
	}
	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) {
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) {
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	VkSwapchainCreateInfoKHR createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = state.surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = imageUsage;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.preTransform = capabilities.currentTransform;
	createInfo.compositeAlpha = ChooseCompositeAlpha( capabilities.supportedCompositeAlpha );
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = state.swapchain;

	VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
	if ( !CheckVulkanResult( state.CreateSwapchainKHR( state.device, &createInfo,
		NULL, &newSwapchain ), "vkCreateSwapchainKHR" ) ) {
		return false;
	}
	unsigned int newImageCount = 0;
	state.GetSwapchainImagesKHR( state.device, newSwapchain, &newImageCount, NULL );
	idList< VkImage > newImages;
	newImages.SetNum( newImageCount );
	if ( !CheckVulkanResult( state.GetSwapchainImagesKHR( state.device, newSwapchain,
		&newImageCount, newImages.Begin() ), "vkGetSwapchainImagesKHR" ) ) {
		state.DestroySwapchainKHR( state.device, newSwapchain, NULL );
		return false;
	}
	idList< VkImageView > newViews;
	newViews.SetNum( newImageCount );
	memset( newViews.Begin(), 0, newImageCount * sizeof( VkImageView ) );
	for ( unsigned int i = 0; i < newImageCount; ++i ) {
		VkImageViewCreateInfo viewCreateInfo;
		memset( &viewCreateInfo, 0, sizeof( viewCreateInfo ) );
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image = newImages[ i ];
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = surfaceFormat.format;
		viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.layerCount = 1;
		if ( !CheckVulkanResult( state.CreateImageView( state.device,
			&viewCreateInfo, NULL, &newViews[ i ] ), "vkCreateImageView" ) ) {
			for ( unsigned int j = 0; j < i; ++j ) {
				state.DestroyImageView( state.device, newViews[ j ], NULL );
			}
			state.DestroySwapchainKHR( state.device, newSwapchain, NULL );
			return false;
		}
	}
	idList< VkImage > newDepthImages;
	idList< VkDeviceMemory > newDepthMemory;
	idList< VkImageView > newDepthViews;
	if ( !CreateDepthResources( state, newImageCount, extent, newDepthImages,
		newDepthMemory, newDepthViews ) ) {
		for ( unsigned int i = 0; i < newImageCount; ++i ) {
			state.DestroyImageView( state.device, newViews[ i ], NULL );
		}
		state.DestroySwapchainKHR( state.device, newSwapchain, NULL );
		return false;
	}
	sdVulkanImageResource newCurrentRenderResource;
	memset( &newCurrentRenderResource, 0, sizeof( newCurrentRenderResource ) );
	const bool canCopyCurrentRender =
		( imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) != 0;
	if ( canCopyCurrentRender && !CreateCurrentRenderResource( state,
		surfaceFormat.format, extent, newCurrentRenderResource ) ) {
		for ( unsigned int i = 0; i < newImageCount; ++i ) {
			state.DestroyImageView( state.device, newDepthViews[ i ], NULL );
			state.DestroyImage( state.device, newDepthImages[ i ], NULL );
			state.FreeMemory( state.device, newDepthMemory[ i ], NULL );
			state.DestroyImageView( state.device, newViews[ i ], NULL );
		}
		state.DestroySwapchainKHR( state.device, newSwapchain, NULL );
		return false;
	}

	DestroyCurrentRenderResource( state, state.currentRenderResource );
	DestroyDepthResources( state );
	for ( int i = 0; i < state.swapchainViews.Num(); ++i ) {
		state.DestroyImageView( state.device, state.swapchainViews[ i ], NULL );
	}
	if ( state.swapchain != VK_NULL_HANDLE ) {
		state.DestroySwapchainKHR( state.device, state.swapchain, NULL );
	}
	state.swapchain = newSwapchain;
	state.swapchainImages = newImages;
	state.swapchainViews = newViews;
	state.imageInitialized.SetNum( newImageCount );
	memset( state.imageInitialized.Begin(), 0, newImageCount );
	state.depthImages = newDepthImages;
	state.depthMemory = newDepthMemory;
	state.depthViews = newDepthViews;
	state.depthInitialized.SetNum( newImageCount );
	memset( state.depthInitialized.Begin(), 0, newImageCount );
	state.currentRenderResource = newCurrentRenderResource;
	state.currentRenderInitialized = false;
	state.swapchainTransferSource = canCopyCurrentRender;
	state.swapchainFormat = surfaceFormat.format;
	state.colorSpace = surfaceFormat.colorSpace;
	state.extent = extent;
	state.swapchainDirty = false;
	common->Printf( "Vulkan swapchain: %ux%u, %u images, format %d, present mode %d\n",
		extent.width, extent.height, newImageCount,
		static_cast< int >( surfaceFormat.format ), static_cast< int >( presentMode ) );
	return true;
}

bool CreateBufferAllocation( sdVulkanBackendState& state, VkDeviceSize bytes,
	VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
	VkBuffer& buffer, VkDeviceMemory& memory );

void DestroyFrames( sdVulkanBackendState& state ) {
	if ( state.device == VK_NULL_HANDLE ) {
		return;
	}
	for ( int i = 0; i < NUM_VULKAN_FRAMES; ++i ) {
		sdVulkanFrame& frame = state.frames[ i ];
		if ( frame.fence != VK_NULL_HANDLE && state.DestroyFence != NULL ) {
			state.DestroyFence( state.device, frame.fence, NULL );
		}
		if ( frame.imageAvailable != VK_NULL_HANDLE && state.DestroySemaphore != NULL ) {
			state.DestroySemaphore( state.device, frame.imageAvailable, NULL );
		}
		if ( frame.renderComplete != VK_NULL_HANDLE && state.DestroySemaphore != NULL ) {
			state.DestroySemaphore( state.device, frame.renderComplete, NULL );
		}
		if ( frame.guiVertexMapped != NULL && state.UnmapMemory != NULL ) {
			state.UnmapMemory( state.device, frame.guiVertexMemory );
			frame.guiVertexMapped = NULL;
		}
		if ( frame.guiVertexBuffer != VK_NULL_HANDLE && state.DestroyBuffer != NULL ) {
			state.DestroyBuffer( state.device, frame.guiVertexBuffer, NULL );
		}
		if ( frame.guiVertexMemory != VK_NULL_HANDLE && state.FreeMemory != NULL ) {
			state.FreeMemory( state.device, frame.guiVertexMemory, NULL );
		}
		if ( frame.commandPool != VK_NULL_HANDLE && state.DestroyCommandPool != NULL ) {
			state.DestroyCommandPool( state.device, frame.commandPool, NULL );
		}
		memset( &frame, 0, sizeof( frame ) );
	}
}

bool CreateFrames( sdVulkanBackendState& state ) {
	VkSemaphoreCreateInfo semaphoreCreateInfo;
	memset( &semaphoreCreateInfo, 0, sizeof( semaphoreCreateInfo ) );
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fenceCreateInfo;
	memset( &fenceCreateInfo, 0, sizeof( fenceCreateInfo ) );
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for ( int i = 0; i < NUM_VULKAN_FRAMES; ++i ) {
		sdVulkanFrame& frame = state.frames[ i ];
		VkCommandPoolCreateInfo poolCreateInfo;
		memset( &poolCreateInfo, 0, sizeof( poolCreateInfo ) );
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolCreateInfo.queueFamilyIndex = state.queueFamilyIndex;
		if ( !CheckVulkanResult( state.CreateCommandPool( state.device,
			&poolCreateInfo, NULL, &frame.commandPool ), "vkCreateCommandPool" ) ) {
			return false;
		}
		VkCommandBufferAllocateInfo allocateInfo;
		memset( &allocateInfo, 0, sizeof( allocateInfo ) );
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = frame.commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		if ( !CheckVulkanResult( state.AllocateCommandBuffers( state.device,
			&allocateInfo, &frame.commandBuffer ), "vkAllocateCommandBuffers" ) ||
			!CheckVulkanResult( state.CreateSemaphore( state.device,
				&semaphoreCreateInfo, NULL, &frame.imageAvailable ), "vkCreateSemaphore" ) ||
			!CheckVulkanResult( state.CreateSemaphore( state.device,
				&semaphoreCreateInfo, NULL, &frame.renderComplete ), "vkCreateSemaphore" ) ||
			!CheckVulkanResult( state.CreateFence( state.device,
				&fenceCreateInfo, NULL, &frame.fence ), "vkCreateFence" ) ) {
			return false;
		}
		if ( !CreateBufferAllocation( state, VULKAN_GUI_VERTEX_BYTES,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			frame.guiVertexBuffer, frame.guiVertexMemory ) ||
			!CheckVulkanResult( state.MapMemory( state.device, frame.guiVertexMemory,
				0, VULKAN_GUI_VERTEX_BYTES, 0, &frame.guiVertexMapped ),
				"vkMapMemory(gui vertices)" ) ) {
			return false;
		}
		frame.guiVertexOffset = 0;
	}
	return true;
}

void RestoreSignaledFence( sdVulkanBackendState& state, sdVulkanFrame& frame ) {
	if ( frame.fence != VK_NULL_HANDLE ) {
		state.DestroyFence( state.device, frame.fence, NULL );
		frame.fence = VK_NULL_HANDLE;
	}
	VkFenceCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	state.CreateFence( state.device, &createInfo, NULL, &frame.fence );
}

bool FindMemoryType( const sdVulkanBackendState& state, unsigned int typeBits,
	VkMemoryPropertyFlags required, unsigned int& typeIndex ) {
	for ( unsigned int i = 0; i < state.memoryProperties.memoryTypeCount; ++i ) {
		if ( ( typeBits & ( 1u << i ) ) != 0 &&
			( state.memoryProperties.memoryTypes[ i ].propertyFlags & required ) == required ) {
			typeIndex = i;
			return true;
		}
	}
	return false;
}

void DestroyImageResource( sdVulkanBackendState& state,
	sdVulkanImageResource& resource ) {
	if ( resource.descriptorSet != VK_NULL_HANDLE &&
		state.guiDescriptorPool != VK_NULL_HANDLE ) {
		state.FreeDescriptorSets( state.device, state.guiDescriptorPool, 1,
			&resource.descriptorSet );
	}
	if ( resource.sampler != VK_NULL_HANDLE ) {
		state.DestroySampler( state.device, resource.sampler, NULL );
	}
	if ( resource.view != VK_NULL_HANDLE ) {
		state.DestroyImageView( state.device, resource.view, NULL );
	}
	if ( resource.image != VK_NULL_HANDLE ) {
		state.DestroyImage( state.device, resource.image, NULL );
	}
	if ( resource.memory != VK_NULL_HANDLE ) {
		state.FreeMemory( state.device, resource.memory, NULL );
	}
	memset( &resource, 0, sizeof( resource ) );
}

void DestroyAllImageResources( sdVulkanBackendState& state ) {
	for ( int i = 0; i < state.imageResources.Num(); ++i ) {
		DestroyImageResource( state, state.imageResources[ i ] );
	}
	state.imageResources.Clear();
	for ( int i = 0; i < state.retiredImageResources.Num(); ++i ) {
		DestroyImageResource( state, state.retiredImageResources[ i ] );
	}
	state.retiredImageResources.Clear();
}

void DestroyBufferResource( sdVulkanBackendState& state,
	sdVulkanBufferResource& resource ) {
	if ( resource.buffer != VK_NULL_HANDLE ) {
		state.DestroyBuffer( state.device, resource.buffer, NULL );
	}
	if ( resource.memory != VK_NULL_HANDLE ) {
		state.FreeMemory( state.device, resource.memory, NULL );
	}
	memset( &resource, 0, sizeof( resource ) );
}

void DestroyAllBufferResources( sdVulkanBackendState& state ) {
	for ( int i = 0; i < state.bufferResources.Num(); ++i ) {
		DestroyBufferResource( state, state.bufferResources[ i ] );
	}
	state.bufferResources.Clear();
	for ( int i = 0; i < state.retiredBufferResources.Num(); ++i ) {
		DestroyBufferResource( state, state.retiredBufferResources[ i ] );
	}
	state.retiredBufferResources.Clear();
}

void DestroyRetiredResources( sdVulkanBackendState& state ) {
	for ( int i = 0; i < state.retiredImageResources.Num(); ++i ) {
		DestroyImageResource( state, state.retiredImageResources[ i ] );
	}
	state.retiredImageResources.Clear();
	for ( int i = 0; i < state.retiredBufferResources.Num(); ++i ) {
		DestroyBufferResource( state, state.retiredBufferResources[ i ] );
	}
	state.retiredBufferResources.Clear();
}

bool CreateBufferAllocation( sdVulkanBackendState& state, VkDeviceSize bytes,
	VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags,
	VkBuffer& buffer, VkDeviceMemory& memory ) {
	buffer = VK_NULL_HANDLE;
	memory = VK_NULL_HANDLE;
	VkBufferCreateInfo bufferInfo;
	memset( &bufferInfo, 0, sizeof( bufferInfo ) );
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bytes;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if ( !CheckVulkanResult( state.CreateBuffer( state.device, &bufferInfo, NULL,
		&buffer ), "vkCreateBuffer" ) ) {
		return false;
	}
	VkMemoryRequirements requirements;
	state.GetBufferMemoryRequirements( state.device, buffer, &requirements );
	unsigned int memoryType = UINT_MAX;
	if ( !FindMemoryType( state, requirements.memoryTypeBits, memoryFlags,
		memoryType ) ) {
		common->Warning( "No Vulkan buffer memory type satisfies flags 0x%x",
			static_cast< unsigned int >( memoryFlags ) );
		state.DestroyBuffer( state.device, buffer, NULL );
		buffer = VK_NULL_HANDLE;
		return false;
	}
	VkMemoryAllocateInfo allocateInfo;
	memset( &allocateInfo, 0, sizeof( allocateInfo ) );
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = memoryType;
	if ( !CheckVulkanResult( state.AllocateMemory( state.device, &allocateInfo,
		NULL, &memory ), "vkAllocateMemory(buffer)" ) ||
		!CheckVulkanResult( state.BindBufferMemory( state.device, buffer, memory, 0 ),
			"vkBindBufferMemory" ) ) {
		if ( memory != VK_NULL_HANDLE ) {
			state.FreeMemory( state.device, memory, NULL );
		}
		state.DestroyBuffer( state.device, buffer, NULL );
		buffer = VK_NULL_HANDLE;
		memory = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

bool SubmitImmediate( sdVulkanBackendState& state, VkCommandPool commandPool,
	VkCommandBuffer commandBuffer ) {
	if ( !CheckVulkanResult( state.EndCommandBuffer( commandBuffer ),
		"vkEndCommandBuffer(upload)" ) ) {
		state.DestroyCommandPool( state.device, commandPool, NULL );
		return false;
	}
	VkCommandBufferSubmitInfo commandInfo;
	memset( &commandInfo, 0, sizeof( commandInfo ) );
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	commandInfo.commandBuffer = commandBuffer;
	VkSubmitInfo2 submitInfo;
	memset( &submitInfo, 0, sizeof( submitInfo ) );
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandInfo;
	const bool submitted = CheckVulkanResult( state.QueueSubmit2(
		state.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE ),
		"vkQueueSubmit2(upload)" );
	const bool completed = submitted && CheckVulkanResult(
		state.QueueWaitIdle( state.graphicsQueue ), "vkQueueWaitIdle(upload)" );
	state.DestroyCommandPool( state.device, commandPool, NULL );
	return completed;
}

bool UploadBufferBytes( sdVulkanBackendState& state, VkBuffer destination,
	VkDeviceSize destinationOffset, const void* data, VkDeviceSize bytes,
	bool indexBuffer ) {
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	if ( !CreateBufferAllocation( state, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingMemory ) ) {
		return false;
	}
	void* mapped = NULL;
	if ( !CheckVulkanResult( state.MapMemory( state.device, stagingMemory, 0,
		bytes, 0, &mapped ), "vkMapMemory(buffer upload)" ) ) {
		state.DestroyBuffer( state.device, stagingBuffer, NULL );
		state.FreeMemory( state.device, stagingMemory, NULL );
		return false;
	}
	memcpy( mapped, data, static_cast< size_t >( bytes ) );
	state.UnmapMemory( state.device, stagingMemory );

	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	poolInfo.queueFamilyIndex = state.queueFamilyIndex;
	bool succeeded = CheckVulkanResult( state.CreateCommandPool( state.device,
		&poolInfo, NULL, &commandPool ), "vkCreateCommandPool(buffer upload)" );
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if ( succeeded ) {
		VkCommandBufferAllocateInfo allocateInfo;
		memset( &allocateInfo, 0, sizeof( allocateInfo ) );
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		succeeded = CheckVulkanResult( state.AllocateCommandBuffers( state.device,
			&allocateInfo, &commandBuffer ), "vkAllocateCommandBuffers(buffer upload)" );
	}
	if ( succeeded ) {
		VkCommandBufferBeginInfo beginInfo;
		memset( &beginInfo, 0, sizeof( beginInfo ) );
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		succeeded = CheckVulkanResult( state.BeginCommandBuffer( commandBuffer,
			&beginInfo ), "vkBeginCommandBuffer(buffer upload)" );
	}
	if ( succeeded ) {
		VkBufferCopy copy;
		copy.srcOffset = 0;
		copy.dstOffset = destinationOffset;
		copy.size = bytes;
		state.CmdCopyBuffer( commandBuffer, stagingBuffer, destination, 1, &copy );
		VkBufferMemoryBarrier2 barrier;
		memset( &barrier, 0, sizeof( barrier ) );
		barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
		barrier.dstAccessMask = indexBuffer ? VK_ACCESS_2_INDEX_READ_BIT :
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = destination;
		barrier.offset = destinationOffset;
		barrier.size = bytes;
		VkDependencyInfo dependencyInfo;
		memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.bufferMemoryBarrierCount = 1;
		dependencyInfo.pBufferMemoryBarriers = &barrier;
		state.CmdPipelineBarrier2( commandBuffer, &dependencyInfo );
		succeeded = SubmitImmediate( state, commandPool, commandBuffer );
		commandPool = VK_NULL_HANDLE;
	}
	if ( commandPool != VK_NULL_HANDLE ) {
		state.DestroyCommandPool( state.device, commandPool, NULL );
	}
	state.DestroyBuffer( state.device, stagingBuffer, NULL );
	state.FreeMemory( state.device, stagingMemory, NULL );
	return succeeded;
}

bool CompileVulkanShaderModule( sdVulkanBackendState& state,
	const char* sourceName, sdSpirvShaderStage stage, VkShaderModule& module ) {
	module = VK_NULL_HANDLE;
	void* sourceBuffer = NULL;
	const int sourceLength = fileSystem != NULL ? fileSystem->ReadFile(
		sourceName, &sourceBuffer ) : -1;
	if ( sourceLength <= 0 || sourceBuffer == NULL ) {
		common->Warning( "Could not load Vulkan shader '%s' through the engine filesystem",
			sourceName );
		return false;
	}
	sdSpirvCompileResult result;
	const bool compiled = R_CompileVulkanGLSL( sourceName,
		static_cast< const char* >( sourceBuffer ), sourceLength, stage,
		"renderer-vulkan-v1", false, result );
	fileSystem->FreeFile( sourceBuffer );
	if ( !compiled || result.words.Num() == 0 ) {
		common->Warning( "Vulkan shader compilation failed for '%s':\n%s",
			sourceName, result.diagnostics.c_str() );
		return false;
	}
	VkShaderModuleCreateInfo createInfo;
	memset( &createInfo, 0, sizeof( createInfo ) );
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = result.words.Num() * sizeof( unsigned int );
	createInfo.pCode = result.words.Begin();
	return CheckVulkanResult( state.CreateShaderModule( state.device,
		&createInfo, NULL, &module ), "vkCreateShaderModule" );
}

void DestroyGuiResources( sdVulkanBackendState& state ) {
	state.materialDescriptors.Clear();
	if ( state.worldHeatHazePipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldHeatHazePipeline, NULL );
		state.worldHeatHazePipeline = VK_NULL_HANDLE;
	}
	if ( state.guiMultiplyPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.guiMultiplyPipeline, NULL );
		state.guiMultiplyPipeline = VK_NULL_HANDLE;
	}
	if ( state.guiAlphaAddPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.guiAlphaAddPipeline, NULL );
		state.guiAlphaAddPipeline = VK_NULL_HANDLE;
	}
	if ( state.guiAddPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.guiAddPipeline, NULL );
		state.guiAddPipeline = VK_NULL_HANDLE;
	}
	if ( state.guiOpaquePipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.guiOpaquePipeline, NULL );
		state.guiOpaquePipeline = VK_NULL_HANDLE;
	}
	if ( state.skyPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.skyPipeline, NULL );
		state.skyPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldAtmospherePipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldAtmospherePipeline, NULL );
		state.worldAtmospherePipeline = VK_NULL_HANDLE;
	}
	if ( state.worldMaterialAddPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldMaterialAddPipeline, NULL );
		state.worldMaterialAddPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldMaterialAlphaPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldMaterialAlphaPipeline, NULL );
		state.worldMaterialAlphaPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldMaterialPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldMaterialPipeline, NULL );
		state.worldMaterialPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldWaterPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldWaterPipeline, NULL );
		state.worldWaterPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldMultiplyPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldMultiplyPipeline, NULL );
		state.worldMultiplyPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldAlphaAddPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldAlphaAddPipeline, NULL );
		state.worldAlphaAddPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldAddPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldAddPipeline, NULL );
		state.worldAddPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldAlphaPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldAlphaPipeline, NULL );
		state.worldAlphaPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldMegaPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldMegaPipeline, NULL );
		state.worldMegaPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldDepthPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldDepthPipeline, NULL );
		state.worldDepthPipeline = VK_NULL_HANDLE;
	}
	if ( state.worldPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.worldPipeline, NULL );
		state.worldPipeline = VK_NULL_HANDLE;
	}
	if ( state.guiPipeline != VK_NULL_HANDLE ) {
		state.DestroyPipeline( state.device, state.guiPipeline, NULL );
		state.guiPipeline = VK_NULL_HANDLE;
	}
	if ( state.guiPipelineLayout != VK_NULL_HANDLE ) {
		state.DestroyPipelineLayout( state.device, state.guiPipelineLayout, NULL );
		state.guiPipelineLayout = VK_NULL_HANDLE;
	}
	if ( state.guiDescriptorPool != VK_NULL_HANDLE ) {
		state.DestroyDescriptorPool( state.device, state.guiDescriptorPool, NULL );
		state.guiDescriptorPool = VK_NULL_HANDLE;
	}
	if ( state.guiDescriptorSetLayout != VK_NULL_HANDLE ) {
		state.DestroyDescriptorSetLayout( state.device,
			state.guiDescriptorSetLayout, NULL );
		state.guiDescriptorSetLayout = VK_NULL_HANDLE;
	}
	state.guiPipelineFormat = VK_FORMAT_UNDEFINED;
}

bool CreateWorldPipelineVariant( sdVulkanBackendState& state,
	const char* vertexShader, const char* fragmentShader,
	VkBlendFactor sourceBlend, VkBlendFactor destinationBlend, bool depthTest,
	bool depthWrite, bool vertexless,
	VkPipeline& pipeline, VkCompareOp depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
	bool colorWrite = true ) {
	VkShaderModule vertexModule = VK_NULL_HANDLE;
	VkShaderModule fragmentModule = VK_NULL_HANDLE;
	if ( !CompileVulkanShaderModule( state, vertexShader,
		SPIRV_SHADER_STAGE_VERTEX, vertexModule ) ||
		!CompileVulkanShaderModule( state, fragmentShader,
			SPIRV_SHADER_STAGE_FRAGMENT, fragmentModule ) ) {
		if ( vertexModule != VK_NULL_HANDLE ) {
			state.DestroyShaderModule( state.device, vertexModule, NULL );
		}
		if ( fragmentModule != VK_NULL_HANDLE ) {
			state.DestroyShaderModule( state.device, fragmentModule, NULL );
		}
		return false;
	}
	VkPipelineShaderStageCreateInfo shaderStages[ 2 ];
	memset( shaderStages, 0, sizeof( shaderStages ) );
	shaderStages[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[ 0 ].module = vertexModule;
	shaderStages[ 0 ].pName = "main";
	shaderStages[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[ 1 ].module = fragmentModule;
	shaderStages[ 1 ].pName = "main";

	VkVertexInputBindingDescription vertexBinding;
	vertexBinding.binding = 0;
	vertexBinding.stride = sizeof( idDrawVert );
	vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription attributes[ 8 ];
	memset( attributes, 0, sizeof( attributes ) );
	attributes[ 0 ].location = 0;
	attributes[ 0 ].binding = 0;
	attributes[ 0 ].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[ 0 ].offset = 0;
	attributes[ 1 ].location = 1;
	attributes[ 1 ].binding = 0;
	attributes[ 1 ].format = VK_FORMAT_R8G8B8A8_UNORM;
	attributes[ 1 ].offset = 12;
	attributes[ 2 ].location = 2;
	attributes[ 2 ].binding = 0;
	attributes[ 2 ].format = VK_FORMAT_R16G16_SSCALED;
	attributes[ 2 ].offset = 28;
	attributes[ 3 ].location = 3;
	attributes[ 3 ].binding = 0;
	attributes[ 3 ].format = VK_FORMAT_R16G16_SNORM;
	attributes[ 3 ].offset = 16;
	attributes[ 4 ].location = 4;
	attributes[ 4 ].binding = 0;
	attributes[ 4 ].format = VK_FORMAT_R8_UINT;
	attributes[ 4 ].offset = 24;
	attributes[ 5 ].location = 5;
	attributes[ 5 ].binding = 0;
	attributes[ 5 ].format = VK_FORMAT_R16G16_SNORM;
	attributes[ 5 ].offset = 20;
	attributes[ 6 ].location = 6;
	attributes[ 6 ].binding = 0;
	attributes[ 6 ].format = VK_FORMAT_R8_UINT;
	attributes[ 6 ].offset = 25;
	attributes[ 7 ].location = 7;
	attributes[ 7 ].binding = 0;
	attributes[ 7 ].format = VK_FORMAT_R8_UINT;
	attributes[ 7 ].offset = 26;
	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = vertexless ? 0 : 1;
	vertexInput.pVertexBindingDescriptions = vertexless ? NULL : &vertexBinding;
	vertexInput.vertexAttributeDescriptionCount = vertexless ? 0 : 8;
	vertexInput.pVertexAttributeDescriptions = vertexless ? NULL : attributes;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	memset( &inputAssembly, 0, sizeof( inputAssembly ) );
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewportState;
	memset( &viewportState, 0, sizeof( viewportState ) );
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rasterization;
	memset( &rasterization, 0, sizeof( rasterization ) );
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample;
	memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	memset( &depthStencil, 0, sizeof( depthStencil ) );
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = depthCompare;
	VkPipelineColorBlendAttachmentState blendAttachment;
	memset( &blendAttachment, 0, sizeof( blendAttachment ) );
	blendAttachment.colorWriteMask = colorWrite ?
		( VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT ) : 0;
	if ( sourceBlend != VK_BLEND_FACTOR_ONE ||
		destinationBlend != VK_BLEND_FACTOR_ZERO ) {
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = sourceBlend;
		blendAttachment.dstColorBlendFactor = destinationBlend;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	VkPipelineColorBlendStateCreateInfo blendState;
	memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = 1;
	blendState.pAttachments = &blendAttachment;
	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState;
	memset( &dynamicState, 0, sizeof( dynamicState ) );
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;
	VkPipelineRenderingCreateInfo renderingInfo;
	memset( &renderingInfo, 0, sizeof( renderingInfo ) );
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &state.swapchainFormat;
	renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
	VkGraphicsPipelineCreateInfo pipelineInfo;
	memset( &pipelineInfo, 0, sizeof( pipelineInfo ) );
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterization;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &blendState;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = state.guiPipelineLayout;
	const bool created = CheckVulkanResult( state.CreateGraphicsPipelines(
		state.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
		&pipeline ), "vkCreateGraphicsPipelines(world)" );
	state.DestroyShaderModule( state.device, vertexModule, NULL );
	state.DestroyShaderModule( state.device, fragmentModule, NULL );
	return created;
}

bool CreateWorldPipeline( sdVulkanBackendState& state ) {
	return CreateWorldPipelineVariant( state, "vkprogs/world/ambient.vert",
		"vkprogs/world/ambient.frag", VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_ZERO, true, true, false, state.worldDepthPipeline,
		VK_COMPARE_OP_LESS_OR_EQUAL, false ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/ambient.vert",
		"vkprogs/world/ambient.frag", VK_BLEND_FACTOR_ONE,
		VK_BLEND_FACTOR_ZERO, true, false, false, state.worldPipeline,
		VK_COMPARE_OP_EQUAL ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/trivial.vert",
			"vkprogs/world/trivial.frag", VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true, false, false,
			state.worldAlphaPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/trivial.vert",
			"vkprogs/world/trivial.frag", VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ONE, true, false, false, state.worldAddPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/trivial.vert",
			"vkprogs/world/trivial.frag", VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE, true, false, false, state.worldAlphaAddPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/trivial.vert",
			"vkprogs/world/trivial.frag", VK_BLEND_FACTOR_DST_COLOR,
			VK_BLEND_FACTOR_ZERO, true, false, false, state.worldMultiplyPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/mega.vert",
			"vkprogs/world/mega.frag", VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true, false, false,
			state.worldMegaPipeline, VK_COMPARE_OP_EQUAL ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/atmosphere.vert",
			"vkprogs/world/atmosphere.frag", VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ONE, true, false, false,
			state.worldAtmospherePipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/material.vert",
			"vkprogs/world/material.frag", VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ZERO, true, false, false,
			state.worldMaterialPipeline, VK_COMPARE_OP_EQUAL ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/material.vert",
			"vkprogs/world/material.frag", VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true, false, false,
			state.worldMaterialAlphaPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/material.vert",
			"vkprogs/world/material.frag", VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ONE, true, false, false,
			state.worldMaterialAddPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/water.vert",
			"vkprogs/world/water.frag", VK_BLEND_FACTOR_SRC_ALPHA,
			VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true, false, false,
			state.worldWaterPipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/heat_haze.vert",
			"vkprogs/world/heat_haze.frag", VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ZERO, true, false, false,
			state.worldHeatHazePipeline ) &&
		CreateWorldPipelineVariant( state, "vkprogs/world/sky.vert",
			"vkprogs/world/sky.frag", VK_BLEND_FACTOR_ONE,
			VK_BLEND_FACTOR_ZERO, false, false, true, state.skyPipeline );
}

bool CreateGuiResources( sdVulkanBackendState& state ) {
	VkDescriptorSetLayoutBinding samplerBindings[ VULKAN_MATERIAL_TEXTURES ];
	memset( samplerBindings, 0, sizeof( samplerBindings ) );
	for ( int bindingIndex = 0; bindingIndex < VULKAN_MATERIAL_TEXTURES;
		++bindingIndex ) {
		samplerBindings[ bindingIndex ].binding = bindingIndex;
		samplerBindings[ bindingIndex ].descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerBindings[ bindingIndex ].descriptorCount = 1;
		samplerBindings[ bindingIndex ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	VkDescriptorSetLayoutCreateInfo setLayoutInfo;
	memset( &setLayoutInfo, 0, sizeof( setLayoutInfo ) );
	setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutInfo.bindingCount = VULKAN_MATERIAL_TEXTURES;
	setLayoutInfo.pBindings = samplerBindings;
	if ( !CheckVulkanResult( state.CreateDescriptorSetLayout( state.device,
		&setLayoutInfo, NULL, &state.guiDescriptorSetLayout ),
		"vkCreateDescriptorSetLayout(gui)" ) ) {
		return false;
	}

	VkDescriptorPoolSize poolSize;
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = 16384 * VULKAN_MATERIAL_TEXTURES;
	VkDescriptorPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 16384;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	if ( !CheckVulkanResult( state.CreateDescriptorPool( state.device,
		&poolInfo, NULL, &state.guiDescriptorPool ),
		"vkCreateDescriptorPool(gui)" ) ) {
		DestroyGuiResources( state );
		return false;
	}

	VkPushConstantRange pushRange;
	memset( &pushRange, 0, sizeof( pushRange ) );
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
		VK_SHADER_STAGE_FRAGMENT_BIT;
	pushRange.size = sizeof( float ) * 32;
	VkPipelineLayoutCreateInfo pipelineLayoutInfo;
	memset( &pipelineLayoutInfo, 0, sizeof( pipelineLayoutInfo ) );
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &state.guiDescriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushRange;
	if ( !CheckVulkanResult( state.CreatePipelineLayout( state.device,
		&pipelineLayoutInfo, NULL, &state.guiPipelineLayout ),
		"vkCreatePipelineLayout(gui)" ) ) {
		DestroyGuiResources( state );
		return false;
	}

	VkShaderModule vertexModule = VK_NULL_HANDLE;
	VkShaderModule fragmentModule = VK_NULL_HANDLE;
	if ( !CompileVulkanShaderModule( state, "vkprogs/gui/gui.vert",
		SPIRV_SHADER_STAGE_VERTEX, vertexModule ) ||
		!CompileVulkanShaderModule( state, "vkprogs/gui/gui.frag",
			SPIRV_SHADER_STAGE_FRAGMENT, fragmentModule ) ) {
		if ( vertexModule != VK_NULL_HANDLE ) {
			state.DestroyShaderModule( state.device, vertexModule, NULL );
		}
		if ( fragmentModule != VK_NULL_HANDLE ) {
			state.DestroyShaderModule( state.device, fragmentModule, NULL );
		}
		DestroyGuiResources( state );
		return false;
	}

	VkPipelineShaderStageCreateInfo shaderStages[ 2 ];
	memset( shaderStages, 0, sizeof( shaderStages ) );
	shaderStages[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[ 0 ].module = vertexModule;
	shaderStages[ 0 ].pName = "main";
	shaderStages[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[ 1 ].module = fragmentModule;
	shaderStages[ 1 ].pName = "main";
	VkVertexInputBindingDescription vertexBinding;
	vertexBinding.binding = 0;
	vertexBinding.stride = sizeof( sdVulkanGuiVertex );
	vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription vertexAttributes[ 2 ];
	memset( vertexAttributes, 0, sizeof( vertexAttributes ) );
	vertexAttributes[ 0 ].location = 0;
	vertexAttributes[ 0 ].binding = 0;
	vertexAttributes[ 0 ].format = VK_FORMAT_R32G32_SFLOAT;
	vertexAttributes[ 0 ].offset = 0;
	vertexAttributes[ 1 ].location = 1;
	vertexAttributes[ 1 ].binding = 0;
	vertexAttributes[ 1 ].format = VK_FORMAT_R32G32_SFLOAT;
	vertexAttributes[ 1 ].offset = sizeof( float ) * 2;
	VkPipelineVertexInputStateCreateInfo vertexInput;
	memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &vertexBinding;
	vertexInput.vertexAttributeDescriptionCount = 2;
	vertexInput.pVertexAttributeDescriptions = vertexAttributes;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	memset( &inputAssembly, 0, sizeof( inputAssembly ) );
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
	VkPipelineViewportStateCreateInfo viewportState;
	memset( &viewportState, 0, sizeof( viewportState ) );
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rasterization;
	memset( &rasterization, 0, sizeof( rasterization ) );
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample;
	memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState blendAttachment;
	memset( &blendAttachment, 0, sizeof( blendAttachment ) );
	blendAttachment.blendEnable = VK_TRUE;
	blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blendState;
	memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = 1;
	blendState.pAttachments = &blendAttachment;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	memset( &depthStencil, 0, sizeof( depthStencil ) );
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;
	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState;
	memset( &dynamicState, 0, sizeof( dynamicState ) );
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;
	VkPipelineRenderingCreateInfo renderingInfo;
	memset( &renderingInfo, 0, sizeof( renderingInfo ) );
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &state.swapchainFormat;
	renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
	VkGraphicsPipelineCreateInfo pipelineInfo;
	memset( &pipelineInfo, 0, sizeof( pipelineInfo ) );
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterization;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pColorBlendState = &blendState;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = state.guiPipelineLayout;
	bool created = CheckVulkanResult( state.CreateGraphicsPipelines(
		state.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
		&state.guiPipeline ), "vkCreateGraphicsPipelines(gui)" );
	if ( created ) {
		blendAttachment.blendEnable = VK_FALSE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		created = CheckVulkanResult( state.CreateGraphicsPipelines(
			state.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
			&state.guiOpaquePipeline ), "vkCreateGraphicsPipelines(gui opaque)" );
	}
	if ( created ) {
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		created = CheckVulkanResult( state.CreateGraphicsPipelines(
			state.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
			&state.guiAddPipeline ), "vkCreateGraphicsPipelines(gui add)" );
	}
	if ( created ) {
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		created = CheckVulkanResult( state.CreateGraphicsPipelines(
			state.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
			&state.guiAlphaAddPipeline ), "vkCreateGraphicsPipelines(gui alpha add)" );
	}
	if ( created ) {
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		created = CheckVulkanResult( state.CreateGraphicsPipelines(
			state.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
			&state.guiMultiplyPipeline ), "vkCreateGraphicsPipelines(gui multiply)" );
	}
	state.DestroyShaderModule( state.device, vertexModule, NULL );
	state.DestroyShaderModule( state.device, fragmentModule, NULL );
	if ( !created ) {
		DestroyGuiResources( state );
		return false;
	}
	if ( !CreateWorldPipeline( state ) ) {
		DestroyGuiResources( state );
		return false;
	}
	state.guiPipelineFormat = state.swapchainFormat;
	return true;
}

bool AllocateImageDescriptor( sdVulkanBackendState& state,
	sdVulkanImageResource& resource ) {
	VkDescriptorSetAllocateInfo allocateInfo;
	memset( &allocateInfo, 0, sizeof( allocateInfo ) );
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = state.guiDescriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &state.guiDescriptorSetLayout;
	if ( !CheckVulkanResult( state.AllocateDescriptorSets( state.device,
		&allocateInfo, &resource.descriptorSet ),
		"vkAllocateDescriptorSets(image)" ) ) {
		return false;
	}
	VkDescriptorImageInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sampler = resource.sampler;
	imageInfo.imageView = resource.view;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet writes[ VULKAN_MATERIAL_TEXTURES ];
	memset( writes, 0, sizeof( writes ) );
	for ( int bindingIndex = 0; bindingIndex < VULKAN_MATERIAL_TEXTURES;
		++bindingIndex ) {
		writes[ bindingIndex ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[ bindingIndex ].dstSet = resource.descriptorSet;
		writes[ bindingIndex ].dstBinding = bindingIndex;
		writes[ bindingIndex ].descriptorCount = 1;
		writes[ bindingIndex ].descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[ bindingIndex ].pImageInfo = &imageInfo;
	}
	state.UpdateDescriptorSets( state.device, VULKAN_MATERIAL_TEXTURES,
		writes, 0, NULL );
	return true;
}

const sdVulkanImageResource* FindVulkanImageResource(
	const sdVulkanBackendState& state, const void* owner ) {
	for ( int resourceIndex = 0; resourceIndex < state.imageResources.Num();
		++resourceIndex ) {
		if ( state.imageResources[ resourceIndex ].owner == owner ) {
			return &state.imageResources[ resourceIndex ];
		}
	}
	return NULL;
}

bool StageUsesVulkanMaterialTextures( const materialStage_t& stage ) {
	for ( int textureIndex = 0; textureIndex < stage.numTextures; ++textureIndex ) {
		const stageTexture_t& texture = stage.textures[ textureIndex ];
		if ( rbinds != NULL && ( texture.renderBinding == rbinds->bumpMap ||
			 texture.renderBinding == rbinds->specularMap ) ) {
			return true;
		}
		if ( texture.renderBinding != NULL &&
			idStr::Icmp( texture.renderBinding->GetName(), "selfillummap" ) == 0 ) {
			return true;
		}
	}
	return false;
}

bool GetVulkanMaterialDescriptor( sdVulkanBackendState& state,
	const materialStage_t& stage, idImage* selectedImage,
	const viewEntity_s* space,
	VkDescriptorSet& descriptorSet ) {
	descriptorSet = VK_NULL_HANDLE;
	if ( globalImages == NULL || selectedImage == NULL ) {
		return false;
	}
	idImage* images[ VULKAN_MATERIAL_TEXTURES ] = {
		selectedImage,
		globalImages->flatNormalMap,
		globalImages->blackImage,
		globalImages->blackImage,
		globalImages->blackCubeMapImage
	};
	if ( space != NULL && space->ambientCubeMap != NULL &&
		space->ambientCubeMap->GetAmbientCubeMap() != NULL ) {
		images[ 4 ] = space->ambientCubeMap->GetAmbientCubeMap();
	}
	for ( int textureIndex = 0; textureIndex < stage.numTextures; ++textureIndex ) {
		const stageTexture_t& texture = stage.textures[ textureIndex ];
		if ( texture.image == NULL || texture.image->defaulted ) {
			continue;
		}
		if ( rbinds != NULL && texture.renderBinding == rbinds->diffuseMap ) {
			images[ 0 ] = texture.image;
		} else if ( rbinds != NULL && texture.renderBinding == rbinds->bumpMap ) {
			images[ 1 ] = texture.image;
		} else if ( rbinds != NULL && texture.renderBinding == rbinds->specularMap ) {
			images[ 2 ] = texture.image;
		} else if ( texture.renderBinding != NULL &&
			idStr::Icmp( texture.renderBinding->GetName(), "selfillummap" ) == 0 ) {
			images[ 3 ] = texture.image;
		}
	}
	const sdVulkanImageResource* resources[ VULKAN_MATERIAL_TEXTURES ];
	for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES; ++imageIndex ) {
		if ( images[ imageIndex ] == NULL ) {
			return false;
		}
		if ( !images[ imageIndex ]->IsLoaded() ) {
			images[ imageIndex ]->BindFragment();
		}
		resources[ imageIndex ] = FindVulkanImageResource( state,
			images[ imageIndex ] );
		if ( resources[ imageIndex ] == NULL ) {
			return false;
		}
	}
	for ( int descriptorIndex = 0;
		descriptorIndex < state.materialDescriptors.Num(); ++descriptorIndex ) {
		const sdVulkanMaterialDescriptor& cached =
			state.materialDescriptors[ descriptorIndex ];
		if ( cached.owner != &stage ) {
			continue;
		}
		bool matches = true;
		for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES;
			++imageIndex ) {
			if ( cached.imageOwners[ imageIndex ] != images[ imageIndex ] ||
				cached.imageViews[ imageIndex ] != resources[ imageIndex ]->view ) {
				matches = false;
				break;
			}
		}
		if ( matches ) {
			descriptorSet = cached.descriptorSet;
			return true;
		}
	}
	sdVulkanMaterialDescriptor materialDescriptor;
	memset( &materialDescriptor, 0, sizeof( materialDescriptor ) );
	materialDescriptor.owner = &stage;
	VkDescriptorSetAllocateInfo allocateInfo;
	memset( &allocateInfo, 0, sizeof( allocateInfo ) );
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = state.guiDescriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &state.guiDescriptorSetLayout;
	if ( !CheckVulkanResult( state.AllocateDescriptorSets( state.device,
		&allocateInfo, &materialDescriptor.descriptorSet ),
		"vkAllocateDescriptorSets(material)" ) ) {
		return false;
	}
	VkDescriptorImageInfo imageInfos[ VULKAN_MATERIAL_TEXTURES ];
	VkWriteDescriptorSet writes[ VULKAN_MATERIAL_TEXTURES ];
	memset( imageInfos, 0, sizeof( imageInfos ) );
	memset( writes, 0, sizeof( writes ) );
	for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES; ++imageIndex ) {
		materialDescriptor.imageOwners[ imageIndex ] = images[ imageIndex ];
		materialDescriptor.imageViews[ imageIndex ] = resources[ imageIndex ]->view;
		imageInfos[ imageIndex ].sampler = resources[ imageIndex ]->sampler;
		imageInfos[ imageIndex ].imageView = resources[ imageIndex ]->view;
		imageInfos[ imageIndex ].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[ imageIndex ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[ imageIndex ].dstSet = materialDescriptor.descriptorSet;
		writes[ imageIndex ].dstBinding = imageIndex;
		writes[ imageIndex ].descriptorCount = 1;
		writes[ imageIndex ].descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[ imageIndex ].pImageInfo = &imageInfos[ imageIndex ];
	}
	state.UpdateDescriptorSets( state.device, VULKAN_MATERIAL_TEXTURES,
		writes, 0, NULL );
	state.materialDescriptors.Append( materialDescriptor );
	descriptorSet = materialDescriptor.descriptorSet;
	return true;
}

bool GetVulkanWaterDescriptor( sdVulkanBackendState& state,
	const materialStage_t& stage, VkDescriptorSet& descriptorSet ) {
	descriptorSet = VK_NULL_HANDLE;
	if ( globalImages == NULL ) {
		return false;
	}
	idImage* images[ VULKAN_MATERIAL_TEXTURES ] = {
		NULL, NULL, globalImages->whiteImage, globalImages->blackImage,
		globalImages->blackCubeMapImage
	};
	for ( int textureIndex = 0; textureIndex < stage.numTextures; ++textureIndex ) {
		const stageTexture_t& texture = stage.textures[ textureIndex ];
		if ( texture.image == NULL || texture.image->defaulted ||
			texture.renderBinding == NULL ) {
			continue;
		}
		const char* bindingName = texture.renderBinding->GetName();
		if ( images[ 0 ] == NULL && idStr::Icmp( bindingName, "bumpmap" ) == 0 ) {
			images[ 0 ] = texture.image;
		} else if ( idStr::Icmp( bindingName, "environmentcubemap" ) == 0 ) {
			images[ 1 ] = texture.image;
		} else if ( idStr::Icmp( bindingName, "map" ) == 0 ) {
			images[ 2 ] = texture.image;
		}
	}
	if ( images[ 0 ] == NULL || images[ 1 ] == NULL ||
		images[ 1 ]->type != TT_CUBIC ) {
		return false;
	}
	const sdVulkanImageResource* resources[ VULKAN_MATERIAL_TEXTURES ];
	for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES; ++imageIndex ) {
		if ( !images[ imageIndex ]->IsLoaded() ) {
			images[ imageIndex ]->BindFragment();
		}
		resources[ imageIndex ] = FindVulkanImageResource( state,
			images[ imageIndex ] );
		if ( resources[ imageIndex ] == NULL ) {
			return false;
		}
	}
	for ( int descriptorIndex = 0;
		descriptorIndex < state.materialDescriptors.Num(); ++descriptorIndex ) {
		const sdVulkanMaterialDescriptor& cached =
			state.materialDescriptors[ descriptorIndex ];
		if ( cached.owner != &stage ) {
			continue;
		}
		bool matches = true;
		for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES;
			++imageIndex ) {
			if ( cached.imageOwners[ imageIndex ] != images[ imageIndex ] ||
				cached.imageViews[ imageIndex ] != resources[ imageIndex ]->view ) {
				matches = false;
				break;
			}
		}
		if ( matches ) {
			descriptorSet = cached.descriptorSet;
			return true;
		}
	}
	sdVulkanMaterialDescriptor waterDescriptor;
	memset( &waterDescriptor, 0, sizeof( waterDescriptor ) );
	waterDescriptor.owner = &stage;
	VkDescriptorSetAllocateInfo allocateInfo;
	memset( &allocateInfo, 0, sizeof( allocateInfo ) );
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = state.guiDescriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &state.guiDescriptorSetLayout;
	if ( !CheckVulkanResult( state.AllocateDescriptorSets( state.device,
		&allocateInfo, &waterDescriptor.descriptorSet ),
		"vkAllocateDescriptorSets(water)" ) ) {
		return false;
	}
	VkDescriptorImageInfo imageInfos[ VULKAN_MATERIAL_TEXTURES ];
	VkWriteDescriptorSet writes[ VULKAN_MATERIAL_TEXTURES ];
	memset( imageInfos, 0, sizeof( imageInfos ) );
	memset( writes, 0, sizeof( writes ) );
	for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES; ++imageIndex ) {
		waterDescriptor.imageOwners[ imageIndex ] = images[ imageIndex ];
		waterDescriptor.imageViews[ imageIndex ] = resources[ imageIndex ]->view;
		imageInfos[ imageIndex ].sampler = resources[ imageIndex ]->sampler;
		imageInfos[ imageIndex ].imageView = resources[ imageIndex ]->view;
		imageInfos[ imageIndex ].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[ imageIndex ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[ imageIndex ].dstSet = waterDescriptor.descriptorSet;
		writes[ imageIndex ].dstBinding = imageIndex;
		writes[ imageIndex ].descriptorCount = 1;
		writes[ imageIndex ].descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[ imageIndex ].pImageInfo = &imageInfos[ imageIndex ];
	}
	state.UpdateDescriptorSets( state.device, VULKAN_MATERIAL_TEXTURES,
		writes, 0, NULL );
	state.materialDescriptors.Append( waterDescriptor );
	descriptorSet = waterDescriptor.descriptorSet;
	return true;
}

bool GetVulkanHeatHazeDescriptor( sdVulkanBackendState& state,
	const materialStage_t& stage, idImage* bumpImage,
	VkDescriptorSet& descriptorSet ) {
	descriptorSet = VK_NULL_HANDLE;
	if ( globalImages == NULL || bumpImage == NULL ||
		state.currentRenderResource.view == VK_NULL_HANDLE ||
		state.currentRenderResource.sampler == VK_NULL_HANDLE ) {
		return false;
	}
	if ( !bumpImage->IsLoaded() ) {
		bumpImage->BindFragment();
	}
	idImage* fallbackImages[ 3 ] = { globalImages->blackImage,
		globalImages->blackImage, globalImages->blackCubeMapImage };
	const sdVulkanImageResource* bumpResource =
		FindVulkanImageResource( state, bumpImage );
	const sdVulkanImageResource* fallbackResources[ 3 ];
	if ( bumpResource == NULL ) {
		return false;
	}
	for ( int fallbackIndex = 0; fallbackIndex < 3; ++fallbackIndex ) {
		if ( !fallbackImages[ fallbackIndex ]->IsLoaded() ) {
			fallbackImages[ fallbackIndex ]->BindFragment();
		}
		fallbackResources[ fallbackIndex ] = FindVulkanImageResource( state,
			fallbackImages[ fallbackIndex ] );
		if ( fallbackResources[ fallbackIndex ] == NULL ) {
			return false;
		}
	}

	const void* owners[ VULKAN_MATERIAL_TEXTURES ] = {
		bumpImage, &state.currentRenderResource, fallbackImages[ 0 ],
		fallbackImages[ 1 ], fallbackImages[ 2 ]
	};
	VkImageView views[ VULKAN_MATERIAL_TEXTURES ] = {
		bumpResource->view, state.currentRenderResource.view,
		fallbackResources[ 0 ]->view, fallbackResources[ 1 ]->view,
		fallbackResources[ 2 ]->view
	};
	VkSampler samplers[ VULKAN_MATERIAL_TEXTURES ] = {
		bumpResource->sampler, state.currentRenderResource.sampler,
		fallbackResources[ 0 ]->sampler, fallbackResources[ 1 ]->sampler,
		fallbackResources[ 2 ]->sampler
	};
	for ( int descriptorIndex = 0;
		descriptorIndex < state.materialDescriptors.Num(); ++descriptorIndex ) {
		const sdVulkanMaterialDescriptor& cached =
			state.materialDescriptors[ descriptorIndex ];
		if ( cached.owner != &stage ) {
			continue;
		}
		bool matches = true;
		for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES;
			++imageIndex ) {
			if ( cached.imageOwners[ imageIndex ] != owners[ imageIndex ] ||
				cached.imageViews[ imageIndex ] != views[ imageIndex ] ) {
				matches = false;
				break;
			}
		}
		if ( matches ) {
			descriptorSet = cached.descriptorSet;
			return true;
		}
	}

	sdVulkanMaterialDescriptor heatDescriptor;
	memset( &heatDescriptor, 0, sizeof( heatDescriptor ) );
	heatDescriptor.owner = &stage;
	VkDescriptorSetAllocateInfo allocateInfo;
	memset( &allocateInfo, 0, sizeof( allocateInfo ) );
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = state.guiDescriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &state.guiDescriptorSetLayout;
	if ( !CheckVulkanResult( state.AllocateDescriptorSets( state.device,
		&allocateInfo, &heatDescriptor.descriptorSet ),
		"vkAllocateDescriptorSets(heat haze)" ) ) {
		return false;
	}
	VkDescriptorImageInfo imageInfos[ VULKAN_MATERIAL_TEXTURES ];
	VkWriteDescriptorSet writes[ VULKAN_MATERIAL_TEXTURES ];
	memset( imageInfos, 0, sizeof( imageInfos ) );
	memset( writes, 0, sizeof( writes ) );
	for ( int imageIndex = 0; imageIndex < VULKAN_MATERIAL_TEXTURES;
		++imageIndex ) {
		heatDescriptor.imageOwners[ imageIndex ] = owners[ imageIndex ];
		heatDescriptor.imageViews[ imageIndex ] = views[ imageIndex ];
		imageInfos[ imageIndex ].sampler = samplers[ imageIndex ];
		imageInfos[ imageIndex ].imageView = views[ imageIndex ];
		imageInfos[ imageIndex ].imageLayout =
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		writes[ imageIndex ].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[ imageIndex ].dstSet = heatDescriptor.descriptorSet;
		writes[ imageIndex ].dstBinding = imageIndex;
		writes[ imageIndex ].descriptorCount = 1;
		writes[ imageIndex ].descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[ imageIndex ].pImageInfo = &imageInfos[ imageIndex ];
	}
	state.UpdateDescriptorSets( state.device, VULKAN_MATERIAL_TEXTURES,
		writes, 0, NULL );
	state.materialDescriptors.Append( heatDescriptor );
	descriptorSet = heatDescriptor.descriptorSet;
	return true;
}

bool CopyCurrentRender( sdVulkanBackendState& state ) {
	if ( !state.frameActive || !state.swapchainTransferSource ||
		state.currentRenderResource.image == VK_NULL_HANDLE ||
		state.CmdCopyImage == NULL ) {
		return false;
	}
	sdVulkanFrame& frame = state.frames[ state.frameIndex ];
	state.CmdEndRendering( frame.commandBuffer );

	VkImageMemoryBarrier2 toTransfer[ 2 ];
	memset( toTransfer, 0, sizeof( toTransfer ) );
	toTransfer[ 0 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toTransfer[ 0 ].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toTransfer[ 0 ].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toTransfer[ 0 ].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toTransfer[ 0 ].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	toTransfer[ 0 ].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toTransfer[ 0 ].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	toTransfer[ 0 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransfer[ 0 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransfer[ 0 ].image = state.swapchainImages[ state.imageIndex ];
	toTransfer[ 0 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toTransfer[ 0 ].subresourceRange.levelCount = 1;
	toTransfer[ 0 ].subresourceRange.layerCount = 1;
	toTransfer[ 1 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toTransfer[ 1 ].srcStageMask = state.currentRenderInitialized ?
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE;
	toTransfer[ 1 ].srcAccessMask = state.currentRenderInitialized ?
		VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : VK_ACCESS_2_NONE;
	toTransfer[ 1 ].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	toTransfer[ 1 ].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toTransfer[ 1 ].oldLayout = state.currentRenderInitialized ?
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	toTransfer[ 1 ].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toTransfer[ 1 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransfer[ 1 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransfer[ 1 ].image = state.currentRenderResource.image;
	toTransfer[ 1 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toTransfer[ 1 ].subresourceRange.levelCount = 1;
	toTransfer[ 1 ].subresourceRange.layerCount = 1;
	VkDependencyInfo dependencyInfo;
	memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 2;
	dependencyInfo.pImageMemoryBarriers = toTransfer;
	state.CmdPipelineBarrier2( frame.commandBuffer, &dependencyInfo );

	VkImageCopy copyRegion;
	memset( &copyRegion, 0, sizeof( copyRegion ) );
	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.extent.width = state.extent.width;
	copyRegion.extent.height = state.extent.height;
	copyRegion.extent.depth = 1;
	state.CmdCopyImage( frame.commandBuffer,
		state.swapchainImages[ state.imageIndex ], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		state.currentRenderResource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copyRegion );

	VkImageMemoryBarrier2 fromTransfer[ 2 ];
	memset( fromTransfer, 0, sizeof( fromTransfer ) );
	fromTransfer[ 0 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	fromTransfer[ 0 ].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	fromTransfer[ 0 ].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	fromTransfer[ 0 ].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	fromTransfer[ 0 ].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	fromTransfer[ 0 ].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	fromTransfer[ 0 ].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	fromTransfer[ 0 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	fromTransfer[ 0 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	fromTransfer[ 0 ].image = state.swapchainImages[ state.imageIndex ];
	fromTransfer[ 0 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	fromTransfer[ 0 ].subresourceRange.levelCount = 1;
	fromTransfer[ 0 ].subresourceRange.layerCount = 1;
	fromTransfer[ 1 ].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	fromTransfer[ 1 ].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	fromTransfer[ 1 ].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	fromTransfer[ 1 ].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	fromTransfer[ 1 ].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	fromTransfer[ 1 ].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	fromTransfer[ 1 ].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	fromTransfer[ 1 ].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	fromTransfer[ 1 ].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	fromTransfer[ 1 ].image = state.currentRenderResource.image;
	fromTransfer[ 1 ].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	fromTransfer[ 1 ].subresourceRange.levelCount = 1;
	fromTransfer[ 1 ].subresourceRange.layerCount = 1;
	dependencyInfo.pImageMemoryBarriers = fromTransfer;
	state.CmdPipelineBarrier2( frame.commandBuffer, &dependencyInfo );

	VkRenderingAttachmentInfo colorAttachment;
	memset( &colorAttachment, 0, sizeof( colorAttachment ) );
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = state.swapchainViews[ state.imageIndex ];
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingAttachmentInfo depthAttachment;
	memset( &depthAttachment, 0, sizeof( depthAttachment ) );
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = state.depthViews[ state.imageIndex ];
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkRenderingInfo renderingInfo;
	memset( &renderingInfo, 0, sizeof( renderingInfo ) );
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.extent = state.extent;
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;
	state.CmdBeginRendering( frame.commandBuffer, &renderingInfo );
	state.currentRenderInitialized = true;
	return true;
}

} // namespace

sdVulkanBackend vulkanBackend;

sdVulkanBackend::sdVulkanBackend() : state( NULL ) {
}

sdVulkanBackend::~sdVulkanBackend() {
	Shutdown();
}

bool sdVulkanBackend::Init( void* nativeWindow, int width, int height ) {
	Shutdown();
	if ( nativeWindow == NULL ) {
		common->Warning( "Vulkan initialization requires a Win32 window" );
		return false;
	}
	state = new sdVulkanBackendState;
	state->nativeWindow = reinterpret_cast< HWND >( nativeWindow );

	common->Printf( "---------- Vulkan Init ----------\n" );
	if ( !LoadVulkanLibrary( *state ) ||
		!CreateVulkanInstance( *state ) ||
		!CreateVulkanSurface( *state ) ||
		!SelectPhysicalDevice( *state ) ||
		!CreateLogicalDevice( *state ) ||
		!CreateSwapchain( *state, width, height ) ||
		!CreateGuiResources( *state ) ||
		!CreateFrames( *state ) ) {
		Shutdown();
		return false;
	}
	common->Printf( "Vulkan device: %s\n", state->deviceName );
	common->Printf( "---------------------------------\n" );
	return true;
}

void sdVulkanBackend::Shutdown() {
	if ( state == NULL ) {
		return;
	}
	common->Printf( "Vulkan frames presented: %u, world draws: %u, GUI draws: %u\n",
		state->framesPresented, state->worldDrawCalls, state->guiDrawCalls );
	common->Printf( "Vulkan world coverage: %u attempts, %u recorded views, %u surfaces, %u cache-ready, "
		"%u material-ready, %u resource-ready\n", state->worldViewAttempts, state->worldViews,
		state->worldSurfaceCandidates, state->worldCacheReady,
		state->worldMaterialReady, state->worldResourceReady );
	common->Printf( "Vulkan world skips: %u geometry, %u cache, %u material, %u resource; "
		"special draws: %u depth, %u MegaTexture, %u CPU-skinned, %u sky, "
		"%u extra stages, %u water, %u stuff (%u water descriptor misses)\n",
		state->worldMissingGeometry, state->worldMissingCache,
		state->worldMissingMaterial, state->worldMissingResource,
		state->worldDepthDrawCalls,
		state->worldMegaDrawCalls, state->worldSkinnedDrawCalls,
		state->worldSkyDrawCalls, state->worldStageDrawCalls,
		state->worldWaterDrawCalls, state->worldStuffDrawCalls,
		state->worldWaterDescriptorMisses );
	if ( state->device != VK_NULL_HANDLE && state->DeviceWaitIdle != NULL ) {
		state->DeviceWaitIdle( state->device );
	}
	DestroyAllImageResources( *state );
	DestroyAllBufferResources( *state );
	DestroyGuiResources( *state );
	DestroyFrames( *state );
	DestroySwapchain( *state );
	if ( state->device != VK_NULL_HANDLE && state->DestroyDevice != NULL ) {
		state->DestroyDevice( state->device, NULL );
	}
	state->device = VK_NULL_HANDLE;
	if ( state->surface != VK_NULL_HANDLE && state->DestroySurfaceKHR != NULL ) {
		state->DestroySurfaceKHR( state->instance, state->surface, NULL );
	}
	state->surface = VK_NULL_HANDLE;
	if ( state->debugMessenger != VK_NULL_HANDLE &&
		state->DestroyDebugUtilsMessengerEXT != NULL ) {
		state->DestroyDebugUtilsMessengerEXT( state->instance,
			state->debugMessenger, NULL );
	}
	state->debugMessenger = VK_NULL_HANDLE;
	if ( state->instance != VK_NULL_HANDLE && state->DestroyInstance != NULL ) {
		state->DestroyInstance( state->instance, NULL );
	}
	state->instance = VK_NULL_HANDLE;
	if ( state->vulkanLibrary != NULL ) {
		FreeLibrary( state->vulkanLibrary );
		state->vulkanLibrary = NULL;
	}
	delete state;
	state = NULL;
}

bool sdVulkanBackend::BeginFrame( int width, int height ) {
	if ( state == NULL || state->device == VK_NULL_HANDLE || state->frameActive ) {
		return false;
	}
	RECT clientRect;
	if ( GetClientRect( state->nativeWindow, &clientRect ) ) {
		width = clientRect.right - clientRect.left;
		height = clientRect.bottom - clientRect.top;
	}
	if ( width <= 0 || height <= 0 ) {
		return false;
	}
	if ( state->retiredImageResources.Num() != 0 ||
		state->retiredBufferResources.Num() != 0 ) {
		// This conservative synchronization is intentional for the first Vulkan
		// port.  It is only reached when the legacy cache frees resources; a later
		// allocator can retire them against individual frame fences instead.
		if ( !CheckVulkanResult( state->DeviceWaitIdle( state->device ),
			"vkDeviceWaitIdle(resource retirement)" ) ) {
			return false;
		}
		DestroyRetiredResources( *state );
	}
	const int swapInterval = cvarSystem->GetCVarInteger( "r_swapInterval" );
	if ( state->extent.width != static_cast< unsigned int >( width ) ||
		state->extent.height != static_cast< unsigned int >( height ) ||
		state->swapInterval != swapInterval ) {
		state->swapchainDirty = true;
	}
	if ( state->swapchainDirty ) {
		state->DeviceWaitIdle( state->device );
		if ( !CreateSwapchain( *state, width, height ) ) {
			return false;
		}
	}

	sdVulkanFrame& frame = state->frames[ state->frameIndex ];
	if ( !CheckVulkanResult( state->WaitForFences( state->device, 1,
		&frame.fence, VK_TRUE, UINT64_MAX ), "vkWaitForFences" ) ) {
		return false;
	}
	frame.guiVertexOffset = 0;
	VkResult acquireResult = state->AcquireNextImageKHR( state->device,
		state->swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE,
		&state->imageIndex );
	if ( acquireResult == VK_ERROR_OUT_OF_DATE_KHR ) {
		state->DeviceWaitIdle( state->device );
		if ( !CreateSwapchain( *state, width, height ) ) {
			return false;
		}
		acquireResult = state->AcquireNextImageKHR( state->device, state->swapchain,
			UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &state->imageIndex );
	}
	if ( acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR ) {
		CheckVulkanResult( acquireResult, "vkAcquireNextImageKHR" );
		return false;
	}
	if ( acquireResult == VK_SUBOPTIMAL_KHR ) {
		state->swapchainDirty = true;
	}

	if ( !CheckVulkanResult( state->ResetFences( state->device, 1, &frame.fence ),
		"vkResetFences" ) ||
		!CheckVulkanResult( state->ResetCommandPool( state->device,
			frame.commandPool, 0 ), "vkResetCommandPool" ) ) {
		RestoreSignaledFence( *state, frame );
		return false;
	}
	VkCommandBufferBeginInfo beginInfo;
	memset( &beginInfo, 0, sizeof( beginInfo ) );
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if ( !CheckVulkanResult( state->BeginCommandBuffer( frame.commandBuffer,
		&beginInfo ), "vkBeginCommandBuffer" ) ) {
		RestoreSignaledFence( *state, frame );
		return false;
	}

	VkImageMemoryBarrier2 toColor;
	memset( &toColor, 0, sizeof( toColor ) );
	toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toColor.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	toColor.srcAccessMask = VK_ACCESS_2_NONE;
	toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toColor.oldLayout = state->imageInitialized[ state->imageIndex ] ?
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
	toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toColor.image = state->swapchainImages[ state->imageIndex ];
	toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toColor.subresourceRange.levelCount = 1;
	toColor.subresourceRange.layerCount = 1;
	VkImageMemoryBarrier2 depthBarrier;
	memset( &depthBarrier, 0, sizeof( depthBarrier ) );
	depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	depthBarrier.srcStageMask = state->depthInitialized[ state->imageIndex ] ?
		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_2_NONE;
	depthBarrier.srcAccessMask = state->depthInitialized[ state->imageIndex ] ?
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_NONE;
	depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	depthBarrier.oldLayout = state->depthInitialized[ state->imageIndex ] ?
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	depthBarrier.image = state->depthImages[ state->imageIndex ];
	depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthBarrier.subresourceRange.levelCount = 1;
	depthBarrier.subresourceRange.layerCount = 1;
	VkImageMemoryBarrier2 frameBarriers[ 2 ] = { toColor, depthBarrier };
	VkDependencyInfo dependencyInfo;
	memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 2;
	dependencyInfo.pImageMemoryBarriers = frameBarriers;
	state->CmdPipelineBarrier2( frame.commandBuffer, &dependencyInfo );

	VkRenderingAttachmentInfo colorAttachment;
	memset( &colorAttachment, 0, sizeof( colorAttachment ) );
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = state->swapchainViews[ state->imageIndex ];
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color.float32[ 0 ] = 0.04f;
	colorAttachment.clearValue.color.float32[ 1 ] = 0.05f;
	colorAttachment.clearValue.color.float32[ 2 ] = 0.07f;
	colorAttachment.clearValue.color.float32[ 3 ] = 1.0f;
	VkRenderingInfo renderingInfo;
	memset( &renderingInfo, 0, sizeof( renderingInfo ) );
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.extent = state->extent;
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	VkRenderingAttachmentInfo depthAttachment;
	memset( &depthAttachment, 0, sizeof( depthAttachment ) );
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = state->depthViews[ state->imageIndex ];
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil.depth = 1.0f;
	renderingInfo.pDepthAttachment = &depthAttachment;
	state->CmdBeginRendering( frame.commandBuffer, &renderingInfo );

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = static_cast< float >( state->extent.height );
	viewport.width = static_cast< float >( state->extent.width );
	viewport.height = -static_cast< float >( state->extent.height );
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	state->CmdSetViewport( frame.commandBuffer, 0, 1, &viewport );
	VkRect2D scissor;
	memset( &scissor, 0, sizeof( scissor ) );
	scissor.extent = state->extent;
	state->CmdSetScissor( frame.commandBuffer, 0, 1, &scissor );
	state->frameActive = true;
	return true;
}

void sdVulkanBackend::EndFrame( bool ) {
	if ( state == NULL || !state->frameActive ) {
		return;
	}
	sdVulkanFrame& frame = state->frames[ state->frameIndex ];
	state->CmdEndRendering( frame.commandBuffer );

	VkImageMemoryBarrier2 toPresent;
	memset( &toPresent, 0, sizeof( toPresent ) );
	toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
	toPresent.dstAccessMask = VK_ACCESS_2_NONE;
	toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toPresent.image = state->swapchainImages[ state->imageIndex ];
	toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	toPresent.subresourceRange.levelCount = 1;
	toPresent.subresourceRange.layerCount = 1;
	VkDependencyInfo dependencyInfo;
	memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 1;
	dependencyInfo.pImageMemoryBarriers = &toPresent;
	state->CmdPipelineBarrier2( frame.commandBuffer, &dependencyInfo );
	if ( !CheckVulkanResult( state->EndCommandBuffer( frame.commandBuffer ),
		"vkEndCommandBuffer" ) ) {
		RestoreSignaledFence( *state, frame );
		state->frameActive = false;
		return;
	}

	VkSemaphoreSubmitInfo waitInfo;
	memset( &waitInfo, 0, sizeof( waitInfo ) );
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = frame.imageAvailable;
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkCommandBufferSubmitInfo commandInfo;
	memset( &commandInfo, 0, sizeof( commandInfo ) );
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	commandInfo.commandBuffer = frame.commandBuffer;
	VkSemaphoreSubmitInfo signalInfo;
	memset( &signalInfo, 0, sizeof( signalInfo ) );
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.semaphore = frame.renderComplete;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSubmitInfo2 submitInfo;
	memset( &submitInfo, 0, sizeof( submitInfo ) );
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &waitInfo;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &signalInfo;
	if ( !CheckVulkanResult( state->QueueSubmit2( state->graphicsQueue, 1,
		&submitInfo, frame.fence ), "vkQueueSubmit2" ) ) {
		RestoreSignaledFence( *state, frame );
		state->frameActive = false;
		return;
	}
	state->imageInitialized[ state->imageIndex ] = 1;
	state->depthInitialized[ state->imageIndex ] = 1;

	VkPresentInfoKHR presentInfo;
	memset( &presentInfo, 0, sizeof( presentInfo ) );
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &frame.renderComplete;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &state->swapchain;
	presentInfo.pImageIndices = &state->imageIndex;
	const VkResult presentResult = state->QueuePresentKHR( state->graphicsQueue,
		&presentInfo );
	if ( presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ) {
		state->swapchainDirty = true;
	} else {
		if ( CheckVulkanResult( presentResult, "vkQueuePresentKHR" ) ) {
			state->framesPresented++;
		}
	}
	state->frameActive = false;
	state->frameIndex = ( state->frameIndex + 1 ) % NUM_VULKAN_FRAMES;
}

void sdVulkanBackend::WaitIdle() {
	if ( state != NULL && state->device != VK_NULL_HANDLE ) {
		state->DeviceWaitIdle( state->device );
	}
}

bool sdVulkanBackend::UploadImage2D( const void* owner,
	const unsigned char* rgba, int width, int height, int mipLevels,
	bool linearFilter, bool repeat ) {
	if ( state == NULL || state->device == VK_NULL_HANDLE || owner == NULL ||
		rgba == NULL || width <= 0 || height <= 0 ) {
		return false;
	}
	int maximumMipLevels = 1;
	for ( int size = Max( width, height ); size > 1; size >>= 1 ) {
		maximumMipLevels++;
	}
	mipLevels = idMath::ClampInt( 1, maximumMipLevels, mipLevels );
	DestroyImage( owner );

	sdVulkanImageResource resource;
	memset( &resource, 0, sizeof( resource ) );
	resource.owner = owner;
	resource.width = width;
	resource.height = height;
	resource.mipLevels = mipLevels;

	VkImageCreateInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent.width = static_cast< unsigned int >( width );
	imageInfo.extent.height = static_cast< unsigned int >( height );
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = static_cast< unsigned int >( mipLevels );
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	if ( mipLevels > 1 ) {
		imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( !CheckVulkanResult( state->CreateImage( state->device, &imageInfo,
		NULL, &resource.image ), "vkCreateImage" ) ) {
		return false;
	}
	VkMemoryRequirements imageRequirements;
	state->GetImageMemoryRequirements( state->device, resource.image,
		&imageRequirements );
	unsigned int imageMemoryType = UINT_MAX;
	if ( !FindMemoryType( *state, imageRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageMemoryType ) ) {
		common->Warning( "No device-local Vulkan image memory type is available" );
		DestroyImageResource( *state, resource );
		return false;
	}
	VkMemoryAllocateInfo imageAllocateInfo;
	memset( &imageAllocateInfo, 0, sizeof( imageAllocateInfo ) );
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageRequirements.size;
	imageAllocateInfo.memoryTypeIndex = imageMemoryType;
	if ( !CheckVulkanResult( state->AllocateMemory( state->device,
		&imageAllocateInfo, NULL, &resource.memory ), "vkAllocateMemory(image)" ) ||
		!CheckVulkanResult( state->BindImageMemory( state->device, resource.image,
			resource.memory, 0 ), "vkBindImageMemory" ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}

	VkImageViewCreateInfo viewInfo;
	memset( &viewInfo, 0, sizeof( viewInfo ) );
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = resource.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = imageInfo.format;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = static_cast< unsigned int >( mipLevels );
	viewInfo.subresourceRange.layerCount = 1;
	if ( !CheckVulkanResult( state->CreateImageView( state->device, &viewInfo,
		NULL, &resource.view ), "vkCreateImageView(image)" ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}

	VkSamplerCreateInfo samplerInfo;
	memset( &samplerInfo, 0, sizeof( samplerInfo ) );
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = linearFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samplerInfo.minFilter = linearFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = linearFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR :
		VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT :
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = samplerInfo.addressModeU;
	samplerInfo.addressModeW = samplerInfo.addressModeU;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = static_cast< float >( mipLevels - 1 );
	samplerInfo.maxAnisotropy = 1.0f;
	if ( !CheckVulkanResult( state->CreateSampler( state->device, &samplerInfo,
		NULL, &resource.sampler ), "vkCreateSampler" ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	if ( !AllocateImageDescriptor( *state, resource ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}

	const VkDeviceSize uploadBytes = static_cast< VkDeviceSize >( width ) *
		static_cast< VkDeviceSize >( height ) * 4;
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	if ( !CreateBufferAllocation( *state, uploadBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingMemory ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	void* mapped = NULL;
	if ( !CheckVulkanResult( state->MapMemory( state->device, stagingMemory, 0,
		uploadBytes, 0, &mapped ), "vkMapMemory(image upload)" ) ) {
		state->DestroyBuffer( state->device, stagingBuffer, NULL );
		state->FreeMemory( state->device, stagingMemory, NULL );
		DestroyImageResource( *state, resource );
		return false;
	}
	memcpy( mapped, rgba, static_cast< size_t >( uploadBytes ) );
	state->UnmapMemory( state->device, stagingMemory );

	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	poolInfo.queueFamilyIndex = state->queueFamilyIndex;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	bool uploadSucceeded = CheckVulkanResult( state->CreateCommandPool(
		state->device, &poolInfo, NULL, &commandPool ),
		"vkCreateCommandPool(upload)" );
	if ( uploadSucceeded ) {
		VkCommandBufferAllocateInfo commandAllocateInfo;
		memset( &commandAllocateInfo, 0, sizeof( commandAllocateInfo ) );
		commandAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandAllocateInfo.commandPool = commandPool;
		commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandAllocateInfo.commandBufferCount = 1;
		uploadSucceeded = CheckVulkanResult( state->AllocateCommandBuffers(
			state->device, &commandAllocateInfo, &commandBuffer ),
			"vkAllocateCommandBuffers(upload)" );
	}
	if ( uploadSucceeded ) {
		VkCommandBufferBeginInfo beginInfo;
		memset( &beginInfo, 0, sizeof( beginInfo ) );
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		uploadSucceeded = CheckVulkanResult( state->BeginCommandBuffer(
			commandBuffer, &beginInfo ), "vkBeginCommandBuffer(upload)" );
	}
	if ( uploadSucceeded ) {
		VkImageMemoryBarrier2 toTransfer;
		memset( &toTransfer, 0, sizeof( toTransfer ) );
		toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.image = resource.image;
		toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toTransfer.subresourceRange.levelCount = static_cast< unsigned int >( mipLevels );
		toTransfer.subresourceRange.layerCount = 1;
		VkDependencyInfo dependencyInfo;
		memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &toTransfer;
		state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );

		VkBufferImageCopy copyRegion;
		memset( &copyRegion, 0, sizeof( copyRegion ) );
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent.width = static_cast< unsigned int >( width );
		copyRegion.imageExtent.height = static_cast< unsigned int >( height );
		copyRegion.imageExtent.depth = 1;
		state->CmdCopyBufferToImage( commandBuffer, stagingBuffer,
			resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

		int mipWidth = width;
		int mipHeight = height;
		for ( int level = 1; level < mipLevels; ++level ) {
			VkImageMemoryBarrier2 toSource;
			memset( &toSource, 0, sizeof( toSource ) );
			toSource.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			toSource.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			toSource.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			toSource.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			toSource.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			toSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			toSource.image = resource.image;
			toSource.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			toSource.subresourceRange.baseMipLevel = level - 1;
			toSource.subresourceRange.levelCount = 1;
			toSource.subresourceRange.layerCount = 1;
			dependencyInfo.pImageMemoryBarriers = &toSource;
			state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );

			const int nextWidth = Max( 1, mipWidth >> 1 );
			const int nextHeight = Max( 1, mipHeight >> 1 );
			VkImageBlit blit;
			memset( &blit, 0, sizeof( blit ) );
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = level - 1;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[ 1 ].x = mipWidth;
			blit.srcOffsets[ 1 ].y = mipHeight;
			blit.srcOffsets[ 1 ].z = 1;
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = level;
			blit.dstSubresource.layerCount = 1;
			blit.dstOffsets[ 1 ].x = nextWidth;
			blit.dstOffsets[ 1 ].y = nextHeight;
			blit.dstOffsets[ 1 ].z = 1;
			state->CmdBlitImage( commandBuffer, resource.image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resource.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR );
			mipWidth = nextWidth;
			mipHeight = nextHeight;
		}

		VkImageMemoryBarrier2 shaderBarriers[ 2 ];
		memset( shaderBarriers, 0, sizeof( shaderBarriers ) );
		unsigned int shaderBarrierCount = 0;
		if ( mipLevels > 1 ) {
			VkImageMemoryBarrier2& fromSource = shaderBarriers[ shaderBarrierCount++ ];
			fromSource.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			fromSource.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			fromSource.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			fromSource.dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
			fromSource.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			fromSource.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			fromSource.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			fromSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			fromSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			fromSource.image = resource.image;
			fromSource.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			fromSource.subresourceRange.levelCount = mipLevels - 1;
			fromSource.subresourceRange.layerCount = 1;
		}
		VkImageMemoryBarrier2& fromDestination = shaderBarriers[ shaderBarrierCount++ ];
		fromDestination.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		fromDestination.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		fromDestination.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		fromDestination.dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		fromDestination.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		fromDestination.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		fromDestination.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		fromDestination.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		fromDestination.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		fromDestination.image = resource.image;
		fromDestination.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		fromDestination.subresourceRange.baseMipLevel = mipLevels - 1;
		fromDestination.subresourceRange.levelCount = 1;
		fromDestination.subresourceRange.layerCount = 1;
		dependencyInfo.imageMemoryBarrierCount = shaderBarrierCount;
		dependencyInfo.pImageMemoryBarriers = shaderBarriers;
		state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );
		uploadSucceeded = SubmitImmediate( *state, commandPool, commandBuffer );
		commandPool = VK_NULL_HANDLE;
	}
	if ( commandPool != VK_NULL_HANDLE ) {
		state->DestroyCommandPool( state->device, commandPool, NULL );
	}
	state->DestroyBuffer( state->device, stagingBuffer, NULL );
	state->FreeMemory( state->device, stagingMemory, NULL );
	if ( !uploadSucceeded ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	state->imageResources.Append( resource );
	return true;
}

bool sdVulkanBackend::UploadImageCube( const void* owner,
	const unsigned char* const rgba[ 6 ], int size, bool linearFilter ) {
	if ( state == NULL || state->device == VK_NULL_HANDLE || owner == NULL ||
		rgba == NULL || size <= 0 ) {
		return false;
	}
	for ( int face = 0; face < 6; ++face ) {
		if ( rgba[ face ] == NULL ) {
			return false;
		}
	}
	DestroyImage( owner );
	sdVulkanImageResource resource;
	memset( &resource, 0, sizeof( resource ) );
	resource.owner = owner;
	resource.width = size;
	resource.height = size;
	resource.mipLevels = 1;
	VkImageCreateInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent.width = size;
	imageInfo.extent.height = size;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 6;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( !CheckVulkanResult( state->CreateImage( state->device, &imageInfo,
		NULL, &resource.image ), "vkCreateImage(cube)" ) ) {
		return false;
	}
	VkMemoryRequirements imageRequirements;
	state->GetImageMemoryRequirements( state->device, resource.image,
		&imageRequirements );
	unsigned int memoryType = UINT_MAX;
	if ( !FindMemoryType( *state, imageRequirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryType ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	VkMemoryAllocateInfo memoryInfo;
	memset( &memoryInfo, 0, sizeof( memoryInfo ) );
	memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryInfo.allocationSize = imageRequirements.size;
	memoryInfo.memoryTypeIndex = memoryType;
	if ( !CheckVulkanResult( state->AllocateMemory( state->device, &memoryInfo,
		NULL, &resource.memory ), "vkAllocateMemory(cube)" ) ||
		!CheckVulkanResult( state->BindImageMemory( state->device, resource.image,
			resource.memory, 0 ), "vkBindImageMemory(cube)" ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	VkImageViewCreateInfo viewInfo;
	memset( &viewInfo, 0, sizeof( viewInfo ) );
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = resource.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 6;
	if ( !CheckVulkanResult( state->CreateImageView( state->device, &viewInfo,
		NULL, &resource.view ), "vkCreateImageView(cube)" ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	VkSamplerCreateInfo samplerInfo;
	memset( &samplerInfo, 0, sizeof( samplerInfo ) );
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = linearFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	samplerInfo.minFilter = samplerInfo.magFilter;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 0.0f;
	if ( !CheckVulkanResult( state->CreateSampler( state->device, &samplerInfo,
		NULL, &resource.sampler ), "vkCreateSampler(cube)" ) ||
		!AllocateImageDescriptor( *state, resource ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	const VkDeviceSize faceBytes = static_cast< VkDeviceSize >( size ) *
		static_cast< VkDeviceSize >( size ) * 4;
	const VkDeviceSize uploadBytes = faceBytes * 6;
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	if ( !CreateBufferAllocation( *state, uploadBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingMemory ) ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	void* mapped = NULL;
	if ( !CheckVulkanResult( state->MapMemory( state->device, stagingMemory, 0,
		uploadBytes, 0, &mapped ), "vkMapMemory(cube upload)" ) ) {
		state->DestroyBuffer( state->device, stagingBuffer, NULL );
		state->FreeMemory( state->device, stagingMemory, NULL );
		DestroyImageResource( *state, resource );
		return false;
	}
	for ( int face = 0; face < 6; ++face ) {
		memcpy( static_cast< byte* >( mapped ) + faceBytes * face,
			rgba[ face ], static_cast< size_t >( faceBytes ) );
	}
	state->UnmapMemory( state->device, stagingMemory );
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	poolInfo.queueFamilyIndex = state->queueFamilyIndex;
	bool succeeded = CheckVulkanResult( state->CreateCommandPool( state->device,
		&poolInfo, NULL, &commandPool ), "vkCreateCommandPool(cube upload)" );
	if ( succeeded ) {
		VkCommandBufferAllocateInfo allocateInfo;
		memset( &allocateInfo, 0, sizeof( allocateInfo ) );
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		succeeded = CheckVulkanResult( state->AllocateCommandBuffers( state->device,
			&allocateInfo, &commandBuffer ), "vkAllocateCommandBuffers(cube upload)" );
	}
	if ( succeeded ) {
		VkCommandBufferBeginInfo beginInfo;
		memset( &beginInfo, 0, sizeof( beginInfo ) );
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		succeeded = CheckVulkanResult( state->BeginCommandBuffer( commandBuffer,
			&beginInfo ), "vkBeginCommandBuffer(cube upload)" );
	}
	if ( succeeded ) {
		VkImageMemoryBarrier2 barrier;
		memset( &barrier, 0, sizeof( barrier ) );
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = resource.image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 6;
		VkDependencyInfo dependencyInfo;
		memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );
		VkBufferImageCopy copyRegions[ 6 ];
		memset( copyRegions, 0, sizeof( copyRegions ) );
		for ( int face = 0; face < 6; ++face ) {
			copyRegions[ face ].bufferOffset = faceBytes * face;
			copyRegions[ face ].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegions[ face ].imageSubresource.baseArrayLayer = face;
			copyRegions[ face ].imageSubresource.layerCount = 1;
			copyRegions[ face ].imageExtent.width = size;
			copyRegions[ face ].imageExtent.height = size;
			copyRegions[ face ].imageExtent.depth = 1;
		}
		state->CmdCopyBufferToImage( commandBuffer, stagingBuffer, resource.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, copyRegions );
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );
		succeeded = SubmitImmediate( *state, commandPool, commandBuffer );
		commandPool = VK_NULL_HANDLE;
	}
	if ( commandPool != VK_NULL_HANDLE ) {
		state->DestroyCommandPool( state->device, commandPool, NULL );
	}
	state->DestroyBuffer( state->device, stagingBuffer, NULL );
	state->FreeMemory( state->device, stagingMemory, NULL );
	if ( !succeeded ) {
		DestroyImageResource( *state, resource );
		return false;
	}
	state->imageResources.Append( resource );
	return true;
}

bool sdVulkanBackend::UpdateImage2D( const void* owner, int mipLevel,
	int x, int y, int width, int height, const unsigned char* rgba ) {
	if ( state == NULL || owner == NULL || rgba == NULL || mipLevel < 0 ||
		x < 0 || y < 0 || width <= 0 || height <= 0 ) {
		return false;
	}
	sdVulkanImageResource* resource = NULL;
	for ( int i = 0; i < state->imageResources.Num(); ++i ) {
		if ( state->imageResources[ i ].owner == owner ) {
			resource = &state->imageResources[ i ];
			break;
		}
	}
	if ( resource == NULL || mipLevel >= resource->mipLevels ) {
		return false;
	}
	const int mipWidth = Max( 1, resource->width >> mipLevel );
	const int mipHeight = Max( 1, resource->height >> mipLevel );
	if ( x + width > mipWidth || y + height > mipHeight ) {
		return false;
	}
	const VkDeviceSize uploadBytes = static_cast< VkDeviceSize >( width ) *
		static_cast< VkDeviceSize >( height ) * 4;
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	if ( !CreateBufferAllocation( *state, uploadBytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		stagingBuffer, stagingMemory ) ) {
		return false;
	}
	void* mapped = NULL;
	bool succeeded = CheckVulkanResult( state->MapMemory( state->device,
		stagingMemory, 0, uploadBytes, 0, &mapped ),
		"vkMapMemory(image update)" );
	if ( succeeded ) {
		memcpy( mapped, rgba, static_cast< size_t >( uploadBytes ) );
		state->UnmapMemory( state->device, stagingMemory );
	}
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if ( succeeded ) {
		VkCommandPoolCreateInfo poolInfo;
		memset( &poolInfo, 0, sizeof( poolInfo ) );
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = state->queueFamilyIndex;
		succeeded = CheckVulkanResult( state->CreateCommandPool( state->device,
			&poolInfo, NULL, &commandPool ), "vkCreateCommandPool(image update)" );
	}
	if ( succeeded ) {
		VkCommandBufferAllocateInfo allocateInfo;
		memset( &allocateInfo, 0, sizeof( allocateInfo ) );
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		succeeded = CheckVulkanResult( state->AllocateCommandBuffers(
			state->device, &allocateInfo, &commandBuffer ),
			"vkAllocateCommandBuffers(image update)" );
	}
	if ( succeeded ) {
		VkCommandBufferBeginInfo beginInfo;
		memset( &beginInfo, 0, sizeof( beginInfo ) );
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		succeeded = CheckVulkanResult( state->BeginCommandBuffer( commandBuffer,
			&beginInfo ), "vkBeginCommandBuffer(image update)" );
	}
	if ( succeeded ) {
		VkImageMemoryBarrier2 toTransfer;
		memset( &toTransfer, 0, sizeof( toTransfer ) );
		toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		toTransfer.srcStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		toTransfer.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		toTransfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		toTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.image = resource->image;
		toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toTransfer.subresourceRange.baseMipLevel = mipLevel;
		toTransfer.subresourceRange.levelCount = 1;
		toTransfer.subresourceRange.layerCount = 1;
		VkDependencyInfo dependencyInfo;
		memset( &dependencyInfo, 0, sizeof( dependencyInfo ) );
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &toTransfer;
		state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );
		VkBufferImageCopy copy;
		memset( &copy, 0, sizeof( copy ) );
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = mipLevel;
		copy.imageSubresource.layerCount = 1;
		copy.imageOffset.x = x;
		copy.imageOffset.y = y;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = 1;
		state->CmdCopyBufferToImage( commandBuffer, stagingBuffer,
			resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
		VkImageMemoryBarrier2 toShader = toTransfer;
		toShader.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		toShader.dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		toShader.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		dependencyInfo.pImageMemoryBarriers = &toShader;
		state->CmdPipelineBarrier2( commandBuffer, &dependencyInfo );
		succeeded = SubmitImmediate( *state, commandPool, commandBuffer );
		commandPool = VK_NULL_HANDLE;
	}
	if ( commandPool != VK_NULL_HANDLE ) {
		state->DestroyCommandPool( state->device, commandPool, NULL );
	}
	state->DestroyBuffer( state->device, stagingBuffer, NULL );
	state->FreeMemory( state->device, stagingMemory, NULL );
	return succeeded;
}

void sdVulkanBackend::DestroyImage( const void* owner ) {
	if ( state == NULL || state->device == VK_NULL_HANDLE || owner == NULL ) {
		return;
	}
	for ( int i = 0; i < state->imageResources.Num(); ++i ) {
		if ( state->imageResources[ i ].owner == owner ) {
			sdVulkanImageResource removed = state->imageResources[ i ];
			state->imageResources.RemoveIndexFast( i );
			if ( state->frameActive ) {
				state->retiredImageResources.Append( removed );
			} else {
				state->DeviceWaitIdle( state->device );
				DestroyImageResource( *state, removed );
				DestroyRetiredResources( *state );
			}
			return;
		}
	}
}

bool sdVulkanBackend::UploadBuffer( const void* owner, const void* data,
	int bytes, bool indexBuffer ) {
	if ( state == NULL || state->device == VK_NULL_HANDLE || owner == NULL ||
		data == NULL || bytes <= 0 ) {
		return false;
	}
	DestroyBuffer( owner );
	sdVulkanBufferResource resource;
	memset( &resource, 0, sizeof( resource ) );
	resource.owner = owner;
	resource.bytes = static_cast< VkDeviceSize >( bytes );
	resource.indexBuffer = indexBuffer;
	// Frame-temporary allocations share one backing buffer for vertices and
	// indexes.  Static allocations use the same neutral capability so a cache
	// block can be rebound by the reconstructed interaction/deform paths without
	// creating a Vulkan buffer whose usage disagrees with the eventual bind.
	const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if ( !CreateBufferAllocation( *state, resource.bytes, usage,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, resource.buffer, resource.memory ) ||
		!UploadBufferBytes( *state, resource.buffer, 0, data, resource.bytes,
			indexBuffer ) ) {
		DestroyBufferResource( *state, resource );
		return false;
	}
	state->bufferResources.Append( resource );
	return true;
}

bool sdVulkanBackend::UpdateBuffer( const void* owner, int offset,
	const void* data, int bytes ) {
	if ( state == NULL || owner == NULL || data == NULL || offset < 0 ||
		bytes <= 0 ) {
		return false;
	}
	for ( int i = 0; i < state->bufferResources.Num(); ++i ) {
		sdVulkanBufferResource& resource = state->bufferResources[ i ];
		if ( resource.owner != owner ) {
			continue;
		}
		if ( static_cast< VkDeviceSize >( offset ) + bytes > resource.bytes ) {
			common->Warning( "Vulkan buffer update exceeds its allocation" );
			return false;
		}
		return UploadBufferBytes( *state, resource.buffer, offset, data, bytes,
			resource.indexBuffer );
	}
	return false;
}

void sdVulkanBackend::DestroyBuffer( const void* owner ) {
	if ( state == NULL || state->device == VK_NULL_HANDLE || owner == NULL ) {
		return;
	}
	for ( int i = 0; i < state->bufferResources.Num(); ++i ) {
		if ( state->bufferResources[ i ].owner == owner ) {
			sdVulkanBufferResource removed = state->bufferResources[ i ];
			state->bufferResources.RemoveIndexFast( i );
			if ( state->frameActive ) {
				state->retiredBufferResources.Append( removed );
			} else {
				state->DeviceWaitIdle( state->device );
				DestroyBufferResource( *state, removed );
				DestroyRetiredResources( *state );
			}
			return;
		}
	}
}

void sdVulkanBackend::DrawView( const viewDef_s* view ) {
	if ( state == NULL || state->worldPipeline == VK_NULL_HANDLE || view == NULL ) {
		return;
	}
	state->worldViewAttempts++;
	// ETQW can submit the 3D render world before idRenderSystem::BeginFrame;
	// OpenGL's immediate path tolerated that ordering.  Start the Vulkan command
	// buffer on first scene use so the later BeginFrame call simply observes an
	// already-active frame and EndFrame still owns submission/presentation.
	if ( !state->frameActive && !BeginFrame( 0, 0 ) ) {
		return;
	}
	state->worldViews++;
	sdVulkanFrame& frame = state->frames[ state->frameIndex ];
	const int viewportWidth = Max( 1, view->viewport.x2 - view->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, view->viewport.y2 - view->viewport.y1 + 1 );
	VkViewport viewport;
	viewport.x = static_cast< float >( view->viewport.x1 );
	viewport.y = static_cast< float >( state->extent.height - view->viewport.y1 );
	viewport.width = static_cast< float >( viewportWidth );
	viewport.height = -static_cast< float >( viewportHeight );
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	state->CmdSetViewport( frame.commandBuffer, 0, 1, &viewport );

	// The OpenGL back end built the atmosphere from a dedicated material pass.
	// Establish the authored sky gradient first so pixels not covered by world
	// geometry no longer expose the swapchain clear color.  This intentionally
	// precedes depth-tested surfaces; clouds and the sun remain later passes.
	if ( view->atmosphere != NULL && state->skyPipeline != VK_NULL_HANDLE ) {
		idImage* skyImage = view->atmosphere->GetSkyGradientImage();
		if ( skyImage != NULL && !skyImage->IsLoaded() ) {
			skyImage->BindFragment();
		}
		const sdVulkanImageResource* skyResource = NULL;
		if ( skyImage != NULL && skyImage->IsLoaded() && !skyImage->defaulted ) {
			for ( int resourceIndex = 0; resourceIndex < state->imageResources.Num();
				++resourceIndex ) {
				if ( state->imageResources[ resourceIndex ].owner == skyImage ) {
					skyResource = &state->imageResources[ resourceIndex ];
					break;
				}
			}
		}
		if ( skyResource != NULL && skyResource->descriptorSet != VK_NULL_HANDLE ) {
			VkRect2D skyScissor;
			skyScissor.offset.x = idMath::ClampInt( 0,
				static_cast< int >( state->extent.width ), view->viewport.x1 );
			skyScissor.offset.y = idMath::ClampInt( 0,
				static_cast< int >( state->extent.height ),
				static_cast< int >( state->extent.height ) -
				( view->viewport.y1 + viewportHeight ) );
			skyScissor.extent.width = static_cast< unsigned int >( Min( viewportWidth,
				static_cast< int >( state->extent.width ) - skyScissor.offset.x ) );
			skyScissor.extent.height = static_cast< unsigned int >( Min( viewportHeight,
				static_cast< int >( state->extent.height ) - skyScissor.offset.y ) );
			if ( skyScissor.extent.width > 0 && skyScissor.extent.height > 0 ) {
				float skyPushConstants[ 32 ];
				memset( skyPushConstants, 0, sizeof( skyPushConstants ) );
				const idVec3& fogColor = view->atmosphere->GetFogColor();
				skyPushConstants[ 16 ] = fogColor.x;
				skyPushConstants[ 17 ] = fogColor.y;
				skyPushConstants[ 18 ] = fogColor.z;
				skyPushConstants[ 19 ] = 1.0f;
				state->CmdSetScissor( frame.commandBuffer, 0, 1, &skyScissor );
				state->CmdBindPipeline( frame.commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS, state->skyPipeline );
				state->CmdBindDescriptorSets( frame.commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS, state->guiPipelineLayout, 0, 1,
					&skyResource->descriptorSet, 0, NULL );
				state->CmdPushConstants( frame.commandBuffer, state->guiPipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					sizeof( skyPushConstants ), skyPushConstants );
				state->CmdDraw( frame.commandBuffer, 3, 1, 0, 0 );
				state->worldSkyDrawCalls++;
			}
		}
	}

	if ( view->drawSurfs == NULL || view->numDrawSurfs <= 0 ) {
		return;
	}
	state->worldSurfaceCandidates += view->numDrawSurfs;
	bool currentRenderCopied = false;

	for ( int surfaceIndex = 0; surfaceIndex < view->numDrawSurfs; ++surfaceIndex ) {
		const drawSurf_s* surface = view->drawSurfs[ surfaceIndex ];
		if ( surface == NULL || surface->geo == NULL || surface->space == NULL ||
			surface->space->culled || surface->material == NULL ||
			surface->materialRegisters == NULL || surface->geo->numIndexes <= 0 ||
			surface->geo->mode == PM_POINTSPRITE ) {
			state->worldMissingGeometry++;
			continue;
		}
		if ( surface->geo->ambientCache == NULL || surface->geo->indexCache == NULL ) {
			state->worldMissingCache++;
			continue;
		}
		state->worldCacheReady++;
		if ( r_vkDebugMaterials.GetBool() ) {
			ReportVulkanDrawMaterial( *state, *surface );
		}

		const materialStage_t* selectedStage = NULL;
		idImage* selectedImage = NULL;
		idMegaTexture* selectedMegaTexture = NULL;
		int selectedImageScore = -0x7fffffff;
		for ( int stageIndex = 0; stageIndex < surface->material->GetNumStages();
			++stageIndex ) {
			const materialStage_t* stage = surface->material->GetStage( stageIndex );
			if ( surface->materialRegisters[ stage->conditionRegister ] == 0.0f ) {
				continue;
			}
			if ( ( stage->drawStateBits & 0xFF ) == 0x21 ) {
				continue;
			}
			if ( stage->megaTexture != NULL ) {
				stage->megaTexture->UpdateMapping( view->renderWorld );
				stage->megaTexture->SetMappingForSurface( surface->geo );
				stage->megaTexture->UpdateForViewOrigin(
					view->renderView.vieworg, view->renderView.time );
				// Shader level zero is the coarsest, always-resident atlas.  It is a
				// correct low-quality fallback while the six-level Vulkan blend
				// pipeline is brought online, and crucially does not drop terrain.
				idMegaTextureLevel* level = stage->megaTexture->GetLevel(
					stage->megaTexture->GetNumLevels() - 1 );
				if ( level != NULL && level->GetImage() != NULL ) {
					selectedStage = stage;
					selectedImage = level->GetImage();
					selectedMegaTexture = stage->megaTexture;
					selectedImageScore = 10000;
				}
				continue;
			}
			for ( int textureIndex = 0; textureIndex < stage->numTextures;
				++textureIndex ) {
				const stageTexture_t& texture = stage->textures[ textureIndex ];
				if ( texture.image == NULL || texture.image->defaulted ) {
					continue;
				}
				int score = 10;
				if ( rbinds != NULL && texture.renderBinding == rbinds->diffuseMap ) {
					score = 100;
				} else if ( rbinds != NULL && texture.renderBinding == rbinds->map ) {
					score = 90;
				} else if ( rbinds != NULL && texture.renderBinding == rbinds->cinematicY ) {
					score = 80;
				}
				// Constant helper maps are common as the first material stage.  Do
				// not let one hide a later authored diffuse texture (the menu planet
				// was reduced to precisely this white helper image).
				if ( globalImages != NULL && ( texture.image == globalImages->whiteImage ||
					 texture.image == globalImages->blackImage ||
					 texture.image == globalImages->grayImage ) ) {
					score -= 50;
				}
				if ( score > selectedImageScore ) {
					selectedStage = stage;
					selectedImage = texture.image;
					selectedMegaTexture = NULL;
					selectedImageScore = score;
				}
			}
		}
		if ( selectedStage == NULL || selectedImage == NULL ) {
			ReportMissingVulkanMaterial( *state, *surface );
			state->worldMissingMaterial++;
			continue;
		}
		if ( !selectedImage->IsLoaded() ) {
			selectedImage->BindFragment();
		}
		if ( !selectedImage->IsLoaded() || selectedImage->defaulted ) {
			ReportMissingVulkanMaterial( *state, *surface );
			state->worldMissingMaterial++;
			continue;
		}
		state->worldMaterialReady++;
		const bool selectedAtmosphereStage = selectedStage->renderProgram != NULL &&
			idStr::Icmp( selectedStage->renderProgram->GetName(), "sfx/atmos" ) == 0;
		const bool selectedWaterStage = selectedStage->renderProgram != NULL &&
			idStr::Icmpn( selectedStage->renderProgram->GetName(), "water/", 6 ) == 0;
		const bool selectedHeatHazeStage = selectedStage->renderProgram != NULL &&
			idStr::Icmpn( selectedStage->renderProgram->GetName(), "heatHaze", 8 ) == 0;
		const bool selectedStuffGrassStage = selectedStage->renderProgram != NULL &&
			idStr::Icmpn( selectedStage->renderProgram->GetName(), "stuff/grass", 11 ) == 0;

		const vertCache_t* vertexCacheBlock = surface->geo->ambientCache;
		const vertCache_t* indexCacheBlock = surface->geo->indexCache;
		const void* vertexOwner = vertexCacheBlock->backendBuffer != NULL ?
			vertexCacheBlock->backendBuffer : vertexCacheBlock;
		const void* indexOwner = indexCacheBlock->backendBuffer != NULL ?
			indexCacheBlock->backendBuffer : indexCacheBlock;
		const sdVulkanBufferResource* vertexResource = NULL;
		const sdVulkanBufferResource* indexResource = NULL;
		for ( int resourceIndex = 0; resourceIndex < state->bufferResources.Num();
			++resourceIndex ) {
			const sdVulkanBufferResource& resource =
				state->bufferResources[ resourceIndex ];
			if ( resource.owner == vertexOwner ) {
				vertexResource = &resource;
			}
			if ( resource.owner == indexOwner ) {
				indexResource = &resource;
			}
		}
		const sdVulkanImageResource* imageResource = NULL;
		for ( int resourceIndex = 0; resourceIndex < state->imageResources.Num();
			++resourceIndex ) {
			if ( state->imageResources[ resourceIndex ].owner == selectedImage ) {
				imageResource = &state->imageResources[ resourceIndex ];
				break;
			}
		}
		if ( vertexResource == NULL || indexResource == NULL ||
			imageResource == NULL || imageResource->descriptorSet == VK_NULL_HANDLE ) {
			state->worldMissingResource++;
			continue;
		}
		state->worldResourceReady++;
		VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE;
		const bool materialTexturesAvailable = selectedMegaTexture == NULL &&
			!selectedAtmosphereStage && !selectedWaterStage &&
			!selectedHeatHazeStage &&
			state->worldMaterialPipeline != VK_NULL_HANDLE &&
			StageUsesVulkanMaterialTextures( *selectedStage ) &&
			GetVulkanMaterialDescriptor( *state, *selectedStage, selectedImage,
				surface->space,
				materialDescriptorSet );
		VkDescriptorSet waterDescriptorSet = VK_NULL_HANDLE;
		const bool waterTexturesAvailable = selectedWaterStage &&
			state->worldWaterPipeline != VK_NULL_HANDLE &&
			GetVulkanWaterDescriptor( *state, *selectedStage, waterDescriptorSet );
		if ( selectedWaterStage && !waterTexturesAvailable ) {
			state->worldWaterDescriptorMisses++;
		}
		VkDescriptorSet heatHazeDescriptorSet = VK_NULL_HANDLE;
		const bool heatHazeTexturesAvailable = selectedHeatHazeStage &&
			state->worldHeatHazePipeline != VK_NULL_HANDLE &&
			GetVulkanHeatHazeDescriptor( *state, *selectedStage, selectedImage,
				heatHazeDescriptorSet );

		VkRect2D scissor;
		int scissorWidth = Max( 1,
			surface->scissorRect.x2 - surface->scissorRect.x1 + 1 );
		int scissorHeight = Max( 1,
			surface->scissorRect.y2 - surface->scissorRect.y1 + 1 );
		scissor.offset.x = view->viewport.x1 + surface->scissorRect.x1;
		scissor.offset.y = static_cast< int >( state->extent.height ) -
			( view->viewport.y1 + surface->scissorRect.y1 + scissorHeight );
		if ( scissor.offset.x < 0 ) {
			scissorWidth -= Min( scissorWidth, -scissor.offset.x );
			scissor.offset.x = 0;
		}
		if ( scissor.offset.y < 0 ) {
			scissorHeight -= Min( scissorHeight, -scissor.offset.y );
			scissor.offset.y = 0;
		}
		scissorWidth = Min( scissorWidth,
			static_cast< int >( state->extent.width ) - scissor.offset.x );
		scissorHeight = Min( scissorHeight,
			static_cast< int >( state->extent.height ) - scissor.offset.y );
		if ( scissorWidth <= 0 || scissorHeight <= 0 ) {
			continue;
		}
		scissor.extent.width = static_cast< unsigned int >( scissorWidth );
		scissor.extent.height = static_cast< unsigned int >( scissorHeight );
		state->CmdSetScissor( frame.commandBuffer, 0, 1, &scissor );

		float surfaceProjection[ 16 ];
		memcpy( surfaceProjection, view->projectionMatrix,
			sizeof( surfaceProjection ) );
		float surfaceMinDepth = idMath::ClampFloat( 0.0f, 1.0f,
			cvarSystem->GetCVarFloat( "r_depthRangeStartDefault" ) );
		float surfaceMaxDepth = 1.0f;
		if ( surface->space->weaponDepthHack ) {
			surfaceMinDepth = 0.0f;
			surfaceMaxDepth = idMath::ClampFloat( 0.0f, 1.0f,
				cvarSystem->GetCVarFloat( "r_depthRangeWeaponHackEnd" ) );
			const float weaponFovX = surface->space->weaponDepthHackFOV_x;
			const float weaponFovY = surface->space->weaponDepthHackFOV_y;
			if ( weaponFovX > 0.0f && weaponFovY > 0.0f ) {
				const float zNear = Max( cvarSystem->GetCVarFloat( "r_znear" ),
					0.001f );
				const float xMax = zNear * idMath::Tan(
					weaponFovX * idMath::M_DEG2RAD * 0.5f );
				const float yMax = zNear * idMath::Tan(
					weaponFovY * idMath::M_DEG2RAD * 0.5f );
				surfaceProjection[ 0 ] = zNear / Max( xMax, 0.001f );
				surfaceProjection[ 5 ] = zNear / Max( yMax, 0.001f );
				surfaceProjection[ 8 ] = 0.0f;
				surfaceProjection[ 9 ] = 0.0f;
			}
			surfaceProjection[ 14 ] *= cvarSystem->GetCVarFloat(
				"r_depthRangeWeaponHackScale" );
		}
		if ( surface->space->modelDepthHack != 0.0f ) {
			// RB_EnterModelDepthHack follows RB_EnterWeaponDepthHack in the
			// OpenGL path and intentionally replaces its projection/depth range.
			memcpy( surfaceProjection, view->projectionMatrix,
				sizeof( surfaceProjection ) );
			surfaceProjection[ 14 ] -= surface->space->modelDepthHack;
			surfaceMinDepth = idMath::ClampFloat( 0.0f, 1.0f,
				cvarSystem->GetCVarFloat( "r_depthRangeStartDefault" ) );
			surfaceMaxDepth = 1.0f;
		}
		VkViewport surfaceViewport = viewport;
		surfaceViewport.minDepth = surfaceMinDepth;
		surfaceViewport.maxDepth = Max( surfaceMinDepth, surfaceMaxDepth );
		state->CmdSetViewport( frame.commandBuffer, 0, 1, &surfaceViewport );

		float pushConstants[ 32 ];
		memset( pushConstants, 0, sizeof( pushConstants ) );
		for ( int column = 0; column < 4; ++column ) {
			for ( int row = 0; row < 4; ++row ) {
				pushConstants[ column * 4 + row ] =
					surfaceProjection[ 0 * 4 + row ] * surface->space->modelViewMatrix[ column * 4 + 0 ] +
					surfaceProjection[ 1 * 4 + row ] * surface->space->modelViewMatrix[ column * 4 + 1 ] +
					surfaceProjection[ 2 * 4 + row ] * surface->space->modelViewMatrix[ column * 4 + 2 ] +
					surfaceProjection[ 3 * 4 + row ] * surface->space->modelViewMatrix[ column * 4 + 3 ];
			}
		}
		// OpenGL clip Z is -W..W; Vulkan clip Z is 0..W.
		for ( int column = 0; column < 4; ++column ) {
			pushConstants[ column * 4 + 2 ] = 0.5f *
				( pushConstants[ column * 4 + 2 ] +
				  pushConstants[ column * 4 + 3 ] );
		}
		pushConstants[ 16 ] = pushConstants[ 17 ] =
			pushConstants[ 18 ] = pushConstants[ 19 ] = 1.0f;
		if ( selectedStage->colorVector != NULL ) {
			for ( int component = 0; component < 4; ++component ) {
				pushConstants[ 16 + component ] = surface->materialRegisters[
					selectedStage->colorVector->registers[ component ] ];
			}
		}
		pushConstants[ 20 ] = 1.0f;
		pushConstants[ 25 ] = 1.0f;
		if ( selectedStage->diffuseTextureMatrix != NULL ) {
			const int ( *matrix )[ 3 ] = selectedStage->diffuseTextureMatrix->matrix;
			pushConstants[ 20 ] = surface->materialRegisters[ matrix[ 0 ][ 0 ] ];
			pushConstants[ 21 ] = surface->materialRegisters[ matrix[ 0 ][ 1 ] ];
			pushConstants[ 22 ] = surface->materialRegisters[ matrix[ 0 ][ 2 ] ];
			pushConstants[ 24 ] = surface->materialRegisters[ matrix[ 1 ][ 0 ] ];
			pushConstants[ 25 ] = surface->materialRegisters[ matrix[ 1 ][ 1 ] ];
			pushConstants[ 26 ] = surface->materialRegisters[ matrix[ 1 ][ 2 ] ];
		}
		if ( selectedWaterStage && selectedStage->numTextureMatrices > 0 ) {
			const int ( *matrix )[ 3 ] = selectedStage->textureMatrices[ 0 ].matrix;
			pushConstants[ 20 ] = surface->materialRegisters[ matrix[ 0 ][ 0 ] ];
			pushConstants[ 21 ] = surface->materialRegisters[ matrix[ 0 ][ 1 ] ];
			pushConstants[ 22 ] = surface->materialRegisters[ matrix[ 0 ][ 2 ] ];
			pushConstants[ 24 ] = surface->materialRegisters[ matrix[ 1 ][ 0 ] ];
			pushConstants[ 25 ] = surface->materialRegisters[ matrix[ 1 ][ 1 ] ];
			pushConstants[ 26 ] = surface->materialRegisters[ matrix[ 1 ][ 2 ] ];
		}
		idVec3 worldSunDirection( 0.35f, 0.45f, 0.82f );
		if ( view->atmosphere != NULL ) {
			worldSunDirection = view->atmosphere->GetSunDirection();
		}
		idVec3 localSunDirection;
		localSunDirection.Set(
			worldSunDirection.x * surface->space->modelMatrix[ 0 ] +
				worldSunDirection.y * surface->space->modelMatrix[ 1 ] +
				worldSunDirection.z * surface->space->modelMatrix[ 2 ],
			worldSunDirection.x * surface->space->modelMatrix[ 4 ] +
				worldSunDirection.y * surface->space->modelMatrix[ 5 ] +
				worldSunDirection.z * surface->space->modelMatrix[ 6 ],
			worldSunDirection.x * surface->space->modelMatrix[ 8 ] +
				worldSunDirection.y * surface->space->modelMatrix[ 9 ] +
				worldSunDirection.z * surface->space->modelMatrix[ 10 ] );
		if ( materialTexturesAvailable ) {
			EncodeModelRotation( surface->space->modelMatrix,
				pushConstants[ 23 ], pushConstants[ 27 ] );
		} else {
			EncodeOctahedralDirection( localSunDirection,
				pushConstants[ 23 ], pushConstants[ 27 ] );
		}
		if ( selectedAtmosphereStage ) {
			const idVec3 translatedViewOrigin(
				view->renderView.vieworg.x - surface->space->modelMatrix[ 12 ],
				view->renderView.vieworg.y - surface->space->modelMatrix[ 13 ],
				view->renderView.vieworg.z - surface->space->modelMatrix[ 14 ] );
			idVec3 localViewOrigin(
				translatedViewOrigin.x * surface->space->modelMatrix[ 0 ] +
					translatedViewOrigin.y * surface->space->modelMatrix[ 1 ] +
					translatedViewOrigin.z * surface->space->modelMatrix[ 2 ],
				translatedViewOrigin.x * surface->space->modelMatrix[ 4 ] +
					translatedViewOrigin.y * surface->space->modelMatrix[ 5 ] +
					translatedViewOrigin.z * surface->space->modelMatrix[ 6 ],
				translatedViewOrigin.x * surface->space->modelMatrix[ 8 ] +
					translatedViewOrigin.y * surface->space->modelMatrix[ 9 ] +
					translatedViewOrigin.z * surface->space->modelMatrix[ 10 ] );
			pushConstants[ 20 ] = localViewOrigin.x;
			pushConstants[ 21 ] = localViewOrigin.y;
			pushConstants[ 22 ] = localViewOrigin.z;
		}
		pushConstants[ 28 ] = selectedStage->vertexColor == SVC_MODULATE ? 1.0f : 0.0f;
		pushConstants[ 29 ] = surface->material->TestMaterialFlag(
			MF_LOWRANGEUVCOMPRESS ) ? ST_TO_FLOAT_LOWRANGE : ST_TO_FLOAT;
		if ( selectedStage->hasAlphaTest || selectedStuffGrassStage ) {
			pushConstants[ 30 ] = selectedStage->hasAlphaTest ?
				surface->materialRegisters[ selectedStage->alphaTestRegister ] : 0.4f;
			pushConstants[ 31 ] = 1.0f;
		}

		const VkDeviceSize vertexOffset = vertexCacheBlock->offset;
		const VkDeviceSize vertexBytes = static_cast< VkDeviceSize >(
			surface->geo->numVerts ) * sizeof( idDrawVert );
		const VkDeviceSize indexBytes = static_cast< VkDeviceSize >(
			surface->geo->numIndexes ) * sizeof( glIndex_t );
		if ( vertexCacheBlock->offset < 0 || indexCacheBlock->offset < 0 ||
			vertexOffset + vertexBytes > vertexResource->bytes ||
			static_cast< VkDeviceSize >( indexCacheBlock->offset ) + indexBytes >
				indexResource->bytes ) {
			state->worldMissingResource++;
			continue;
		}
		state->CmdBindVertexBuffers( frame.commandBuffer, 0, 1,
			&vertexResource->buffer, &vertexOffset );
		state->CmdBindIndexBuffer( frame.commandBuffer, indexResource->buffer,
			indexCacheBlock->offset, VK_INDEX_TYPE_UINT16 );
		const bool requiresDepthPrepass =
			( surface->material->Coverage() != MC_TRANSLUCENT ||
			  selectedStuffGrassStage ) &&
			!selectedAtmosphereStage && !selectedWaterStage &&
			!selectedHeatHazeStage && state->worldDepthPipeline != VK_NULL_HANDLE;
		if ( requiresDepthPrepass ) {
			state->CmdBindPipeline( frame.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS, state->worldDepthPipeline );
			state->CmdBindDescriptorSets( frame.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS, state->guiPipelineLayout, 0, 1,
				&imageResource->descriptorSet, 0, NULL );
			state->CmdPushConstants( frame.commandBuffer,
				state->guiPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				sizeof( pushConstants ), pushConstants );
			state->CmdDrawIndexed( frame.commandBuffer,
				surface->geo->numIndexes, 1, 0, 0, 0 );
			state->worldDrawCalls++;
			state->worldDepthDrawCalls++;
		}
		if ( selectedMegaTexture != NULL && state->worldMegaPipeline != VK_NULL_HANDLE ) {
			state->CmdBindPipeline( frame.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS, state->worldMegaPipeline );
			const int shaderLevels = Min( selectedMegaTexture->GetNumLevels(), 6 );
			for ( int shaderLevel = 0; shaderLevel < shaderLevels; ++shaderLevel ) {
				idMegaTextureLevel* level = selectedMegaTexture->GetLevel(
					selectedMegaTexture->GetNumLevels() - shaderLevel - 1 );
				if ( level == NULL || level->GetImage() == NULL ||
					( shaderLevel != 0 && !level->ImageIsValid() ) ) {
					continue;
				}
				idImage* levelImage = level->GetImage();
				if ( !levelImage->IsLoaded() ) {
					levelImage->BindFragment();
				}
				const sdVulkanImageResource* levelResource = NULL;
				for ( int resourceIndex = 0; resourceIndex < state->imageResources.Num();
					++resourceIndex ) {
					if ( state->imageResources[ resourceIndex ].owner == levelImage ) {
						levelResource = &state->imageResources[ resourceIndex ];
						break;
					}
				}
				if ( levelResource == NULL || levelResource->descriptorSet == VK_NULL_HANDLE ) {
					continue;
				}
				float megaPushConstants[ 32 ];
				memcpy( megaPushConstants, pushConstants, sizeof( megaPushConstants ) );
				const float* levelParms = level->GetParms();
				megaPushConstants[ 20 ] = levelParms[ 0 ];
				megaPushConstants[ 21 ] = levelParms[ 1 ];
				megaPushConstants[ 22 ] = levelParms[ 2 ];
				megaPushConstants[ 23 ] = levelParms[ 3 ];
				megaPushConstants[ 24 ] = static_cast< float >( 1 << shaderLevel );
				megaPushConstants[ 25 ] = 1.0f;
				megaPushConstants[ 26 ] = shaderLevel == 0 ? 0.0f : 1.0f;
				megaPushConstants[ 30 ] = pushConstants[ 23 ];
				megaPushConstants[ 31 ] = pushConstants[ 27 ];
				if ( shaderLevel != 0 ) {
					const int fadeMilliseconds = Max( 0,
						cvarSystem->GetCVarInteger( "r_megaFadeTime" ) );
					megaPushConstants[ 25 ] = fadeMilliseconds > 0 ?
						idMath::ClampFloat( 0.0f, 1.0f,
							( view->renderView.time - level->GetFadeTime() ) /
							static_cast< float >( fadeMilliseconds ) ) : 1.0f;
				}
				state->CmdBindDescriptorSets( frame.commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS, state->guiPipelineLayout, 0, 1,
					&levelResource->descriptorSet, 0, NULL );
				state->CmdPushConstants( frame.commandBuffer, state->guiPipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					sizeof( megaPushConstants ), megaPushConstants );
				state->CmdDrawIndexed( frame.commandBuffer, surface->geo->numIndexes,
					1, 0, 0, 0 );
				state->worldDrawCalls++;
				state->worldMegaDrawCalls++;
			}
		} else {
			VkPipeline materialPipeline = state->worldPipeline;
			VkDescriptorSet drawDescriptorSet = imageResource->descriptorSet;
			const int blendBits = selectedStage->drawStateBits & 0xFF;
			if ( selectedHeatHazeStage ) {
				if ( !heatHazeTexturesAvailable ||
					( !currentRenderCopied && !CopyCurrentRender( *state ) ) ) {
					// A postprocess normal map is never a valid visible fallback.
					// If this surface cannot snapshot the scene, omit it instead of
					// covering the framebuffer with an opaque gray quad.
					continue;
				}
				currentRenderCopied = true;
				materialPipeline = state->worldHeatHazePipeline;
				drawDescriptorSet = heatHazeDescriptorSet;
			} else if ( waterTexturesAvailable ) {
				materialPipeline = state->worldWaterPipeline;
				drawDescriptorSet = waterDescriptorSet;
			} else if ( selectedAtmosphereStage &&
				state->worldAtmospherePipeline != VK_NULL_HANDLE ) {
				materialPipeline = state->worldAtmospherePipeline;
			} else if ( materialTexturesAvailable && blendBits == 0x65 &&
				state->worldMaterialAlphaPipeline != VK_NULL_HANDLE ) {
				materialPipeline = state->worldMaterialAlphaPipeline;
				drawDescriptorSet = materialDescriptorSet;
			} else if ( materialTexturesAvailable && blendBits == 0x20 &&
				state->worldMaterialAddPipeline != VK_NULL_HANDLE ) {
				materialPipeline = state->worldMaterialAddPipeline;
				drawDescriptorSet = materialDescriptorSet;
			} else if ( selectedStuffGrassStage ) {
				// Grass is declared translucent to control sorting, but its stage is
				// alpha-to-coverage + writeDepth.  With a single-sample swapchain the
				// faithful fallback is an alpha-tested opaque/depth-writing pass, not
				// source-alpha blending of the whole polygon card.
				materialPipeline = state->worldPipeline;
			} else if ( materialTexturesAvailable && ( blendBits == 0x00 ||
				blendBits == 0x01 ) ) {
				materialPipeline = state->worldMaterialPipeline;
				drawDescriptorSet = materialDescriptorSet;
			} else if ( blendBits == 0x65 ) {
				materialPipeline = state->worldAlphaPipeline;
			} else if ( blendBits == 0x20 ) {
				materialPipeline = state->worldAddPipeline;
			} else if ( blendBits == 0x25 ) {
				materialPipeline = state->worldAlphaAddPipeline;
			} else if ( blendBits == 0x03 ) {
				materialPipeline = state->worldMultiplyPipeline;
			} else if ( surface->material->Coverage() == MC_TRANSLUCENT ) {
				materialPipeline = state->worldAlphaPipeline;
			}
			state->CmdBindPipeline( frame.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS, materialPipeline );
			state->CmdBindDescriptorSets( frame.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS, state->guiPipelineLayout, 0, 1,
				&drawDescriptorSet, 0, NULL );
			state->CmdPushConstants( frame.commandBuffer, state->guiPipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
				sizeof( pushConstants ), pushConstants );
			state->CmdDrawIndexed( frame.commandBuffer, surface->geo->numIndexes,
				1, 0, 0, 0 );
			state->worldDrawCalls++;
			if ( waterTexturesAvailable ) {
				state->worldWaterDrawCalls++;
			}
			if ( selectedStuffGrassStage ) {
				state->worldStuffDrawCalls++;
			}
			if ( selectedHeatHazeStage ) {
				continue;
			}

			// Preserve additional blended material stages in declaration order.
			// The base selection above chooses the best diffuse-capable stage; these
			// passes restore decals, glows, detail modulation, and other overlays
			// that the one-stage fallback previously discarded entirely.
			for ( int stageIndex = 0;
				stageIndex < surface->material->GetNumStages(); ++stageIndex ) {
				const materialStage_t* overlayStage =
					surface->material->GetStage( stageIndex );
				if ( overlayStage == selectedStage || overlayStage->megaTexture != NULL ||
					surface->materialRegisters[ overlayStage->conditionRegister ] == 0.0f ) {
					continue;
				}
				const int overlayBlendBits = overlayStage->drawStateBits & 0xFF;
				if ( overlayBlendBits != 0x65 && overlayBlendBits != 0x20 &&
					overlayBlendBits != 0x25 && overlayBlendBits != 0x03 ) {
					continue;
				}
				idImage* overlayImage = NULL;
				int overlayImageScore = -0x7fffffff;
				for ( int textureIndex = 0; textureIndex < overlayStage->numTextures;
					++textureIndex ) {
					const stageTexture_t& texture = overlayStage->textures[ textureIndex ];
					if ( texture.image == NULL || texture.image->defaulted ||
						( globalImages != NULL && ( texture.image == globalImages->blackImage ||
						  texture.image == globalImages->grayImage ) ) ) {
						continue;
					}
					int score = 10;
					if ( rbinds != NULL && texture.renderBinding == rbinds->diffuseMap ) {
						score = 100;
					} else if ( rbinds != NULL && texture.renderBinding == rbinds->map ) {
						score = 90;
					}
					if ( score > overlayImageScore ) {
						overlayImage = texture.image;
						overlayImageScore = score;
					}
				}
				if ( overlayImage == NULL ) {
					continue;
				}
				if ( !overlayImage->IsLoaded() ) {
					overlayImage->BindFragment();
				}
				const sdVulkanImageResource* overlayResource =
					FindVulkanImageResource( *state, overlayImage );
				if ( overlayResource == NULL ||
					overlayResource->descriptorSet == VK_NULL_HANDLE ) {
					continue;
				}
				float overlayPushConstants[ 32 ];
				memcpy( overlayPushConstants, pushConstants,
					sizeof( overlayPushConstants ) );
				overlayPushConstants[ 16 ] = overlayPushConstants[ 17 ] =
					overlayPushConstants[ 18 ] = overlayPushConstants[ 19 ] = 1.0f;
				if ( overlayStage->colorVector != NULL ) {
					for ( int component = 0; component < 4; ++component ) {
						overlayPushConstants[ 16 + component ] =
							surface->materialRegisters[
								overlayStage->colorVector->registers[ component ] ];
					}
				}
				overlayPushConstants[ 20 ] = 1.0f;
				overlayPushConstants[ 21 ] = 0.0f;
				overlayPushConstants[ 22 ] = 0.0f;
				overlayPushConstants[ 24 ] = 0.0f;
				overlayPushConstants[ 25 ] = 1.0f;
				overlayPushConstants[ 26 ] = 0.0f;
				if ( overlayStage->diffuseTextureMatrix != NULL ) {
					const int ( *matrix )[ 3 ] =
						overlayStage->diffuseTextureMatrix->matrix;
					overlayPushConstants[ 20 ] = surface->materialRegisters[ matrix[ 0 ][ 0 ] ];
					overlayPushConstants[ 21 ] = surface->materialRegisters[ matrix[ 0 ][ 1 ] ];
					overlayPushConstants[ 22 ] = surface->materialRegisters[ matrix[ 0 ][ 2 ] ];
					overlayPushConstants[ 24 ] = surface->materialRegisters[ matrix[ 1 ][ 0 ] ];
					overlayPushConstants[ 25 ] = surface->materialRegisters[ matrix[ 1 ][ 1 ] ];
					overlayPushConstants[ 26 ] = surface->materialRegisters[ matrix[ 1 ][ 2 ] ];
				}
				overlayPushConstants[ 28 ] =
					overlayStage->vertexColor == SVC_MODULATE ? 1.0f : 0.0f;
				overlayPushConstants[ 30 ] = overlayStage->hasAlphaTest ?
					surface->materialRegisters[ overlayStage->alphaTestRegister ] : 0.0f;
				overlayPushConstants[ 31 ] = overlayStage->hasAlphaTest ? 1.0f : 0.0f;

				VkPipeline overlayPipeline = state->worldAlphaPipeline;
				if ( overlayBlendBits == 0x20 ) {
					overlayPipeline = state->worldAddPipeline;
				} else if ( overlayBlendBits == 0x25 ) {
					overlayPipeline = state->worldAlphaAddPipeline;
				} else if ( overlayBlendBits == 0x03 ) {
					overlayPipeline = state->worldMultiplyPipeline;
				}
				VkDescriptorSet overlayDescriptorSet = overlayResource->descriptorSet;
				if ( StageUsesVulkanMaterialTextures( *overlayStage ) ) {
					VkDescriptorSet combinedDescriptorSet = VK_NULL_HANDLE;
					if ( GetVulkanMaterialDescriptor( *state, *overlayStage,
						overlayImage, surface->space, combinedDescriptorSet ) ) {
						EncodeModelRotation( surface->space->modelMatrix,
							overlayPushConstants[ 23 ],
							overlayPushConstants[ 27 ] );
						if ( overlayBlendBits == 0x65 ) {
							overlayPipeline = state->worldMaterialAlphaPipeline;
							overlayDescriptorSet = combinedDescriptorSet;
						} else if ( overlayBlendBits == 0x20 ) {
							overlayPipeline = state->worldMaterialAddPipeline;
							overlayDescriptorSet = combinedDescriptorSet;
						}
					}
				}
				state->CmdBindPipeline( frame.commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline );
				state->CmdBindDescriptorSets( frame.commandBuffer,
					VK_PIPELINE_BIND_POINT_GRAPHICS, state->guiPipelineLayout, 0, 1,
					&overlayDescriptorSet, 0, NULL );
				state->CmdPushConstants( frame.commandBuffer,
					state->guiPipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					sizeof( overlayPushConstants ), overlayPushConstants );
				state->CmdDrawIndexed( frame.commandBuffer,
					surface->geo->numIndexes, 1, 0, 0, 0 );
				state->worldDrawCalls++;
				state->worldStageDrawCalls++;
			}
		}
		if ( surface->space->model != NULL &&
			idStr::Icmp( surface->space->model->Name(), "_MD5_Snapshot_" ) == 0 ) {
			state->worldSkinnedDrawCalls++;
		}
	}
}

bool sdVulkanBackend::DrawGuiFan( const void* imageOwner,
	const sdVulkanGuiVertex* vertices, int vertexCount, const float* color,
	int drawStateBits ) {
	if ( state == NULL || !state->frameActive || state->guiPipeline == VK_NULL_HANDLE ||
		imageOwner == NULL || vertices == NULL || vertexCount < 3 ||
		r_vkSkipGui.GetBool() ) {
		return false;
	}
	const sdVulkanImageResource* imageResource = NULL;
	for ( int i = 0; i < state->imageResources.Num(); ++i ) {
		if ( state->imageResources[ i ].owner == imageOwner ) {
			imageResource = &state->imageResources[ i ];
			break;
		}
	}
	if ( imageResource == NULL || imageResource->descriptorSet == VK_NULL_HANDLE ) {
		return false;
	}
	sdVulkanFrame& frame = state->frames[ state->frameIndex ];
	const VkDeviceSize vertexBytes = static_cast< VkDeviceSize >( vertexCount ) *
		sizeof( sdVulkanGuiVertex );
	const VkDeviceSize vertexOffset = ( frame.guiVertexOffset + 15 ) & ~15ULL;
	if ( vertexOffset + vertexBytes > VULKAN_GUI_VERTEX_BYTES ) {
		common->Warning( "Vulkan GUI vertex buffer overflow (%u vertices)",
			vertexCount );
		return false;
	}
	memcpy( static_cast< byte* >( frame.guiVertexMapped ) + vertexOffset,
		vertices, static_cast< size_t >( vertexBytes ) );
	frame.guiVertexOffset = vertexOffset + vertexBytes;

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = static_cast< float >( state->extent.height );
	viewport.width = static_cast< float >( state->extent.width );
	viewport.height = -static_cast< float >( state->extent.height );
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	state->CmdSetViewport( frame.commandBuffer, 0, 1, &viewport );
	VkRect2D scissor;
	memset( &scissor, 0, sizeof( scissor ) );
	scissor.extent = state->extent;
	state->CmdSetScissor( frame.commandBuffer, 0, 1, &scissor );
	VkPipeline guiPipeline = state->guiPipeline;
	const int blendBits = drawStateBits & 0xFF;
	if ( blendBits == 0x00 && state->guiOpaquePipeline != VK_NULL_HANDLE ) {
		guiPipeline = state->guiOpaquePipeline;
	} else if ( blendBits == 0x20 && state->guiAddPipeline != VK_NULL_HANDLE ) {
		guiPipeline = state->guiAddPipeline;
	} else if ( blendBits == 0x25 && state->guiAlphaAddPipeline != VK_NULL_HANDLE ) {
		guiPipeline = state->guiAlphaAddPipeline;
	} else if ( blendBits == 0x03 && state->guiMultiplyPipeline != VK_NULL_HANDLE ) {
		guiPipeline = state->guiMultiplyPipeline;
	}
	state->CmdBindPipeline( frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		guiPipeline );
	state->CmdBindDescriptorSets( frame.commandBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, state->guiPipelineLayout, 0, 1,
		&imageResource->descriptorSet, 0, NULL );
	float pushConstants[ 8 ] = {
		2.0f / 640.0f, -2.0f / 480.0f, -1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f
	};
	if ( color != NULL ) {
		memcpy( pushConstants + 4, color, sizeof( float ) * 4 );
	}
	state->CmdPushConstants( frame.commandBuffer, state->guiPipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		sizeof( pushConstants ), pushConstants );
	state->CmdBindVertexBuffers( frame.commandBuffer, 0, 1,
		&frame.guiVertexBuffer, &vertexOffset );
	state->CmdDraw( frame.commandBuffer, vertexCount, 1, 0, 0 );
	state->guiDrawCalls++;
	return true;
}

bool sdVulkanBackend::IsInitialized() const {
	return state != NULL && state->device != VK_NULL_HANDLE &&
		state->swapchain != VK_NULL_HANDLE;
}

bool sdVulkanBackend::IsFrameActive() const {
	return state != NULL && state->frameActive;
}

const char* sdVulkanBackend::GetDeviceName() const {
	return state != NULL ? state->deviceName : "";
}

bool R_UseVulkanBackend() {
	return cvarSystem != NULL &&
		idStr::Icmp( cvarSystem->GetCVarString( "r_renderAPI" ), "vulkan" ) == 0;
}
