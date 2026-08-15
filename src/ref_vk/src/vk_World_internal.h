//
// vk_World_internal.h -- shared internals of the world-rendering module
// (vk_Surface.c, vk_Lightmap.c, vk_Light.c, vk_Sky.c, vk_Warp.c).
//
// H2 semantics from gl1_Surface.c / gl1_Lightmap.c / gl1_Light.c / gl1_Sky.c /
// gl1_Warp.c via the ALREADY-VALIDATED H2 ports in gl3_Surface.c /
// gl3_Lightmap.c / gl3_Light.c / gl3_Sky.c / gl3_Warp.c
// (gl3_World_internal.h is the direct ancestor of this header); Vulkan draw
// submission technique from yquake2remaster vk_surf.c / vk_warp.c / vk_light.c
// (streaming vertex/index/uniform buffers + per-draw descriptor sets instead
// of GL state).
//
// Copyright 1998 Raven Software
//

#pragma once

#include "vk_Local.h"

// gl1 BSP/model structures are reused VERBATIM (they are the binding
// model-data ABI for all H2R renderers - see gl3_World_internal.h).
// NOTE: gl1_Model.h must be included below the image_t definition (provided
// by vk_Local.h).
#include "gl1_Model.h"

#include "Vector.h" // VectorNormalize() for the Mat4 helpers below.

#pragma region ========================== SHARED GLOBALS ==========================

// Owned by the vk_Model.c module port (gl1: defined in gl1_Main.c; the
// foundation's vk_Main.c already carries a tentative 'struct model_s*'
// definition - -fcommon merges them with the one in vk_Surface.c).
extern model_t* r_worldmodel;

// Owned by vk_Surface.c (gl1: defined in gl1_Main.c).
extern cplane_t frustum[4];

extern int r_viewcluster;
extern int r_viewcluster2;
extern int r_oldviewcluster;
extern int r_oldviewcluster2;

extern int c_visible_lightmaps;
extern int c_visible_textures;

// Owned by gl3_Light.c counterpart -> vk_Light.c.
extern byte minlight[256]; // YQ2

// View-projection matrix for the current 3D scene (yq2 vk_main.c
// r_viewproj_matrix). Computed by the frame module (vk_Main.c port of yq2
// R_SetupVulkan: Mat_Perspective x Vulkan clip correction x view matrix)
// before the world/entity flow; vk_Surface.c carries a tentative definition
// so the world module links before the frame port lands (-fcommon merges).
// Every world push constant mvp/vpMatrix comes from here.
extern float r_viewproj_matrix[16];

// Current model (entity) matrix - vk replacement for gl3state.uni3DData
// .transModelMat4 (gl1: the GL modelview stack). Identity in world space;
// R_DrawBrushModel() composes the entity transform onto it and restores it.
// Owned by vk_Surface.c.
extern float r_local_model_matrix[16];

// vk replacement for the gl3 "ambient" uni3D alpha / alphaTestRef state
// (gl1: the ambient glColor alpha + glAlphaFunc ref): set by the caller
// before surface emission (world pass: 1.0 / -1.0 = disabled; alpha-sorted
// pass: gl_trans33/66 / 0.05; RF_TRANS_ANY bmodels: 0.25).
// Owned by vk_Surface.c, read by vk_Warp.c.
extern float r_surf_alpha;
extern float r_surf_alpha_test;

// Current fog block, gl1 R_Fog()/R_WaterFog() semantics via gl3's uni3D fog
// members - copied into EVERY per-draw world/model UBO (vklmapubo_t /
// vkmodelubo_t / the fog-less basic.frag pipelines simply have no fog).
// Filled by R_SetupFog() (vk_Surface.c) once per frame; fogSkipAdditive is
// toggled by the particle module around additive draws (gl1
// glDisable(GL_FOG) parity). The flexmodel module reads this for its
// vkmodelubo_t fog block too.
extern vkfogblock_t r_world_fog;

#pragma endregion

#pragma region ========================== CROSS-MODULE FUNCTIONS (other vk module ports, gl1 names) ==========================

extern image_t* R_FindImage(const char* name, imagetype_t type);	// vk_Image.c (gl1_Image.h).
extern void R_DrawSpriteModel(entity_t* e);							// vk_Sprite.c (gl1_Sprite.h).
extern void R_DrawFlexModel(entity_t* e);							// vk_FlexModel.c (gl1_FlexModel.h).
extern void R_DrawNullModel(const entity_t* e);						// vk_Misc.c (gl1_Misc.h).

#pragma endregion

#pragma region ========================== LIGHTMAP STATE (gl1_Lightmap.h on the yq2/gl3 4-atlas model) ==========================

#define MAX_TALLWALL_LIGHTMAPS	512 // H2

// BLOCK_WIDTH / BLOCK_HEIGHT / LIGHTMAP_BYTES / MAX_LIGHTMAPS /
// MAX_LIGHTMAPS_PER_SURFACE come from vk_Local.h (gl3 parity: 4 big 1024x512
// atlases, each with 4 lightstyle sub-lightmaps; the qvktexture_t objects
// live in vk_state.lightmap_textures[][]).
typedef struct
{
	int current_lightmap_texture; // Index into vk_state.lightmap_textures[].

	msurface_t* lightmap_surfaces[MAX_LIGHTMAPS];
	msurface_t* tallwall_lightmap_surfaces[MAX_TALLWALL_LIGHTMAPS]; // H2
	int tallwall_lightmaptexturenum; // H2

	int allocated[BLOCK_WIDTH];

	// The lightmap texture data needs to be kept in main memory so the atlas
	// upload / future partial updates can work. One buffer per lightstyle
	// sub-lightmap (yq2/gl3).
	byte lightmap_buffers[MAX_LIGHTMAPS_PER_SURFACE][BLOCK_WIDTH * BLOCK_HEIGHT * LIGHTMAP_BYTES];

	// One combined descriptor set per atlas: 4 combined image samplers in one
	// binding (vk_samplerLightmapDescSetLayout - the H2 4-lightstyle-sampler
	// set bound at set = 2 of the polygon_lmap pipeline). vk analog of gl3's
	// GL3_BindLightmap() tmu 1..4 binding.
	VkDescriptorSet lightmap_descriptor_sets[MAX_LIGHTMAPS];
} vklightmapstate_t;

extern vklightmapstate_t gl_lms;

#pragma endregion

#pragma region ========================== MODULE PROTOTYPES ==========================

// --- vk_Surface.c (gl1_Surface.h + world-frame helpers from gl1_Main.c) ---
extern void R_SortAndDrawAlphaSurfaces(void);
extern void R_DrawBrushModel(entity_t* ent);
extern void R_DrawWorld(void);
extern void R_MarkLeaves(void);
extern void R_SetFrustum(void);			// gl1: static in gl1_Main.c R_RenderView() flow.
extern void R_SetViewClusters(void);	// gl1: the viewcluster part of R_SetupFrame() (gl1_Main.c); gl3: GL3_SetViewClusters().
extern void R_SetupFog(void);			// gl1: GL_Fog()/GL_WaterFog() picked in R_Clear() (gl3_Main.c R_Clear() fog part) - fills r_world_fog.

// vk backend helpers owned by vk_Surface.c:
// Streams a triangle fan through the currently bound pipeline as an indexed
// triangle list (all vk world pipelines use TRIANGLE_LIST topology - yq2
// R_GenFanIndexes() technique). Pipeline, descriptor sets and push constants
// must already be bound/pushed by the caller.
extern void QVk_DrawTriangleFan(const void* verts, VkDeviceSize vert_size, int numverts);
// Draws vk_3D_vtx_t verts through vk_drawPolyPipeline (polygon.vert +
// basic.frag: mvp push constant, constant-color UBO, neutral world grade).
// The trans33/66 / underwater / quake-floor / drawflat-less textured path.
extern void R_DrawPolyVerts(const vk_3D_vtx_t* verts, int numverts, const image_t* image, const float color[4], float alpha_test_ref);

// --- vk_Lightmap.c (gl1_Lightmap.h) ---
extern void LM_InitBlock(void);
extern void LM_UploadBlock(void); // gl1 had a 'dynamic' arg; the gl3/vk lightmap model has no dynamic block.
extern qboolean LM_AllocBlock(int w, int h, int* x, int* y);
extern void LM_BuildPolygonFromSurface(const model_t* mdl, msurface_t* fa);
extern void LM_CreateSurfaceLightmap(msurface_t* surf);
extern void LM_BeginBuildingLightmaps(void);
extern void LM_EndBuildingLightmaps(void);
extern void LM_ShutdownLightmaps(void); // vk: destroys the atlas images/descriptor sets (call from R_ShutdownImages()/RI_ShutdownContext() before QVk_Shutdown()).

// --- vk_Light.c (gl1_Light.h + R_SetLightLevel from gl1_Main.c) ---
extern void R_RenderDlights(void);
extern void R_MarkLights(dlight_t* light, int bit, const mnode_t* node);
extern void R_PushDlights(void);
extern void R_ResetBmodelTransforms(void); //mxd
extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels);
extern void R_SetCacheState(msurface_t* surf);
extern void R_InitMinlight(void); //mxd. Call from R_InitImages() (gl1 parity, gl1_Image.c:814).
extern void R_BuildLightMap(const msurface_t* surf, int offset_in_lm_buf, int stride); // Writes into the 4 style buffers (yq2/gl3).
extern void R_SetLightLevel(void); // gl1: in gl1_Main.c; needs R_LightPoint(), so it lives here.

// --- vk_Sky.c (gl1_Sky.h; RI_SetSky() prototype comes from vk_Local.h) ---
extern void R_AddSkySurface(const msurface_t* fa);
extern void R_ClearSkyBox(void);
extern void R_DrawSkyBox(void);

// --- vk_Warp.c (gl1_Warp.h) ---
// vk: 'image' args added (no ambient GL texture binding to inherit - gl3
// bound the texture before calling these).
extern void R_EmitWaterPolys(const msurface_t* fa, const image_t* image, qboolean undulate);
extern void R_EmitUnderwaterPolys(const msurface_t* fa, const image_t* image);
extern void R_EmitQuakeFloorPolys(const msurface_t* fa, const image_t* image);
extern void R_SubdivideSurface(const model_t* mdl, msurface_t* fa); //mxd. Added 'mdl' arg.

#pragma endregion

#pragma region ========================== INLINE BACKEND HELPERS ==========================

#define MAX_POLY_VERTS	64 // R_SubdividePolygon() emits at most 62 verts; world faces stay well below.

// Usable for sampling? (Module-port ordering guard: world textures come from
// the vk_Image.c module port.)
static inline qboolean R_ImageUsable(const image_t* image)
{
	return (image != NULL && image->vk_texture.descriptorSet != VK_NULL_HANDLE);
}

// Pushes the vertex-stage mvp/vp matrix (offset 0 of the shared push constant
// layout - see vk_Local.h).
static inline void QVk_PushMatrix(const qvkpipeline_t* pipeline, const float* mat16)
{
	vkCmdPushConstants(vk_activeCmdbuffer, pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16 * sizeof(float), mat16);
}

// Pushes the fragment-stage constants for basic.frag / model.frag world draws:
// the real H2ColorGrade trio (RP_WORLD grades per-fragment now, matching gl1's
// baked-texture gamma / gl3's in-shader grade) plus the gl1 glAlphaFunc(GL_GREATER,
// x) alpha test ref (< 0.0 = disabled).
static inline void QVk_PushWorldFragmentConstants(const qvkpipeline_t* pipeline, const float alpha_test_ref)
{
	const float push[4] = { vk_gradePush[0], vk_gradePush[1], vk_gradePush[2], alpha_test_ref };
	vkCmdPushConstants(vk_activeCmdbuffer, pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT,
		PUSH_CONSTANT_VERTEX_SIZE * sizeof(float), sizeof(push), push);
}

// ---------------------------------------------------------------------------
// 16-float matrix helpers, ported from yq2 vk_main.c Mat_* (row-vector
// convention: v' = v * M, composition Mat4_Multiply(first, second, res);
// memcpy'd verbatim into the mat4 push constants / UBO members - the GLSL
// column-major read of this layout yields the matching column-vector matrix).
// static inline (not the yq2 global names) so the frame module's own Mat_*
// ports can't collide at link time.
// ---------------------------------------------------------------------------

static inline void Mat4_Identity(float* matrix)
{
	memset(matrix, 0, 16 * sizeof(float));
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

static inline void Mat4_Multiply(const float* m1, const float* m2, float* res)
{
	float mul[16];

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			mul[i * 4 + j] = m1[i * 4 + 0] * m2[0 * 4 + j] + m1[i * 4 + 1] * m2[1 * 4 + j] +
							 m1[i * 4 + 2] * m2[2 * 4 + j] + m1[i * 4 + 3] * m2[3 * 4 + j];
		}
	}

	memcpy(res, mul, sizeof(mul));
}

static inline void Mat4_Translate(float* matrix, const float x, const float y, const float z)
{
	const float t[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		   x,	 y,	   z, 1.0f
	};

	Mat4_Multiply(matrix, t, matrix);
}

static inline void Mat4_Rotate(float* matrix, const float deg, const float x, const float y, const float z)
{
	const double c = cos((double)deg * M_PI / 180.0);
	const double s = sin((double)deg * M_PI / 180.0);
	const double cd = 1.0 - c;

	vec3_t r = { x, y, z };
	VectorNormalize(r);

	const float rot[16] = {
		(float)(r[0] * r[0] * cd + c),		  (float)(r[1] * r[0] * cd + r[2] * s), (float)(r[0] * r[2] * cd - r[1] * s), 0.0f,
		(float)(r[0] * r[1] * cd - r[2] * s), (float)(r[1] * r[1] * cd + c),		(float)(r[1] * r[2] * cd + r[0] * s), 0.0f,
		(float)(r[0] * r[2] * cd + r[1] * s), (float)(r[1] * r[2] * cd - r[0] * s), (float)(r[2] * r[2] * cd + c),		  0.0f,
		0.0f,								  0.0f,									0.0f,								  1.0f
	};

	Mat4_Multiply(matrix, rot, matrix);
}

#pragma endregion
