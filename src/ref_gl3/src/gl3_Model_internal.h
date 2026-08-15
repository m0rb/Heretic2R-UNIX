//
// gl3_Model_internal.h -- model module interface (gl1_Model.h counterpart for ref_gl3).
//
// In-memory model representation shared with the world (gl3_Surface.c / gl3_Light.c /
// gl3_Lightmap.c / gl3_Warp.c), sky and flexmodel module ports. Structures are ported
// VERBATIM from gl1_Model.h, except glpoly_t whose vertices use the yq2 gl3 streaming
// layout (gl3_3D_vtx_t) instead of gl1's float[VERTEXSIZE].
//
// The FlexModel data structures (fmdl_t & friends, formerly in gl1_FlexModel.h) also
// live here: they describe model_t.extradata for mod_fmdl and are needed both by the
// loader/renderer (gl3_FlexModel.c) and by RI_GetReferencedID() (gl3_Model.c).
//
// Copyright 1998 Raven Software
//

#pragma once

#include "gl3_Local.h"
#include "qfiles.h"
#include "FlexModel.h"
#include "Reference.h"

#pragma region ========================== BRUSH MODELS ==========================

// Q2 counterpart
typedef struct
{
	vec3_t position;
} mvertex_t;

// Q2 counterpart
typedef struct
{
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;	// For sounds or lights.
	float radius;
	int headnode;
	int visleafs;	// Not including the solid leaf 0.
	int firstface;
	int numfaces;
} mmodel_t;

#define SIDE_FRONT			0
#define SIDE_BACK			1
#define SIDE_ON				2

//TODO: differentiate from texinfo SURF_ flags.
#define SURF_PLANEBACK		2
#define SURF_DRAWSKY		4
#define SURF_SKIPDRAW		8 //mxd. Because SURF_NODRAW is already used...
#define SURF_DRAWTURB		16

// Q2 counterpart
typedef struct
{
	ushort v[2];
	uint cachededgeoffset;
} medge_t;

// Q2 counterpart
typedef struct mtexinfo_s
{
	float vecs[2][4];
	int flags;
	int numframes;
	struct mtexinfo_s* next; // Animation chain.
	image_t* image;
} mtexinfo_t;

// gl3: vertices use the yq2 gl3 brush-surface streaming layout (gl3_3D_vtx_t: pos, st,
// lightmap st, normal, lightFlags) instead of gl1's float[VERTEXSIZE] (xyz s1t1 s2t2).
// Filled by LM_BuildPolygonFromSurface() / R_SubdivideSurface() (world module ports),
// drawn via GL3_BufferAndDraw3D().
typedef struct glpoly_s
{
	struct glpoly_s* next;
	struct glpoly_s* chain;
	int numverts;
	int flags;
	gl3_3D_vtx_t vertices[4]; // Variable sized.
} glpoly_t;

// Q2 counterpart
typedef struct msurface_s
{
	int visframe; // Should be drawn when node is crossed.

	cplane_t* plane;
	int flags;

	int firstedge; // Look up in model->surfedges[], negative numbers are backwards edges.
	int numedges;

	short texturemins[2];
	short extents[2];

	int light_s; // gl lightmap coordinates.
	int light_t;

	int dlight_s; // gl lightmap coordinates for dynamic lightmaps.
	int dlight_t;

	glpoly_t* polys; // Multiple if warped.
	struct msurface_s* texturechain;
	struct msurface_s* lightmapchain;

	mtexinfo_t* texinfo;

	// Lighting info
	int dlightframe;
	int dlightbits;

	int lightmaptexturenum;
	byte styles[MAXLIGHTMAPS];
	float cached_light[MAXLIGHTMAPS]; // Values currently used in lightmap.
	byte* samples; // [numstyles * surfsize]
} msurface_t;

// Q2 counterpart
typedef struct mnode_s
{
	// Common with leaf.
	int contents; // -1, to differentiate from leafs.
	int visframe; // Node needs to be traversed if current.

	float minmaxs[6]; // For bounding box culling.

	struct mnode_s* parent;

	// Node specific.
	cplane_t* plane;
	struct mnode_s* children[2];

	ushort firstsurface;
	ushort numsurfaces;
} mnode_t;

// Q2 counterpart
typedef struct mleaf_s
{
	// Common with node.
	int contents; // Will be a negative contents number.
	int visframe; // Node needs to be traversed if current.

	float minmaxs[6]; // For bounding box culling.

	struct mnode_s* parent;

	// Leaf specific
	int cluster;
	int area;

	msurface_t** firstmarksurface;
	int nummarksurfaces;
} mleaf_t;

#pragma endregion

#pragma region ========================== WHOLE MODEL ==========================

typedef enum
{
	mod_bad,
	mod_brush,
	mod_sprite,
	mod_alias,	//TODO: unused in H2.Remove?
	mod_unknown,//TODO: unused in H2.Remove?
	mod_fmdl,	// H2
	mod_book	// H2
} modtype_t;

typedef struct model_s
{
	char name[MAX_QPATH];
	int registration_sequence;
	modtype_t type;

	// Volume occupied by the model graphics.
	vec3_t mins;
	vec3_t maxs;
	float radius;

	// Brush model.
	int firstmodelsurface;
	int nummodelsurfaces;

	int numsubmodels;
	mmodel_t* submodels;

	int numplanes;
	cplane_t* planes;

	int numleafs; // Number of visible leafs, not counting 0.
	mleaf_t* leafs;

	int numvertexes;
	mvertex_t* vertexes;

	int numedges;
	medge_t* edges;

	int numnodes;
	int firstnode;
	mnode_t* nodes;

	int numtexinfo;
	mtexinfo_t* texinfo;

	int numsurfaces;
	msurface_t* surfaces;

	int numsurfedges;
	int* surfedges;

	int nummarksurfaces;
	msurface_t** marksurfaces;

	dvis_t* vis;
	byte* lightdata;

	// For models and skins.
	image_t* skins[MAX_FRAMES];

	int extradatasize;
	void* extradata;
} model_t;

#pragma endregion

#pragma region ========================== FLEXMODELS (from gl1_FlexModel.h) ==========================

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

#pragma region ========================== GLOBALS & PROTOTYPES (gl3_Model.c) ==========================

extern int registration_sequence;

// gl1: defined in gl1_Main.c. Homed in gl3_Model.c because gl3_Main.c is foundation-locked;
// the world module (R_MarkLeaves / R_SetupFrame ports) externs them from here.
extern model_t* r_worldmodel;
extern int r_viewcluster;
extern int r_viewcluster2;
extern int r_oldviewcluster;
extern int r_oldviewcluster2;

extern mleaf_t* Mod_PointInLeaf(vec3_t p, const model_t* model);
extern byte* Mod_ClusterPVS(int cluster, const model_t* model);

// NOTE: Mod_Init() / Mod_FreeAll() / Mod_Modellist_f() / RI_BeginRegistration() /
// RI_RegisterModel() / RI_EndRegistration() / RI_GetReferencedID() prototypes live
// in gl3_Local.h (module function set).

#pragma endregion

#pragma region ========================== CROSS-MODULE PROTOTYPES (implemented elsewhere) ==========================

// --- gl3_Lightmap.c (world module port; signatures mirror gl1_Lightmap.h) ---
extern void LM_BuildPolygonFromSurface(const model_t* mdl, msurface_t* fa);
extern void LM_CreateSurfaceLightmap(msurface_t* surf);
extern void LM_BeginBuildingLightmaps(void);
extern void LM_EndBuildingLightmaps(void);

// --- gl3_Warp.c (world module port; signature mirrors gl1_Warp.h) ---
extern void R_SubdivideSurface(const model_t* mdl, msurface_t* fa); //mxd. Added 'mdl' arg.

// --- gl3_FlexModel.c (flexmodel module port; signatures mirror gl1_FlexModel.h) ---
extern void Mod_LoadFlexModel(model_t* mod, void* buffer, int length);
extern void Mod_RegisterFlexModel(model_t* mod);

#pragma endregion
