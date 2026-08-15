//
// vk_Local.h -- renderer-wide header for the Vulkan renderer (ref_vk).
//
// H2 rendering semantics from ref_gl1 (gl1_Local.h is the semantic reference,
// the validated H2 ports in ref_gl3 are the H2-decisions reference), backend
// architecture from the yquake2remaster vk renderer (header/local.h + qvk.h +
// util.h are the backend reference, vkQuake2-derived).
//
// Vulkan comes exclusively through vendored volk (src/ref_vk/volk/) - never
// include vulkan/vulkan.h directly and never link libvulkan (volk dlopens it).
// NOTE(integrator): "volk/volk.h" resolves via an include dir pointing at
// src/ref_vk (the CMake target must add ${CMAKE_SOURCE_DIR}/src/ref_vk).
//
// Copyright 1998 Raven Software
//

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <assert.h>

// volk defines VK_NO_PROTOTYPES itself before pulling in the Vulkan headers,
// but define it explicitly so no other include path can leak prototypes.
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "volk/volk.h"

#include "client/ref.h"
#include "client/vid.h" // viddef_t / extern viddef (engine global, resolved at dlopen).

#define REF_TITLE			"Vulkan"

#define MAX_VKTEXTURES		2048 // Same as gl1/gl3 MAX_GLTEXTURES.

#pragma region ========================== CVARS ==========================

// Same extern set as gl3_Local.h (registered in R_Register(), vk_Main.c).
// Cvar pointer variables marked "engine global" below the list live in the
// engine executable and are resolved at dlopen (see gl1_Main.c externs).

extern cvar_t* r_norefresh;
extern cvar_t* r_fullbright;
extern cvar_t* r_drawentities;
extern cvar_t* r_drawworld;
extern cvar_t* r_novis;
extern cvar_t* r_nocull;
extern cvar_t* r_lerpmodels;
extern cvar_t* r_vsync; // YQ2
extern cvar_t* r_anisotropic; // YQ2
extern cvar_t* r_msaa_samples; // YQ2

extern cvar_t* r_lightlevel;

extern cvar_t* r_farclipdist;
extern cvar_t* r_fog;
extern cvar_t* r_fog_mode;
extern cvar_t* r_fog_density;
extern cvar_t* r_fog_startdist;
extern cvar_t* r_fog_lightmap_adjust;
extern cvar_t* r_fog_underwater;
extern cvar_t* r_fog_underwater_lightmap_adjust;
extern cvar_t* r_frameswap;
extern cvar_t* r_references;

extern cvar_t* gl_noartifacts;

extern cvar_t* gl_modulate;
extern cvar_t* gl_lightmap;
extern cvar_t* gl_dynamic;
extern cvar_t* gl_nobind; // unused in vk
extern cvar_t* gl_showtris; // unused in vk
extern cvar_t* gl_flashblend;
extern cvar_t* gl_texturemode;
extern cvar_t* gl_lockpvs;
extern cvar_t* gl_minlight; // YQ2

extern cvar_t* gl_drawflat;
extern cvar_t* gl_trans33;
extern cvar_t* gl_trans66;
extern cvar_t* gl_bookalpha;

extern cvar_t* gl_drawbuffer; // unused in vk (registered for config compat)
extern cvar_t* gl_saturatelighting;
extern cvar_t* gl_clear; // Maps to VK_ATTACHMENT_LOAD_OP_CLEAR on the render pass color attachments.

// vk-specific (CONTRACT.md):
extern cvar_t* vk_device_idx;		// Registered as "vk_device": preferred physical device index, -1 = auto.
									// (The C variable keeps yq2's name - 'vk_device' is the qvkdevice_t global below.)
extern cvar_t* vk_validation;		// 0 = off, 1 = validation layers, 2 = + best practices / verbose.
extern cvar_t* r_underwater_warp;	// Underwater screen distortion strength (world_warp pass), default 1.

extern cvar_t* vid_gamma;		// engine global
extern cvar_t* vid_brightness;	// engine global
extern cvar_t* vid_contrast;	// engine global

extern cvar_t* vid_ref;			// engine global

extern cvar_t* vid_mode;					// engine global
extern cvar_t* menus_active;				// engine global
extern cvar_t* cl_camera_under_surface;		// engine global
extern cvar_t* quake_amount;				// engine global

#pragma endregion

#pragma region ========================== BACKEND TYPES (from yq2remaster vk header/util.h + qvk.h) ==========================

// --- util.h types ---

typedef struct BufferResource_s
{
	VkBuffer buffer;
	VkDeviceMemory memory;			// Shared memory used for buffer.
	VkDeviceSize size;
	VkDeviceSize offset;			// Position in shared memory.
	VkBool32 is_mapped;
	VkMemoryPropertyFlags flags;
} BufferResource_t;

typedef struct ImageResource_s
{
	VkImage image;
	VkDeviceMemory memory;			// Shared memory used for image.
	VkDeviceSize size;
	VkDeviceSize offset;			// Position in shared memory.
} ImageResource_t;

// --- qvk.h types ---

// Vulkan device
typedef struct
{
	VkPhysicalDevice physical;
	VkDevice logical;
	VkPhysicalDeviceMemoryProperties mem_properties;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;
	VkQueue gfxQueue;
	VkQueue presentQueue;
	VkQueue transferQueue;
	int gfxFamilyIndex;
	int presentFamilyIndex;
	int transferFamilyIndex;
	qboolean screenshotSupported;
} qvkdevice_t;

// Vulkan swapchain
typedef struct
{
	VkSwapchainKHR sc;
	VkFormat format;
	VkPresentModeKHR presentMode;
	VkExtent2D extent;
	VkImage* images;
	int imageCount;
} qvkswapchain_t;

// Available sampler types.
typedef enum
{
	S_NEAREST = 0,
	S_LINEAR = 1,
	S_MIPMAP_NEAREST = 2,
	S_MIPMAP_LINEAR = 3,
	S_NEAREST_UNNORMALIZED = 4,
	S_SAMPLER_CNT = 5
} qvksampler_t;

#define NUM_SAMPLERS (S_SAMPLER_CNT * 2) // Repeat + clamp-to-edge variants.

// Texture object.
typedef struct
{
	ImageResource_t resource;

	VkImageView imageView;
	VkSharingMode sharingMode;
	VkSampleCountFlagBits sampleCount;
	VkFormat format;
	VkDescriptorSet descriptorSet;
	uint32_t mipLevels;
	qboolean clampToEdge;
} qvktexture_t;

#define QVVKTEXTURE_INIT { \
	.resource = { \
		.image = VK_NULL_HANDLE, \
		.memory = VK_NULL_HANDLE, \
		.size = 0, \
	}, \
	.imageView = VK_NULL_HANDLE, \
	.sharingMode = VK_SHARING_MODE_MAX_ENUM, \
	.sampleCount = VK_SAMPLE_COUNT_1_BIT, \
	.format = VK_FORMAT_R8G8B8A8_UNORM, \
	.descriptorSet = VK_NULL_HANDLE, \
	.mipLevels = 1, \
}

#define QVVKTEXTURE_CLEAR(i) { \
	(i).resource.image = VK_NULL_HANDLE; \
	(i).resource.memory = VK_NULL_HANDLE; \
	(i).resource.size = 0; \
	(i).imageView = VK_NULL_HANDLE; \
	(i).sharingMode = VK_SHARING_MODE_MAX_ENUM; \
	(i).sampleCount = VK_SAMPLE_COUNT_1_BIT; \
	(i).format = VK_FORMAT_R8G8B8A8_UNORM; \
	(i).mipLevels = 1; \
}

// Vulkan renderpass
typedef struct
{
	VkRenderPass rp;
	VkAttachmentLoadOp colorLoadOp;
	VkSampleCountFlagBits sampleCount;
} qvkrenderpass_t;

// Vulkan buffer
typedef struct
{
	VkDeviceSize currentOffset;

	BufferResource_t resource;
	void* pMappedData;
} qvkbuffer_t;

// Vulkan staging buffer
typedef struct
{
	VkDeviceSize currentOffset;
	VkCommandBuffer cmdBuffer;
	VkFence fence;
	qboolean submitted;

	BufferResource_t resource;
	void* pMappedData;
} qvkstagingbuffer_t;

// Vulkan buffer options
typedef struct
{
	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags reqMemFlags;
	VkMemoryPropertyFlags prefMemFlags;
} qvkbufferopts_t;

// Vulkan pipeline
typedef struct
{
	VkPipelineLayout layout;
	VkPipeline pl;
	VkPipelineCreateFlags flags;
	VkCullModeFlags cullMode;
	VkPrimitiveTopology topology;
	VkPipelineColorBlendAttachmentState blendOpts;
	VkBool32 depthTestEnable;
	VkBool32 depthWriteEnable;
} qvkpipeline_t;

// Vulkan shader
typedef struct
{
	VkPipelineShaderStageCreateInfo createInfo;
	VkShaderModule module;
} qvkshader_t;

#define QVKPIPELINE_INIT { \
	.layout = VK_NULL_HANDLE, \
	.pl = VK_NULL_HANDLE, \
	.flags = 0, \
	.cullMode = VK_CULL_MODE_BACK_BIT, \
	.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, \
	.blendOpts = { \
		.blendEnable = VK_FALSE, \
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, \
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, \
		.colorBlendOp = VK_BLEND_OP_ADD, \
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, \
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, \
		.alphaBlendOp = VK_BLEND_OP_ADD, \
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT \
	}, \
	.depthTestEnable = VK_TRUE, \
	.depthWriteEnable = VK_TRUE \
}

// Renderpass type. CONTRACT.md: RP_WORLD (offscreen color+depth+msaa resolve),
// RP_WORLD_WARP (fullscreen underwater distortion), RP_UI (swapchain).
typedef enum
{
	RP_WORLD = 0,		// Renders game world to offscreen buffer.
	RP_UI = 1,			// Renders UI elements / console / menus / books to the swapchain.
	RP_WORLD_WARP = 2,	// Postprocessing on RP_WORLD output (underwater screen warp).
	RP_COUNT = 3
} qvkrenderpasstype_t;

// Vulkan constants: command and dynamic buffer count = 2 frames in flight,
// matching vkQuake / yquake2remaster / Quake3e. The earlier bump to 3 let the CPU
// run 3 frames ahead of the GPU, loosening the per-frame pacing so cl.time got
// sampled unevenly and uniform movers (hive doors) juddered. At 2 the pacing is
// tight: measured DEAD-EVEN 16.67ms/60fps under MAILBOX (r_vsync 0) on morb's
// hybrid laptop - the confirmed smooth mode there. (FIFO also vblank-gates at 2,
// but on a hybrid/PRIME GPU its blocking cross-GPU flip stalls to chaotic
// half-rate - use MAILBOX on such setups; see vk_swapchain.c.) Keep NUM_DYNBUFFERS
// == NUM_CMDBUFFERS: a dynamic buffer must not be reused while a frame that
// references it is still in flight (BeginFrame waits on the fence NUM_CMDBUFFERS ago).
#define NUM_CMDBUFFERS 2
#define NUM_DYNBUFFERS 2
// Vulkan constants: number of image semaphores (introduced with VulkanSDK 1.3.275).
#define NUM_IMG_SEMAPHORES (NUM_CMDBUFFERS * 2)

// ---------------------------------------------------------------------------
// Push constant layout shared by ALL pipelines (QVk_CreatePipeline() declares
// one vertex range + one fragment range on every pipeline layout):
//
// Vertex stage, offset 0, 17 floats:
//   [0..15] mat4 mvpMatrix / vpMatrix
//   [16]    float alpha (sprite.vert)
//
// Fragment stage, offset 68 (17 * 4), 11 floats. Per-shader views:
//   basic.frag / basic_color_quad.frag / polygon_lmap.frag (graded in-shader):
//     68 gamma / 72 brightness / 76 contrast (H2ColorGrade trio)
//     80 alphaTestRef (discard when a <= ref; < 0 disables;
//        gl1 glAlphaFunc(GL_GREATER, x): 0.666 world, 0.05 UI/sprites, 0.0 additive)
//   model.frag (graded in-shader too, gl3 parity):
//     68 gamma / 72 brightness / 76 contrast / 80 alphaTestRef
//   postprocess.frag (world -> swapchain blit, no grade - RP_WORLD grades per-fragment):
//     80 scrWidth / 84 scrHeight / 88 offsetX / 92 offsetY (68..76 unused)
//   world_warp.frag (underwater distortion, keyed to r_underwater_warp):
//     68 time / 72 intensity / 76 scrWidth / 80 scrHeight / 84 offsetX / 88 offsetY /
//     92 refdefX / 96 refdefY / 100 refdefWidth / 104 refdefHeight
// ---------------------------------------------------------------------------
#define PUSH_CONSTANT_VERTEX_SIZE 17
#define PUSH_CONSTANT_FRAGMENT_SIZE 11

#pragma endregion

#pragma region ========================== IMAGES ==========================

typedef enum //mxd. Changed in H2
{
	it_skin = 1,
	it_sprite = 2,
	it_wall = 4,
	it_pic = 5,
	it_sky = 6
} imagetype_t;

// Ported VERBATIM from gl1_Local.h / gl3_Local.h (H2 image struct), except the
// GL texture name (texnum) is replaced by the Vulkan texture object handle.
typedef struct image_s //mxd. Changed in H2. Original size: 104 bytes
{
	struct image_s* next;
	char name[MAX_QPATH];				// Game path, including extension.
	imagetype_t type;
	int width;
	int height;
	int registration_sequence;			// 0 = free
	struct msurface_s* texturechain;	// For sort-by-texture world drawing.
	struct msurface_s* multitexturechain;
	qvktexture_t vk_texture;			// Vulkan texture handle (gl1/gl3: int texnum).
	byte has_alpha;
	byte num_frames;
	struct paletteRGB_s* palette;		// .M8 palette.
} image_t;

extern image_t vktextures[MAX_VKTEXTURES];
extern int numvktextures;

extern image_t* r_notexture;
extern image_t* r_particletexture;
extern image_t* r_aparticletexture;
extern image_t* r_reflecttexture;
extern image_t* r_font1;
extern image_t* r_font2;

#pragma endregion

#pragma region ========================== VK BACKEND STATE ==========================

typedef enum
{
	RSERR_OK,
	RSERR_INVALID_MODE
} rserr_t;

// Lightmap atlas config, gl3 parity: 4 big atlases with 4 lightstyle sub-lightmaps each.
enum
{
	BLOCK_WIDTH = 1024,
	BLOCK_HEIGHT = 512,
	LIGHTMAP_BYTES = 4,
	MAX_LIGHTMAPS = 4,
	MAX_LIGHTMAPS_PER_SURFACE = 4 // == MAXLIGHTMAPS from qfiles.h.
}; //mxd. Moved from gl3_Local.h

// Vertex layout used for brush surfaces (world geometry): 10 floats + 1 uint.
// Identical to gl3's gl3_3D_vtx_t; matches the MEM_VERTEX_T vertex input state
// in vk_common.c CreatePipelines() and polygon*/skybox vertex shader inputs.
typedef struct vk_3D_vtx_s
{
	vec3_t pos;
	float texCoord[2];
	float lmTexCoord[2];	// Lightmap texture coordinate (sometimes unused).
	vec3_t normal;
	uint32_t lightFlags;	// Bit i set means: dynlight i affects surface.
} vk_3D_vtx_t;

// Vertex layout used for flexmodels/sprites/particle quads: 9 floats
// (X, Y, Z), (R, G, B, A), (S, T) - matches the RGB_RGBA_RG vertex input state
// and model.vert/particle.vert inputs (yq2 attribute order: pos, color, st).
typedef struct vk_alias_vtx_s
{
	float pos[3];
	float color[4];
	float texCoord[2];
} vk_alias_vtx_t;

// ---------------------------------------------------------------------------
// Per-draw UBO data structs for the H2-modified world/model shaders.
// The GLSL blocks use std140, but every member below is either a scalar
// (4-byte aligned in std140) or a vec4/mat4 (16-byte aligned at 16-byte
// offsets here), so a plain packed C struct matches the GLSL layout exactly.
// ---------------------------------------------------------------------------

// Fog block appended to the world/model per-draw UBOs - gl3 uni3D fog members
// (gl1 R_Fog()/R_WaterFog()/R_BlendLightmaps() semantics; see gl3_Shaders.c).
typedef struct
{
	float fogColor[4];			// r_fog_color_* / r_fog_underwater_color_* cvars.
	int32_t fogMode;			// -1 = fog off, 0 = GL_LINEAR, 1 = GL_EXP, 2 = GL_EXP2 (gl1 fog_modes[]).
	float fogDensity;			// r_fog_density / r_fog_underwater_density.
	float fogStart;				// r_fog_startdist / r_fog_underwater_startdist (linear mode).
	float fogEnd;				// r_farclipdist (linear mode).
	float fogLightmapAdjust;	// r_fog_lightmap_adjust: scales start/end/density for the lightmap term.
	int32_t fogSkipAdditive;	// 1 = suppress fog for the current (additive) draw (gl1 glDisable(GL_FOG) around aparticles).
} vkfogblock_t;

// polygon_lmap.vert/.frag UBO (set 1, binding 0).
typedef struct
{
	float model[16];			// Per-entity model matrix.
	float lmScales[4][4];		// H2 lightstyle scales for the 4 lightmap samplers (gl3 si3Dlm lmScales).
	vkfogblock_t fog;
	float viewLightmaps;		// 1.0 = gl_lightmap (draw lightmaps only).
} vklmapubo_t;

// model.vert/.frag UBO (set 1, binding 0).
typedef struct
{
	float model[16];			// Per-entity model matrix.
	vkfogblock_t fog;
	int32_t textured;			// 0 = flat per-vertex color only.
} vkmodelubo_t;

typedef struct
{
	uint32_t vk_version;
	const char* vendor_name;
	const char* device_type;
	const char* present_mode;
	const char* supported_present_modes[256];
	const char* extensions[256];
	const char* layers[256];
	uint32_t vertex_buffer_usage;
	uint32_t vertex_buffer_max_usage;
	uint32_t vertex_buffer_size;
	uint32_t index_buffer_usage;
	uint32_t index_buffer_max_usage;
	uint32_t index_buffer_size;
	uint32_t uniform_buffer_usage;
	uint32_t uniform_buffer_max_usage;
	uint32_t uniform_buffer_size;
} vkconfig_t;

typedef struct
{
	qboolean fullscreen;

	int prev_mode;

	// Each lightmap atlas consists of MAX_LIGHTMAPS_PER_SURFACE style
	// sub-lightmaps (H2 lightstyles) - filled by the vk_Lightmap.c module port.
	qvktexture_t lightmap_textures[MAX_LIGHTMAPS][MAX_LIGHTMAPS_PER_SURFACE];

	VkPipeline current_pipeline;
	qvkrenderpasstype_t current_renderpass;
} vkstate_t;

extern vkconfig_t vk_config;
extern vkstate_t vk_state;

// Vulkan instance
extern VkInstance vk_instance;
// Vulkan surface
extern VkSurfaceKHR vk_surface;
// Vulkan device
extern qvkdevice_t vk_device;
// Vulkan swapchain
extern qvkswapchain_t vk_swapchain;
// Vulkan command buffer currently in use
extern VkCommandBuffer vk_activeCmdbuffer;
// Vulkan command pools
extern VkCommandPool vk_commandPool[NUM_CMDBUFFERS];
extern VkCommandPool vk_transferCommandPool;
// Vulkan descriptor pool
extern VkDescriptorPool vk_descriptorPool;
// Viewport/scissor
extern VkViewport vk_viewport;
extern VkRect2D vk_scissor;

// Vulkan descriptor set layouts
extern VkDescriptorSetLayout vk_uboDescSetLayout;
extern VkDescriptorSetLayout vk_samplerDescSetLayout;
extern VkDescriptorSetLayout vk_samplerLightmapDescSetLayout; // H2: 4 combined image samplers in one binding (lightstyles).

// *** pipelines ***
extern qvkpipeline_t vk_drawTexQuadPipeline[RP_COUNT];		// 2D textured (HUD, console, menus, books, cinematics).
extern qvkpipeline_t vk_drawTexQuadTintedPipeline[RP_COUNT];// 2D textured + tint (DrawChar color, DrawPic alpha).
extern qvkpipeline_t vk_drawColorQuadPipeline[RP_COUNT];	// 2D flat color (Draw_Fill, Draw_FadeScreen, screen flash).
extern qvkpipeline_t vk_drawModelPipelineFan[RP_COUNT];		// Flexmodels (opaque).
extern qvkpipeline_t vk_drawNoDepthModelPipelineFan;		// Flexmodels (translucent, no depth write).
extern qvkpipeline_t vk_drawLefthandModelPipelineFan;		// Flexmodels (front-face culled).
// Flexmodel/sprite transparency matrix (R_HandleTransparency() state matrix -
// see vk_Entity_internal.h / QVk_SelectEntityPipeline()). Indexed
// [depth_test_disabled][vk_entityBlendMode - 1]:
//   [*][0] standard (SRC_ALPHA/ONE_MINUS_SRC_ALPHA), [*][1] additive (ONE/ONE),
//   [*][2] additive-alpha (SRC_ALPHA/ONE); row [1] = RF_NODEPTHTEST (depth test off).
// Row [0] keeps depth writes ON (gl1 never masks depth for entities).
extern qvkpipeline_t vk_drawModelBlendPipelines[2][3];
extern qvkpipeline_t vk_drawNullModelPipeline;				// R_DrawNullModel().
extern qvkpipeline_t vk_drawParticlesPipeline;				// H2 particles: textured atlas quads.
extern qvkpipeline_t vk_drawAParticlesPipeline;				// H2 aparticles: additive (ONE/ONE) atlas quads, no depth write.
extern qvkpipeline_t vk_drawPointParticlesPipeline;			// yq2 point-sprite particles (unused by H2 - CONTRACT: atlas quads; kept mechanical).
extern qvkpipeline_t vk_drawSpritePipeline;					// Sprites.
extern qvkpipeline_t vk_drawSpriteFlaresPipeline;			// Additive sprites.
extern qvkpipeline_t vk_drawPolyPipeline;					// World faces without lightmap (trans33/66 etc.).
extern qvkpipeline_t vk_drawPolyLmapPipeline;				// Opaque world faces with 4-sampler lightmap.
extern qvkpipeline_t vk_drawPolyWarpPipeline;				// Warped surfaces (water, lava, ...), blended.
extern qvkpipeline_t vk_drawPolySolidWarpPipeline;			// Warped surfaces, solid.
extern qvkpipeline_t vk_drawBeamPipeline;					// Beams (H2: line sprites candidate).
extern qvkpipeline_t vk_drawSkyboxPipeline;					// Skybox.
extern qvkpipeline_t vk_drawDLightPipeline;					// gl_flashblend dlight blobs.
extern qvkpipeline_t vk_showTrisPipeline;					// Debug (unused in H2 - gl_showtris is registered-but-ignored).
extern qvkpipeline_t vk_shadowsPipelineFan;					// Entity shadows (unused by H2 gl1 semantics; kept mechanical).
extern qvkpipeline_t vk_worldWarpPipeline;					// RP_WORLD_WARP fullscreen pass.
extern qvkpipeline_t vk_postprocessPipeline;				// RP_UI world blit + H2 color grade.

// Color buffer containing main game/world view.
extern qvktexture_t vk_colorbuffer;
// Color buffer with postprocessed game view.
extern qvktexture_t vk_colorbufferWarp;
// Indicator if the frame is currently being rendered.
extern qboolean vk_frameStarted;
// Indicates if the swapchain needs to be rebuilt.
extern qboolean vk_recreateSwapchainNeeded;
// Is QVk initialized?
extern qboolean vk_initialized;
// Index of the currently acquired swapchain image.
extern uint32_t vk_imageIndex;
// Index of active command buffer.
extern int vk_activeBufferIdx;

// Debug/validation function pointers (loaded in vk_SDL.c when vk_validation is set).
extern PFN_vkCreateDebugUtilsMessengerEXT qvkCreateDebugUtilsMessengerEXT;
extern PFN_vkDestroyDebugUtilsMessengerEXT qvkDestroyDebugUtilsMessengerEXT;
extern PFN_vkSetDebugUtilsObjectNameEXT qvkSetDebugUtilsObjectNameEXT;
extern PFN_vkSetDebugUtilsObjectTagEXT qvkSetDebugUtilsObjectTagEXT;
extern PFN_vkCmdBeginDebugUtilsLabelEXT qvkCmdBeginDebugUtilsLabelEXT;
extern PFN_vkCmdEndDebugUtilsLabelEXT qvkCmdEndDebugUtilsLabelEXT;
extern PFN_vkCmdInsertDebugUtilsLabelEXT qvkInsertDebugUtilsLabelEXT;
extern PFN_vkCreateDebugReportCallbackEXT qvkCreateDebugReportCallbackEXT;
extern PFN_vkDestroyDebugReportCallbackEXT qvkDestroyDebugReportCallbackEXT;

#pragma endregion

#pragma region ========================== HELPER MACROS ==========================

// Verify that a VkResult is VK_SUCCESS (yq2 VK_VERIFY, ri.Con_Printf flavor).
#define VK_VERIFY(x) { \
	const VkResult vk_verify_res = (x); \
	if (vk_verify_res != VK_SUCCESS) { \
		ri.Con_Printf(PRINT_ALL, "%s:%d: VkResult verification failed: %s\n", \
			__func__, __LINE__, QVk_GetError(vk_verify_res)); \
	} \
}

// Out-of-memory guard (yq2 YQ2_COM_CHECK_OOM equivalent; ri.Sys_Error() never returns).
#define VK_CHECK_OOM(ptr, what) { \
	if ((ptr) == NULL) \
		ri.Sys_Error(ERR_FATAL, "%s: out of memory (%s)", __func__, (what)); \
}

// Round 'a' up to a multiple of 'b' (b does not need to be a power of two).
#define ROUNDUP(a, b) ((((a) + (b) - 1) / (b)) * (b))

static inline uint32_t NextPow2(uint32_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;

	return v + 1;
}

#pragma endregion

#pragma region ========================== GLOBALS (gl1 parity) ==========================

extern refdef_t r_newrefdef;

extern int r_framecount;

extern vec3_t vup;
extern vec3_t vpn;
extern vec3_t vright;
extern vec3_t r_origin;

extern int c_brush_polys;
extern int c_alias_polys;

// vid_gamma / vid_brightness / vid_contrast snapshot, refreshed in
// RI_BeginFrame() - pushed into the fragment shaders (H2ColorGrade trio)
// by the 2D batcher (vk_common.c) and the postprocess blit (vk_Main.c).
extern float vk_gradePush[3];

#pragma endregion

#pragma region ========================== CROSS-FILE PROTOTYPES ==========================

struct model_s; // Opaque until the vk_Model.c module port lands.

// --- vk_common.c (The Interface Functions (tm)) ---
extern qboolean QVk_CreateInstance(const char* const* sdlExtensions, uint32_t sdlExtensionCount);
extern qboolean QVk_Init(void);
extern void QVk_WaitAndShutdownAll(void);
extern VkFormat QVk_FindDepthFormat(void);
extern VkResult QVk_CreateImageView(const VkImage* image, VkImageAspectFlags aspectFlags, VkImageView* imageView, VkFormat format, uint32_t mipLevels);
extern VkResult QVk_CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, qvktexture_t* texture);
extern void QVk_CreateDepthBuffer(VkSampleCountFlagBits sampleCount, qvktexture_t* depthBuffer);
extern void QVk_CreateColorBuffer(VkSampleCountFlagBits sampleCount, qvktexture_t* colorBuffer, int extraFlags);
extern VkSampler QVk_UpdateTextureSampler(qvktexture_t* texture, qvksampler_t samplerType, qboolean clampToEdge);
extern const char* QVk_GetError(VkResult errorCode);
extern VkResult QVk_BeginFrame(const VkViewport* viewport, const VkRect2D* scissor);
extern VkResult QVk_EndFrame(qboolean force);
extern void QVk_BeginRenderpass(qvkrenderpasstype_t rpType);
extern qboolean QVk_RecreateSwapchain(void);
extern uint8_t* QVk_GetVertexBuffer(VkDeviceSize size, VkBuffer* dstBuffer, VkDeviceSize* dstOffset);
extern uint8_t* QVk_GetUniformBuffer(VkDeviceSize size, uint32_t* dstOffset, VkDescriptorSet* dstUboDescriptorSet);
extern uint8_t* QVk_GetStagingBuffer(VkDeviceSize size, int alignment, VkCommandBuffer* cmdBuffer, VkBuffer* buffer, uint32_t* dstOffset);
extern VkBuffer* UpdateIndexBuffer(const uint16_t* data, VkDeviceSize bufferSize, VkDeviceSize* dstOffset);
extern void QVk_Draw2DCallsRender(void);
extern void QVk_DrawColorRect(float x, float y, float w, float h, float r, float g, float b, float a, qvkrenderpasstype_t rpType);
extern void QVk_DrawTexRect(float x, float y, float w, float h, float u, float v, float us, float vs, const qvktexture_t* texture);
extern void QVk_DrawTexRectTinted(float x, float y, float w, float h, float u, float v, float us, float vs, float r, float g, float b, float a, const qvktexture_t* texture);
extern void QVk_BindPipeline(qvkpipeline_t* pipeline);
extern void QVk_SubmitStagingBuffers(void);
extern qboolean QVk_CheckExtent(void);

// --- vk_device.c ---
extern qboolean QVk_CreateDevice(int preferredDeviceIdx);
extern void QVk_DebugSetObjectName(uint64_t obj, VkObjectType objType, const char* objName);
extern void QVk_DebugSetObjectTag(uint64_t obj, VkObjectType objType, uint64_t tagName, size_t tagSize, const void* tagData);
extern void QVk_DebugLabelBegin(const VkCommandBuffer* cmdBuffer, const char* labelName, float r, float g, float b);
extern void QVk_DebugLabelEnd(const VkCommandBuffer* cmdBuffer);
extern void QVk_DebugLabelInsert(const VkCommandBuffer* cmdBuffer, const char* labelName, float r, float g, float b);

// --- vk_swapchain.c ---
extern VkResult QVk_CreateSwapchain(void);

// --- vk_cmd.c ---
extern VkResult QVk_BeginCommand(const VkCommandBuffer* commandBuffer);
extern void QVk_SubmitCommand(const VkCommandBuffer* commandBuffer, const VkQueue* queue);
extern VkResult QVk_CreateCommandPool(VkCommandPool* commandPool, uint32_t queueFamilyIndex);
extern VkCommandBuffer QVk_CreateCommandBuffer(const VkCommandPool* commandPool, VkCommandBufferLevel level);

// --- vk_buffer.c ---
extern void QVk_FreeBuffer(qvkbuffer_t* buffer);
extern void QVk_FreeStagingBuffer(qvkstagingbuffer_t* buffer);
extern VkResult QVk_CreateStagingBuffer(VkDeviceSize size, qvkstagingbuffer_t* dstBuffer, VkMemoryPropertyFlags reqMemFlags, VkMemoryPropertyFlags prefMemFlags);
extern VkResult QVk_CreateUniformBuffer(VkDeviceSize size, qvkbuffer_t* dstBuffer, VkMemoryPropertyFlags reqMemFlags, VkMemoryPropertyFlags prefMemFlags);
extern void QVk_CreateVertexBuffer(const void* data, VkDeviceSize size, qvkbuffer_t* dstBuffer, VkMemoryPropertyFlags reqMemFlags, VkMemoryPropertyFlags prefMemFlags);
extern void QVk_CreateIndexBuffer(const void* data, VkDeviceSize size, qvkbuffer_t* dstBuffer, VkMemoryPropertyFlags reqMemFlags, VkMemoryPropertyFlags prefMemFlags);

// --- vk_pipeline.c ---
extern qvkshader_t QVk_CreateShader(const uint32_t* shaderSrc, size_t shaderCodeSize, VkShaderStageFlagBits shaderStage);
extern void QVk_CreatePipeline(const VkDescriptorSetLayout* descriptorLayout, uint32_t descLayoutCount, const VkPipelineVertexInputStateCreateInfo* vertexInputInfo,
	qvkpipeline_t* pipeline, const qvkrenderpass_t* renderpass, const qvkshader_t* shaders, uint32_t shaderCount);
extern void QVk_DestroyPipeline(qvkpipeline_t* pipeline);

// --- vk_validation.c ---
extern void QVk_CreateValidationLayers(void);
extern void QVk_DestroyValidationLayers(void);

// --- vk_util.c ---
extern VkResult buffer_create(BufferResource_t* buf, VkBufferCreateInfo buf_create_info,
	VkMemoryPropertyFlags mem_properties, VkMemoryPropertyFlags mem_preferences, VkMemoryPropertyFlags mem_skip);
extern VkResult buffer_destroy(BufferResource_t* buf);
extern void buffer_unmap(BufferResource_t* buf);
extern void* buffer_map(BufferResource_t* buf);
extern VkResult buffer_flush(const BufferResource_t* buf);
extern VkResult buffer_invalidate(const BufferResource_t* buf);
extern VkResult image_create(ImageResource_t* img, VkImageCreateInfo img_create_info,
	VkMemoryPropertyFlags mem_properties, VkMemoryPropertyFlags mem_preferences, VkMemoryPropertyFlags mem_skip);
extern VkResult image_destroy(ImageResource_t* img);
extern void vulkan_memory_init(void);
extern void vulkan_memory_types_show(void);
extern void vulkan_memory_free_unused(void);
extern void vulkan_memory_delete(void);

// --- vk_SDL.c ---
extern void RI_EndFrame(void);
extern qboolean RI_InitContext(void* win);
extern void RI_ShutdownContext(void);
extern int RI_PrepareForWindow(void);
extern void R_SetVsync(void);
extern void QVk_GetDrawableSize(int* width, int* height);

// --- vk_Main.c ---
// Ends the world render pass: RP_WORLD -> RP_WORLD_WARP (underwater distortion)
// -> RP_UI. Idempotent per frame. H2R's API has no EndWorldRenderpass export -
// RI_RenderFrame() calls this internally after the 3D flow (CONTRACT.md).
extern qboolean R_EndWorldRenderpass(void);
extern void R_ScreenShot_f(void);
extern void Vk_Strings_f(void);

// ---------------------------------------------------------------------------
// Module functions. During the foundation pass ALL of these are provided as
// no-ops by vk_Stubs.c; the module ports (vk_Image.c, vk_Draw.c, ...) will
// replace them one by one. Prototypes mirror gl1_*.h / gl3_Local.h.
// ---------------------------------------------------------------------------

// --- vk_Model.c (stubbed) ---
extern void RI_BeginRegistration(const char* map);
extern struct model_s* RI_RegisterModel(const char* name);
extern void RI_EndRegistration(void);
extern int RI_GetReferencedID(const struct model_s* model);
extern void Mod_Init(void);
extern void Mod_FreeAll(void);
extern void Mod_Modellist_f(void);

// --- vk_Image.c (stubbed) ---
extern void R_InitImages(void);
extern void R_ShutdownImages(void);
extern void R_ImageList_f(void);
extern void R_TextureMode(const char* string);
extern struct image_s* RI_RegisterSkin(const char* name, qboolean* retval);

// --- vk_Draw.c (stubbed) ---
extern void Draw_InitLocal(void);
extern void ShutdownFonts(void);
extern image_t* Draw_FindPic(const char* name);
extern void Draw_GetPicSize(int* w, int* h, const char* name);
extern void Draw_Pic(int x, int y, int scale, const char* name, float alpha);
extern void Draw_StretchPic(int x, int y, int w, int h, const char* name, float alpha, DrawStretchPicScaleMode_t mode);
extern void Draw_Char(int x, int y, int scale, int c, paletteRGBA_t color, qboolean draw_shadow);
extern void Draw_TileClear(int x, int y, int w, int h, const char* name);
extern void Draw_Fill(int x, int y, int w, int h, paletteRGBA_t color);
extern void Draw_FadeScreen(paletteRGBA_t color);
extern void Draw_Name(const vec3_t origin, const char* name, paletteRGBA_t color);

// --- vk_DrawBook.c (stubbed) ---
extern void Draw_BigFont(int x, int y, const char* text, float alpha);
extern int BF_Strlen(const char* text);
extern void Draw_BookPic(const char* name, float scale, float alpha);

// --- vk_DrawCinematic.c (stubbed) ---
extern void Draw_InitCinematic(int width, int height);
extern void Draw_CloseCinematic(void);
extern void Draw_Cinematic(const byte* data, const paletteRGB_t* palette);
extern void Draw_InitCinematicRGBA(int width, int height);
extern void Draw_CinematicRGBA(const byte* rgba);

// --- vk_Sky.c (stubbed) ---
extern void RI_SetSky(const char* name, float rotate, const vec3_t axis);

#ifdef _DEBUG
// --- vk_Debug.c (stubbed) --- //mxd. Debug draw logic.
extern void RI_AddDebugBox(const vec3_t center, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugBbox(const vec3_t mins, const vec3_t maxs, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugEntityBbox(const edict_t* ent, paletteRGBA_t color);
extern void RI_AddDebugLabel(const vec3_t origin, paletteRGBA_t color, float lifetime, const char* label);
extern void RI_AddDebugEntityLabel(const edict_t* ent, paletteRGBA_t color, const char* label);
extern void RI_AddDebugLine(const vec3_t start, const vec3_t end, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugArrow(const vec3_t start, const vec3_t end, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugDirection(const vec3_t start, const vec3_t direction, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugAngles(const vec3_t start, const vec3_t angles, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugAnglesRad(const vec3_t start, const vec3_t angles, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugMarker(const vec3_t center, float size, paletteRGBA_t color, float lifetime);
extern void R_FreeDebugPrimitives(void);
#endif

#pragma endregion

#pragma region ========================== IMPORTED FUNCTIONS ==========================

extern refimport_t ri;

#pragma endregion
