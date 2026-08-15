//
// gl3_Draw_internal.h -- internals shared by the 2D/UI module files
// (gl3_Draw.c, gl3_DrawBook.c, gl3_DrawCinematic.c). Not for other modules.
//
// Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

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

// Vertex layout for the streaming 2D quad VBO (matches the vertexSrc2D /
// vertexSrc2Dcolor attribute set: position, texCoord, vertColor; the
// color-only program simply ignores texCoord).
typedef struct gl3_2D_vtx_s
{
	GLfloat pos[2];
	GLfloat texCoord[2];
	GLfloat color[4];
} gl3_2D_vtx_t;

// --- gl3_Draw.c ---

// Buffers and draws one 2D quad (triangle strip) through the streaming 2D VBO.
// The caller sets the program (si2D / si2Dcolor), texture and blend state first.
extern void GL3_Draw_EmitQuad(float xl, float yt, float xr, float yb, float sl, float tl, float sh, float th, const float color[4]);

extern void Draw_Render(int x, int y, int w, int h, const image_t* image, float alpha);

// ---------------------------------------------------------------------------
// Cross-module prototypes not yet in gl3_Local.h. These are the gl1_Image.h /
// gl1_Model.h members this module uses; the gl3_Image.c / gl3_Model.c module
// ports provide the definitions (signatures match gl1 EXACTLY).
// TODO(integration): move these to gl3_Local.h (or gl3_Image.h / gl3_Model.h)
// once those module ports land, then drop this block.
// ---------------------------------------------------------------------------

// --- gl3_Image.c (see gl1_Image.h) ---
extern image_t* R_FindImage(const char* name, imagetype_t type);
extern image_t* R_CreateFallbackTexture(const char* name, imagetype_t type);
extern image_t* R_GetFreeImage(void);
extern void R_BindImage(const image_t* image);
extern void R_UploadPaletted(int level, const byte* data, const paletteRGB_t* palette, int width, int height);
extern void R_UploadRGBA(int level, const byte* rgba, int width, int height); // Cinematic RGBA upload (MPEG). --morb
extern void R_FreeImageNoHash(image_t* image);
extern void R_SetFilter(const image_t* image);

// --- gl3_Model.c (see gl1_Model.h) ---
extern int registration_sequence;
