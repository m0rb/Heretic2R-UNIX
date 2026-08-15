#include "compat.h"
//
// gl3_Lightmap.c -- lightmap atlas building.
//
// H2 semantics from gl1_Lightmap.c on the yq2 gl3_lightmap.c atlas model:
// 4 big BLOCK_WIDTH x BLOCK_HEIGHT atlases, each consisting of 4 lightstyle
// sub-lightmaps (bound to texture units 1..4). The gl1 dynamic lightmap
// (slot 0) is gone: lightstyles are scaled in the fragment shader (lmScales)
// and dynamic lights are applied per-fragment from the uniLights UBO.
//
// Copyright 1998 Raven Software
//

#include "gl3_World_internal.h"
#include "Hunk.h"
#include "Vector.h"

gl3lightmapstate_t gl_lms;

#pragma region ========================== LIGHTMAP ALLOCATION ==========================

// YQ2. Binds the 4 lightstyle sub-lightmaps of the given atlas to texture units 1..4.
void GL3_BindLightmap(const int lightmapnum)
{
	if (lightmapnum < 0 || lightmapnum >= MAX_LIGHTMAPS)
	{
		ri.Con_Printf(PRINT_ALL, "WARNING: GL3_BindLightmap(%d) out of range!\n", lightmapnum);
		return;
	}

	if (gl3state.currentlightmap == lightmapnum)
		return;

	gl3state.currentlightmap = lightmapnum;

	for (int i = 0; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
	{
		// This assumes that GL_TEXTURE<i+1> = GL_TEXTURE1 + i.
		GL3_SelectTMU(GL_TEXTURE1 + i);
		glBindTexture(GL_TEXTURE_2D, gl3state.lightmap_textureIDs[lightmapnum][i]);
	}

	GL3_SelectTMU(GL_TEXTURE0);
}

// Q2 counterpart
void LM_InitBlock(void)
{
	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));
}

// Q2 counterpart (gl1 had a 'dynamic' arg; the gl3 lightmap model has no dynamic block -
// all lightmaps are uploaded at level load and never change afterwards).
void LM_UploadBlock(void)
{
	// Bypass the GL3_BindLightmap() cache: during level load the texture names
	// haven't been bound/initialized yet.
	gl3state.currentlightmap = gl_lms.current_lightmap_texture;

	// Upload all 4 lightstyle sub-lightmaps. // YQ2
	for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
	{
		// This assumes that GL_TEXTURE<map+1> = GL_TEXTURE1 + map.
		GL3_SelectTMU(GL_TEXTURE1 + map);
		glBindTexture(GL_TEXTURE_2D, gl3state.lightmap_textureIDs[gl_lms.current_lightmap_texture][map]);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); //mxd. qglTexParameterf -> qglTexParameteri
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); //mxd. qglTexParameterf -> qglTexParameteri

		// gl1 GL_TEX_SOLID_FORMAT note: use GL_RGBA to match GL_LIGHTMAP_FORMAT (4 bytes per pixel).
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, gl_lms.lightmap_buffers[map]);
	}

	GL3_SelectTMU(GL_TEXTURE0);

	gl_lms.current_lightmap_texture++;

	if (gl_lms.current_lightmap_texture == MAX_LIGHTMAPS)
		ri.Sys_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
}

// Q2 counterpart. Returns a texture number and the position inside it.
qboolean LM_AllocBlock(const int w, const int h, int* x, int* y)
{
	int j;
	int best = BLOCK_HEIGHT;

	for (int i = 0; i < BLOCK_WIDTH - w; i++)
	{
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (gl_lms.allocated[i + j] >= best)
				break;

			if (gl_lms.allocated[i + j] > best2)
				best2 = gl_lms.allocated[i + j];
		}

		if (j == w)
		{
			// This is a valid spot.
			*x = i;
			*y = best2;
			best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return false;

	for (int i = 0; i < w; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

#pragma endregion

#pragma region ========================== LIGHTMAP BUILDING ==========================

// Q2 counterpart
void LM_BuildPolygonFromSurface(const model_t* mdl, msurface_t* fa) //mxd. Original logic uses 'currentmodel' global var here.
{
	// Reconstruct the polygon.
	const medge_t* pedges = mdl->edges;
	const int lnumverts = fa->numedges;

	// Draw texture.
	glpoly_t* poly = Hunk_Alloc((int)sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (int i = 0; i < lnumverts; i++)
	{
		const int lindex = mdl->surfedges[fa->firstedge + i];

		float* vec;
		if (lindex > 0)
		{
			const medge_t* r_pedge = &pedges[lindex];
			vec = mdl->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			const medge_t* r_pedge = &pedges[-lindex];
			vec = mdl->vertexes[r_pedge->v[1]].position;
		}

		float s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= (float)fa->texinfo->image->width;

		float t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= (float)fa->texinfo->image->height;

		VectorCopy(vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// Lightmap texture coordinates.
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= (float)fa->texturemins[0];
		s += (float)fa->light_s * 16;
		s += 8;
		s /= BLOCK_WIDTH * 16;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= (float)fa->texturemins[1];
		t += (float)fa->light_t * 16;
		t += 8;
		t /= BLOCK_HEIGHT * 16;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}
}

// Q2 counterpart
void LM_CreateSurfaceLightmap(msurface_t* surf)
{
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	const int smax = ((int)surf->extents[0] >> 4) + 1;
	const int tmax = ((int)surf->extents[1] >> 4) + 1;

	if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
	{
		LM_UploadBlock();
		LM_InitBlock();

		if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
			ri.Sys_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax);
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	R_SetCacheState(surf);
	R_BuildLightMap(surf, (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES, BLOCK_WIDTH * LIGHTMAP_BYTES);
}

// Q2 counterpart
void LM_BeginBuildingLightmaps(void) //mxd. Removed unused model_t* arg.
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];

	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));

	r_framecount = 1; // No dlightcache.

	// Setup the base lightstyles so the lightmaps won't have to be regenerated the first time they're seen.
	for (int i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1.0f;
		lightstyles[i].rgb[1] = 1.0f;
		lightstyles[i].rgb[2] = 1.0f;
		lightstyles[i].white = 3.0f;
	}

	r_newrefdef.lightstyles = lightstyles;

	// gl3: no dynamic lightmap (gl1 slot 0) - static atlases start at 0.
	// The texture names were pre-generated in RI_Init() (gl3state.lightmap_textureIDs).
	gl_lms.current_lightmap_texture = 0;
	gl3state.currentlightmap = -1; // Invalidate the GL3_BindLightmap() cache.
}

// Q2 counterpart
void LM_EndBuildingLightmaps(void)
{
	LM_UploadBlock();
}

#pragma endregion
