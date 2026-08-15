#include "compat.h"
//
// vk_Warp.c -- warped (water) surfaces rendering and subdivision.
//
// H2 semantics from gl1_Warp.c via the validated gl3_Warp.c port:
//  - The classic turbsin ST warp + SURF_FLOWING scroll runs ANALYTICALLY in
//    the polygon_warp.vert shader (locked foundation shader, yq2 technique):
//    the vertex stream carries the ST from R_SubdividePolygon() normalized by
//    64 and the per-draw UBO carries time + normalized scroll. The shared
//    engine turbsin[] table is left UNTOUCHED by ref_vk (gl1 halved it at
//    RI_Init()).
//  - The H2 vertex Z displacements (SURF_UNDULATE water bobbing, the
//    underwater whole-world warp and the quake_amount floor ripple) stay
//    CPU-side, exactly like gl1/gl3 - but since the shared turbsin[] table is
//    not halved here, the gl1 displacement factors are multiplied by 0.5.
//  - R_EmitUnderwaterPolys()/R_EmitQuakeFloorPolys() draw plain-textured with
//    the raw UNNORMALIZED subdivision ST (H2 quirk kept - repeat sampler
//    wraps it) through the polygon pipeline (vk_Surface.c R_DrawPolyVerts()).
//
// Copyright 1998 Raven Software
//

#include "vk_World_internal.h"
#include "Hunk.h"
#include "turbsin.h"

#define SUBDIVIDE_SIZE	64.0f

#define MAX_WARP_VERTS	MAX_POLY_VERTS // R_SubdividePolygon() emits at most 62 verts.

static vk_3D_vtx_t warp_vtx[MAX_WARP_VERTS];

// polygon_warp.vert per-draw UBO (set 1, binding 0).
typedef struct
{
	float model[16];
	float color[4];
	float time;
	float sscroll;
	float tscroll;
} vkwarpubo_t;

#pragma region ========================== POLYGON GENERATION ==========================

// Converts a warp poly (raw gl1 7-float verts) to vk_3D_vtx_t scratch verts,
// displacing vertex Z by 'z_scale' x the H2 turbsin terms (z_scale 0.0 = no
// displacement). 'st_scale' is 1/64 for the warp shader (which expects
// normalized ST - yq2 EmitWaterPolys()) and 1.0 for the plain-textured
// underwater/quake paths (gl1 passed the raw subdivision ST unnormalized to
// plain texturing, H2 quirk kept).
static const vk_3D_vtx_t* R_BuildWarpVerts(const msurface_t* surf, const glpoly_t* p, const float z_scale, const float st_scale)
{
	if (p->numverts > MAX_WARP_VERTS)
		ri.Sys_Error(ERR_DROP, "R_BuildWarpVerts: too many verts (%i)", p->numverts);

	vec3_t normal;
	VectorCopy(surf->plane->normal, normal);

	if (surf->flags & SURF_PLANEBACK)
		VectorScale(normal, -1.0f, normal);

	vk_3D_vtx_t* vtx = &warp_vtx[0];
	const float* v = p->verts[0];

	for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE, vtx++)
	{
		VectorCopy(v, vtx->pos);

		if (z_scale != 0.0f)
		{
			// gl1 factors 0.5f / 0.25f on the HALVED turbsin table -> 0.25f / 0.125f on the raw one.
			vtx->pos[2] += turbsin[TURBSIN_V0(v[0], v[1], r_newrefdef.time)] * z_scale * 0.25f +
						   turbsin[TURBSIN_V1(v[0], v[1], r_newrefdef.time)] * z_scale * 0.125f;
		}

		vtx->texCoord[0] = v[3] * st_scale;
		vtx->texCoord[1] = v[4] * st_scale;
		vtx->lmTexCoord[0] = 0.0f;
		vtx->lmTexCoord[1] = 0.0f;
		VectorCopy(normal, vtx->normal);
		vtx->lightFlags = 0;
	}

	return warp_vtx;
}

// Does a water warp on the pre-fragmented glpoly_t chain.
void R_EmitWaterPolys(const msurface_t* fa, const image_t* image, const qboolean undulate) // H2: extra 'undulate' arg.
{
	if (!R_ImageUsable(image))
		return; // Module-port ordering guard (world textures come from vk_Image.c).

	float scroll;

	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64.0f * ((r_newrefdef.time * 0.5f) - floorf(r_newrefdef.time * 0.5f)); //mxd. Replaced int cast with floorf.
	else
		scroll = 0.0f;

	// The turbsin ST warp itself runs in the polygon_warp vertex shader;
	// alpha / alpha test state set by the caller (opaque pass: 1.0 / off,
	// alpha pass: gl_trans33/66 / 0.05) is kept (r_surf_alpha /
	// r_surf_alpha_test - the gl3 "ambient uni3D state" equivalents).
	vkwarpubo_t warp_ubo;
	memcpy(warp_ubo.model, r_local_model_matrix, sizeof(warp_ubo.model));
	warp_ubo.color[0] = 1.0f;
	warp_ubo.color[1] = 1.0f;
	warp_ubo.color[2] = 1.0f; // gl1: glColor4f(inverse_intensity x3, alpha); inverse_intensity == 1.0 (gl3 parity).
	warp_ubo.color[3] = r_surf_alpha;
	warp_ubo.time = r_newrefdef.time;
	warp_ubo.sscroll = scroll / 64.0f; // The warp shader works on ST normalized by 64 (yq2).
	warp_ubo.tscroll = 0.0f;

	// gl1: opaque world water drew unblended, the alpha-sorted pass blended
	// (yq2: solid vs blend warp pipeline picked by alpha == 1.0).
	qvkpipeline_t* pipeline = ((r_surf_alpha == 1.0f) ? &vk_drawPolySolidWarpPipeline : &vk_drawPolyWarpPipeline);

	uint32_t ubo_offset;
	VkDescriptorSet ubo_set;
	uint8_t* ubo = QVk_GetUniformBuffer(sizeof(warp_ubo), &ubo_offset, &ubo_set);
	memcpy(ubo, &warp_ubo, sizeof(warp_ubo));

	QVk_BindPipeline(pipeline);

	const VkDescriptorSet desc_sets[] = { image->vk_texture.descriptorSet, ubo_set };
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, 2, desc_sets, 1, &ubo_offset);

	QVk_PushMatrix(pipeline, r_viewproj_matrix);
	QVk_PushWorldFragmentConstants(pipeline, r_surf_alpha_test);

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		// H2: new undulate logic (gl1: z += turbsin_halved[V0] * 0.25f + turbsin_halved[V1] * 0.125f).
		const vk_3D_vtx_t* verts = R_BuildWarpVerts(fa, p, (undulate ? 0.5f : 0.0f), 1.0f / 64.0f);
		QVk_DrawTriangleFan(verts, sizeof(vk_3D_vtx_t), p->numverts);
	}
}

//TODO: Warps all bmodel polys when camera is underwater. Seems to be used only when r_fullbright is 1. H2 bug?
void R_EmitUnderwaterPolys(const msurface_t* fa, const image_t* image) // H2
{
	// Plain textured draw with displaced Z (gl1: z += turbsin_halved[V0] * 0.5f + turbsin_halved[V1] * 0.25f).
	const float color[4] = { 1.0f, 1.0f, 1.0f, r_surf_alpha };

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		const vk_3D_vtx_t* verts = R_BuildWarpVerts(fa, p, 1.0f, 1.0f);
		R_DrawPolyVerts(verts, p->numverts, image, color, r_surf_alpha_test);
	}
}

//TODO: Warps all bmodel polys when quake_amount > 0. Seems to be used only when r_fullbright is 1. H2 bug?
void R_EmitQuakeFloorPolys(const msurface_t* fa, const image_t* image) // H2
{
	const float amount = (quake_amount->value * 0.05f);

	// gl1: z += turbsin_halved[V0] * amount * 0.5f + turbsin_halved[V1] * amount * 0.25f.
	const float color[4] = { 1.0f, 1.0f, 1.0f, r_surf_alpha };

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		const vk_3D_vtx_t* verts = R_BuildWarpVerts(fa, p, amount, 1.0f);
		R_DrawPolyVerts(verts, p->numverts, image, color, r_surf_alpha_test);
	}
}

#pragma endregion

#pragma region ========================== SURFACE SUBDIVISION ==========================

// Q2 counterpart
static void R_BoundPoly(const int numverts, const float* verts, vec3_t mins, vec3_t maxs)
{
	ClearBounds(mins, maxs); //mxd. Original code directly sets mins to 9999, maxs to -9999.

	const float* v = verts;
	for (int i = 0; i < numverts; i++)
	{
		for (int j = 0; j < 3; j++, v++)
		{
			mins[j] = min(*v, mins[j]);
			maxs[j] = max(*v, maxs[j]);
		}
	}
}

// Q2 counterpart
static void R_SubdividePolygon(msurface_t* warpface, const int numverts, float* verts) //mxd. Added 'warpface' arg.
{
	vec3_t mins;
	vec3_t maxs;
	vec3_t front[64];
	vec3_t back[64];
	float dist[64];
	vec3_t total;

	if (numverts > 60)
		ri.Sys_Error(ERR_DROP, "numverts = %i", numverts);

	R_BoundPoly(numverts, verts, mins, maxs);

	for (int i = 0; i < 3; i++)
	{
		float m = (mins[i] + maxs[i]) * 0.5f;
		m = SUBDIVIDE_SIZE * floorf(m / SUBDIVIDE_SIZE + 0.5f); //mxd. floor -> floorf

		if (maxs[i] - m < 8.0f || m - mins[i] < 8.0f)
			continue;

		// Cut it.
		float* v = verts + i;
		for (int j = 0; j < numverts; j++, v += 3)
			dist[j] = *v - m;

		// Wrap cases.
		dist[numverts] = dist[0];
		v -= i;
		VectorCopy(verts, v);

		int f = 0;
		int b = 0;
		v = verts;
		for (int j = 0; j < numverts; j++, v += 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy(v, front[f]);
				f++;
			}

			if (dist[j] <= 0)
			{
				VectorCopy(v, back[b]);
				b++;
			}

			if (dist[j] == 0.0f || dist[j + 1] == 0.0f)
				continue;

			if ((dist[j] > 0) != (dist[j + 1] > 0))
			{
				// Clip point.
				const float frac = dist[j] / (dist[j] - dist[j + 1]);

				for (int k = 0; k < 3; k++)
					front[f][k] = back[b][k] = v[k] + frac * (v[3 + k] - v[k]);

				f++;
				b++;
			}
		}

		R_SubdividePolygon(warpface, f, front[0]);
		R_SubdividePolygon(warpface, b, back[0]);

		return;
	}

	// Add a point in the center to help keep warp valid.
	glpoly_t* poly = Hunk_Alloc((int)sizeof(glpoly_t) + ((numverts - 4) + 2) * VERTEXSIZE * sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts + 2;
	VectorClear(total);

	float total_s = 0.0f;
	float total_t = 0.0f;

	for (int i = 0; i < numverts; i++, verts += 3)
	{
		VectorCopy(verts, poly->verts[i + 1]);
		const float s = DotProduct(verts, warpface->texinfo->vecs[0]);
		const float t = DotProduct(verts, warpface->texinfo->vecs[1]);

		total_s += s;
		total_t += t;
		Vec3AddAssign(verts, total);

		poly->verts[i + 1][3] = s;
		poly->verts[i + 1][4] = t;
	}

	VectorScale(total, 1.0f / (float)numverts, poly->verts[0]);
	poly->verts[0][3] = total_s / (float)numverts;
	poly->verts[0][4] = total_t / (float)numverts;

	// Copy first vertex to last.
	memcpy(poly->verts[numverts + 1], poly->verts[1], sizeof(poly->verts[0]));
}

// Breaks a polygon up along axial 64 unit boundaries so that turbulent and sky warps can be done reasonably.
void R_SubdivideSurface(const model_t* mdl, msurface_t* fa)
{
	static vec3_t verts[64]; //mxd. Made static.
	float* vec;

	// Convert edges back to a normal polygon.
	int numverts;
	for (numverts = 0; numverts < fa->numedges; numverts++)
	{
		const int lindex = mdl->surfedges[fa->firstedge + numverts];

		if (lindex > 0)
			vec = mdl->vertexes[mdl->edges[lindex].v[0]].position;
		else
			vec = mdl->vertexes[mdl->edges[-lindex].v[1]].position;

		VectorCopy(vec, verts[numverts]);
	}

	R_SubdividePolygon(fa, numverts, verts[0]);
}

#pragma endregion
