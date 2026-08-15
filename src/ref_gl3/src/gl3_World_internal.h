//
// gl3_World_internal.h -- shared internals of the world-rendering module
// (gl3_Surface.c, gl3_Lightmap.c, gl3_Light.c, gl3_Sky.c, gl3_Warp.c).
//
// H2 semantics from gl1_Surface.c / gl1_Lightmap.c / gl1_Light.c / gl1_Sky.c /
// gl1_Warp.c on the yq2 gl3 backend (gl3_surf.c / gl3_lightmap.c / gl3_light.c /
// gl3_warp.c technique).
//
// Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

// gl1 BSP/model structures are reused VERBATIM (gl1_FindSurface.c is compiled
// into ref_gl3 against them, so they are the binding model-data ABI for this
// renderer). NOTE: gl1_Model.h must be included below the image_t definition
// (provided by gl3_Local.h).
#include "gl1_Model.h"

#pragma region ========================== SHARED GLOBALS ==========================

// Owned by the gl3_Model.c module port (gl1: defined in gl1_Main.c).
// gl3_Surface.c carries a tentative definition so the module links before
// gl3_Model.c lands (-fcommon merges them).
extern model_t* r_worldmodel;

// Owned by gl3_Surface.c (gl1: defined in gl1_Main.c).
extern cplane_t frustum[4];

extern int r_viewcluster;
extern int r_viewcluster2;
extern int r_oldviewcluster;
extern int r_oldviewcluster2;

extern int c_visible_lightmaps;
extern int c_visible_textures;

// Owned by gl3_Light.c.
extern byte minlight[256]; // YQ2

#pragma endregion

#pragma region ========================== CROSS-MODULE FUNCTIONS (other gl3 module ports, gl1 names) ==========================

extern image_t* R_FindImage(const char* name, imagetype_t type);	// gl3_Image.c (gl1_Image.h).
extern void R_DrawSpriteModel(entity_t* e);							// gl3_Sprite.c (gl1_Sprite.h).
extern void R_DrawFlexModel(entity_t* e);							// gl3_FlexModel.c (gl1_FlexModel.h).
extern void R_DrawNullModel(const entity_t* e);						// gl3_Misc.c (gl1_Misc.h).

#pragma endregion

#pragma region ========================== LIGHTMAP STATE (gl1_Lightmap.h on the yq2 4-atlas model) ==========================

#define MAX_TALLWALL_LIGHTMAPS	512 // H2
#define GL_LIGHTMAP_FORMAT		GL_RGBA

// BLOCK_WIDTH / BLOCK_HEIGHT / LIGHTMAP_BYTES / MAX_LIGHTMAPS /
// MAX_LIGHTMAPS_PER_SURFACE come from gl3_Local.h (yq2 atlas config:
// 4 big 1024x512 atlases, each with 4 lightstyle sub-lightmaps on tmu 1..4).
typedef struct
{
	int current_lightmap_texture; // Index into gl3state.lightmap_textureIDs[].

	msurface_t* lightmap_surfaces[MAX_LIGHTMAPS];
	msurface_t* tallwall_lightmap_surfaces[MAX_TALLWALL_LIGHTMAPS]; // H2
	int tallwall_lightmaptexturenum; // H2

	int allocated[BLOCK_WIDTH];

	// The lightmap texture data needs to be kept in main memory so texsubimage can update properly.
	// One buffer per lightstyle sub-lightmap (yq2).
	byte lightmap_buffers[MAX_LIGHTMAPS_PER_SURFACE][BLOCK_WIDTH * BLOCK_HEIGHT * LIGHTMAP_BYTES];
} gl3lightmapstate_t;

extern gl3lightmapstate_t gl_lms;

#pragma endregion

#pragma region ========================== MODULE PROTOTYPES ==========================

// --- gl3_Surface.c (gl1_Surface.h + world-frame helpers from gl1_Main.c) ---
extern void R_SortAndDrawAlphaSurfaces(void);
extern void R_DrawBrushModel(entity_t* ent);
extern void R_DrawWorld(void);
extern void R_MarkLeaves(void);
extern void R_SetFrustum(void);			// gl1: static in gl1_Main.c R_RenderView() flow.
extern void GL3_SetViewClusters(void);	// gl1: the viewcluster part of R_SetupFrame() (gl1_Main.c).
extern GLuint GL3_WhiteTexture(void);	// 1x1 white texture for untextured color polys (drawflat / tallwalls / gl_lightmap / flashblend).

// --- gl3_Lightmap.c (gl1_Lightmap.h) ---
extern void LM_InitBlock(void);
extern void LM_UploadBlock(void); // gl1 had a 'dynamic' arg; the gl3 lightmap model has no dynamic block.
extern qboolean LM_AllocBlock(int w, int h, int* x, int* y);
extern void LM_BuildPolygonFromSurface(const model_t* mdl, msurface_t* fa);
extern void LM_CreateSurfaceLightmap(msurface_t* surf);
extern void LM_BeginBuildingLightmaps(void);
extern void LM_EndBuildingLightmaps(void);
extern void GL3_BindLightmap(int lightmapnum); // Binds the 4 style sub-lightmaps of an atlas to tmu 1..4 (yq2).

// --- gl3_Light.c (gl1_Light.h + R_SetLightLevel from gl1_Main.c) ---
extern void R_RenderDlights(void);
extern void R_MarkLights(dlight_t* light, int bit, const mnode_t* node);
extern void R_PushDlights(void);
extern void R_ResetBmodelTransforms(void); //mxd
extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels);
extern void R_SetCacheState(msurface_t* surf);
extern void R_InitMinlight(void); //mxd. Call from R_InitImages() (gl1 parity, gl1_Image.c:814).
extern void R_BuildLightMap(const msurface_t* surf, int offset_in_lm_buf, int stride); // gl1 wrote into a caller buffer; gl3 writes into the 4 style buffers (yq2).
extern void R_SetLightLevel(void); // gl1: in gl1_Main.c; needs R_LightPoint(), so it lives here.

// --- gl3_Sky.c (gl1_Sky.h; RI_SetSky() prototype comes from gl3_Local.h) ---
extern void R_AddSkySurface(const msurface_t* fa);
extern void R_ClearSkyBox(void);
extern void R_DrawSkyBox(void);

// --- gl3_Warp.c (gl1_Warp.h) ---
extern void R_EmitWaterPolys(const msurface_t* fa, qboolean undulate);
extern void R_EmitUnderwaterPolys(const msurface_t* fa);
extern void R_EmitQuakeFloorPolys(const msurface_t* fa);
extern void R_SubdivideSurface(const model_t* mdl, msurface_t* fa); //mxd. Added 'mdl' arg.

#pragma endregion

#pragma region ========================== INLINE BACKEND HELPERS ==========================

// yq2 GL3_Bind(): bind a texture name to GL_TEXTURE0 with gl3state caching.
// (The gl3_Image.c module port provides R_BindImage()/R_Bind() for images; this
// low-level variant exists so the world module has no link-time dependency on it.)
static inline void GL3_BindTexnum(const GLuint texnum)
{
	if (gl3state.currenttexture != texnum)
	{
		gl3state.currenttexture = texnum;
		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texnum);
	}
}

// Sets the per-draw uni3D surface state (SURF_FLOWING scroll / trans33/66 alpha /
// glAlphaFunc ref; alphaTestRef < 0.0 == alpha test disabled), uploading the UBO
// only when something changed.
static inline void GL3_UpdateSurfState(const float scroll, const float alpha, const float alpha_test_ref)
{
	gl3Uni3D_t* u = &gl3state.uni3DData;

	if (u->scroll != scroll || u->alpha != alpha || u->alphaTestRef != alpha_test_ref)
	{
		u->scroll = scroll;
		u->alpha = alpha;
		u->alphaTestRef = alpha_test_ref;

		GL3_UpdateUBO3D();
	}
}

#pragma endregion
