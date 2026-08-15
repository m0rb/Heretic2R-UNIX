#include "compat.h"
//
// gl3_FlexModel.c -- Heretic 2 FlexModel loading and rendering
// (gl1_FlexModel.c port for the OpenGL 3.2 core renderer).
//
// The CPU pipeline (block loader, FrameLerp via the compiled-in Skeletons/
// sources, mesh-node iteration, shadedots lighting, reference write-back) is
// identical to gl1. The immediate-mode glcmds emission becomes the YQ2
// gl3_mesh.c technique: triangle strips/fans are converted to plain indexed
// triangles batched per mesh node (one glDrawElements() per node - nodes can
// switch texture/color/reflection state).
//
// GL_SPHERE_MAP texgen (RF_REFLECTION / FMNI_USE_REFLECT) is computed CPU-side
// per vertex (GL3_SphereMapST()) instead of using the si3DflexSphere program:
// the shared alias vertex layout has no normal attribute, and gl1 already runs
// this path per-vertex on the CPU anyway (glNormal3f per vertex).
//
// Copyright 1998 Raven Software
//

#include "gl3_Entity_internal.h"
#include "DG_dynarr.h"
#include "anormtab.h"
#include "anorms.h"
#include "Hunk.h"
#include "Vector.h"

#pragma region ========================== FLEX MODEL LOADING ==========================

static qboolean fmLoadHeader(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_HEADER_VER)
		ri.Sys_Error(ERR_DROP, "Invalid HEADER version for block %s: %d != %d\n", FM_HEADER_NAME, FM_HEADER_VER, version);

	// Read header...
	memcpy(&fmdl->header, buffer, sizeof(fmheader_t));

	// Sanity checks...
	const fmheader_t* h = &fmdl->header;

	if (h->skinwidth < 1 || h->skinwidth > SKINPAGE_WIDTH || h->skinheight < 1 || h->skinheight > SKINPAGE_HEIGHT) //mxd. Added SKINPAGE_WIDTH check.
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid skin size (%ix%i)", model->name, h->skinwidth, h->skinheight);

	if (h->num_xyz < 1 || h->num_xyz >= MAX_FM_VERTS)
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid number of vertices (%i)", model->name, h->num_xyz);

	if (h->num_st < 1)
		ri.Sys_Error(ERR_DROP, "Model '%s' has no st vertices", model->name);

	if (h->num_tris < 1 || h->num_tris >= MAX_FM_TRIANGLES) //mxd. Added MAX_FM_TRIANGLES check.
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid number of triangles (%i)", model->name, h->num_tris);

	if (h->num_frames < 1 || h->num_frames >= MAX_FM_FRAMES) //mxd. Added MAX_FM_FRAMES check.
		ri.Sys_Error(ERR_DROP, "Model '%s' has invalid number of frames (%i)", model->name, h->num_frames);

	VectorSet(model->mins, -32.0f, -32.0f, -32.0f);
	VectorSet(model->maxs, 32.0f, 32.0f, 32.0f);

	return true;
}

static qboolean fmLoadSkin(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_SKIN_VER)
		ri.Sys_Error(ERR_DROP, "Invalid SKIN version for block %s: %d != %d\n", FM_SKIN_NAME, FM_SKIN_VER, version);

	const int skin_names_size = fmdl->header.num_skins * MAX_FRAMENAME;
	if (skin_names_size != datasize)
	{
		ri.Con_Printf(PRINT_ALL, "Skin sizes do not match: %d != %d\n", datasize, skin_names_size);
		return false;
	}

	fmdl->skin_names = (char*)Hunk_Alloc(skin_names_size);
	memcpy(fmdl->skin_names, buffer, skin_names_size);

	// Precache skins...
	char* skin_name = fmdl->skin_names;
	for (int i = 0; i < fmdl->header.num_skins; i++, skin_name += MAX_FRAMENAME)
		model->skins[i] = R_FindImage(skin_name, it_skin);

	return true;
}

static qboolean fmLoadST(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	return true;
}

static qboolean fmLoadTris(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	return true;
}

static qboolean fmLoadFrames(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_FRAME_VER)
		ri.Sys_Error(ERR_DROP, "Invalid FRAMES version for block %s: %d != %d\n", FM_FRAME_NAME, FM_FRAME_VER, version);

	fmdl->frames = Hunk_Alloc(fmdl->header.num_frames * fmdl->header.framesize);

	for (int i = 0; i < fmdl->header.num_frames; i++)
	{
		const fmaliasframe_t* in = (const fmaliasframe_t*)((const byte*)buffer + i * fmdl->header.framesize);
		fmaliasframe_t* out = (fmaliasframe_t*)((byte*)fmdl->frames + i * fmdl->header.framesize);

		VectorCopy(in->scale, out->scale);
		VectorCopy(in->translate, out->translate);

		memcpy(out->name, in->name, sizeof(out->name));
		memcpy(out->verts, in->verts, fmdl->header.num_xyz * sizeof(fmtrivertx_t));
	}

	return true;
}

static qboolean fmLoadGLCmds(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_GLCMDS_VER)
		ri.Sys_Error(ERR_DROP, "Invalid GLCMDS version for block %s: %d != %d\n", FM_GLCMDS_NAME, FM_GLCMDS_VER, version);

	const uint size = fmdl->header.num_glcmds * sizeof(int);
	fmdl->glcmds = Hunk_Alloc((int)size);
	memcpy(fmdl->glcmds, buffer, size);

	return true;
}

static qboolean fmLoadMeshNodes(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_MESH_VER)
		ri.Sys_Error(ERR_DROP, "Invalid MESH version for block %s: %d != %d\n", FM_MESH_NAME, FM_MESH_VER, version);

	if (fmdl->header.num_mesh_nodes < 1)
		return true;

	fmdl->mesh_nodes = Hunk_Alloc(fmdl->header.num_mesh_nodes * (int)sizeof(fmmeshnode_t));

	const fmmeshnode_t* in = buffer;
	fmmeshnode_t* out = &fmdl->mesh_nodes[0];

	for (int i = 0; i < fmdl->header.num_mesh_nodes; i++, in++, out++)
	{
		//mxd. Don't copy tris and verts (unused).

		// Copy glcmds.
		out->start_glcmds = in->start_glcmds;
		out->num_glcmds = in->num_glcmds;
	}

	return true;
}

//mxd. FM_SHORT_FRAME, FM_NORMAL and FM_COMP blocks are never used in any of H2 models.
static qboolean fmSkipBlock(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	return false;
}

static qboolean fmLoadSkeleton(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	if (version != FM_SKELETON_VER)
	{
		ri.Con_Printf(PRINT_ALL, "Invalid SKELETON version for block %s: %d != %d\n", FM_SKELETON_NAME, FM_SKELETON_VER, version);
		return false;
	}

	const int* in_i = buffer;

	fmdl->skeletalType = *in_i;
	fmdl->rootCluster = CreateSkeleton(fmdl->skeletalType);

	const int num_clusters = *(++in_i);

	// Count and allocate verts...
	int num_verts = 0;
	for (int cluster = num_clusters - 1; cluster > -1; cluster--)
	{
		num_verts += *(++in_i);

		const int cluster_index = fmdl->rootCluster + cluster;
		SkeletalClusters[cluster_index].numVerticies = num_verts;
		SkeletalClusters[cluster_index].verticies = Hunk_Alloc(num_verts * (int)sizeof(int));
	}

	int start_vert_index = 0;
	for (int cluster = num_clusters - 1; cluster > -1; cluster--)
	{
		for (int v = start_vert_index; v < SkeletalClusters[fmdl->rootCluster + cluster].numVerticies; v++)
		{
			const int vert_index = *(++in_i);
			for (int c = 0; c <= cluster; c++)
				SkeletalClusters[fmdl->rootCluster + c].verticies[v] = vert_index;
		}

		start_vert_index = SkeletalClusters[fmdl->rootCluster + cluster].numVerticies;
	}

	// Check for duplicates...
	for (int i = 0; i < num_clusters; i++)
	{
		const int c = fmdl->rootCluster + i;
		for (int v1 = 0; v1 < SkeletalClusters[c].numVerticies - 1; v1++)
			for (int v2 = v1 + 1; v2 < SkeletalClusters[c].numVerticies; v2++)
				if (SkeletalClusters[c].verticies[v1] == SkeletalClusters[c].verticies[v2])
					ri.Con_Printf(PRINT_ALL, "Warning: duplicate skeletal cluster vertex: %d\n", SkeletalClusters[c].verticies[v1]); //mxd. Com_Printf() -> ri.Con_Printf().
	}

	const qboolean have_skeleton = *(++in_i);

	// Create skeleton.
	if (have_skeleton)
	{
		const float* in_f = (const float*)in_i;

		fmdl->skeletons = Hunk_Alloc(fmdl->header.num_frames * (int)sizeof(ModelSkeleton_t));

		for (int i = 0; i < fmdl->header.num_frames; i++)
		{
			CreateSkeletonAsHunk(fmdl->skeletalType, fmdl->skeletons + i);

			for (int c = 0; c < num_clusters; c++)
			{
				fmdl->skeletons[i].rootJoint[c].model.origin[0] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.origin[1] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.origin[2] = *(++in_f);

				fmdl->skeletons[i].rootJoint[c].model.direction[0] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.direction[1] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.direction[2] = *(++in_f);

				fmdl->skeletons[i].rootJoint[c].model.up[0] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.up[1] = *(++in_f);
				fmdl->skeletons[i].rootJoint[c].model.up[2] = *(++in_f);

				VectorCopy(fmdl->skeletons[i].rootJoint[c].model.origin,    fmdl->skeletons[i].rootJoint[c].parent.origin);
				VectorCopy(fmdl->skeletons[i].rootJoint[c].model.direction, fmdl->skeletons[i].rootJoint[c].parent.direction);
				VectorCopy(fmdl->skeletons[i].rootJoint[c].model.up,        fmdl->skeletons[i].rootJoint[c].parent.up);
			}
		}
	}
	else
	{
		fmdl->header.num_xyz -= num_clusters * 3;
	}

	return true;
}

static qboolean fmLoadReferences(fmdl_t* fmdl, model_t* model, const int version, const int datasize, const void* buffer)
{
	//mxd. Helper data type...
	typedef struct
	{
		int referenceType;
		qboolean haveRefs;
		Placement_t* refsForFrame;
	} dmreferences_t;

	if (version != FM_REFERENCES_VER)
	{
		ri.Con_Printf(PRINT_ALL, "Invalid REFERENCES version for block %s: %d != %d\n", FM_REFERENCES_NAME, FM_REFERENCES_VER, version);
		return false;
	}

	const dmreferences_t* in = buffer;
	fmdl->referenceType = in->referenceType;

	if (!in->haveRefs)
	{
		fmdl->header.num_xyz -= numReferences[fmdl->referenceType] * 3;
		return true;
	}

	const int num_refs = numReferences[fmdl->referenceType];
	fmdl->refsForFrame = Hunk_Alloc(fmdl->header.num_frames * num_refs * (int)sizeof(Placement_t));

	if (fmdl->header.num_frames < 1)
		return true;

	const Placement_t* ref_in = (const Placement_t*)&in->refsForFrame;
	Placement_t* ref_out = fmdl->refsForFrame;

	for (int i = 0; i < fmdl->header.num_frames; i++)
	{
		for (int j = 0; j < num_refs; j++, ref_in++, ref_out++)
		{
			for (int c = 0; c < 3; c++)
			{
				//TODO: done this way to perform byte-swapping (otherwise we can just memcpy the whole thing)?
				ref_out->origin[c] = ref_in->origin[c];
				ref_out->direction[c] = ref_in->direction[c];
				ref_out->up[c] = ref_in->up[c];
			}
		}
	}

	return true;
}

// FlexModel block loaders.
typedef struct
{
	char ident[FMDL_BLOCK_IDENT_SIZE];
	qboolean (*load)(fmdl_t* fmdl, model_t* model, int version, int datasize, const void* buffer);
} fmdl_loader_t;

static fmdl_loader_t fmblocks[] =
{
	{ FM_HEADER_NAME,		fmLoadHeader },
	{ FM_SKIN_NAME,			fmLoadSkin },
	{ FM_ST_NAME,			fmLoadST },
	{ FM_TRI_NAME,			fmLoadTris },
	{ FM_FRAME_NAME,		fmLoadFrames },
	{ FM_GLCMDS_NAME,		fmLoadGLCmds },
	{ FM_MESH_NAME,			fmLoadMeshNodes },
	{ FM_SHORT_FRAME_NAME,	fmSkipBlock },
	{ FM_NORMAL_NAME,		fmSkipBlock },
	{ FM_COMP_NAME,			fmSkipBlock },
	{ FM_SKELETON_NAME,		fmLoadSkeleton },
	{ FM_REFERENCES_NAME,	fmLoadReferences },
	{ "",					NULL },
};

void Mod_LoadFlexModel(model_t* mod, void* buffer, int length)
{
	mod->type = mod_fmdl;

	// Stored in mod->extradata.
	fmdl_t* fmdl = Hunk_Alloc(sizeof(fmdl_t));
	fmdl->skeletalType = SKEL_NULL;
	fmdl->referenceType = REF_NULL;

	byte* in = buffer;

	while (length > 0)
	{
		fmdl_blockheader_t* header = (fmdl_blockheader_t*)in;
		in += sizeof(fmdl_blockheader_t); // Block data is stored after block header.

		// Find appropriate loader...
		fmdl_loader_t* loader;
		for (loader = &fmblocks[0]; loader->ident[0] != 0; loader++)
		{
			if (Q_stricmp(loader->ident, header->ident) == 0)
			{
				if (!loader->load(fmdl, mod, header->version, header->size, in)) //mxd. Added sanity check.
				{
					ri.Com_Error(ERR_DROP, "Mod_LoadFlexModel: failed to load block %s\n", header->ident); //mxd. Com_Error() -> ri.Com_Error().
					return;
				}

				break;
			}
		}

		if (loader->ident[0] == 0)
			ri.Con_Printf(PRINT_ALL, "Mod_LoadFlexModel: unknown block %s\n", header->ident);

		in += header->size; // Skip to next block header...
		length -= (header->size + (int)sizeof(fmdl_blockheader_t));
	}

	//mxd. Never null?
	assert(fmdl->frames != NULL);
}

void Mod_RegisterFlexModel(model_t* mod)
{
	const fmdl_t* fmdl = (fmdl_t*)mod->extradata;

	// Precache skins... //TODO: also done in fmLoadSkin(). One of these isn't needed?
	if (fmdl != NULL && fmdl->skin_names != NULL && fmdl->header.num_skins > 0)
	{
		char* skin_name = fmdl->skin_names;
		for (int i = 0; i < fmdl->header.num_skins; i++, skin_name += MAX_FRAMENAME)
			mod->skins[i] = R_FindImage(skin_name, it_skin);
	}
}

#pragma endregion

#pragma region ========================== FLEX MODEL RENDERING ==========================

// YQ2 gl3_mesh.c: strips/fans are converted to indexed triangles and drawn with
// a single glDrawElements() per mesh node (buffers are static globals so we don't
// malloc()/free() for each rendered model).
DA_TYPEDEF(gl3_alias_vtx_t, FlexVtxArray_t);
DA_TYPEDEF(GLushort, FlexIdxArray_t);
static FlexVtxArray_t vtxBuf = { 0 };
static FlexIdxArray_t idxBuf = { 0 };

// Modelview matrix (view * model) of the current flexmodel entity - the CPU-side
// GL_SPHERE_MAP texgen needs eye-space positions/normals.
static hmm_mat4 flex_mvMat;

//mxd. Somewhat similar to R_CullAliasModel from Q2.
static qboolean R_CullFlexModel(const fmdl_t* fmdl, entity_t* e)
{
	vec3_t mins;
	vec3_t maxs;

	if (e->frame < 0 || e->frame >= fmdl->header.num_frames)
		e->frame = 0;

	if (e->oldframe < 0 || e->oldframe >= fmdl->header.num_frames)
		e->oldframe = 0;

	// Compute axially aligned mins and maxs.
	if (fmdl->frames == NULL)
	{
		VectorCopy(fmdl->compdata[fmdl->frame_to_group[e->frame]].bmin, mins);
		VectorCopy(fmdl->compdata[fmdl->frame_to_group[e->frame]].bmax, maxs);
	}
	else
	{
		const fmaliasframe_t* pframe = (fmaliasframe_t*)((byte*)fmdl->frames + e->frame * fmdl->header.framesize);
		const fmaliasframe_t* poldframe = (fmaliasframe_t*)((byte*)fmdl->frames + e->oldframe * fmdl->header.framesize);

		if (pframe == poldframe)
		{
			for (int i = 0; i < 3; i++)
			{
				mins[i] = pframe->translate[i];
				maxs[i] = mins[i] + pframe->scale[i] * 255.0f;
			}
		}
		else
		{
			for (int i = 0; i < 3; i++)
			{
				const float thismins = pframe->translate[i];
				const float thismaxs = thismins + pframe->scale[i] * 255.0f;

				const float oldmins = poldframe->translate[i];
				const float oldmaxs = oldmins + poldframe->scale[i] * 255.0f;

				mins[i] = min(thismins, oldmins);
				maxs[i] = max(thismaxs, oldmaxs);
			}
		}
	}

	// Apply model scale.
	if (e->cl_scale != 0.0f && e->cl_scale != 1.0f)
	{
		Vec3ScaleAssign(e->cl_scale, mins);
		Vec3ScaleAssign(e->cl_scale, maxs);
	}

	// Compute a full bounding box.
	vec3_t bbox[8];
	for (int i = 0; i < 8; i++)
	{
		bbox[i][0] = (i & 1 ? mins[0] : maxs[0]);
		bbox[i][1] = (i & 2 ? mins[1] : maxs[1]);
		bbox[i][2] = (i & 4 ? mins[2] : maxs[2]);
	}

	// Rotate the bounding box.
	const vec3_t angles =
	{
		e->angles[0] * RAD_TO_ANGLE,
		e->angles[1] * RAD_TO_ANGLE * -1.0f,
		e->angles[2] * RAD_TO_ANGLE,
	};

	vec3_t vectors[3];
	AngleVectors(angles, vectors[0], vectors[1], vectors[2]);

	for (int i = 0; i < 8; i++)
	{
		const vec3_t tmp = VEC3_INIT(bbox[i]);

		bbox[i][0] = DotProduct(vectors[0], tmp);
		bbox[i][1] = -DotProduct(vectors[1], tmp);
		bbox[i][2] = DotProduct(vectors[2], tmp);

		Vec3AddAssign(e->origin, bbox[i]);
	}

	int aggregatemask = -1;

	for (int p = 0; p < 8; p++)
	{
		int mask = 0;

		for (int f = 0; f < 4; f++)
		{
			const float dp = DotProduct(frustum[f].normal, bbox[p]);
			if (dp - frustum[f].dist < 0.0f)
				mask |= 1 << f;
		}

		aggregatemask &= mask;
	}

	return aggregatemask != 0;
}

static image_t* R_GetSkin(const entity_t* ent) //mxd. Rewrote to use entity_t* arg instead of 'currententity'.
{
	if (ent->skin != NULL)
		return ent->skin;

	const int skinnum = (ent->skinnum < MAX_FRAMES ? ent->skinnum : 0);
	const model_t* mdl = *ent->model;

	if (mdl->skins[skinnum] != NULL)
		return mdl->skins[skinnum];

	if (mdl->skins[0] != NULL)
		return mdl->skins[0];

	return r_notexture;
}

static image_t* R_GetSkinFromNode(const entity_t* ent, const int skin_index) //mxd. Rewrote to use entity_t* arg instead of 'currententity'.
{
	image_t* skin;
	const model_t* mdl = *ent->model;

	if (ent->skin != NULL && ent->skins != NULL)
		skin = ent->skins[skin_index];
	else
		skin = mdl->skins[skin_index];

	if (skin != NULL)
		return skin;

	if (ent->skin != NULL)
		return ent->skin;

	if (mdl->skins[0] != NULL)
		return mdl->skins[0];

	return r_notexture;
}

static void R_InterpolateVertexNormals(const int num_xyz, const float lerp, const fmtrivertx_t* verts, const fmtrivertx_t* old_verts, vec3_t* normals)
{
	const fmtrivertx_t* v = &verts[0];
	const fmtrivertx_t* ov = &old_verts[0];
	vec3_t* n = &normals[0];

	for (int i = 0; i < num_xyz; i++, v++, ov++, n++)
		VectorLerp(bytedirs[v->lightnormalindex], lerp, bytedirs[ov->lightnormalindex], *n);
}

static void R_EnableReflection(void) //mxd. Added to reduce code duplication.
{
	// gl1: glEnable(GL_TEXTURE_GEN_S/T) + GL_SPHERE_MAP texgen. In gl3 the sphere-map
	// texture coordinates are computed CPU-side (GL3_SphereMapST()) - only the texture
	// bind remains here.
	GL3_BindImage(r_reflecttexture);
}

// CPU-side equivalence of fixed-function GL_SPHERE_MAP texgen (GL 1.x spec 2.10.4):
// u = normalized eye-space vertex position, n = normalized eye-space normal,
// r = reflect(u, n), m = 2*sqrt(rx^2 + ry^2 + (rz+1)^2), (s,t) = r.xy / m + 0.5.
static void GL3_SphereMapST(const vec3_t pos, const vec3_t normal, GLfloat* st)
{
	const float (*mv)[4] = flex_mvMat.Elements; // Column-major.

	// Eye-space vertex position (mv * vec4(pos, 1)).
	vec3_t u =
	{
		mv[0][0] * pos[0] + mv[1][0] * pos[1] + mv[2][0] * pos[2] + mv[3][0],
		mv[0][1] * pos[0] + mv[1][1] * pos[1] + mv[2][1] * pos[2] + mv[3][1],
		mv[0][2] * pos[0] + mv[1][2] * pos[1] + mv[2][2] * pos[2] + mv[3][2],
	};

	// Eye-space normal (mat3(mv) * normal - the H2 entity modelview is rotation +
	// translation only (entity scale is baked into the lerped verts), so the upper
	// 3x3 works without an inverse-transpose).
	vec3_t n =
	{
		mv[0][0] * normal[0] + mv[1][0] * normal[1] + mv[2][0] * normal[2],
		mv[0][1] * normal[0] + mv[1][1] * normal[1] + mv[2][1] * normal[2],
		mv[0][2] * normal[0] + mv[1][2] * normal[1] + mv[2][2] * normal[2],
	};

	VectorNormalize(u);
	VectorNormalize(n); // Interpolated normals aren't unit-length; texgen math assumes unit n.

	// r = reflect(u, n).
	const float dp2 = 2.0f * DotProduct(u, n);
	const vec3_t r = { u[0] - dp2 * n[0], u[1] - dp2 * n[1], u[2] - dp2 * n[2] };

	const float m = 2.0f * sqrtf(r[0] * r[0] + r[1] * r[1] + (r[2] + 1.0f) * (r[2] + 1.0f));

	if (m > 0.0f)
	{
		st[0] = r[0] / m + 0.5f;
		st[1] = r[1] / m + 0.5f;
	}
	else // Degenerate case (r == (0, 0, -1)); GL leaves s,t at 0.5 here too.
	{
		st[0] = 0.5f;
		st[1] = 0.5f;
	}
}

// Draws the current mesh node's accumulated triangles with the flex program
// (single glDrawElements() - YQ2 gl3_mesh.c batching).
static void GL3_FlushFlexNode(void)
{
	if (da_count(idxBuf) > 0)
	{
		GL3_UseProgram(gl3state.si3Dflex.shaderProgram);
		GL3_BindVAO(gl3state.vaoAlias);
		GL3_BindVBO(gl3state.vboAlias);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(da_count(vtxBuf) * sizeof(gl3_alias_vtx_t)), vtxBuf.p, GL_STREAM_DRAW);

		GL3_BindEBO(gl3state.eboAlias);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(da_count(idxBuf) * sizeof(GLushort)), idxBuf.p, GL_STREAM_DRAW);

		glDrawElements(GL_TRIANGLES, (GLsizei)da_count(idxBuf), GL_UNSIGNED_SHORT, NULL);
	}

	da_clear(vtxBuf);
	da_clear(idxBuf);
}

static void R_DrawFlexFrameLerp(const fmdl_t* fmdl, entity_t* e, vec3_t shadelight) //mxd. Original logic uses 'fmodel', 'currententity', 'framelerp' and 'shadelight' global vars.
{
	static vec3_t normals_array[MAX_FM_VERTS]; //mxd. Made static.

	const qboolean draw_reflection = (e->flags & RF_REFLECTION); //mxd. Skipped gl_envmap_broken check.
	const image_t* skin = R_GetSkin(e);
	float alpha = 1.0f; //mxd. Set in Loki Linux version, but not in Windows version.

	if (e->color.a != 255 || e->flags & RF_TRANS_ANY || skin->has_alpha)
	{
		if (e->flags & RF_TRANS_GHOST)
			alpha = shadelight[0] * 0.5f;
		else
			alpha = (float)e->color.a / 255.0f;

		R_HandleTransparency(e);
	}

	if (!(int)r_frameswap->value)
		e->swapFrame = NO_SWAP_FRAME;

	FrameLerp(fmdl, e); // Also lerps + writes back e->referenceInfo placements (LerpReferences() in Skeletons/r_SkeletonLerp.c).

	if (draw_reflection)
	{
		if (fmdl->frames != NULL)
			R_InterpolateVertexNormals(fmdl->header.num_xyz, e->backlerp, sfl_cur_skel.verts, sfl_cur_skel.old_verts, normals_array);

		R_EnableReflection();
	}

	fmnodeinfo_t* nodeinfo = &e->fmnodeinfo[0];
	for (int i = 0; i < fmdl->header.num_mesh_nodes; i++, nodeinfo++)
	{
		qboolean use_color = false;
		qboolean use_skin = false;
		qboolean use_reflect = false;

		if (nodeinfo != NULL)
		{
			if (nodeinfo->flags & FMNI_NO_DRAW)
				continue;

			use_color = (nodeinfo->flags & FMNI_USE_COLOR);
			if (use_color)
			{
				glEnable(GL_BLEND);
				GL3_SetCurrentColorRGBA(nodeinfo->color); // glColor4ub(nodeinfo->color.r, .g, .b, .a).
			}

			if ((nodeinfo->flags & FMNI_USE_REFLECT) && !draw_reflection)
			{
				use_skin = true;
				use_reflect = true;

				R_EnableReflection();
			}
			else if (nodeinfo->flags & FMNI_USE_SKIN)
			{
				use_skin = true;
				GL3_BindImage(R_GetSkinFromNode(e, nodeinfo->skin));
			}
		}

		const int start_glcmds = fmdl->mesh_nodes[i].start_glcmds; // short -> int (sign-extend).
		if (start_glcmds < 0 || start_glcmds >= fmdl->header.num_glcmds)
		{
			ri.Con_Printf(PRINT_ALL, "R_DrawFlexFrameLerp: mesh node %d has out-of-range start_glcmds %d (num_glcmds=%d)\n",
				i, start_glcmds, fmdl->header.num_glcmds);
			continue;
		}

		int* order = &fmdl->glcmds[start_glcmds];

		while (true)
		{
			// Get the vertex count and primitive type.
			int num_verts = *order++;
			if (num_verts == 0)
				break; // Done.

			qboolean fan = false; // glBegin(GL_TRIANGLE_STRIP).
			if (num_verts < 0)
			{
				num_verts = -num_verts;
				fan = true; // glBegin(GL_TRIANGLE_FAN).
			}

			const GLushort first_vtx = (GLushort)da_count(vtxBuf);
			int emitted = 0; // gl1 skips out-of-range verts, so the strip/fan can end up shorter than num_verts.

			for (int c = 0; c < num_verts; c++)
			{
				const int index_xyz = order[2];

				if (index_xyz < 0 || index_xyz >= fmdl->header.num_xyz)
				{
					order += 3;
					continue;
				}

				gl3_alias_vtx_t vtx;

				if (draw_reflection || use_reflect)
				{
					vec3_t* normal;
					if (fmdl->frames == NULL)
						normal = &bytedirs[fmdl->lightnormalindex[index_xyz]];
					else if (draw_reflection)
						normal = &normals_array[index_xyz];
					else
						normal = &bytedirs[sfl_cur_skel.verts[index_xyz].lightnormalindex];

					// glNormal3f() + GL_SPHERE_MAP texgen -> CPU-side sphere-map ST.
					GL3_SphereMapST(s_lerped[index_xyz], *normal, vtx.texCoord);
				}
				else
				{
					// Texture coordinates come from the draw list.
					vtx.texCoord[0] = ((float*)order)[0];
					vtx.texCoord[1] = ((float*)order)[1];
				}

				order += 3;

				if (!use_color && !(e->flags & RF_FULLBRIGHT))
				{
					float l;
					if (fmdl->frames == NULL)
					{
						l = shadedots[fmdl->lightnormalindex[index_xyz]];
					}
					else
					{
						//mxd. Interpolate light scaler.
						const float cl = shadedots[sfl_cur_skel.verts[index_xyz].lightnormalindex];
						const float ol = shadedots[sfl_cur_skel.old_verts[index_xyz].lightnormalindex];

						l = LerpFloat(cl, ol, e->backlerp);
					}

					// glColor4f(l * shadelight[0..2], alpha).
					vtx.color[0] = l * shadelight[0];
					vtx.color[1] = l * shadelight[1];
					vtx.color[2] = l * shadelight[2];
					vtx.color[3] = alpha;
				}
				else
				{
					// gl1: the current glColor applies (node color / R_HandleTransparency() color / white).
					for (int j = 0; j < 4; j++)
						vtx.color[j] = gl3_currentDrawColor[j];
				}

				VectorCopy(s_lerped[index_xyz], vtx.pos); // glVertex3fv(s_lerped[index_xyz]).

				da_push(vtxBuf, vtx);
				emitted++;
			}

			// glEnd(): translate the fan/strip to plain triangle indices (YQ2 gl3_mesh.c).
			if (emitted >= 3)
			{
				if (fan)
				{
					for (GLushort v = 1; v < (GLushort)(emitted - 1); v++)
					{
						GLushort* add = da_addn_uninit(idxBuf, 3);

						add[0] = first_vtx;
						add[1] = first_vtx + v;
						add[2] = first_vtx + v + 1;
					}
				}
				else // Triangle strip.
				{
					GLushort v;
					for (v = 1; v < (GLushort)(emitted - 2); v += 2)
					{
						// Add two triangles at once, because the vertex order is different for odd vs even triangles.
						GLushort* add = da_addn_uninit(idxBuf, 6);

						add[0] = first_vtx + v - 1;
						add[1] = first_vtx + v;
						add[2] = first_vtx + v + 1;

						add[3] = first_vtx + v;
						add[4] = first_vtx + v + 2;
						add[5] = first_vtx + v + 1;
					}

					// Add remaining triangle, if any.
					if (v < (GLushort)(emitted - 1))
					{
						GLushort* add = da_addn_uninit(idxBuf, 3);

						add[0] = first_vtx + v - 1;
						add[1] = first_vtx + v;
						add[2] = first_vtx + v + 1;
					}
				}
			}
		}

		// Draw everything this mesh node emitted (texture/color state is per-node).
		GL3_FlushFlexNode();

		// if (use_reflect): glDisable(GL_TEXTURE_GEN_S/T) - no-op in gl3 (CPU-side texgen).

		if (use_skin)
			GL3_BindImage(skin);
	}

	if (draw_reflection)
	{
		// glDisable(GL_TEXTURE_GEN_S/T): no-op in gl3 (CPU-side texgen).
		GL3_BindImage(skin);
	}

	R_CleanupTransparency(e);
}

//mxd. Somewhat similar to R_DrawAliasModel from Q2. Original code used 'currententity' global var instead of 'e' arg.
void R_DrawFlexModel(entity_t* e)
{
	const fmdl_t* fmdl = (fmdl_t*)(*e->model)->extradata; //mxd. Original code used 'currentmodel' global var here.

	if (R_CullFlexModel(fmdl, e))
		return;

	// Get lighting information.
	vec3_t shadelight;
	qboolean apply_minlight = false; //mxd

	if (e->flags & RF_TRANS_ADD_ALPHA)
	{
		const float alpha = (float)e->color.a / 255.0f;
		VectorSet(shadelight, alpha, alpha, alpha);
	}
	else if (e->flags & RF_FULLBRIGHT)
	{
		VectorSet(shadelight, 1.0f, 1.0f, 1.0f);
	}
	else if (e->absLight.r != 0 || e->absLight.g != 0 || e->absLight.b != 0)
	{
		VectorSet(shadelight, (float)e->absLight.r / 255.0f, (float)e->absLight.g / 255.0f, (float)e->absLight.b / 255.0f);
		apply_minlight = true; //mxd
	}
	else if (!(e->flags & RF_GLOW)) //mxd. Skip when result is going to be ignored.
	{
		R_LightPoint(e->origin, shadelight, true); //mxd. Skip RF_WEAPONMODEL logic (never set in H2), skip gl_monolightmap logic.
		apply_minlight = true; //mxd
	}

	// YQ2. Apply minlight?
	if (apply_minlight && gl3state.minlight_set)
	{
		for (int i = 0; i < 3; i++)
		{
			const int l = (int)(shadelight[i] * 255.0f);
			shadelight[i] = (float)minlight[min(255, l)] / 255.0f;
		}
	}

	for (int i = 0; i < 3; i++)
		shadelight[i] *= (float)e->color.c_array[i] / 255.0f;

	if ((e->flags & RF_MINLIGHT) && shadelight[0] <= 0.1f && shadelight[1] <= 0.1f && shadelight[2] <= 0.1f)
		VectorSet(shadelight, 0.1f, 0.1f, 0.1f);

	if (e->flags & RF_GLOW)
	{
		// Bonus items will pulse with time.
		const float val = sinf(r_newrefdef.time * 7.0f) * 0.3f + 0.7f;
		VectorSet(shadelight, val, val, val);
	}

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0f * RAD_TO_ANGLE)) & (SHADEDOT_QUANT - 1))];

	// Locate the proper data.
	c_alias_polys += fmdl->header.num_tris;

	// Draw all the triangles.
	if (e->flags & RF_DEPTHHACK) // Hack the depth range to prevent view model from poking into walls.
		glDepthRange(gldepthmin, (gldepthmax - gldepthmin) * 0.3f + gldepthmin);

	// glPushMatrix() + R_RotateForEntity(e): entity matrix onto uni3D transModelMat4.
	R_RotateForEntity(e);

	// Modelview for the CPU-side sphere-map texgen (transModelMat4 now holds the entity matrix).
	flex_mvMat = HMM_MultiplyMat4(gl3state.viewMat3D, gl3state.uni3DData.transModelMat4);

	// Select skin.
	GL3_BindImage(R_GetSkin(e));

	// Draw it.
	// glShadeModel(GL_SMOOTH) + R_TexEnv(GL_MODULATE): inherent in the flex shader
	// (interpolated per-vertex color x texture).

	if (e->frame < 0 || e->frame >= fmdl->header.num_frames)
	{
		e->frame = 0;
		e->oldframe = 0;
	}

	if (e->oldframe < 0 || e->oldframe >= fmdl->header.num_frames)
	{
		ri.Con_Printf(PRINT_ALL, "R_DrawFlexModel: no such oldframe %d\n", e->oldframe); //mxd. --gl3: added missing arg (gl1 passes none).
		e->frame = 0;
		e->oldframe = 0;
	}

	if (!(int)r_lerpmodels->value)
		e->backlerp = 0.0f;

	R_DrawFlexFrameLerp(fmdl, e, shadelight);

	// R_TexEnv(GL_REPLACE) + glShadeModel(GL_FLAT): no-ops in gl3 (shader-determined).
	GL3_RestoreModelIdentity(); // glPopMatrix().

	if (e->flags & RF_TRANS_ANY)
		glDisable(GL_BLEND);

	if (e->flags & RF_DEPTHHACK)
		glDepthRange(gldepthmin, gldepthmax);

	GL3_SetCurrentColor(1.0f, 1.0f, 1.0f, 1.0f); // glColor4f(1, 1, 1, 1).
}

// Also called by LerpReferences() in Skeletons/r_SkeletonLerp.c (compiled in).
void R_LerpVert(const vec3_t new_point, const vec3_t old_point, vec3_t interpolated_point, const float move[3], const float frontv[3], const float backv[3])
{
	for (int i = 0; i < 3; i++)
		interpolated_point[i] = new_point[i] * frontv[i] + old_point[i] * backv[i] + move[i];
}

#pragma endregion
