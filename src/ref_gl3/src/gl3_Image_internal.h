//
// gl3_Image_internal.h -- image module interface (gl1_Image.h counterpart for ref_gl3).
//
// Everything other module ports (draw, world, sky, cinematics, ...) need from
// gl3_Image.c beyond the prototypes already declared in gl3_Local.h lives here.
//
// Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

extern int gl_filter_min;
extern int gl_filter_max;

extern int registration_sequence; // Defined in gl3_Model.c (gl1: gl1_Model.c).

// gl1's R_Bind() / R_BindImage() / R_MBind() / R_MBindImage() / R_SelectTexture() collapse
// into these + the GL3_SelectTMU() inline from gl3_Local.h (yq2 gl3 backend style).
// NOTE: gl1's R_EnableMultitexture() / R_TexEnv() / R_BlendFunc() / R_AlphaFunc() have no
// gl3 counterparts: texenv/alpha test are shader-side, blend state is set by the draw code.
extern void GL3_Bind(GLuint texnum);
extern void GL3_BindImage(const image_t* image);
extern void GL3_BindLightmap(int lightmapnum);

extern image_t* R_FindImage(const char* name, imagetype_t type);
extern image_t* R_CreateFallbackTexture(const char* name, imagetype_t type);
extern image_t* R_GetFreeImage(void);
extern void R_UploadPaletted(int level, const byte* data, const paletteRGB_t* palette, int width, int height);
extern void R_UploadRGBA(int level, const byte* rgba, int width, int height); // Cinematic RGBA upload (MPEG). --morb
extern void R_FreeImageNoHash(image_t* image);
extern void R_FreeUnusedImages(void);
extern void R_SetTexAnisotropy(void); // YQ2
extern void R_SetFilter(const image_t* image);
extern void R_DisplayHashTable(void);

// NOTE: R_InitImages() / R_ShutdownImages() / R_ImageList_f() / R_TextureMode() /
// RI_RegisterSkin() prototypes live in gl3_Local.h (module function set).
// gl1's R_InitGammaTable() / R_GammaAffect() / R_RefreshImage() are REMOVED in gl3:
// H2 gamma/brightness/contrast run in the fragment shaders (uniCommon), textures are
// uploaded raw and never need re-uploading (gl3_Main.c already no-ops the
// vid_textures_refresh_required handshake).
