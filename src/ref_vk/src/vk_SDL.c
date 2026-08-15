#include "compat.h"
//
// vk_SDL.c -- SDL3-only context layer for the Vulkan renderer. The ONLY file
// in ref_vk that includes SDL headers (CONTRACT.md rule 5).
//
// Window/context flow (same engine handshake as gl1/gl3):
//   GLimp_InitGraphics() -> re.PrepareForWindow() -> SDL_CreateWindow() ->
//   re.InitContext(window).
//
// Modeled on yq2remaster's glimp/vk_main.c SDL3 paths (RE_PrepareForWindow /
// RE_InitContext / Vkimp_CreateSurface) restructured into the gl3_SDL.c shape.
//
// Copyright 1998 Raven Software
//

#include "vk_Local.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

static SDL_Window* window = NULL;

// Ends the world render pass if 2D-only frames left it open, then submits and
// presents the frame. VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR from
// vkAcquireNextImageKHR/vkQueuePresentKHR set vk_recreateSwapchainNeeded
// (QVk_BeginFrame()/QVk_EndFrame()) - the swapchain is rebuilt right here so
// the next frame starts from a valid one.
void RI_EndFrame(void) //mxd. GLimp_EndFrame in original logic.
{
	// On 2D-only frames (menus/console without a world) RI_RenderFrame() never
	// runs, so make sure the RP_WORLD -> RP_WORLD_WARP -> RP_UI transition
	// still happens before submit (no-op if RI_RenderFrame() already did it).
	R_EndWorldRenderpass();

	QVk_EndFrame(false);

	if (vk_recreateSwapchainNeeded && vk_initialized)
		QVk_RecreateSwapchain();
}

// This function returns the flags used at the SDL window creation by GLimp_InitGraphics().
// In case of error -1 is returned.
int RI_PrepareForWindow(void)
{
	if (!SDL_Vulkan_LoadLibrary(NULL))
	{
		ri.Con_Printf(PRINT_ALL, "RI_PrepareForWindow(): SDL_Vulkan_LoadLibrary() failed: %s\n", SDL_GetError());
		return -1;
	}

	return SDL_WINDOW_VULKAN;
}

// Changes the vsync state. In Vulkan the vsync state is a property of the
// swapchain (present mode: FIFO when r_vsync is set, MAILBOX/IMMEDIATE
// otherwise - see QVk_CreateSwapchain()), so flag it for recreation; the
// rebuild happens in RI_EndFrame()/RI_BeginFrame() outside an active frame.
void R_SetVsync(void)
{
	if (vk_initialized)
	{
		ri.Con_Printf(PRINT_ALL, "R_SetVsync(): scheduling swapchain rebuild (r_vsync %d)\n", (int)r_vsync->value);
		vk_recreateSwapchainNeeded = true;
	}
}

// Initializes the Vulkan context: volk -> instance (with SDL instance
// extensions) -> surface -> QVk_Init() (device/swapchain/passes/pipelines).
qboolean RI_InitContext(void* win)
{
	if (win == NULL)
	{
		ri.Sys_Error(ERR_FATAL, "RI_InitContext() called with NULL argument!");
		return false;
	}

	window = (SDL_Window*)win;

	// Initialize volk through SDL's vkGetInstanceProcAddr (the Vulkan library
	// was loaded by SDL_Vulkan_LoadLibrary() in RI_PrepareForWindow()).
	volkInitializeCustom((PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr());

	if (vkGetInstanceProcAddr == NULL || vkCreateInstance == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to load Vulkan global entry points through volk!\n");
		window = NULL;
		return false;
	}

	// Query the instance extensions required by SDL for surface creation.
	Uint32 sdlExtCount = 0;
	const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

	if (sdlExtensions == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): SDL_Vulkan_GetInstanceExtensions() failed: %s\n", SDL_GetError());
		window = NULL;
		return false;
	}

	if (!QVk_CreateInstance(sdlExtensions, sdlExtCount))
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): could not create Vulkan instance!\n");
		window = NULL;
		return false;
	}

	// Create the presentation surface.
	if (!SDL_Vulkan_CreateSurface(window, vk_instance, NULL, &vk_surface))
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): SDL_Vulkan_CreateSurface() failed: %s\n", SDL_GetError());
		vkDestroyInstance(vk_instance, NULL);
		vk_instance = VK_NULL_HANDLE;
		window = NULL;
		return false;
	}
	ri.Con_Printf(PRINT_ALL, "...created Vulkan surface\n");

	// Device pick, swapchain, render passes, per-frame resources, pipelines.
	if (!QVk_Init())
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): could not initialize Vulkan!\n");
		RI_ShutdownContext();
		return false;
	}

	// Report the selected device.
	ri.Con_Printf(PRINT_ALL, "VK: device=%s vendor=%s (%s) driver=%d.%d.%d api=%d.%d.%d\n",
		vk_device.properties.deviceName,
		vk_config.vendor_name,
		vk_config.device_type,
		VK_VERSION_MAJOR(vk_device.properties.driverVersion),
		VK_VERSION_MINOR(vk_device.properties.driverVersion),
		VK_VERSION_PATCH(vk_device.properties.driverVersion),
		VK_VERSION_MAJOR(vk_device.properties.apiVersion),
		VK_VERSION_MINOR(vk_device.properties.apiVersion),
		VK_VERSION_PATCH(vk_device.properties.apiVersion));

	return true;
}

// Shuts the Vulkan context down: full QVk teardown (pipelines, buffers,
// swapchain, device) + surface/instance destruction (QVk_Shutdown()).
void RI_ShutdownContext(void)
{
	// Full teardown - QVk_WaitAndShutdownAll() waits for the device to go idle
	// and destroys everything including vk_surface and vk_instance. If QVk_Init()
	// failed half-way (vk_initialized still false) clean up instance/surface here.
	if (vk_initialized)
	{
		QVk_WaitAndShutdownAll();
	}
	else if (vk_instance != VK_NULL_HANDLE)
	{
		if (vk_device.logical != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(vk_device.logical);
			vulkan_memory_delete();
			vkDestroyDevice(vk_device.logical, NULL);
			vk_device.logical = VK_NULL_HANDLE;
			vk_device.physical = VK_NULL_HANDLE;
		}

		if (vk_surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(vk_instance, vk_surface, NULL);
			vk_surface = VK_NULL_HANDLE;
		}

		QVk_DestroyValidationLayers();
		vkDestroyInstance(vk_instance, NULL);
		vk_instance = VK_NULL_HANDLE;
	}

	if (window != NULL)
	{
		SDL_Vulkan_UnloadLibrary();
		window = NULL;
	}
}

// Fills the actual size of the drawable into width and height.
void QVk_GetDrawableSize(int* width, int* height)
{
	SDL_GetWindowSizeInPixels(window, width, height);
}
