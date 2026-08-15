//
// vk_Image_internal.h -- image module interface (gl3_Image_internal.h counterpart for ref_vk).
//
// Everything other module ports (draw, world, lightmaps, sky, cinematics, ...) need
// from vk_Image.c beyond the prototypes already declared in vk_Local.h lives here.
//
// Copyright 1998 Raven Software
//

#pragma once

#include "vk_Local.h"

extern int registration_sequence; // Defined in vk_Model.c (gl1: gl1_Model.c).

// gl1's R_Bind() / R_BindImage() / R_MBind() / R_MBindImage() / R_SelectTexture() have no
// vk counterparts: textures bind through per-image descriptor sets at draw submission
// (image->vk_texture.descriptorSet). gl1's R_EnableMultitexture() / R_TexEnv() /
// R_BlendFunc() / R_AlphaFunc() are gone too: texenv/alpha test are shader-side,
// blend state is baked into the pipelines.

extern image_t* R_FindImage(const char* name, imagetype_t type);
extern image_t* R_CreateFallbackTexture(const char* name, imagetype_t type);
extern image_t* R_GetFreeImage(void);

// Cinematic streaming upload path (gl3: R_UploadPaletted(level, ...)/R_UploadRGBA(level, ...)
// operating on the bound texture; vk has no bound texture, so the target image is passed
// explicitly). Creates image->vk_texture on first call, streams into it afterwards.
// NOTE: the sampler is attached at creation (it_pic = S_NEAREST) - do NOT call
// R_SetFilter() per frame on streamed textures (descriptor sets of in-flight frames
// must not be rewritten; gl3's per-frame R_SetFilter(cin_frame) has no vk equivalent).
extern void R_UploadPaletted(image_t* image, const byte* data, const paletteRGB_t* palette, int width, int height);
extern void R_UploadRGBA(image_t* image, const byte* rgba, int width, int height); // Cinematic RGBA upload (MPEG). --morb

extern void R_FreeImageNoHash(image_t* image);
extern void R_FreeUnusedImages(void);
extern void R_SetFilter(image_t* image);
extern void R_DisplayHashTable(void);

// --- Vulkan texture helpers (yq2remaster vk_image.c texture upload path) ---
// Single-level RGBA texture create/update/release - for module-managed qvktexture_t
// objects that aren't image_t slots (lightmap atlases in vk_Lightmap.c, etc.).
// QVk_CreateTexture() allocates the per-texture descriptor set from vk_descriptorPool
// (vk_samplerDescSetLayout) and attaches the requested sampler.
extern void QVk_CreateTexture(qvktexture_t* texture, const byte* data, uint32_t width, uint32_t height, qvksampler_t samplerType, qboolean clampToEdge);
extern void QVk_UpdateTextureData(qvktexture_t* texture, const byte* data, uint32_t offset_x, uint32_t offset_y, uint32_t width, uint32_t height);
// Frees image + view + descriptor set. tosync: submit pending staging uploads and wait
// for device idle first (use when the texture may still be referenced by in-flight frames).
extern void QVk_ReleaseTexture(qvktexture_t* texture, qboolean tosync);

// Swapchain readback (yq2 vk_image.c) - for the future R_ScreenShot_f implementation
// (vk_Main.c currently stubs the "screenshot" command).
extern void QVk_ReadPixels(uint8_t* dstBuffer, const VkOffset2D* offset, const VkExtent2D* extent);

// NOTE: R_InitImages() / R_ShutdownImages() / R_ImageList_f() / R_TextureMode() /
// RI_RegisterSkin() prototypes live in vk_Local.h (module function set).
// gl1's R_InitGammaTable() / R_GammaAffect() / R_RefreshImage() are REMOVED (gl3 parity):
// H2 gamma/brightness/contrast run in the fragment shaders (vk_gradePush push constants),
// textures are uploaded raw and never need re-uploading (vk_Main.c already no-ops the
// vid_textures_refresh_required handshake).
