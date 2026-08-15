#include "compat.h"
//
// vk_DrawCinematic.c -- cinematic frame drawing for the Vulkan renderer.
//
// Ported from gl3_DrawCinematic.c (the validated H2 port of
// gl1_DrawCinematic.c): 8-bit paletted frames are CPU-expanded to RGBA and
// re-uploaded each frame, then blitted integer-scaled + centered on a
// black-filled screen through the 2D pipeline. The RGBA variants (Loki/MPEG
// cinematics) upload caller-provided RGBA frames directly.
//
// Backend deviations from gl3:
//  - The cinematic streaming texture is a module-local image_t wrapper around
//    a qvktexture_t instead of a vktextures[] slot (gl1/gl3 needed an image
//    slot for their GL texnum bookkeeping; the vk texture object is
//    self-contained). Created lazily on the first frame (gl1's
//    glGenTextures-without-upload "fresh context, black screen" equivalence),
//    streamed via QVk_UpdateTextureData() (yq2 RE_Draw_StretchRaw technique).
//  - gl1 R_SetFilter() (it_pic -> GL_NEAREST) becomes the S_NEAREST sampler
//    baked in at texture creation (clamp-to-edge: avoids u/v == 1.0 wrapping).
//  - gl3's R_UploadPaletted() palette -> RGBA expansion is done here
//    (Vulkan has no paletted upload path; QVk_UpdateTextureData() wants RGBA).
//
// Copyright 1998 Raven Software
//

#include "vk_Draw_internal.h"
#include "client/vid.h"

static image_t cin_frame_image;	// Module-local wrapper: only width/height/type/palette/vk_texture are used (not a vktextures[] slot).
static image_t* cin_frame;
static byte* cin_frame_data;	// 8-bit paletted frame scratch buffer.
static byte* cin_frame_rgba;	// CPU-expanded RGBA upload scratch buffer.

// Common Draw_InitCinematic() / Draw_InitCinematicRGBA() setup.
static void InitCinematicImage(const int width, const int height)
{
	memset(&cin_frame_image, 0, sizeof(cin_frame_image));
	QVVKTEXTURE_CLEAR(cin_frame_image.vk_texture); // Sets format = VK_FORMAT_R8G8B8A8_UNORM, mipLevels = 1.

	cin_frame = &cin_frame_image;

	strcpy(cin_frame->name, "*cinematic");
	cin_frame->width = width;
	cin_frame->height = height;
	cin_frame->type = it_pic;
	cin_frame->has_alpha = false;
	// vk: no texture object yet - created lazily on the first uploaded frame
	// (gl1: glGenTextures() without upload -> black screen until then).
}

// Creates or updates the cinematic streaming texture with a ready RGBA frame.
static void UploadCinematicFrame(const byte* rgba)
{
	qvktexture_t* texture = &cin_frame->vk_texture;

	if (texture->resource.image != VK_NULL_HANDLE)
	{
		QVk_UpdateTextureData(texture, rgba, 0, 0, cin_frame->width, cin_frame->height);
	}
	else
	{
		// gl1 R_SetFilter() it_pic parity: nearest filtering.
		QVk_CreateTexture(texture, rgba, cin_frame->width, cin_frame->height, S_NEAREST, true);

		QVk_DebugSetObjectName((uint64_t)texture->resource.image, VK_OBJECT_TYPE_IMAGE, "Image: cinematic frame");
		QVk_DebugSetObjectName((uint64_t)texture->imageView, VK_OBJECT_TYPE_IMAGE_VIEW, "Image View: cinematic frame");
		QVk_DebugSetObjectName((uint64_t)texture->descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Descriptor Set: cinematic frame");
		QVk_DebugSetObjectName((uint64_t)texture->resource.memory, VK_OBJECT_TYPE_DEVICE_MEMORY, "Memory: cinematic frame");
	}
}

void Draw_InitCinematic(const int width, const int height) // H2
{
	InitCinematicImage(width, height);
	cin_frame->palette = malloc(sizeof(paletteRGB_t) * 256);

	cin_frame_data = malloc(width * height);
	cin_frame_rgba = malloc(width * height * 4);
}

void Draw_CloseCinematic(void) // H2
{
	// gl1/gl3: R_FreeImageNoHash(cin_frame) - here the texture is module-local.
	if (cin_frame != NULL)
	{
		if (cin_frame->vk_texture.resource.image != VK_NULL_HANDLE)
			QVk_ReleaseTexture(&cin_frame->vk_texture, true); // tosync: flush staging + wait for in-flight frames.

		free(cin_frame->palette); // Will also free palette (gl1 parity).
		cin_frame->palette = NULL;
		cin_frame = NULL;
	}

	free(cin_frame_data);
	cin_frame_data = NULL;

	free(cin_frame_rgba);
	cin_frame_rgba = NULL;
}

// Black-fill the screen and blit the uploaded cin_frame
// scaled and preserved aspect ratio.
static void Draw_CinematicScaled(void)
{
	Draw_Fill(0, 0, viddef.width, viddef.height, TextPalette[P_BLACK]);

	// gl1 R_SetFilter(cin_frame): nearest filtering - baked into the texture's
	// sampler at creation (UploadCinematicFrame()).

	const int scaler = min(viddef.width / cin_frame->width, viddef.height / cin_frame->height);
	const int w = cin_frame->width * scaler;
	const int h = cin_frame->height * scaler;
	const int x = (int)(roundf((float)(viddef.width - w) * 0.5f));
	const int y = (int)(roundf((float)(viddef.height - h) * 0.5f));

	Draw_Render(x, y, w, h, cin_frame, 1.0f);
}

void Draw_Cinematic(const byte* data, const paletteRGB_t* palette) // H2
{
	// SCR_DrawCinematic() runs right after RI_BeginFrame() with no
	// RI_RenderFrame() - Draw_Begin2D() moves us into the UI pass (and guards
	// against frames that never started - vk_Draw.c).
	if (cin_frame == NULL || !Draw_Begin2D())
		return;

	// Copy frame palette and data.
	memcpy(cin_frame->palette, palette, sizeof(paletteRGB_t) * 256);
	memcpy(cin_frame_data, data, cin_frame->width * cin_frame->height);

	// gl3 R_UploadPaletted() equivalent: CPU-expand the 8-bit frame to RGBA.
	const int num_pixels = cin_frame->width * cin_frame->height;
	const byte* src = cin_frame_data;
	byte* dst = cin_frame_rgba;

	for (int i = 0; i < num_pixels; i++, dst += 4)
	{
		const paletteRGB_t* c = &cin_frame->palette[src[i]];

		dst[0] = c->r;
		dst[1] = c->g;
		dst[2] = c->b;
		dst[3] = 255;
	}

	UploadCinematicFrame(cin_frame_rgba);

	Draw_CinematicScaled();
}

// Full-color cinematic path (MPEG). No palette, no scratch buffer;
// caller hands us a ready RGBA frame each draw. --morb
void Draw_InitCinematicRGBA(const int width, const int height)
{
	InitCinematicImage(width, height);
	cin_frame->palette = NULL; // RGBA: no palette.

	cin_frame_data = NULL;
	cin_frame_rgba = NULL;
}

void Draw_CinematicRGBA(const byte* rgba) // For Loki cinematics --morb
{
	if (cin_frame == NULL || !Draw_Begin2D())
		return;

	UploadCinematicFrame(rgba);

	Draw_CinematicScaled();
}
