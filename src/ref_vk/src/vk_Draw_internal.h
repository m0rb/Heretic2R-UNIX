//
// vk_Draw_internal.h -- internals shared by the 2D/UI module files
// (vk_Draw.c, vk_DrawBook.c, vk_DrawCinematic.c). Not for other modules.
//
// Mirror of gl3_Draw_internal.h on the vk backend.
//
// Copyright 1998 Raven Software
//

#pragma once

#include "vk_Local.h"

// H2. Font character definition struct (ported from gl1_Draw.h).
typedef struct glxy_s
{
	float xl;
	float yt;
	float xr;
	float yb;
	int w;
	int h;
	int baseline;
} glxy_t;

extern glxy_t* font1; // H2
extern glxy_t* font2; // H2

extern image_t* draw_chars;

// --- vk_Draw.c ---

// Ensures 2D drawing can start: forces the RP_WORLD -> RP_WORLD_WARP -> RP_UI
// transition (R_EndWorldRenderpass(), idempotent per frame) so the quads land
// in the UI pass even on frames where RI_RenderFrame() never runs (cinematics,
// console/menu-only, loading screens - H2R's API has no EndWorldRenderpass
// export, see CONTRACT.md). Doubles as the vk_frameStarted guard: returns
// false when no frame is in progress (draws must be skipped then, because the
// 2D batcher would capture an invalid renderpass index).
extern qboolean Draw_Begin2D(void);

// Buffers one textured 2D quad (gl3 GL3_Draw_EmitQuad() corner-based
// signature: pixel corners + UV corners) through the batched 2D rect renderer
// (vk_common.c), modulated by 'color' (gl1 glColor4* + GL_MODULATE -> tinted
// 2D pipeline: blending enabled, in-shader 0.05 alpha test + H2ColorGrade).
extern void Draw_EmitQuad(float xl, float yt, float xr, float yb, float sl, float tl, float sh, float th, const float color[4], const image_t* image);

extern void Draw_Render(int x, int y, int w, int h, const image_t* image, float alpha);

// ---------------------------------------------------------------------------
// Cross-module prototypes not yet in vk_Local.h (gl3_Draw_internal.h parity).
// The vk_Image.c module port provides the definitions:
//  - R_FindImage()/R_CreateFallbackTexture(): H2 image lookup/loading -
//    signatures match gl1_Image.h / gl3_Image.c EXACTLY.
//  - QVk_CreateTexture()/QVk_UpdateTextureData()/QVk_ReleaseTexture(): RGBA
//    texture create/stream/destroy - signatures match yq2remaster vk_image.c
//    (header/qvk.h) EXACTLY. Used for the cinematic streaming texture
//    (vk_DrawCinematic.c), always with mipLevels == 1 textures.
// TODO(integration): move these to vk_Local.h (or vk_Image.h) once the image
// module port lands, then drop this block.
// ---------------------------------------------------------------------------

// --- vk_Image.c (see gl1_Image.h) ---
extern image_t* R_FindImage(const char* name, imagetype_t type);
extern image_t* R_CreateFallbackTexture(const char* name, imagetype_t type);

// --- vk_Image.c (see yq2remaster vk_image.c) ---
extern void QVk_CreateTexture(qvktexture_t* texture, const unsigned char* data, uint32_t width, uint32_t height, qvksampler_t samplerType, qboolean clampToEdge);
extern void QVk_UpdateTextureData(qvktexture_t* texture, const unsigned char* data, uint32_t offset_x, uint32_t offset_y, uint32_t width, uint32_t height);
extern void QVk_ReleaseTexture(qvktexture_t* texture, qboolean tosync);
