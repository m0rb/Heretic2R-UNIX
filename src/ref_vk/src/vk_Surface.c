#include "compat.h"
//
// vk_Surface.c -- world / brush-model surface rendering.
//
// H2 semantics ported from gl1_Surface.c via the validated gl3_Surface.c port
// (plus the world-frame helpers R_SetFrustum() / viewcluster setup from
// gl1_Main.c and the fog block setup from gl3_Main.c's R_Clear() flow, which
// vk_Main.c does not own in this file layout); Vulkan draw submission from
// yq2 vk_surf.c:
//  - The gl1 single-pass multitexture path becomes vk_drawPolyLmapPipeline
//    (polygon_lmap shaders: 4 lightstyle sub-lightmap samplers weighted by the
//    per-draw UBO lmScales + the H2 fog block - gl3 si3Dlm semantics).
//  - glpoly_t keeps the gl1 7-float vertex layout (it is the model-data ABI
//    shared with gl1_FindSurface.c); polys are converted to vk_3D_vtx_t
//    scratch verts at draw time and streamed through the QVk dynamic
//    vertex/index buffers as fan-indexed triangle lists (all world pipelines
//    are TRIANGLE_LIST - yq2 R_GenFanIndexes() technique).
//  - The world path pushes the real H2ColorGrade trio (QVk_PushWorldFragmentConstants)
//    so basic.frag / polygon_lmap.frag grade per-fragment (gl3 parity); the
//    postprocess blit no longer grades.
//
// Copyright 1998 Raven Software
//

#include "vk_World_internal.h"
#include "Angles.h"

//mxd. Reconstructed data type. Original name unknown.
typedef struct
{
	union
	{
		entity_t* entity;
		msurface_t* surface;
	};
	float depth;
} AlphaSurfaceSortInfo_t;

int c_visible_lightmaps;
int c_visible_textures;

// Defined in gl1_Main.c in gl1; owned by the world module in vk.


// View-projection matrix of the current 3D scene - computed by the frame
// module (vk_Main.c R_SetupVulkan port, yq2 vk_main.c). Tentative definition
// (-fcommon) so the world module links before the frame port lands.

// Current model (entity) matrix (vk replacement for the gl1 modelview stack /
// gl3 transModelMat4) and the "ambient" surface alpha / alpha test state
// (gl1 ambient glColor alpha + glAlphaFunc ref) - see vk_World_internal.h.
float r_local_model_matrix[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
float r_surf_alpha = 1.0f;
float r_surf_alpha_test = -1.0f;

// Current fog block (gl1 R_Fog()/R_WaterFog(), gl3 uni3D fog members) -
// copied into every per-draw world/model UBO. Filled by R_SetupFog().
vkfogblock_t r_world_fog = { .fogMode = -1 };

static int r_visframecount; // Bumped when going to a new PVS // Q2: defined in gl_rmain.c //mxd. Moved here & made static.
static qboolean multitexture_mode; // H2 (vk: "multitexture" means the single-pass lightmapped pipeline; name kept for gl1 parity).

static vec3_t modelorg; // Relative to viewpoint.

static msurface_t* r_alpha_surfaces;

// vk: replaces gl1's ambient glColor4f(1.0f, 1.0f, 1.0f, 0.25f) set by R_DrawInlineBModel() for RF_TRANS_ANY brush models.
static float r_bmodel_alpha = 1.0f;

#pragma region ========================== BACKEND HELPERS ==========================

static vk_3D_vtx_t poly_vtx[MAX_POLY_VERTS];
static vk_alias_vtx_t poly_color_vtx[MAX_POLY_VERTS];

// Streams a triangle fan through the currently bound pipeline as an indexed
// triangle list (yq2 R_GenFanIndexes() technique - every world pipeline uses
// TRIANGLE_LIST topology). Pipeline, descriptor sets and push constants must
// already be bound/pushed by the caller.
void QVk_DrawTriangleFan(const void* verts, const VkDeviceSize vert_size, const int numverts)
{
	static uint16_t fan_indices[(MAX_POLY_VERTS - 2) * 3];

	if (numverts < 3 || numverts > MAX_POLY_VERTS)
		ri.Sys_Error(ERR_DROP, "QVk_DrawTriangleFan: bad vertex count (%i)", numverts);

	VkBuffer vbo;
	VkDeviceSize vbo_offset;
	uint8_t* vert_data = QVk_GetVertexBuffer(vert_size * numverts, &vbo, &vbo_offset);
	memcpy(vert_data, verts, vert_size * numverts);

	const int num_indices = (numverts - 2) * 3;
	uint16_t* idx = fan_indices;
	for (int i = 0; i < numverts - 2; i++)
	{
		*idx++ = 0;
		*idx++ = (uint16_t)(i + 1);
		*idx++ = (uint16_t)(i + 2);
	}

	VkDeviceSize ibo_offset;
	VkBuffer* ibo = UpdateIndexBuffer(fan_indices, num_indices * sizeof(uint16_t), &ibo_offset);

	vkCmdBindVertexBuffers(vk_activeCmdbuffer, 0, 1, &vbo, &vbo_offset);
	vkCmdBindIndexBuffer(vk_activeCmdbuffer, *ibo, ibo_offset, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(vk_activeCmdbuffer, num_indices, 1, 0, 0, 0);
}

// Converts a gl1-layout poly (float[7]: xyz s1t1 s2t2) to vk_3D_vtx_t scratch verts.
// Normal comes from the surface plane (yq2), lightFlags from the dlight marking
// (yq2 SetLightFlags(); gl_dynamic gates dynamic lights like gl1's lightmap-update
// logic did - the locked polygon_lmap shader doesn't consume them YET, but the
// vertex plumbing is in place for a dynamic-light shader extension).
// 'sscroll' is the SURF_FLOWING scroll in normalized texture units (gl3 restored
// Q2's scrolling on lit surfaces via si3DlmFlow; vk applies it CPU-side).
static const vk_3D_vtx_t* R_BuildPolyVerts(const msurface_t* surf, const glpoly_t* p, const float sscroll)
{
	if (p->numverts > MAX_POLY_VERTS)
		ri.Sys_Error(ERR_DROP, "R_BuildPolyVerts: too many verts (%i)", p->numverts);

	vec3_t normal;
	VectorCopy(surf->plane->normal, normal);

	if (surf->flags & SURF_PLANEBACK)
		VectorScale(normal, -1.0f, normal); // YQ2: invert, so it's usable for the shader.

	uint32_t light_flags = 0;
	if ((int)gl_dynamic->value && surf->dlightframe == r_framecount)
		light_flags = (uint32_t)surf->dlightbits;

	vk_3D_vtx_t* vtx = &poly_vtx[0];
	const float* v = p->verts[0];

	for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE, vtx++)
	{
		VectorCopy(v, vtx->pos);
		vtx->texCoord[0] = v[3] + sscroll;
		vtx->texCoord[1] = v[4];
		vtx->lmTexCoord[0] = v[5];
		vtx->lmTexCoord[1] = v[6];
		VectorCopy(normal, vtx->normal);
		vtx->lightFlags = light_flags;
	}

	return poly_vtx;
}

// Draws vk_3D_vtx_t verts through vk_drawPolyPipeline (polygon.vert +
// basic.frag): constant-color UBO, mvp push constant, neutral world grade +
// alpha test ref. The trans33/66 / underwater / quake-floor / plain-textured
// path (gl3: si3Dtrans program).
void R_DrawPolyVerts(const vk_3D_vtx_t* verts, const int numverts, const image_t* image, const float color[4], const float alpha_test_ref)
{
	if (!R_ImageUsable(image))
		return; // Module-port ordering guard (world textures come from vk_Image.c).

	// polygon.vert has no model matrix in its UBO - compose it into the push mvp.
	float mvp[16];
	Mat4_Multiply(r_local_model_matrix, r_viewproj_matrix, mvp);

	uint32_t ubo_offset;
	VkDescriptorSet ubo_set;
	uint8_t* ubo = QVk_GetUniformBuffer(4 * sizeof(float), &ubo_offset, &ubo_set);
	memcpy(ubo, color, 4 * sizeof(float));

	QVk_BindPipeline(&vk_drawPolyPipeline);

	const VkDescriptorSet desc_sets[] = { image->vk_texture.descriptorSet, ubo_set };
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk_drawPolyPipeline.layout, 0, 2, desc_sets, 1, &ubo_offset);

	QVk_PushMatrix(&vk_drawPolyPipeline, mvp);
	QVk_PushWorldFragmentConstants(&vk_drawPolyPipeline, alpha_test_ref);

	QVk_DrawTriangleFan(verts, sizeof(vk_3D_vtx_t), numverts);
}

// Q2 counterpart. Draws fa->polys with the polygon pipeline / given color state.
static void R_DrawGLPoly(const msurface_t* fa, const image_t* image, const float color[4], const float alpha_test_ref)
{
	R_DrawPolyVerts(R_BuildPolyVerts(fa, fa->polys, 0.0f), fa->polys->numverts, image, color, alpha_test_ref);
}

// Draws an untextured constant-color poly (gl1: glDisable(GL_TEXTURE_2D) +
// glColor + glVertex): per-vertex color through the alias vertex layout and
// the flexmodel pipeline with ubo.textured == 0 (gl3: si3Dsprite + white
// texture; the vk model.frag has a real untextured path - and a fog block,
// so drawflat polys stay fogged like gl1).
static void R_DrawColorPoly(const glpoly_t* p, const paletteRGBA_t color)
{
	if (p->numverts > MAX_POLY_VERTS)
		ri.Sys_Error(ERR_DROP, "R_DrawColorPoly: too many verts (%i)", p->numverts);

	// model.frag statically references sTexture even on the untextured path -
	// a valid sampler descriptor set must be bound (r_notexture is created by
	// the vk_Draw.c module port).
	if (!R_ImageUsable(r_notexture))
		return;

	vk_alias_vtx_t* vtx = &poly_color_vtx[0];
	const float* v = p->verts[0];

	for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE, vtx++)
	{
		VectorCopy(v, vtx->pos);
		vtx->texCoord[0] = 0.0f;
		vtx->texCoord[1] = 0.0f;

		for (int c = 0; c < 4; c++)
			vtx->color[c] = (float)color.c_array[c] / 255.0f;
	}

	vkmodelubo_t ubo_data;
	memcpy(ubo_data.model, r_local_model_matrix, sizeof(ubo_data.model));
	ubo_data.fog = r_world_fog;
	ubo_data.textured = 0;

	uint32_t ubo_offset;
	VkDescriptorSet ubo_set;
	uint8_t* ubo = QVk_GetUniformBuffer(sizeof(ubo_data), &ubo_offset, &ubo_set);
	memcpy(ubo, &ubo_data, sizeof(ubo_data));

	qvkpipeline_t* pipeline = &vk_drawModelPipelineFan[RP_WORLD];
	QVk_BindPipeline(pipeline);

	const VkDescriptorSet desc_sets[] = { r_notexture->vk_texture.descriptorSet, ubo_set };
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline->layout, 0, 2, desc_sets, 1, &ubo_offset);

	QVk_PushMatrix(pipeline, r_viewproj_matrix); // model.vert: vpMatrix push, model matrix in the UBO.
	QVk_PushWorldFragmentConstants(pipeline, -1.0f);

	QVk_DrawTriangleFan(poly_color_vtx, sizeof(vk_alias_vtx_t), p->numverts);
}

#pragma endregion

#pragma region ========================== ALPHA SURFACES RENDERING ==========================

static int AlphaSurfComp(const AlphaSurfaceSortInfo_t* info1, const AlphaSurfaceSortInfo_t* info2) // H2
{
	return (int)((info2->depth - info1->depth) * 1000.0f);
}

// Transforms vector to screen space? // gl1: in gl1_Misc.c; local copy to avoid a cross-module dependency.
static void R_TransformVector(const vec3_t v, vec3_t out)
{
	out[0] = DotProduct(v, vright);
	out[1] = DotProduct(v, vup);
	out[2] = DotProduct(v, vpn);
}

//TODO: logic identical to for loop logic in R_DrawEntitiesOnList(). Move to vk_Main as R_DrawEntity and replace said logic?
static void R_DrawAlphaEntity(entity_t* ent) // H2
{
	if (!(int)r_drawentities->value)
		return;

	if (ent->model == NULL)
	{
		ri.Con_Printf(PRINT_DEVELOPER, "Attempt to draw NULL alpha model\n"); //mxd. Com_DPrintf() -> ri.Con_Printf().
		R_DrawNullModel(ent);

		return;
	}

	const model_t* mdl = *ent->model; //mxd. Original logic uses 'currentmodel' global var.

	if (mdl == NULL)
	{
		R_DrawNullModel(ent);
		return;
	}

	switch (mdl->type)
	{
		case mod_bad:
			ri.Con_Printf(PRINT_ALL, "WARNING: currentmodel->type == 0; reload the map\n"); //mxd. Com_Printf() -> ri.Con_Printf().
			break;

		case mod_brush:
			R_DrawBrushModel(ent);
			break;

		case mod_sprite:
			R_DrawSpriteModel(ent);
			break;

		case mod_fmdl:
			R_DrawFlexModel(ent);
			break;

		default:
			ri.Sys_Error(ERR_DROP, "Bad modeltype"); //mxd. Sys_Error() -> ri.Sys_Error().
			break;
	}
}

static void R_DrawAlphaSurface(const msurface_t* fa) // H2
{
	// gl1: glLoadMatrixf(r_world_matrix) - alpha surfaces (including inline-bmodel ones) always draw in world space.
	Mat4_Identity(r_local_model_matrix);

	// gl1: glEnable(GL_BLEND) + glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
	// (H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR) - baked into
	// vk_drawPolyPipeline / vk_drawPolyWarpPipeline.

	c_brush_polys += 1;

	float alpha;
	if (fa->texinfo->flags & SURF_TRANS33)
		alpha = gl_trans33->value;
	else if (fa->texinfo->flags & SURF_TRANS66)
		alpha = gl_trans66->value;
	else
		alpha = 1.0f;

	// gl1: glColor4f(inverse_intensity x3, alpha) + GL_MODULATE (inverse_intensity == 1.0 - intensity is fixed at 1.0)
	// + glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.05f).
	r_surf_alpha = alpha;
	r_surf_alpha_test = 0.05f;

	if (fa->flags & SURF_DRAWTURB)
	{
		R_EmitWaterPolys(fa, fa->texinfo->image, fa->flags & SURF_UNDULATE);
	}
	else
	{
		const float color[4] = { 1.0f, 1.0f, 1.0f, alpha };
		R_DrawGLPoly(fa, fa->texinfo->image, color, 0.05f);
	}

	r_surf_alpha = 1.0f; // gl1: R_AlphaFunc(GL_GREATER, 0.666f); the ambient 3D state is "alpha test off".
	r_surf_alpha_test = -1.0f;
}

void R_SortAndDrawAlphaSurfaces(void)
{
#define MAX_ALPHA_SURFACES 512 //TODO: is max number of alpha surfaces actually defined somewhere?

	AlphaSurfaceSortInfo_t sorted_ents[MAX_ALPHA_ENTITIES + 1]; //mxd. Extra slot for terminator (depth -100000) entry.
	AlphaSurfaceSortInfo_t sorted_surfs[MAX_ALPHA_SURFACES + 1]; //mxd. Extra slot for terminator (depth -100000) entry.

	// Add alpha entities to array...
	AlphaSurfaceSortInfo_t* info = &sorted_ents[0];
	for (int i = 0; i < r_newrefdef.num_alpha_entities; i++, info++)
	{
		entity_t* ent = r_newrefdef.alpha_entities[i];

		info->entity = ent;
		info->depth = ent->depth;
	}

	VectorScale(r_origin, -1.0f, modelorg);

	// Initialize last entity entry...
	info = &sorted_ents[r_newrefdef.num_alpha_entities];
	info->entity = NULL;
	info->depth = -100000.0f;

	// Add alpha surfaces to array.
	int num_surfaces;
	msurface_t* surf = r_alpha_surfaces;
	info = &sorted_surfs[0];
	for (num_surfaces = 0; surf != NULL; num_surfaces++, surf = surf->texturechain, info++)
	{
		info->surface = surf;
		info->depth = -100000.0f;

		for (int i = 0; i < surf->numedges; i++)
		{
			const int lindex = r_worldmodel->surfedges[surf->firstedge + i];
			float* vec;

			if (lindex > 0)
			{
				const medge_t* edge = &r_worldmodel->edges[lindex];
				vec = r_worldmodel->vertexes[edge->v[0]].position;
			}
			else
			{
				const medge_t* edge = &r_worldmodel->edges[-lindex];
				vec = r_worldmodel->vertexes[edge->v[1]].position;
			}

			vec3_t diff;
			VectorSubtract(vec, r_origin, diff);

			vec3_t screen_pos;
			R_TransformVector(diff, screen_pos);

			info->depth = max(info->depth, screen_pos[2]);
		}

		if (num_surfaces >= MAX_ALPHA_SURFACES)
		{
			ri.Con_Printf(PRINT_DEVELOPER, "Warning: attempting to draw too many alpha surfaces\n"); //mxd. Com_DPrintf() -> ri.Con_Printf().
			break;
		}
	}

	// Initialize last surface entry...
	info = &sorted_surfs[num_surfaces];
	info->surface = NULL;
	info->depth = -100000.0f;

	// Sort surfaces...
	qsort(sorted_surfs, num_surfaces, sizeof(AlphaSurfaceSortInfo_t), (int (*)(const void*, const void*))AlphaSurfComp);

	const int num_elements = r_newrefdef.num_alpha_entities + num_surfaces;
	const AlphaSurfaceSortInfo_t* sorted_ent = &sorted_ents[0];
	const AlphaSurfaceSortInfo_t* sorted_surf = &sorted_surfs[0];

	// Draw them all.
	for (int i = 0; i < num_elements; i++)
	{
		if (sorted_surf->depth > sorted_ent->depth)
		{
			R_DrawAlphaSurface(sorted_surf->surface);
			sorted_surf++;
		}
		else
		{
			R_DrawAlphaEntity(sorted_ent->entity);
			sorted_ent++;
		}
	}

	r_alpha_surfaces = NULL;
}

#pragma endregion

#pragma region ========================== BRUSH MODELS RENDERING ==========================

// Returns the proper texture for a given time and base texture.
static image_t* R_TextureAnimation(const entity_t* ent, const mtexinfo_t* tex) //mxd. Original logic uses 'currententity' global var.
{
	if (tex->next != NULL)
	{
		int frame;

		if ((tex->flags & SURF_ANIMSPEED) && tex->image->num_frames > 0) // H2: extra SURF_ANIMSPEED logic.
			frame = (int)((float)tex->image->num_frames * r_newrefdef.time);
		else if (ent != NULL) //mxd. Added sanity check.
			frame = ent->frame;
		else
			return tex->image;

		frame %= tex->numframes;

		while (frame-- > 0 && tex->next != NULL) //mxd. Added tex->next sanity check.
			tex = tex->next;
	}

	return tex->image;
}

// This routine took all the lightmapped surfaces in the world and blended them into the framebuffer (gl1).
// vk: gl1's dynamic-lightmap rebuild machinery is gone (lightstyles are shader-side lmScales), and the two
// multiply-into-framebuffer passes have no equivalent in the LOCKED pipeline inventory (no
// GL_ZERO/GL_SRC_COLOR blend pipeline):
//  - the H2 tallwall tint (styles bytes x texture) is pre-multiplied into the textured draw instead
//    (R_RenderBrushPoly() - same final framebuffer color as gl1's two passes);
//  - the static-lightmap multiply pass over the lightmapchains only ever draws for the
//    gl_drawflat >= 2 debug mode (fullbright early-outs here; the normal world path is single-pass
//    lightmapped) - skipped, gl_drawflat >= 2 behaves like gl_drawflat 1.
// Only the r_speeds counter bookkeeping remains.
static void R_BlendLightmaps(const model_t* mdl) //mxd. Original logic uses 'currentmodel' global var.
{
	// Don't bother if we're set to fullbright.
	if ((int)r_fullbright->value || r_worldmodel->lightdata == NULL)
		return;

	if (mdl == r_worldmodel)
		c_visible_lightmaps = 0;

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (gl_lms.lightmap_surfaces[i] == NULL)
			continue;

		if (mdl == r_worldmodel)
			c_visible_lightmaps++;
	}
}

//mxd. Similar to Q2's GL_RenderLightmappedPoly. gl1-H2 note: "missing SURF_FLOWING logic" - restored
// here via the CPU-side texcoord scroll (gl3 restored it via the si3DlmFlow program; original H2
// lost Q2's scrolling on lit surfaces).
static void R_RenderLightmappedPoly(const entity_t* ent, msurface_t* surf) //mxd. Added 'ent' arg.
{
	c_brush_polys++;

	// gl1's lightstyle-change / dynamic-light glTexSubImage2D lightmap updates are gone:
	// lightstyles scale the 4 style sub-lightmaps via lmScales (yq2/gl3 model).

	const image_t* image = R_TextureAnimation(ent, surf->texinfo); // H2: GL_MBind -> GL_MBindImage
	if (!R_ImageUsable(image))
		return; // Module-port ordering guard (world textures come from vk_Image.c).

	const VkDescriptorSet lmap_set = gl_lms.lightmap_descriptor_sets[surf->lightmaptexturenum];
	if (lmap_set == VK_NULL_HANDLE)
		return; //mxd. Defensive check (lightmaps not uploaded yet).

	// Per-draw UBO: model matrix + H2 lightstyle lmScales + fog block (vklmapubo_t).
	vklmapubo_t ubo_data;
	memcpy(ubo_data.model, r_local_model_matrix, sizeof(ubo_data.model));

	// H2 lightstyles -> lmScales: gl1 rebuilt the lightmap texels on lightstyle change
	// (R_BuildLightMap() with scale = gl_modulate * lightstyle.rgb); vk scales the 4 raw
	// style sub-lightmaps in the fragment shader instead (yq2/gl3), so gl_modulate is
	// baked into the scales here.
	memset(ubo_data.lmScales, 0, sizeof(ubo_data.lmScales));
	for (int c = 0; c < 4; c++) // Fullbright fallback for surfaces without light data (styles[0] == 255).
		ubo_data.lmScales[0][c] = 1.0f;

	for (int map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
	{
		ubo_data.lmScales[map][0] = r_newrefdef.lightstyles[surf->styles[map]].rgb[0] * gl_modulate->value;
		ubo_data.lmScales[map][1] = r_newrefdef.lightstyles[surf->styles[map]].rgb[1] * gl_modulate->value;
		ubo_data.lmScales[map][2] = r_newrefdef.lightstyles[surf->styles[map]].rgb[2] * gl_modulate->value;
		ubo_data.lmScales[map][3] = 1.0f;
	}

	ubo_data.fog = r_world_fog;

	// gl_lightmap: show only the lightmap term (gl1: GL_TEXTURE1 GL_REPLACE in R_DrawTextureChains();
	// vk: polygon_lmap.frag viewLightmaps - no white texture needed).
	ubo_data.viewLightmaps = ((int)gl_lightmap->value ? 1.0f : 0.0f);

	float scroll = 0.0f;
	if (surf->texinfo->flags & SURF_FLOWING)
	{
		scroll = -64.0f * ((r_newrefdef.time / 40.0f) - floorf(r_newrefdef.time / 40.0f)); // YQ2
		if (scroll == 0.0f)
			scroll = -64.0f;
	}

	uint32_t ubo_offset;
	VkDescriptorSet ubo_set;
	uint8_t* ubo = QVk_GetUniformBuffer(sizeof(ubo_data), &ubo_offset, &ubo_set);
	memcpy(ubo, &ubo_data, sizeof(ubo_data));

	QVk_BindPipeline(&vk_drawPolyLmapPipeline);

	const VkDescriptorSet desc_sets[] = { image->vk_texture.descriptorSet, ubo_set, lmap_set };
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk_drawPolyLmapPipeline.layout, 0, 3, desc_sets, 1, &ubo_offset);

	QVk_PushMatrix(&vk_drawPolyLmapPipeline, r_viewproj_matrix); // polygon_lmap.vert: vpMatrix push, model matrix in the UBO.
	QVk_PushWorldFragmentConstants(&vk_drawPolyLmapPipeline, -1.0f); // polygon_lmap.frag reads only the grade trio (no alpha test).

	for (const glpoly_t* p = surf->polys; p != NULL; p = p->chain)
		QVk_DrawTriangleFan(R_BuildPolyVerts(surf, p, scroll), sizeof(vk_3D_vtx_t), p->numverts);
}

static void R_RenderBrushPoly(const entity_t* ent, msurface_t* fa) //mxd. Added 'ent' arg.
{
	c_brush_polys++;

	image_t* image = R_TextureAnimation(ent, fa->texinfo); // Q2: GL_Bind().

	// gl1 relied on the ambient glColor alpha (1.0, or 0.25 for RF_TRANS_ANY bmodels) here;
	// set the equivalent state explicitly so the underwater / quake paths are deterministic.
	r_surf_alpha = r_bmodel_alpha;
	r_surf_alpha_test = -1.0f;

	// H2: new cl_camera_under_surface logic.
	if ((int)cl_camera_under_surface->value)
	{
		R_EmitUnderwaterPolys(fa, image);
		return;
	}

	// H2: new quake_amount logic.
	if ((int)quake_amount->value)
	{
		R_EmitQuakeFloorPolys(fa, image);
		return;
	}

	if (fa->flags & SURF_DRAWTURB)
	{
		// Warp texture, no lightmaps.
		// gl1: GL_MODULATE + glColor4f(inverse_intensity x3, 1.0f); inverse_intensity == 1.0 in vk (gl3 parity).
		r_surf_alpha = 1.0f;
		R_EmitWaterPolys(fa, image, fa->flags & SURF_UNDULATE);
		r_surf_alpha = r_bmodel_alpha;

		return;
	}

	// Textured pass without lightmap (gl1: GL_REPLACE + R_DrawGLPoly()).
	// H2: missing SURF_FLOWING flag logic.
	// r_bmodel_alpha is 0.25 for RF_TRANS_ANY brush models (vk_drawPolyPipeline always blends), 1.0 otherwise.
	float color[4] = { 1.0f, 1.0f, 1.0f, r_bmodel_alpha };

	// H2 tallwall tint: gl1/gl3 drew the texture untinted here and multiplied a
	// styles-bytes-colored untextured pass over it in R_BlendLightmaps()
	// (GL_ZERO, GL_SRC_COLOR). The locked vk pipeline set has no multiply-blend
	// pipeline, so the tint is pre-multiplied into this single textured draw -
	// identical final framebuffer color (tex * styles-color).
	if (fa->texinfo->flags & SURF_TALL_WALL)
	{
		for (int c = 0; c < 3; c++)
			color[c] = (float)fa->styles[c] / 255.0f;

		color[3] = 1.0f; // gl1 tint pass: glColor3ubv - alpha stays 1.0.
	}

	R_DrawGLPoly(fa, image, color, -1.0f);

	// vk: gl1's "check for lightmap modification" / dynamic-lightmap branches collapse -
	// lightstyles are shader-side. Only the blend-pass chain bookkeeping remains
	// (see R_BlendLightmaps()).

	// H2: new tall wall logic:
	if (!(fa->texinfo->flags & SURF_TALL_WALL))
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
	else if (gl_lms.tallwall_lightmaptexturenum < MAX_TALLWALL_LIGHTMAPS)
	{
		gl_lms.tallwall_lightmap_surfaces[gl_lms.tallwall_lightmaptexturenum] = fa;
		gl_lms.tallwall_lightmaptexturenum++;
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "WARNING: too many tall wall surfaces!"); //mxd. Com_Printf() -> ri.Con_Printf().
	}
}

static void R_RenderFlatShadedBrushPoly(const entity_t* ent, msurface_t* fa) // H2 //mxd. Added 'ent' arg.
{
	(void)ent;

	c_brush_polys++;

	// Use fa->polys pointer as random, but constant color...
	paletteRGBA_t color;
	color.c = (uint)(uintptr_t)fa->polys;
	color.a = 255; // gl1: glColor3ubv - alpha stays 1.0.

	R_DrawColorPoly(fa->polys, color);

	// Done when gl_drawflat == 1.
	if ((int)gl_drawflat->value == 1)
		return;

	// Chain lightmaps (gl_drawflat >= 2) for the R_BlendLightmaps() multiply pass
	// (vk: counted only - see R_BlendLightmaps()).
	if (!(fa->texinfo->flags & SURF_TALL_WALL))
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}

static void R_DrawTextureChains(const entity_t* ent) // Q2: DrawTextureChains(). //mxd. Added 'ent' arg.
{
	c_visible_textures = 0;

	// gl_zfix (YQ2 GL polygon offset workaround) has no vk equivalent - dropped.
	// YQ2 alpha-to-coverage for alpha-tested world surfaces: the locked pipelines
	// have alphaToCoverageEnable off - dropped (alpha test itself is shader-side).

	// H2: extra gl_sortmulti logic (vk: the single-pass lightmapped pipeline replaces gl1's
	// GL_TEXTURE0 GL_REPLACE x GL_TEXTURE1 GL_MODULATE multitexture setup):
	if (multitexture_mode)
	{
		image_t* image = &vktextures[0];
		for (int i = 0; i < numvktextures; i++, image++)
		{
			if (image->registration_sequence == 0 || image->multitexturechain == NULL)
				continue;

			c_visible_textures++;

			for (msurface_t* s = image->multitexturechain; s != NULL; s = s->texturechain)
				R_RenderLightmappedPoly(ent, s);

			image->multitexturechain = NULL;
		}

		multitexture_mode = false;
	}

	void (*render_brush_poly)(const entity_t*, msurface_t*) = ((int)gl_drawflat->value ? R_RenderFlatShadedBrushPoly : R_RenderBrushPoly); // H2: new gl_drawflat logic.

	// Original Q2 logic:

	// Render lightmapped surfaces.
	image_t* image = &vktextures[0];
	for (int i = 0; i < numvktextures; i++, image++)
	{
		if (!image->registration_sequence || image->texturechain == NULL)
			continue;

		c_visible_textures++;

		for (msurface_t* s = image->texturechain; s != NULL; s = s->texturechain)
			if (!(s->flags & SURF_DRAWTURB))
				render_brush_poly(ent, s); // H2: new gl_drawflat logic.
	}

	// Render warping (water) surfaces (no lightmaps).
	image = &vktextures[0];
	for (int i = 0; i < numvktextures; i++, image++)
	{
		if (!image->registration_sequence || image->texturechain == NULL)
			continue;

		for (msurface_t* s = image->texturechain; s != NULL; s = s->texturechain)
			if (s->flags & SURF_DRAWTURB)
				render_brush_poly(ent, s); // H2: new gl_drawflat logic.

		image->texturechain = NULL;
	}
}

static qboolean R_CullBox(const vec3_t mins, const vec3_t maxs)
{
	if (!(int)r_nocull->value)
	{
		for (int i = 0; i < 4; i++)
			if (BoxOnPlaneSide(mins, maxs, &frustum[i]) == 2) // H2: BoxOnPlaneSide call instead of BOX_ON_PLANE_SIDE macro.
				return true;
	}

	return false;
}

// gl1 R_RotateForEntity() (gl1_Misc.c) as a matrix (yq2 R_RotateForEntity() technique):
// H2 RAD_TO_ANGLE-scaled rotations + translate, composed onto the current model matrix.
// (Row-vector composition order: X, Y, Z rotations, then translation - equals gl1's
// glTranslatef + glRotatef(Z) + glRotatef(-Y) + glRotatef(-X) call sequence.)
static void R_RotateForEntity(const entity_t* e)
{
	float mat[16];
	Mat4_Identity(mat);

	Mat4_Rotate(mat, -e->angles[2] * RAD_TO_ANGLE, 1.0f, 0.0f, 0.0f);
	Mat4_Rotate(mat, -e->angles[0] * RAD_TO_ANGLE, 0.0f, 1.0f, 0.0f);
	Mat4_Rotate(mat, e->angles[1] * RAD_TO_ANGLE, 0.0f, 0.0f, 1.0f);
	Mat4_Translate(mat, e->origin[0], e->origin[1], e->origin[2]);

	Mat4_Multiply(mat, r_local_model_matrix, r_local_model_matrix);
}

static void R_DrawInlineBModel(const entity_t* ent) //mxd. Original logic uses 'currententity' global var.
{
#define BACKFACE_EPSILON 0.01f // Q2: defined in gl_local.h

	const model_t* mdl = *ent->model; //mxd. Original logic uses 'currentmodel' global var instead.

	// Calculate dynamic lighting for bmodel.
	if (!(int)gl_flashblend->value)
	{
		dlight_t* lt = r_newrefdef.dlights;
		for (int k = 0; k < r_newrefdef.num_dlights; k++, lt++)
			R_MarkLights(lt, 1 << k, mdl->nodes + mdl->firstnode);
	}

	msurface_t* psurf = &mdl->surfaces[mdl->firstmodelsurface];

	// H2: extra RF_TRANS_ADD and RF_TRANS_GHOST flags.
	// gl1 kept the lightmapped multitexture path with glColor4f(1,1,1,0.25) + GL_MODULATE;
	// the lightmapped pipeline has no constant alpha, so translucent bmodels go through the
	// polygon pipeline (alpha 0.25, no lightmap) instead (gl3 parity).
	const qboolean trans_ent = (ent->flags & RF_TRANS_ANY) != 0;
	if (trans_ent)
		r_bmodel_alpha = 0.25f; // gl1: glEnable(GL_BLEND) - vk_drawPolyPipeline always blends.

	// Draw texture.
	for (int i = 0; i < mdl->nummodelsurfaces; i++, psurf++)
	{
		// Find which side of the node we are on.
		const cplane_t* pplane = psurf->plane;
		const float dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		// Draw the polygon.
		if (((psurf->flags & SURF_PLANEBACK) && dot < -BACKFACE_EPSILON) ||
			(!(psurf->flags & SURF_PLANEBACK) && dot > BACKFACE_EPSILON))
		{
			if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
			{
				// Add to the translucent chain.
				psurf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = psurf;
			}
			else if (!(psurf->flags & SURF_DRAWTURB) && !(int)r_fullbright->value && !(int)gl_drawflat->value && !trans_ent) // H2: extra r_fullbright and gl_drawflat checks
			{
				R_RenderLightmappedPoly(ent, psurf); // Q2: GL_RenderLightmappedPoly
			}
			else //mxd. Skipped qglMTexCoord2fSGIS check.
			{
				R_RenderBrushPoly(ent, psurf);
			}
		}
	}

	// H2: extra RF_TRANS_ADD and RF_TRANS_GHOST flags.
	if (trans_ent)
		r_bmodel_alpha = 1.0f;
}

void R_DrawBrushModel(entity_t* ent)
{
	const model_t* mdl = *ent->model; //mxd. Original logic uses 'currentmodel' global var instead.

	if (mdl->nummodelsurfaces == 0)
		return;

	// H2: missing: currententity = ent;
	// gl1 reset gl_state.currenttextures[] here (multitexture unit cache invalidation) -
	// vk binds descriptor sets per draw, nothing to reset.

	vec3_t mins;
	vec3_t maxs;
	qboolean rotated;

	if (ent->angles[0] != 0.0f || ent->angles[1] != 0.0f || ent->angles[2] != 0.0f)
	{
		for (int i = 0; i < 3; i++)
		{
			mins[i] = ent->origin[i] - mdl->radius;
			maxs[i] = ent->origin[i] + mdl->radius;
		}

		rotated = true;
	}
	else
	{
		VectorAdd(ent->origin, mdl->mins, mins);
		VectorAdd(ent->origin, mdl->maxs, maxs);

		rotated = false;
	}

	if (R_CullBox(mins, maxs))
		return;

	//mxd. Skip H2 gl_drawmode logic.

	memset((void*)gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	VectorSubtract(r_newrefdef.vieworg, ent->origin, modelorg);

	if (rotated)
	{
		vec3_t angles;
		VectorScale(ent->angles, RAD_TO_ANGLE, angles); // H2: new RAD_TO_ANGLE rescale.

		const vec3_t temp = VEC3_INIT(modelorg);

		vec3_t forward;
		vec3_t right;
		vec3_t up;
		AngleVectors(angles, forward, right, up);

		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	// gl1: glPushMatrix() + R_RotateForEntity() + glPopMatrix().
	float old_model_mat[16];
	memcpy(old_model_mat, r_local_model_matrix, sizeof(old_model_mat));

	ent->angles[0] *= -1.0f; // stupid quake bug.
	ent->angles[2] *= -1.0f; // stupid quake bug.
	R_RotateForEntity(ent);
	ent->angles[0] *= -1.0f; // stupid quake bug.
	ent->angles[2] *= -1.0f; // stupid quake bug.

	// gl1's R_EnableMultitexture(true) + REPLACE/MODULATE texenv setup is implicit in the lightmapped pipeline.
	R_DrawInlineBModel(ent);

	//mxd. Skip H2 gl_drawmode logic.
	memcpy(r_local_model_matrix, old_model_mat, sizeof(old_model_mat));
}

#pragma endregion

#pragma region ========================== WORLD MODEL RENDERING ==========================

static void R_RecursiveWorldNode(const entity_t* ent, mnode_t* node) //mxd. Added 'ent' arg.
{
	if (node->contents == CONTENTS_SOLID || node->visframe != r_visframecount || R_CullBox(node->minmaxs, node->minmaxs + 3))
		return;

	// If a leaf node, draw stuff.
	if (node->contents != -1)
	{
		const mleaf_t* pleaf = (mleaf_t*)node;

		// Check for door connected areas.
		if (r_newrefdef.areabits != NULL && !(r_newrefdef.areabits[pleaf->area >> 3] & (1 << (pleaf->area & 7))))
			return; // Not visible.

		msurface_t** mark = pleaf->firstmarksurface;
		for (int i = pleaf->nummarksurfaces; i > 0; i--)
		{
			(*mark)->visframe = r_framecount;
			mark++;
		}

		return;
	}

	// Node is just a decision point, so go down the appropriate sides.

	// Find which side of the node we are on.
	const cplane_t* plane = node->plane;
	float dot;

	switch (plane->type)
	{
		case PLANE_X:
		case PLANE_Y:
		case PLANE_Z:
			dot = modelorg[plane->type] - plane->dist;
			break;

		default:
			dot = DotProduct(modelorg, plane->normal) - plane->dist;
			break;
	}

	const int side = ((dot >= 0.0f) ? 0 : 1);
	const int sidebit = ((dot >= 0.0f) ? 0 : SURF_PLANEBACK);

	// Recurse down the children, front side first.
	R_RecursiveWorldNode(ent, node->children[side]);

	// Draw stuff.
	msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int c = node->numsurfaces; c > 0; c--, surf++)
	{
		if (surf->visframe != r_framecount || (surf->flags & SURF_PLANEBACK) != sidebit)
			continue; // Wrong frame or side.

		if (surf->texinfo->flags & SURF_SKY)
		{
			// Just adds to visible sky bounds.
			R_AddSkySurface(surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			// Add to the translucent texture chain.
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
		}
		else if (!(surf->flags & SURF_DRAWTURB) && !(surf->flags & SURF_TALL_WALL) && !(int)r_fullbright->value && !(int)gl_drawflat->value) // H2: extra SURF_TALL_WALL, r_fullbright, gl_drawflat checks.
		{
			// The polygon is visible, so add it to the sorted multi-texture chain.
			image_t* image = R_TextureAnimation(ent, surf->texinfo);
			surf->texturechain = image->multitexturechain;
			image->multitexturechain = surf;

			multitexture_mode = true;
		}
		else //mxd. Skipping qglMTexCoord2fSGIS logic...
		{
			// The polygon is visible, so add it to the sorted texture chain.
			// FIXME: this is a hack for animation.
			image_t* image = R_TextureAnimation(ent, surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;
		}
	}

	// Recurse down the back side.
	R_RecursiveWorldNode(ent, node->children[!side]);
}

void R_DrawWorld(void)
{
	if (!(int)r_drawworld->value || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		return;

	if (r_worldmodel == NULL) //mxd. Defensive check (gl1 R_RenderView() Sys_Errors on NULL worldmodel).
		return;

	VectorCopy(r_newrefdef.vieworg, modelorg);

	// Auto cycle the world frame for texture animation.
	const entity_t ent = { .frame = (int)(r_newrefdef.time * 2.0f) }; //mxd. memset -> zero initialization.

	//mxd. Skip H2 gl_drawmode logic.

	// The world draws in world space with the ambient surface state
	// (gl1: base world matrix + glColor4f(1,1,1,1) + alpha test off).
	Mat4_Identity(r_local_model_matrix);
	r_surf_alpha = 1.0f;
	r_surf_alpha_test = -1.0f;

	memset((void*)gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	gl_lms.tallwall_lightmaptexturenum = 0; // H2
	multitexture_mode = false; // H2

	R_ClearSkyBox();

	// H2: new r_fullbright and gl_drawflat cvar checks (gl1 wrapped R_RecursiveWorldNode() in
	// R_EnableMultitexture(true) + texenv setup here; implicit in the vk pipelines).
	R_RecursiveWorldNode(&ent, r_worldmodel->nodes);

	// Theoretically nothing should happen in the next two functions if multitexture is enabled.

	// H2: new gl_drawflat cvar logic (gl1: glDisable(GL_TEXTURE_2D) around the call -
	// the vk flat-shaded path uses the untextured model pipeline instead).
	R_DrawTextureChains(&ent);

	R_BlendLightmaps(r_worldmodel);

	//mxd. Skip H2 gl_drawmode logic.
	R_DrawSkyBox();

	// gl1: R_DrawTriangleOutlines() (gl_showtris) - unused in vk.
}

// Q2 counterpart
// Mark the leaves and nodes that are in the PVS for the current cluster.
void R_MarkLeaves(void)
{
	static byte fatvis[MAX_MAP_LEAFS / 8]; //mxd. Made static.

	if (r_worldmodel == NULL) //mxd. Defensive check.
		return;

	if (r_oldviewcluster == r_viewcluster && r_oldviewcluster2 == r_viewcluster2 && !(int)r_novis->value && r_viewcluster != -1)
		return;

	// Development aid to let you run around and see exactly where the pvs ends.
	if ((int)gl_lockpvs->value)
		return;

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if ((int)r_novis->value || r_viewcluster == -1 || r_worldmodel->vis == NULL)
	{
		// Mark everything.
		for (int i = 0; i < r_worldmodel->numleafs; i++)
			r_worldmodel->leafs[i].visframe = r_visframecount;

		for (int i = 0; i < r_worldmodel->numnodes; i++)
			r_worldmodel->nodes[i].visframe = r_visframecount;

		return;
	}

	byte* vis = Mod_ClusterPVS(r_viewcluster, r_worldmodel);

	// May have to combine two clusters because of solid water boundaries.
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy(fatvis, vis, (r_worldmodel->numleafs + 7) / 8);
		vis = Mod_ClusterPVS(r_viewcluster2, r_worldmodel);

		const int c = (r_worldmodel->numleafs + 31) / 32;
		for (int i = 0; i < c; i++)
			((int*)fatvis)[i] |= ((int*)vis)[i];

		vis = fatvis;
	}

	mleaf_t* leaf = &r_worldmodel->leafs[0];
	for (int i = 0; i < r_worldmodel->numleafs; i++, leaf++)
	{
		const int cluster = leaf->cluster;
		if (cluster == -1)
			continue;

		if (vis[cluster >> 3] & 1 << (cluster & 7))
		{
			mnode_t* node = (mnode_t*)leaf;
			do
			{
				if (node->visframe == r_visframecount)
					break;

				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}

#pragma endregion

#pragma region ========================== FRAME SETUP (from gl1_Main.c / gl3_Main.c) ==========================

// Q2 counterpart
static byte R_SignbitsForPlane(const cplane_t* plane) //mxd. Changed return type to byte.
{
	// For fast box on planeside test.
	byte bits = 0;
	for (int i = 0; i < 3; i++)
		if (plane->normal[i] < 0.0f)
			bits |= 1 << i;

	return bits;
}

// gl1: static in gl1_Main.c; called from R_RenderView() before world / entity drawing.
void R_SetFrustum(void)
{
	RotatePointAroundVector(frustum[0].normal, vup,	   vpn, -(90.0f - r_newrefdef.fov_x * 0.5f));	// Rotate VPN right by FOV_X/2 degrees.
	RotatePointAroundVector(frustum[1].normal, vup,	   vpn,   90.0f - r_newrefdef.fov_x * 0.5f);	// Rotate VPN left by FOV_X/2 degrees.
	RotatePointAroundVector(frustum[2].normal, vright, vpn,   90.0f - r_newrefdef.fov_y * 0.5f);	// Rotate VPN up by FOV_X/2 degrees.
	RotatePointAroundVector(frustum[3].normal, vright, vpn, -(90.0f - r_newrefdef.fov_y * 0.5f));	// Rotate VPN down by FOV_X/2 degrees.

	for (int i = 0; i < 4; i++)
	{
		// H2:
		const float frustum_dist = VectorLength(frustum[i].normal);
		if (frustum_dist <= 0.999999f)
			ri.Con_Printf(PRINT_ALL, "Frustum normal dist %f < 1.0\n", (double)frustum_dist); //mxd. Com_Printf() -> ri.Con_Printf().

		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct(r_origin, frustum[i].normal);
		frustum[i].signbits = R_SignbitsForPlane(&frustum[i]);
	}
}

// The viewcluster part of gl1 R_SetupFrame() (gl1_Main.c); called from R_RenderView()
// before R_MarkLeaves(). The rest of R_SetupFrame() lives in vk_Main.c.
// (gl3: GL3_SetViewClusters().)
void R_SetViewClusters(void)
{
	if ((r_newrefdef.rdflags & RDF_NOWORLDMODEL) || r_worldmodel == NULL)
		return;

	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	const mleaf_t* leaf = Mod_PointInLeaf(r_origin, r_worldmodel);
	r_viewcluster = leaf->cluster;
	r_viewcluster2 = r_viewcluster;

	// Check above and below so crossing solid water doesn't draw wrong.
	vec3_t temp = VEC3_INIT(r_origin);

	if (leaf->contents == 0)
		temp[2] -= 16.0f; // Look down a bit.
	else
		temp[2] += 16.0f; // Look up a bit.

	leaf = Mod_PointInLeaf(temp, r_worldmodel);
	if (!(leaf->contents & CONTENTS_SOLID))
		r_viewcluster2 = leaf->cluster;
}

#pragma endregion

#pragma region ========================== FOG SETUP (from gl3_Main.c R_Clear() flow) ==========================

// The fog color cvars are registered (file-static) by vk_Main.c R_Register();
// re-fetched lazily here - ri.Cvar_Get() returns the already-registered cvar.
static cvar_t* r_fog_color_r;
static cvar_t* r_fog_color_g;
static cvar_t* r_fog_color_b;
static cvar_t* r_fog_color_a;
static cvar_t* r_fog_underwater_mode;
static cvar_t* r_fog_underwater_density;
static cvar_t* r_fog_underwater_startdist;
static cvar_t* r_fog_underwater_color_r;
static cvar_t* r_fog_underwater_color_g;
static cvar_t* r_fog_underwater_color_b;
static cvar_t* r_fog_underwater_color_a;

static void R_GetFogCvars(void)
{
	if (r_fog_color_r != NULL)
		return;

	r_fog_color_r = ri.Cvar_Get("r_fog_color_r", "1.0", 0);
	r_fog_color_g = ri.Cvar_Get("r_fog_color_g", "1.0", 0);
	r_fog_color_b = ri.Cvar_Get("r_fog_color_b", "1.0", 0);
	r_fog_color_a = ri.Cvar_Get("r_fog_color_a", "0.0", 0);
	r_fog_underwater_mode = ri.Cvar_Get("r_fog_underwater_mode", "1", 0);
	r_fog_underwater_density = ri.Cvar_Get("r_fog_underwater_density", "0.0015", 0);
	r_fog_underwater_startdist = ri.Cvar_Get("r_fog_underwater_startdist", "100.0", 0);
	r_fog_underwater_color_r = ri.Cvar_Get("r_fog_underwater_color_r", "1.0", 0);
	r_fog_underwater_color_g = ri.Cvar_Get("r_fog_underwater_color_g", "1.0", 0);
	r_fog_underwater_color_b = ri.Cvar_Get("r_fog_underwater_color_b", "1.0", 0);
	r_fog_underwater_color_a = ri.Cvar_Get("r_fog_underwater_color_a", "0.0", 0);
}

static void R_Fog(void) // H2: GL_Fog. Sets the r_world_fog block instead of glFog*().
{
	r_world_fog.fogMode = ClampI((int)r_fog_mode->value, 0, 2); //mxd. Added ClampI().
	r_world_fog.fogStart = r_fog_startdist->value;
	r_world_fog.fogEnd = r_farclipdist->value;
	r_world_fog.fogDensity = r_fog_density->value;
	r_world_fog.fogLightmapAdjust = r_fog_lightmap_adjust->value;
	r_world_fog.fogColor[0] = r_fog_color_r->value;
	r_world_fog.fogColor[1] = r_fog_color_g->value;
	r_world_fog.fogColor[2] = r_fog_color_b->value;
	r_world_fog.fogColor[3] = r_fog_color_a->value;

	// gl1/gl3 also glClearColor'd to the fog color here - in vk the fog-aware
	// clear is the frame module's business (vk_Main.c reads r_world_fog.fogColor).
}

static void R_WaterFog(void) // H2: GL_WaterFog. Sets the r_world_fog block instead of glFog*().
{
	r_world_fog.fogMode = ClampI((int)r_fog_underwater_mode->value, 0, 2); //mxd. Added ClampI().
	r_world_fog.fogStart = r_fog_underwater_startdist->value;
	r_world_fog.fogEnd = r_farclipdist->value;
	r_world_fog.fogDensity = r_fog_underwater_density->value;
	r_world_fog.fogLightmapAdjust = r_fog_underwater_lightmap_adjust->value;
	r_world_fog.fogColor[0] = r_fog_underwater_color_r->value;
	r_world_fog.fogColor[1] = r_fog_underwater_color_g->value;
	r_world_fog.fogColor[2] = r_fog_underwater_color_b->value;
	r_world_fog.fogColor[3] = r_fog_underwater_color_a->value;
}

// The fog-selection part of gl1's R_Clear() (gl3_Main.c R_Clear() port) -
// fills the r_world_fog block copied into every per-draw world/model UBO.
// Call once per frame from RI_RenderFrame() before any 3D drawing.
void R_SetupFog(void)
{
	R_GetFogCvars();

	// H2: extra fog rendering logic. //mxd. Removed gl_fog_broken cvar checks.
	if ((int)cl_camera_under_surface->value) //TODO: r_fog_underwater cvar check seems logical here, but isn't present in original dll.
	{
		R_WaterFog();
	}
	//mxd. Removed 'r_fog_startdist->value < r_farclipdist->value' check, because it's relevant only for fog mode 0.
	// Also there's no r_fog_underwater_startdist check in GL_WaterFog case in original .dll.
	else if ((int)r_fog->value)
	{
		R_Fog();
	}
	else
	{
		r_world_fog.fogMode = -1; // Fog off (gl1: glDisable(GL_FOG); the shaders check this).
	}

	r_world_fog.fogSkipAdditive = 0; // Toggled by the particle module around additive draws (gl1 glDisable(GL_FOG) parity).
}

#pragma endregion
