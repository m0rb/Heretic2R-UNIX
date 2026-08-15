#include "compat.h"
//
// gl3_Light.c -- dynamic lights, lightmap sampling and R_LightPoint().
//
// H2 semantics from gl1_Light.c (plus R_SetLightLevel() from gl1_Main.c) on the
// yq2 gl3_light.c backend: R_PushDlights() marks surfaces AND fills the
// uniLights UBO (dynamic lights are applied per-fragment in the lightmapped
// shaders); R_BuildLightMap() copies the raw per-style H2 lightmap samples into
// the 4 style atlas buffers (lightstyle/gl_modulate scaling moved to the
// shader-side lmScales; gl_minlight remap kept CPU-side).
//
// Copyright 1998 Raven Software
//

#include "gl3_World_internal.h"
#include "gl1_Matrix4.h"
#include "Angles.h"
#include "Vector.h"

#define DLIGHT_CUTOFF	64.0f

byte minlight[256]; // YQ2

static int r_dlightframecount; //mxd. Made static.

static vec3_t pointcolor;
static vec3_t lightspot; // DQII

typedef struct BmodelTransform_s //mxd
{
	matrix4_t matrix;
	qboolean updated;
} BmodelTransform_t;

static BmodelTransform_t r_bmodel_transforms[MAX_ENTITIES]; //mxd

#pragma region ========================== DYNAMIC LIGHTS RENDERING ==========================

// Q2 counterpart (except for dlight color handling).
// gl3: untextured additive triangle fan -> white texture + per-vertex colors through
// the alias vertex layout and the si3Dsprite program.
static void R_RenderDlight(const dlight_t* light)
{
	gl3_alias_vtx_t verts[18];

	const float rad = light->intensity * 0.35f;

	gl3_alias_vtx_t* vtx = &verts[0];
	for (int i = 0; i < 3; i++)
		vtx->pos[i] = light->origin[i] - vpn[i] * rad;

	vtx->texCoord[0] = 0.0f;
	vtx->texCoord[1] = 0.0f;
	vtx->color[0] = (float)light->color.r / 255.0f * 0.2f;
	vtx->color[1] = (float)light->color.g / 255.0f * 0.2f;
	vtx->color[2] = (float)light->color.b / 255.0f * 0.2f;
	vtx->color[3] = 1.0f;

	vtx++;
	for (int i = 16; i >= 0; i--, vtx++)
	{
		const float a = (float)i / 16.0f * ANGLE_360;
		const float sin_a = sinf(a); //mxd. Avoid calculating the same value 3 times...
		const float cos_a = cosf(a); //mxd. Avoid calculating the same value 3 times...

		for (int j = 0; j < 3; j++)
			vtx->pos[j] = light->origin[j] + vright[j] * cos_a * rad + vup[j] * sin_a * rad;

		vtx->texCoord[0] = 0.0f;
		vtx->texCoord[1] = 0.0f;
		vtx->color[0] = 0.0f;
		vtx->color[1] = 0.0f;
		vtx->color[2] = 0.0f;
		vtx->color[3] = 1.0f;
	}

	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 18);
}

void R_RenderDlights(void)
{
	if (!(int)gl_flashblend->value) // H2_1.07: the check is inverted.
		return;

	r_dlightframecount = r_framecount + 1; // Because the count hasn't advanced yet for this frame.

	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	// Coronas are drawn in world space.
	if (memcmp(&gl3state.uni3DData.transModelMat4, &gl3_identityMat4, sizeof(hmm_mat4)) != 0)
	{
		gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
		GL3_UpdateUBO3D();
	}

	GL3_UpdateSurfState(0.0f, 1.0f, -1.0f);
	GL3_UseProgram(gl3state.si3Dsprite.shaderProgram);
	GL3_BindTexnum(GL3_WhiteTexture());
	GL3_BindVAO(gl3state.vaoAlias);
	GL3_BindVBO(gl3state.vboAlias);

	dlight_t* l = &r_newrefdef.dlights[0];
	for (int i = 0; i < r_newrefdef.num_dlights; i++, l++)
		R_RenderDlight(l);

	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR
	glDepthMask(GL_TRUE);
}

#pragma endregion

#pragma region ========================== DYNAMIC LIGHTS MANAGEMENT ==========================

// Q2 counterpart
void R_MarkLights(dlight_t* light, const int bit, const mnode_t* node)
{
	if (node->contents != -1)
		return;

	const cplane_t* splitplane = node->plane;
	const float dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

	if (dist > light->intensity - DLIGHT_CUTOFF)
	{
		R_MarkLights(light, bit, node->children[0]);
		return;
	}

	if (dist < -light->intensity + DLIGHT_CUTOFF)
	{
		R_MarkLights(light, bit, node->children[1]);
		return;
	}

	// Mark the polygons.
	msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}

		surf->dlightbits |= bit;
	}

	R_MarkLights(light, bit, node->children[0]);
	R_MarkLights(light, bit, node->children[1]);
}

// Q2 counterpart + YQ2 uniLights UBO fill (the lightmapped shaders apply the
// marked dynamic lights per-fragment).
void R_PushDlights(void)
{
	// Fill the lights UBO. gl_modulate is baked into the colors
	// (gl1 R_AddDynamicLights() scaled the lightmap texel contribution by it).
	gl3state.uniLightsData.numDynLights = (GLuint)r_newrefdef.num_dlights;

	const dlight_t* dl = &r_newrefdef.dlights[0];
	for (int i = 0; i < r_newrefdef.num_dlights; i++, dl++)
	{
		gl3UniDynLight* udl = &gl3state.uniLightsData.dynLights[i];

		VectorCopy(dl->origin, udl->origin);
		for (int c = 0; c < 3; c++)
			udl->color[c] = (float)dl->color.c_array[c] * gl_modulate->value / 255.0f;

		udl->intensity = dl->intensity;
	}

	if (r_newrefdef.num_dlights < MAX_DLIGHTS)
	{
		memset(&gl3state.uniLightsData.dynLights[r_newrefdef.num_dlights], 0,
			(MAX_DLIGHTS - r_newrefdef.num_dlights) * sizeof(gl3state.uniLightsData.dynLights[0]));
	}

	GL3_UpdateUBOLights();

	if (!(int)gl_flashblend->value && r_worldmodel != NULL) //mxd. Added defensive r_worldmodel check.
	{
		r_dlightframecount = r_framecount + 1; // Because the count hasn't advanced yet for this frame.

		dlight_t* l = &r_newrefdef.dlights[0];
		for (int i = 0; i < r_newrefdef.num_dlights; i++, l++)
			R_MarkLights(l, 1 << i, r_worldmodel->nodes);
	}
}

static void R_SetPointColor(const msurface_t* surf, const int ds, const int dt, vec3_t color) //mxd. KMQ2 interpolated lighting logic.
{
	VectorClear(color);

	int r00 = 0;
	int g00 = 0;
	int b00 = 0;
	int r01 = 0;
	int g01 = 0;
	int b01 = 0;
	int r10 = 0;
	int g10 = 0;
	int b10 = 0;
	int r11 = 0;
	int g11 = 0;
	int b11 = 0;

	const int dsfrac = ds & 15;
	const int dtfrac = dt & 15;
	const int light_smax = (surf->extents[0] >> 4) + 1;
	const int light_tmax = (surf->extents[1] >> 4) + 1;
	const int line3 = light_smax * 3;
	const byte* lightmap = surf->samples + ((dt >> 4) * light_smax + (ds >> 4)) * 3;

	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		vec3_t scale;
		for (int c = 0; c < 3; c++)
			scale[c] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[c];

		r00 += (int)((float)lightmap[0] * scale[0]);
		g00 += (int)((float)lightmap[1] * scale[1]);
		b00 += (int)((float)lightmap[2] * scale[2]);

		r01 += (int)((float)lightmap[3] * scale[0]);
		g01 += (int)((float)lightmap[4] * scale[1]);
		b01 += (int)((float)lightmap[5] * scale[2]);

		r10 += (int)((float)lightmap[line3 + 0] * scale[0]);
		g10 += (int)((float)lightmap[line3 + 1] * scale[1]);
		b10 += (int)((float)lightmap[line3 + 2] * scale[2]);

		r11 += (int)((float)lightmap[line3 + 3] * scale[0]);
		g11 += (int)((float)lightmap[line3 + 4] * scale[1]);
		b11 += (int)((float)lightmap[line3 + 5] * scale[2]);

		lightmap += light_smax * light_tmax * 3;
	}

	color[0] += (float)(((((((r11 - r10) * dsfrac >> 4) + r10) - (((r01 - r00) * dsfrac >> 4) + r00)) * dtfrac) >> 4) + (((r01 - r00) * dsfrac >> 4) + r00)) / 255.0f;
	color[1] += (float)(((((((g11 - g10) * dsfrac >> 4) + g10) - (((g01 - g00) * dsfrac >> 4) + g00)) * dtfrac) >> 4) + (((g01 - g00) * dsfrac >> 4) + g00)) / 255.0f;
	color[2] += (float)(((((((b11 - b10) * dsfrac >> 4) + b10) - (((b01 - b00) * dsfrac >> 4) + b00)) * dtfrac) >> 4) + (((b01 - b00) * dsfrac >> 4) + b00)) / 255.0f;

	Vec3ScaleAssign(gl_modulate->value, color);
}

void R_ResetBmodelTransforms(void) //mxd
{
	for (int i = 0; i < r_newrefdef.num_entities; i++)
		r_bmodel_transforms[i].updated = false;
}

static int R_RecursiveLightPoint(const mnode_t* node, const vec3_t start, const vec3_t end)
{
	// Guard against NULL or sentinel-value node pointers (can occur with corrupt BSP data or
	// entities positioned outside the world).
	if (node == NULL || (uintptr_t)node == (uintptr_t)-1)
		return -1;

	// Didn't hit anything.
	if (node->contents != -1)
		return -1;

	// Calculate mid point.
	const cplane_t* plane = node->plane;
	if (plane == NULL || (uintptr_t)plane == (uintptr_t)-1)
		return -1;

	const float front = DotProduct(start, plane->normal) - plane->dist;
	const float back = DotProduct(end, plane->normal) - plane->dist;
	const int side = (front < 0.0f);

	if ((back < 0.0f) == side)
		return R_RecursiveLightPoint(node->children[side], start, end);

	const float frac = front / (front - back);

	vec3_t mid;
	VectorLerp(start, frac, end, mid);

	// Go down front side.
	const int r = R_RecursiveLightPoint(node->children[side], start, mid);

	// Hit something.
	if (r >= 0)
		return r;

	// Didn't hit anything.
	if ((back < 0.0f) == side)
		return -1;

	VectorCopy(mid, lightspot); // DQII

	// Check for impact on this node.
	msurface_t* surf = &r_worldmodel->surfaces[node->firstsurface];
	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->samples == NULL)
			continue; // No lightmap data. Was 'return 0' in original logic --mxd.

		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY | SURF_SKIPDRAW)) //mxd. Also skip SURF_NODRAW surfaces.
			continue; // No lightmaps.

		const mtexinfo_t* tex = surf->texinfo;

		const int s = (int)(DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3]);
		const int t = (int)(DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3]);

		if (s < surf->texturemins[0] || t < surf->texturemins[1])
			continue;

		const int ds = s - surf->texturemins[0];
		const int dt = t - surf->texturemins[1];

		if (ds > surf->extents[0] || dt > surf->extents[1])
			continue;

		R_SetPointColor(surf, ds, dt, pointcolor); //mxd

		return 1;
	}

	// Go down back side.
	return R_RecursiveLightPoint(node->children[!side], mid, end);
}

void R_LightPoint(const vec3_t p, vec3_t color, const qboolean check_bmodels)
{
	if (r_worldmodel == NULL || r_worldmodel->lightdata == NULL) //mxd. Added defensive r_worldmodel check.
	{
		VectorSet(color, 1.0f, 1.0f, 1.0f);
		return;
	}

	float dist_z;
	const vec3_t end = VEC3_INITA(p, 0.0f, 0.0f, -3072.0f); // Q2: p[2] - 2048
	const int r = R_RecursiveLightPoint(r_worldmodel->nodes, p, end);

	if (r == -1)
	{
		VectorSet(color, 0.25f, 0.25f, 0.25f); // Q2: VectorCopy(vec3_origin, color)
		dist_z = end[2]; // DQII
	}
	else
	{
		VectorCopy(pointcolor, color);
		dist_z = lightspot[2]; // DQII
	}

	//mxd. Ported DQII R_LightPoint logic (https://github.com/mhQuake/DirectQII/blob/4a2ae6383f74ae3deda327b19748f0924d212daf/DirectQII/r_light.c#L156).
	if (check_bmodels)
	{
		// Find bmodels under the lightpoint - move the point to bmodel space, trace down, then check.
		// If r < 0, it didn't find a bmodel, otherwise it did (a bmodel under a valid world hit will hit here too).
		for (int i = 0; i < r_newrefdef.num_entities; i++)
		{
			const entity_t* e = r_newrefdef.entities[i];

			if ((e->flags & RF_TRANSLUCENT) || e->model == NULL || *e->model == NULL || (*e->model)->type != mod_brush)
				continue;

			const model_t* mdl = *e->model;

			if (Vec3IsZero(e->origin))
			{
				//mxd. For non-rotating bmodels, check bbox.
				if (p[0] < mdl->mins[0] || p[0] > mdl->maxs[0] ||
					p[1] < mdl->mins[1] || p[1] > mdl->maxs[1] ||
					p[2] < mdl->mins[2] || end[2] > mdl->maxs[2])
				{
					continue;
				}
			}
			else
			{
				//mxd. For bmodels with defined origin, skip when not within model radius.
				if (p[0] < e->origin[0] - mdl->radius || p[0] > e->origin[0] + mdl->radius ||
					p[1] < e->origin[1] - mdl->radius || p[1] > e->origin[1] + mdl->radius ||
					p[2] < e->origin[2] - mdl->radius || end[2] > e->origin[2] + mdl->radius)
				{
					continue;
				}
			}

			//mxd. Lazily update bmodel transform...
			BmodelTransform_t* t = &r_bmodel_transforms[i];
			if (!t->updated)
			{
				R_MatrixIdentity(&t->matrix);
				R_MatrixTranslate(&t->matrix, e->origin);
				R_MatrixRotate(&t->matrix, e->angles);

				t->updated = true;
			}

			// Move start and end points into the entity's frame of reference.
			vec3_t e_start;
			R_VectorInverseTransform(&t->matrix, e_start, p);

			vec3_t e_end;
			R_VectorInverseTransform(&t->matrix, e_end, end);

			// And run the recursive light point on it too.
			if (R_RecursiveLightPoint(mdl->nodes + mdl->firstnode, e_start, e_end) == -1)
				continue;

			// A bmodel under a valid world hit will hit here too, so take the highest lightspot on all hits.
			vec3_t cur_spot;
			R_VectorTransform(&t->matrix, cur_spot, lightspot); // Move lightspot back to world space.

			if (cur_spot[2] > dist_z)
			{
				// Found a bmodel so copy it over.
				VectorCopy(pointcolor, color);
				dist_z = cur_spot[2];
			}
		}
	}

	// Add dynamic lights.
	dlight_t* dl = &r_newrefdef.dlights[0];
	vec3_t dl_color = VEC3_ZERO; //mxd
	for (int lnum = 0; lnum < r_newrefdef.num_dlights; lnum++, dl++)
	{
		const float dist = VectorSeparation(p, dl->origin); //mxd. Original logic uses 'currententity->origin' instead of 'p' here.
		const float add = (dl->intensity - dist) / 256.0f;

		if (add > 0.0f)
			for (int i = 0; i < 3; i++)
				dl_color[i] += (float)dl->color.c_array[i] / 255.0f * add;
	}

	Vec3ScaleAssign(gl_modulate->value, dl_color); //mxd. Original logic scales 'color' var here (which is already scaled by gl_modulate in R_RecursiveLightPoint()).
	Vec3AddAssign(dl_color, color);
}

// gl1: in gl1_Main.c. Saves off light value for server to look at (BIG HACK!).
void R_SetLightLevel(void)
{
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		vec3_t shadelight;
		R_LightPoint(r_newrefdef.clientmodelorg, shadelight, true); // H2: vieworg -> clientmodelorg

		// Pick the greatest component, which should be the same as the mono value returned by software.
		// Max. shadelight can exceed 1.0 when player is affected by dynamic lights --mxd.
		r_lightlevel->value = max(shadelight[0], max(shadelight[1], shadelight[2])) * 150.0f / gl_modulate->value; //mxd. Undo gl_modulate scaler (to avoid affecting cmd.lightlevel).
	}
}

#pragma endregion

#pragma region ========================== LIGHTMAP BUILDING ==========================

// Q2 counterpart
void R_SetCacheState(msurface_t* surf)
{
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
}

void R_InitMinlight(void) //mxd
{
	const float ml = Clamp(gl_minlight->value, 0.0f, 255.0f);
	gl3state.minlight_set = (ml != 0.0f);

	if (gl3state.minlight_set)
	{
		for (int i = 0; i < 256; i++)
		{
			const int inf = (int)((255.0f - ml) * (float)i / 255.0f + ml);
			minlight[i] = (byte)ClampI(inf, 0, 255);
		}
	}
	else
	{
		for (int i = 0; i < 256; i++)
			minlight[i] = (byte)i;
	}
}

// Q2 counterpart (in H2, except for extra SURF_TALL_WALL flag).
// gl3 (yq2): copies the raw per-style H2 lightmap samples into the 4 style atlas buffers.
// Unlike gl1, no lightstyle / gl_modulate scaling (shader-side lmScales), no dynamic lights
// (uniLights UBO) and no channel-max rescale (raw bytes can't exceed 255) happen here;
// the YQ2 gl_minlight remap is kept (applied per style map - exact for the common
// single-style case).
void R_BuildLightMap(const msurface_t* surf, const int offset_in_lm_buf, int stride)
{
	if (surf->texinfo->flags & SURF_FULLBRIGHT) //mxd. SURF_FULLBRIGHT define.
		ri.Sys_Error(ERR_DROP, "R_BuildLightMap called for non-lit surface");

	const int smax = (surf->extents[0] >> 4) + 1;
	const int tmax = (surf->extents[1] >> 4) + 1;
	const int size = smax * tmax;

	if (size > 34 * 34)
		ri.Sys_Error(ERR_DROP, "Bad s_blocklights size");

	stride -= (smax << 2);

	// Count the number of lightmaps this surf actually has. // YQ2
	int num_maps;
	for (num_maps = 0; num_maps < MAXLIGHTMAPS && surf->styles[num_maps] != 255; num_maps++)
	{
	}

	// Set to full bright if no light data.
	if (surf->samples == NULL)
	{
		// YQ2: make sure at least one lightmap is set to fullbright, the rest to 0 -
		// all 4 sub-lightmaps share the atlas layout, so the shader can use the same
		// texture coordinates for all of them.
		if (num_maps == 0)
			num_maps = 1;

		for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
		{
			const byte c = (byte)((map < num_maps) ? 255 : 0);
			byte* dest = gl_lms.lightmap_buffers[map] + offset_in_lm_buf;

			for (int i = 0; i < tmax; i++, dest += stride)
			{
				memset(dest, c, 4 * smax);
				dest += 4 * smax;
			}
		}

		return;
	}

	// Add all the lightmaps.
	const byte* lightmap = surf->samples;

	int map;
	for (map = 0; map < num_maps; map++)
	{
		byte* dest = gl_lms.lightmap_buffers[map] + offset_in_lm_buf;
		int sample = 0;

		for (int i = 0; i < tmax; i++, dest += stride)
		{
			for (int j = 0; j < smax; j++, sample += 3, dest += 4)
			{
				byte r = lightmap[sample + 0];
				byte g = lightmap[sample + 1];
				byte b = lightmap[sample + 2];

				if (gl3state.minlight_set) // YQ2
				{
					r = minlight[r];
					g = minlight[g];
					b = minlight[b];
				}

				dest[0] = r;
				dest[1] = g;
				dest[2] = b;
				dest[3] = 255; //mxd. Alpha was ONLY used for the mono lightmap case.
			}
		}

		// Skip to next lightmap.
		lightmap += size * 3;
	}

	// Fill up the remaining style sub-lightmaps with 0. // YQ2
	for (; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
	{
		byte* dest = gl_lms.lightmap_buffers[map] + offset_in_lm_buf;

		for (int i = 0; i < tmax; i++, dest += stride)
		{
			memset(dest, 0, 4 * smax);
			dest += 4 * smax;
		}
	}
}

#pragma endregion
