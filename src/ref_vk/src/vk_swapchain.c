#include "compat.h"
//
// vk_swapchain.c -- Vulkan swapchain creation. Present mode is derived from
// r_vsync: FIFO when vsync is on, MAILBOX (with IMMEDIATE fallback) otherwise;
// R_SetVsync() (vk_SDL.c) triggers a swapchain rebuild on change.
//
// Mechanical port of yquake2remaster vk_swapchain.c (Copyright (C) 2018-2019
// Krzysztof Kondrak, GPLv2) - yq2 refimport calls replaced with H2R's ri.*,
// vid.* replaced with viddef.* (CONTRACT.md).
//

#include "vk_Local.h"

/* internal helper */
static const char* presentModeString(VkPresentModeKHR presentMode)
{
#define PMSTR(r) case VK_ ##r: return "VK_"#r
	switch (presentMode)
	{
		PMSTR(PRESENT_MODE_IMMEDIATE_KHR);
		PMSTR(PRESENT_MODE_MAILBOX_KHR);
		PMSTR(PRESENT_MODE_FIFO_KHR);
		PMSTR(PRESENT_MODE_FIFO_RELAXED_KHR);
		default: return "<unknown>";
	}
#undef PMSTR
}

/* internal helper */
static VkSurfaceFormatKHR getSwapSurfaceFormat(const VkSurfaceFormatKHR* surfaceFormats, uint32_t formatCount)
{
	VkSurfaceFormatKHR swapSurfaceFormat;
	memset(&swapSurfaceFormat, 0, sizeof(swapSurfaceFormat));
	if (!surfaceFormats || !formatCount)
	{
		return swapSurfaceFormat;
	}

	for (size_t i = 0; i < formatCount; ++i)
	{
		if (surfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
			surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
		{
			swapSurfaceFormat.colorSpace = surfaceFormats[i].colorSpace;
			swapSurfaceFormat.format = surfaceFormats[i].format;
			return swapSurfaceFormat;
		}
	}
	// no preferred format, so get the first one from list
	swapSurfaceFormat.colorSpace = surfaceFormats[0].colorSpace;
	swapSurfaceFormat.format = surfaceFormats[0].format;

	return swapSurfaceFormat;
}

/* internal helper */
// look to https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPresentModeKHR.html for more information
static VkPresentModeKHR getSwapPresentMode(const VkPresentModeKHR* presentModes, uint32_t presentModesCount, VkPresentModeKHR desiredMode)
{
	// PRESENT_MODE_FIFO_KHR is guaranteed to exist due to spec requirements
	VkPresentModeKHR usedPresentMode = VK_PRESENT_MODE_FIFO_KHR;

	if (!presentModes)
	{
		return usedPresentMode;
	}

	// check if the desired present mode is supported
	for (uint32_t i = 0; i < presentModesCount; ++i)
	{
		// mode supported, nothing to do here
		if (presentModes[i] == desiredMode)
		{
			vk_config.present_mode = presentModeString(desiredMode);
			ri.Con_Printf(PRINT_ALL, "...using present mode: %s\n", vk_config.present_mode);
			return desiredMode;
		}
	}

	// preferred present mode not found - choose the next best thing
	for (uint32_t i = 0; i < presentModesCount; ++i)
	{
		// always prefer mailbox for triple buffering with whole image replace
		if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			usedPresentMode = presentModes[i];
			break;
		}
		// prefer immediate update with tearing
		else if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
		{
			usedPresentMode = presentModes[i];
		}
	}

	vk_config.present_mode = presentModeString(usedPresentMode);
	ri.Con_Printf(PRINT_ALL, "...present mode %s not supported, using present mode: %s\n", presentModeString(desiredMode), vk_config.present_mode);
	return usedPresentMode;
}

static const VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[] = {
	VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
	VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
	VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
};

/* internal helper */
static VkCompositeAlphaFlagBitsKHR getSupportedCompositeAlpha(VkCompositeAlphaFlagsKHR supportedFlags)
{
	for (int i = 0; i < 4; ++i)
	{
		if (supportedFlags & compositeAlphaFlags[i])
			return compositeAlphaFlags[i];
	}

	return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

qboolean QVk_CheckExtent(void)
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_VERIFY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_device.physical, vk_surface, &surfaceCaps));

	if (surfaceCaps.currentExtent.width == 0 || surfaceCaps.currentExtent.height == 0)
	{
		return false;
	}

	return true;
}

VkResult QVk_CreateSwapchain(void)
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VkSurfaceFormatKHR* surfaceFormats = NULL;
	VkPresentModeKHR* presentModes = NULL;
	uint32_t formatCount, presentModesCount;
	VkResult res;
	VkImage* tmp;

	VK_VERIFY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_device.physical, vk_surface, &surfaceCaps));
	VK_VERIFY(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_device.physical, vk_surface, &formatCount, NULL));
	VK_VERIFY(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_device.physical, vk_surface, &presentModesCount, NULL));

	if (formatCount > 0)
	{
		surfaceFormats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));

		if (!surfaceFormats)
		{
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		}

		res = vkGetPhysicalDeviceSurfaceFormatsKHR(vk_device.physical, vk_surface, &formatCount, surfaceFormats);

		if (res != VK_SUCCESS)
		{
			free(surfaceFormats);
			return res;
		}
	}

	if (presentModesCount > 0)
	{
		presentModes = (VkPresentModeKHR*)malloc(presentModesCount * sizeof(VkPresentModeKHR));

		if (!presentModes)
		{
			free(surfaceFormats);
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		}

		res = vkGetPhysicalDeviceSurfacePresentModesKHR(vk_device.physical, vk_surface, &presentModesCount, presentModes);

		if (res != VK_SUCCESS)
		{
			free(surfaceFormats);
			free(presentModes);
			return res;
		}

		ri.Con_Printf(PRINT_ALL, "Supported present modes: ");
		for (int i = 0; i < presentModesCount; i++)
		{
			ri.Con_Printf(PRINT_ALL, "%s ", presentModeString(presentModes[i]));
			vk_config.supported_present_modes[i] = presentModeString(presentModes[i]);
		}
		ri.Con_Printf(PRINT_ALL, "\n");
	}

	// r_vsync: FIFO (vsync on) vs MAILBOX/IMMEDIATE (vsync off; getSwapPresentMode()
	// falls back MAILBOX -> IMMEDIATE -> FIFO depending on support).
	VkSurfaceFormatKHR swapSurfaceFormat = getSwapSurfaceFormat(surfaceFormats, formatCount);
	VkPresentModeKHR swapPresentMode = getSwapPresentMode(presentModes, presentModesCount,
		(r_vsync->value > 0.0f ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR));
	free(surfaceFormats);
	free(presentModes);

	VkExtent2D extent = surfaceCaps.currentExtent;
	if (extent.width == UINT32_MAX || extent.height == UINT32_MAX)
	{
		extent.width = max(surfaceCaps.minImageExtent.width, min(surfaceCaps.maxImageExtent.width, (uint32_t)viddef.width));
		extent.height = max(surfaceCaps.minImageExtent.height, min(surfaceCaps.maxImageExtent.height, (uint32_t)viddef.height));
	}

	// >=3 images with 2 frames in flight (NUM_CMDBUFFERS) gives an even vblank lock with jitter
	// tolerance (Quake3e config). NOTE: on hybrid/PRIME laptops FIFO's cross-GPU (or compositor)
	// vblank flip stalls regardless of image count - MAILBOX (r_vsync 0) is the smooth mode there,
	// measured dead-even 16.67ms vs FIFO's chaotic half-rate. >=2 also fixes AMD fullscreen crashes.
	uint32_t imageCount = max(3, surfaceCaps.minImageCount);

	if (surfaceCaps.maxImageCount > 0)
	{
		imageCount = min(imageCount, surfaceCaps.maxImageCount);
	}

	VkImageUsageFlags imgUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	// TRANSFER_SRC_BIT is required for taking screenshots
	if (surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	{
		imgUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		vk_device.screenshotSupported = true;
	}

	VkSwapchainKHR oldSwapchain = vk_swapchain.sc;
	VkSwapchainCreateInfoKHR scCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = NULL,
		.flags = 0,
		.surface = vk_surface,
		.minImageCount = imageCount,
		.imageFormat = swapSurfaceFormat.format,
		.imageColorSpace = swapSurfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = imgUsage,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL,
		.preTransform = (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : surfaceCaps.currentTransform,
		.compositeAlpha = getSupportedCompositeAlpha(surfaceCaps.supportedCompositeAlpha),
		.presentMode = swapPresentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = oldSwapchain
	};

	uint32_t queueFamilyIndices[] = { (uint32_t)vk_device.gfxFamilyIndex, (uint32_t)vk_device.presentFamilyIndex };
	if (vk_device.presentFamilyIndex != vk_device.gfxFamilyIndex)
	{
		scCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		scCreateInfo.queueFamilyIndexCount = 2;
		scCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
	}

	vk_swapchain.format = swapSurfaceFormat.format;
	vk_swapchain.presentMode = swapPresentMode;
	// vk's present never CPU-blocks the render loop (vkQueuePresentKHR is non-blocking and
	// vkAcquireNextImageKHR returns immediately), so the loop is paced by the software vid_maxfps
	// cap for EVERY present mode. Report 0 (not vsync-active): the frame loop must NOT add the
	// refresh*1.2 headroom - that only suits gl's BLOCKING SwapWindow. Rendering above the refresh
	// under a non-blocking present just beats against vblank as a periodic skip (worst under FIFO,
	// which presents in-order; MAILBOX hides it by showing the freshest frame). So vk renders AT
	// the refresh (vid_maxfps == display Hz), and MAILBOX (r_vsync 0) is the smoothest mode here.
	ri.Cvar_SetValue("vid_vsync_active", 0.0f);
	vk_swapchain.extent = extent;
	ri.Con_Printf(PRINT_ALL, "...trying swapchain extent: %dx%d\n", vk_swapchain.extent.width, vk_swapchain.extent.height);
	ri.Con_Printf(PRINT_ALL, "...trying swapchain image format: %d\n", vk_swapchain.format);

	res = vkCreateSwapchainKHR(vk_device.logical, &scCreateInfo, NULL, &vk_swapchain.sc);
	if (res != VK_SUCCESS)
	{
		return res;
	}

	VK_VERIFY(vkGetSwapchainImagesKHR(vk_device.logical, vk_swapchain.sc, &imageCount, NULL));
	tmp = (VkImage*)realloc(vk_swapchain.images, imageCount * sizeof(VkImage));
	VK_CHECK_OOM(tmp, "realloc() VkImage")

	vk_swapchain.images = tmp;
	vk_swapchain.imageCount = imageCount;
	res = vkGetSwapchainImagesKHR(vk_device.logical, vk_swapchain.sc, &imageCount, vk_swapchain.images);

	if (oldSwapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(vk_device.logical, oldSwapchain, NULL);

	return res;
}
