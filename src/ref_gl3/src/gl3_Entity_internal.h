//
// gl3_Entity_internal.h -- internals shared by the entity-rendering module
// (gl3_FlexModel.c / gl3_Sprite.c / gl3_Misc.c). NOT for other modules;
// cross-module prototypes belong in gl3_Local.h (see the module report).
//
// Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"

// model_t and friends. gl1_Model.h is renderer-agnostic (it only needs image_t,
// which gl3_Local.h defines VERBATIM from gl1_Local.h, plus qfiles.h) and its
// layout is already binding for ref_gl3: the compiled-in gl1 sources
// (Skeletons/r_SkeletonLerp.c, gl1_FindSurface.c - CONTRACT.md rule 4) were
// compiled against it.
#include "gl1_Model.h"

#include "FlexModel.h"	// fmdl_blockheader_t, fmheader_t (on-disk format, qcommon).
#include "Reference.h"	// Placement_t, LERPedReferences_t, numReferences[], REF_*.
#include "Skeletons/r_Skeletons.h"	// SkeletalClusters[], CreateSkeleton*() (r_Skeletons.c is compiled in).

#pragma region ========================== gl1_FlexModel.h mirror ==========================

// NOTE: this block MUST stay in sync with src/ref_gl1/src/gl1_FlexModel.h!
// Skeletons/r_SkeletonLerp.c and Skeletons/r_Skeletons.c are compiled into
// ref_gl3 against THAT header (CONTRACT.md rule 4); fmdl_t/fmtrivertx_t/
// fmaliasframe_t below are the shared ABI with those objects. The header
// itself can't be included here because it pulls in gl1_Local.h, which
// redefines struct image_s/imagetype_t/rserr_t against gl3_Local.h.

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

// These mirror gl1 declarations; the owning gl3 module ports define them.
// Centralize in gl3_Local.h once those ports land (see the module report).

extern cplane_t frustum[4]; // View frustum; defined in gl3_Main.c (gl1_Main.c R_SetFrustum() parity).
extern float r_world_matrix[16]; // View matrix as GL-style float[16]; defined in gl3_Main.c (gl1 glGetFloatv() capture parity).
extern float r_projection_matrix[16]; //mxd. Defined in gl3_Main.c.
extern byte minlight[256]; // YQ2 minlight remap table (gl1_Light.c R_InitMinlight()); tentative definition in gl3_Misc.c (-fcommon merges with gl3_Light.c's once that port lands).

extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels); // gl3_Light.c module port (gl1_Light.h).
extern struct image_s* R_FindImage(const char* name, imagetype_t type); // gl3_Image.c module port (gl1_Image.h).

#pragma endregion

#pragma region ========================== ENTITY MODULE PROTOTYPES ==========================

// --- gl3_FlexModel.c (gl1_FlexModel.h parity) ---
extern void Mod_LoadFlexModel(model_t* mod, void* buffer, int length);
extern void Mod_RegisterFlexModel(model_t* mod);
extern void R_DrawFlexModel(entity_t* e);
extern void R_LerpVert(const vec3_t new_point, const vec3_t old_point, vec3_t interpolated_point, const float move[3], const float frontv[3], const float backv[3]);

// --- gl3_Sprite.c (gl1_Sprite.h parity) ---
extern void R_DrawSpriteModel(entity_t* e);

// --- gl3_Misc.c (gl1_Misc.h parity) ---
extern void R_SetDefaultState(void);
extern void R_DrawNullModel(const entity_t* e);
extern void R_TransformVector(const vec3_t v, vec3_t out);
extern void R_RotateForEntity(const entity_t* e);
extern qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos); //mxd
extern paletteRGBA_t R_ModulateRGBA(paletteRGBA_t a, paletteRGBA_t b); //mxd
extern paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, byte alpha); //mxd
extern void R_HandleTransparency(const entity_t* e);
extern void R_CleanupTransparency(const entity_t* e);

// --- gl3_Misc.c (new gl3 backend helpers) ---

// gl1 glColor* state mirror: per-primitive colors are per-vertex attributes in gl3,
// so the "current color" every gl1 glColor* call set is tracked here and baked into
// the streamed gl3_alias_vtx_t colors by the flex/sprite/nullmodel emitters.
extern float gl3_currentDrawColor[4];

// Streams alias-layout vertices through vaoAlias/vboAlias and draws them (non-indexed).
extern void GL3_DrawAliasVerts(GLuint shaderProgram, const gl3_alias_vtx_t* verts, int numVerts, GLenum drawMode);

// glPopMatrix() equivalent for R_RotateForEntity(): entity model matrix back to identity.
extern void GL3_RestoreModelIdentity(void);

#pragma endregion

#pragma region ========================== INLINE HELPERS ==========================

// gl1 glColor4f()/glColor4ub() equivalents (glColor3* variants set alpha to 1.0 - pass it explicitly).
static inline void GL3_SetCurrentColor(const float r, const float g, const float b, const float a)
{
	gl3_currentDrawColor[0] = r;
	gl3_currentDrawColor[1] = g;
	gl3_currentDrawColor[2] = b;
	gl3_currentDrawColor[3] = a;
}

static inline void GL3_SetCurrentColorRGBA(const paletteRGBA_t c)
{
	GL3_SetCurrentColor((float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f, (float)c.a / 255.0f);
}

// gl1 R_BindImage() equivalent on the gl3state texture cache (YQ2 GL3_Bind()).
static inline void GL3_BindTexnum(const GLuint texnum)
{
	GL3_SelectTMU(GL_TEXTURE0);

	if (gl3state.currenttexture != texnum)
	{
		gl3state.currenttexture = texnum;
		glBindTexture(GL_TEXTURE_2D, texnum);
	}
}

static inline void GL3_BindImage(const image_t* image)
{
	GL3_BindTexnum((GLuint)image->texnum);
}

#pragma endregion
