#include "compat.h"
//
// gl3_Warp.c -- warped (water) surfaces rendering and subdivision.
//
// H2 semantics from gl1_Warp.c:
//  - The classic turbsin ST warp + SURF_FLOWING scroll runs ANALYTICALLY in the
//    si3Dturb fragment shader (foundation decision, see gl3_Shaders.c): the
//    vertex stream carries the raw unwarped ST from R_SubdividePolygon() and
//    uni3D.scroll carries the flow offset. The shader's sin() amplitude (4.0)
//    bakes in the 0.5 turbsin-table halving gl1 did at RI_Init() - the shared
//    engine turbsin[] table is left UNTOUCHED by ref_gl3.
//  - The H2 vertex Z displacements (SURF_UNDULATE water bobbing, the underwater
//    whole-world warp and the quake_amount floor ripple) stay CPU-side, exactly
//    like gl1 - but since the shared turbsin[] table is not halved here, the gl1
//    displacement factors are multiplied by 0.5.
//
// Copyright 1998 Raven Software
//

#include "gl3_World_internal.h"
#include "Hunk.h"
#include "turbsin.h"
#include "Vector.h"

#define SUBDIVIDE_SIZE	64.0f

#define MAX_WARP_VERTS	64 // R_SubdividePolygon() emits at most 62 verts.

static gl3_3D_vtx_t warp_vtx[MAX_WARP_VERTS];

#pragma region ========================== POLYGON GENERATION ==========================

// Converts a warp poly (raw gl1 7-float verts) to gl3_3D_vtx_t scratch verts,
// displacing vertex Z by 'z_scale' x the H2 turbsin terms (z_scale 0.0 = no displacement).
// NOTE: the raw ST from subdivision goes into texCoord verbatim - the turb shader
// warps and normalizes it (and gl1 R_EmitUnderwaterPolys()/R_EmitQuakeFloorPolys()
// pass it unnormalized to plain texturing, H2 quirk kept).
static const gl3_3D_vtx_t* R_BuildWarpVerts(const msurface_t* surf, const glpoly_t* p, const float z_scale)
{
	if (p->numverts > MAX_WARP_VERTS)
		ri.Sys_Error(ERR_DROP, "R_BuildWarpVerts: too many verts (%i)", p->numverts);

	vec3_t normal;
	VectorCopy(surf->plane->normal, normal);

	if (surf->flags & SURF_PLANEBACK)
		VectorScale(normal, -1.0f, normal);

	gl3_3D_vtx_t* vtx = &warp_vtx[0];
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

		vtx->texCoord[0] = v[3];
		vtx->texCoord[1] = v[4];
		vtx->lmTexCoord[0] = 0.0f;
		vtx->lmTexCoord[1] = 0.0f;
		VectorCopy(normal, vtx->normal);
		vtx->lightFlags = 0;
	}

	return warp_vtx;
}

// Does a water warp on the pre-fragmented glpoly_t chain.
void R_EmitWaterPolys(const msurface_t* fa, const qboolean undulate) // H2: extra 'undulate' arg.
{
	float scroll;

	if (fa->texinfo->flags & SURF_FLOWING)
		scroll = -64.0f * ((r_newrefdef.time * 0.5f) - floorf(r_newrefdef.time * 0.5f)); //mxd. Replaced int cast with floorf.
	else
		scroll = 0.0f;

	// The turbsin ST warp itself runs in the si3Dturb fragment shader; alpha / alpha test
	// state set by the caller (opaque pass: 1.0 / off, alpha pass: gl_trans33/66 / 0.05) is kept.
	GL3_UpdateSurfState(scroll, gl3state.uni3DData.alpha, gl3state.uni3DData.alphaTestRef);
	GL3_UseProgram(gl3state.si3Dturb.shaderProgram);

	GL3_BindVAO(gl3state.vao3D);
	GL3_BindVBO(gl3state.vbo3D);

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
	{
		// H2: new undulate logic (gl1: z += turbsin_halved[V0] * 0.25f + turbsin_halved[V1] * 0.125f).
		GL3_BufferAndDraw3D(R_BuildWarpVerts(fa, p, undulate ? 0.5f : 0.0f), p->numverts, GL_TRIANGLE_FAN);
	}
}

//TODO: Warps all bmodel polys when camera is underwater. Seems to be used only when r_fullbright is 1. H2 bug?
void R_EmitUnderwaterPolys(const msurface_t* fa) // H2
{
	// Plain textured draw with displaced Z (gl1: z += turbsin_halved[V0] * 0.5f + turbsin_halved[V1] * 0.25f).
	GL3_UpdateSurfState(0.0f, gl3state.uni3DData.alpha, gl3state.uni3DData.alphaTestRef);
	GL3_UseProgram(gl3state.si3Dtrans.shaderProgram);

	GL3_BindVAO(gl3state.vao3D);
	GL3_BindVBO(gl3state.vbo3D);

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
		GL3_BufferAndDraw3D(R_BuildWarpVerts(fa, p, 1.0f), p->numverts, GL_TRIANGLE_FAN);
}

//TODO: Warps all bmodel polys when quake_amount > 0. Seems to be used only when r_fullbright is 1. H2 bug?
void R_EmitQuakeFloorPolys(const msurface_t* fa) // H2
{
	const float amount = (quake_amount->value * 0.05f);

	// gl1: z += turbsin_halved[V0] * amount * 0.5f + turbsin_halved[V1] * amount * 0.25f.
	GL3_UpdateSurfState(0.0f, gl3state.uni3DData.alpha, gl3state.uni3DData.alphaTestRef);
	GL3_UseProgram(gl3state.si3Dtrans.shaderProgram);

	GL3_BindVAO(gl3state.vao3D);
	GL3_BindVBO(gl3state.vbo3D);

	for (const glpoly_t* p = fa->polys; p != NULL; p = p->next)
		GL3_BufferAndDraw3D(R_BuildWarpVerts(fa, p, amount), p->numverts, GL_TRIANGLE_FAN);
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
