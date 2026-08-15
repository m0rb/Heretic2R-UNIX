//
// vk_Entity_internal.h -- internals shared by the entity-rendering module
// (vk_FlexModel.c / vk_Sprite.c / vk_Misc.c). NOT for other modules;
// cross-module prototypes belong in vk_Local.h (see the module report).
// Mirrors ref_gl3's gl3_Entity_internal.h approach (CONTRACT.md rule 1).
//
// Copyright 1998 Raven Software
//

#pragma once

#include "vk_Local.h"

// model_t and friends. gl1_Model.h is renderer-agnostic (it only needs image_t,
// which vk_Local.h defines VERBATIM from gl1_Local.h, plus qfiles.h) and its
// layout is already binding for ref_vk: the compiled-in gl1 sources
// (Skeletons/r_SkeletonLerp.c - CONTRACT.md rule 4) were compiled against it.
#include "gl1_Model.h"

#include "gl1_Matrix4.h" // matrix4_t + R_Matrix*() (gl1_Matrix4.c is compiled in).
#include "FlexModel.h"	// fmdl_blockheader_t, fmheader_t (on-disk format, qcommon).
#include "Reference.h"	// Placement_t, LERPedReferences_t, numReferences[], REF_*.
#include "Skeletons/r_Skeletons.h"	// SkeletalClusters[], CreateSkeleton*() (r_Skeletons.c is compiled in).

#pragma region ========================== gl1_FlexModel.h mirror ==========================

// NOTE: this block MUST stay in sync with src/ref_gl1/src/gl1_FlexModel.h!
// Skeletons/r_SkeletonLerp.c and Skeletons/r_Skeletons.c are compiled into
// ref_vk against THAT header (CONTRACT.md rule 4); fmdl_t/fmtrivertx_t/
// fmaliasframe_t below are the shared ABI with those objects. The header
// itself can't be included here because it pulls in gl1_Local.h, which
// redefines struct image_s/imagetype_t/rserr_t against vk_Local.h.
// The _Static_asserts at the end of this region are the ABI tripwire.

#define	MAX_FM_TRIANGLES	2048
#define MAX_FM_VERTS		2048
#define MAX_FM_FRAMES		2048

#define SKINPAGE_WIDTH		640
#define SKINPAGE_HEIGHT		480

// Skin header.
#define FM_SKIN_NAME		"skin"
#define FM_SKIN_VER			1

// ST coord header.
#define FM_ST_NAME			"st coord"
#define FM_ST_VER			1

// Tri header.
#define FM_TRI_NAME			"tris"
#define FM_TRI_VER			1

// Frame header.
#define FM_FRAME_NAME		"frames"
#define FM_FRAME_VER		1

// Frame for compression, just the names.
#define FM_SHORT_FRAME_NAME	"short frames"
#define FM_SHORT_FRAME_VER	1

// Normals for compressed frames.
#define FM_NORMAL_NAME		"normals"
#define FM_NORMAL_VER		1

// Compressed frame data.
#define FM_COMP_NAME		"comp data"
#define FM_COMP_VER			1

// GLCmds header.
#define FM_GLCMDS_NAME		"glcmds"
#define FM_GLCMDS_VER		1

// Mesh nodes header.
#define FM_MESH_NAME		"mesh nodes"
#define FM_MESH_VER			3

// Skeleton header.
#define FM_SKELETON_NAME	"skeleton"
#define FM_SKELETON_VER		1

// References header.
#define FM_REFERENCES_NAME	"references"
#define FM_REFERENCES_VER	1

typedef struct
{
	short s;
	short t;
} fmstvert_t;

typedef struct
{
	short index_xyz[3];
	short index_st[3];
} fmtriangle_t;

typedef struct
{
	byte unused_tris[MAX_FM_TRIANGLES >> 3]; // 2048 >> 3 == 256 //mxd. Unused tris array. Needed here to maintain struct layout...
	byte unused_verts[MAX_FM_VERTS >> 3]; // 2048 >> 3 == 256 //mxd. Unused verts array. Needed here to maintain struct layout...
	short start_glcmds;
	short num_glcmds;
} fmmeshnode_t;

// Frame info.
typedef struct
{
	byte v[3]; // Scaled byte to fit in frame mins/maxs.
	byte lightnormalindex;
} fmtrivertx_t;

typedef struct
{
	float scale[3];			// Multiply byte verts by this.
	float translate[3];		// Then add this.
	char name[16];			// Frame name from grabbing.
	fmtrivertx_t verts[1];	// Variable sized.
} fmaliasframe_t;

typedef struct
{
	int start_frame; //TODO: unused.
	int num_frames; //TODO: unused.
	int degrees; //TODO: unused.
	char* mat; //TODO: unused.
	char* ccomp; //TODO: unused.
	byte* cbase; //TODO: unused.
	float* cscale; //TODO: unused.
	float* coffset; //TODO: unused.
	float trans[3]; //TODO: unused.
	float scale[3]; //TODO: unused.
	float bmin[3];
	float bmax[3];
	float* complerp; //TODO: unused.
} fmgroup_t;

typedef struct fmdl_s
{
	fmheader_t header;
	fmstvert_t* st_verts; //TODO: unused.
	fmtriangle_t* tris; //TODO: unused.
	fmaliasframe_t* frames;
	int* glcmds;
	char* skin_names;
	fmmeshnode_t* mesh_nodes;

	// Compression stuff.
	int ngroups; //TODO: unused.
	fmgroup_t* compdata;
	int* frame_to_group;
	char* framenames; //TODO: unused.
	byte* lightnormalindex;

	int skeletalType;
	int rootCluster;
	struct ModelSkeleton_s* skeletons;

	int referenceType;
	Placement_t* refsForFrame; //mxd. 'struct M_Reference_s*' in original logic.
} fmdl_t;

// ABI tripwire: the compiled-in Skeletons/r_SkeletonLerp.c reads fmdl_t/
// fmtrivertx_t/fmaliasframe_t/fmmeshnode_t through gl1_FlexModel.h - these
// asserts pin the mirror above to that exact layout (offsetof-verified).
_Static_assert(sizeof(fmtrivertx_t) == 4, "fmtrivertx_t ABI drift vs gl1_FlexModel.h");
_Static_assert(sizeof(fmstvert_t) == 4, "fmstvert_t ABI drift vs gl1_FlexModel.h");
_Static_assert(sizeof(fmtriangle_t) == 12, "fmtriangle_t ABI drift vs gl1_FlexModel.h");
_Static_assert(offsetof(fmmeshnode_t, start_glcmds) == 512 && sizeof(fmmeshnode_t) == 516, "fmmeshnode_t ABI drift vs gl1_FlexModel.h");
_Static_assert(offsetof(fmaliasframe_t, name) == 24 && offsetof(fmaliasframe_t, verts) == 40, "fmaliasframe_t ABI drift vs gl1_FlexModel.h");
_Static_assert(sizeof(fmheader_t) == 40, "fmheader_t ABI drift vs FlexModel.h");
_Static_assert(offsetof(fmdl_t, frames) == sizeof(fmheader_t) + 2 * sizeof(void*), "fmdl_t ABI drift vs gl1_FlexModel.h");
_Static_assert(offsetof(fmdl_t, ngroups) == sizeof(fmheader_t) + 6 * sizeof(void*), "fmdl_t ABI drift vs gl1_FlexModel.h");
_Static_assert(offsetof(fmdl_t, refsForFrame) == sizeof(fmdl_t) - sizeof(void*), "fmdl_t ABI drift vs gl1_FlexModel.h");

#pragma endregion

#pragma region ========================== Skeletons/r_SkeletonLerp.h mirror ==========================

// NOTE: MUST stay in sync with src/ref_gl1/src/Skeletons/r_SkeletonLerp.h
// (same gl1_Local.h include problem as above; r_SkeletonLerp.c is compiled in).

//mxd. Reconstructed data type. Original name unknown.
typedef struct
{
	vec3_t front_vector;
	vec3_t back_vector;
	fmtrivertx_t* verts;
	fmtrivertx_t* old_verts;
} SkeletonFrameLerpInfo_t;

extern vec3_t s_lerped[MAX_FM_VERTS];			// Defined in Skeletons/r_SkeletonLerp.c (compiled in).
extern SkeletonFrameLerpInfo_t sfl_cur_skel;	// Defined in Skeletons/r_SkeletonLerp.c (compiled in).

extern void FrameLerp(const fmdl_t* fmdl, entity_t* e); //mxd. +fmdl arg. Defined in Skeletons/r_SkeletonLerp.c (compiled in).

#pragma endregion

#pragma region ========================== EXTERNS PROVIDED BY OTHER MODULE PORTS ==========================

// These mirror gl1/gl3 declarations; the owning vk module ports define them.
// All plain-data globals below have TENTATIVE definitions in vk_Misc.c
// (-fcommon merges them with the owning module's definitions once those ports
// land - same trick gl3_Misc.c used for minlight[]). Centralize in vk_Local.h
// once the ports land (see the module report).

extern cplane_t frustum[4]; // View frustum; owned by the frame module port (gl1_Main.c R_SetFrustum() parity).
extern float r_world_matrix[16]; // View matrix as GL-style column-major float[16]; owned by the frame module port (gl1 glGetFloatv() capture parity).
extern float r_projection_matrix[16]; //mxd. Owned by the frame module port.

// Combined view-projection matrix in VULKAN clip conventions (Y down, Z 0..1;
// includes the yq2 r_vulkan_correction premultiply): column-major float[16],
// pushed as the pc.vpMatrix/mvpMatrix vertex push constant by every entity
// draw. Owned by the frame module port (yq2 vk_main.c r_viewproj_matrix).
extern float r_viewproj_matrix[16];

extern byte minlight[256]; // YQ2 minlight remap table; owned by the vk_Light.c port (gl1_Light.c R_InitMinlight()).
extern qboolean r_minlight_set; // YQ2. True when gl_minlight > 0; owned by the vk_Light.c port (gl1/gl3 kept this in gl_state/gl3state - vkstate_t is locked, so it's a global).

// Frame-wide fog state for the per-draw UBO fog blocks (gl3 uni3D fog members).
// Owned by the frame module port (R_Fog()/R_WaterFog() semantics); fogMode == -1
// means fog off. fogSkipAdditive is left 0 here - the entity module overrides it
// per draw (R_HandleTransparency()).
extern vkfogblock_t vk_fogblock;

extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels); // vk_Light.c module port (gl1_Light.h).
extern struct image_s* R_FindImage(const char* name, imagetype_t type); // vk_Image.c module port (gl1_Image.h).

// PENDING SHARED CHANGE (see the module report for the exact vk_common.c /
// vk_Local.h additions): flexmodel/sprite transparency pipeline matrix -
// model.vert/model.frag + RGB_RGBA_RG vertex input on RP_WORLD, indexed
// [depth_test_disabled][blend_mode - 1]:
//   [*][0] = glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)  (standard/ghost)
//   [*][1] = glBlendFunc(GL_ONE, GL_ONE)                        (RF_TRANS_ADD)
//   [*][2] = glBlendFunc(GL_SRC_ALPHA, GL_ONE)                  (RF_TRANS_ADD + RF_ALPHA_TEXTURE)
//   [1][*] = depth test disabled (RF_NODEPTHTEST sprites)
// All keep depth writes enabled ([0][*]) - gl1 never masks depth for entities
// (vk_drawNoDepthModelPipelineFan is yq2's translucent variant and does NOT
// match H2 semantics).
extern qvkpipeline_t vk_drawModelBlendPipelines[2][3];

#pragma endregion

#pragma region ========================== ENTITY MODULE PROTOTYPES ==========================

// --- vk_FlexModel.c (gl1_FlexModel.h parity) ---
extern void Mod_LoadFlexModel(model_t* mod, void* buffer, int length);
extern void Mod_RegisterFlexModel(model_t* mod);
extern void R_DrawFlexModel(entity_t* e);
// R_LerpVert() is declared... nowhere: Skeletons/r_SkeletonLerp.c declares it
// extern locally (gl1 parity). Defined in vk_FlexModel.c (replaces the
// vk_Stubs.c placeholder - see the module report).

// --- vk_Sprite.c (gl1_Sprite.h parity) ---
extern void R_DrawSpriteModel(entity_t* e);

// --- vk_Misc.c (gl1_Misc.h parity) ---
extern void R_SetDefaultState(void);
extern void R_DrawNullModel(const entity_t* e);
extern void R_TransformVector(const vec3_t v, vec3_t out);
extern void R_RotateForEntity(const entity_t* e);
extern qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos); //mxd
extern paletteRGBA_t R_ModulateRGBA(paletteRGBA_t a, paletteRGBA_t b); //mxd
extern paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, byte alpha); //mxd
extern void R_HandleTransparency(const entity_t* e);
extern void R_CleanupTransparency(const entity_t* e);

#pragma endregion

#pragma region ========================== NEW VK BACKEND HELPERS (vk_Misc.c) ==========================

// gl1 glBlendFunc state mirror: R_HandleTransparency()'s blend-func matrix
// becomes a pipeline variant choice (blend factors are baked into VkPipelines).
typedef enum
{
	ENTITY_BLEND_NONE = 0,		// glDisable(GL_BLEND) - opaque model pipeline.
	ENTITY_BLEND_STANDARD = 1,	// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
	ENTITY_BLEND_ADD = 2,		// glBlendFunc(GL_ONE, GL_ONE).
	ENTITY_BLEND_ADD_ALPHA = 3,	// glBlendFunc(GL_SRC_ALPHA, GL_ONE).
} vk_entityblend_t;

// gl1 glColor* state mirror: per-primitive colors are per-vertex attributes in
// vk, so the "current color" every gl1 glColor* call set is tracked here and
// baked into the streamed vk_alias_vtx_t colors by the flex/sprite emitters.
extern float vk_currentDrawColor[4];

// gl1 R_BindImage() state mirror: the image whose descriptor set the next
// entity draw binds (set 0).
extern const image_t* vk_currentTexture;

// gl1 GL_BLEND / glBlendFunc / GL_ALPHA_TEST / GL_FOG state mirrors
// (set by R_HandleTransparency()/R_CleanupTransparency(), consumed by the
// entity draw submission in this module).
extern vk_entityblend_t vk_entityBlendMode;
extern float vk_alphaTestRef;		// glAlphaFunc(GL_GREATER, x); < 0 = alpha test disabled. Pushed at fragment PC offset 80.
extern int vk_fogSkipAdditive;		// glDisable(GL_FOG) around additive entity draws (written into the per-draw UBO fog block).

// gl1 modelview matrix stack mirror: R_RotateForEntity() multiplies onto this,
// QVk_RestoreModelIdentity() (glPopMatrix()) resets it; the per-draw UBO's
// model matrix is snapshotted from it (column-major, like GL).
extern matrix4_t vk_modelMatrix;

// glEnable(GL_BLEND) with the currently latched blend func (gl1 FMNI_USE_COLOR
// node path): upgrades ENTITY_BLEND_NONE to ENTITY_BLEND_STANDARD, keeps any
// mode R_HandleTransparency() already selected.
extern void QVk_EnableBlend(void);

// Maps the current vk_entityBlendMode (+ RF_NODEPTHTEST) onto the flexmodel/
// sprite pipeline matrix. THE state matrix mapping other modules can reuse.
extern qvkpipeline_t* QVk_SelectEntityPipeline(qboolean no_depth_test);

// Allocates + fills a per-draw vkmodelubo_t (model matrix from vk_modelMatrix,
// fog block from vk_fogblock with vk_fogSkipAdditive applied) on the streaming
// uniform buffer. Returns the dynamic offset + descriptor set for set 1.
extern void QVk_GetEntityUbo(qboolean textured, uint32_t* uboOffset, VkDescriptorSet* uboDescSet);

// Pushes the shared entity push constants: vertex = r_viewproj_matrix (+ 1.0
// sprite alpha), fragment = the H2ColorGrade trio (RP_WORLD grades per-fragment)
// + vk_alphaTestRef.
extern void QVk_PushEntityConstants(const qvkpipeline_t* pipeline);

// Streams alias-layout vertices as a triangle fan (converted to an indexed
// triangle list) through the currently selected entity pipeline state.
// Alias-layout counterpart of gl3's GL3_DrawAliasVerts(); used by sprites.
extern void QVk_DrawEntityFan(const vk_alias_vtx_t* verts, int numVerts, qboolean no_depth_test);

// glPopMatrix() equivalent for R_RotateForEntity(): entity model matrix back to identity.
extern void QVk_RestoreModelIdentity(void);

#pragma endregion

#pragma region ========================== INLINE HELPERS ==========================

// gl1 glColor4f()/glColor4ub() equivalents (glColor3* variants set alpha to 1.0 - pass it explicitly).
static inline void QVk_SetCurrentColor(const float r, const float g, const float b, const float a)
{
	vk_currentDrawColor[0] = r;
	vk_currentDrawColor[1] = g;
	vk_currentDrawColor[2] = b;
	vk_currentDrawColor[3] = a;
}

static inline void QVk_SetCurrentColorRGBA(const paletteRGBA_t c)
{
	QVk_SetCurrentColor((float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f, (float)c.a / 255.0f);
}

// gl1 R_BindImage() equivalent: in vk the bind happens at draw submission
// (descriptor set 0), so this just latches the image.
static inline void QVk_BindImage(const image_t* image)
{
	vk_currentTexture = image;
}

#pragma endregion
