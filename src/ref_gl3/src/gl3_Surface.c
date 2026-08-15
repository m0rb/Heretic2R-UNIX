#include "compat.h"
//
// gl3_Surface.c -- world / brush-model surface rendering.
//
// H2 semantics ported from gl1_Surface.c (plus the world-frame helpers
// R_SetFrustum() / viewcluster setup from gl1_Main.c, which gl3_Main.c does not
// own in the gl3 file layout); backend technique from yq2 gl3_surf.c:
//  - The gl1 single-pass multitexture path becomes the si3Dlm/si3DlmFlow
//    programs (4 lightmap units, lmScales from H2 lightstyles, dynamic lights
//    from the uniLights UBO via per-vertex lightFlags).
//  - glpoly_t keeps the gl1 7-float vertex layout (it is the model-data ABI
//    shared with gl1_FindSurface.c); polys are converted to gl3_3D_vtx_t
//    scratch verts at draw time and streamed via GL3_BufferAndDraw3D()
//    (equivalent CPU cost to gl1's immediate-mode emission).
//
// Copyright 1998 Raven Software
//

#include "gl3_World_internal.h"
#include "Angles.h"
#include "Vector.h"

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

// Defined in gl1_Main.c in gl1; owned by the world module in gl3.
extern model_t* r_worldmodel; // Owned by gl3_Main.c.
extern cplane_t frustum[4]; // Owned by gl3_Main.c.

extern int r_viewcluster; // Owned by gl3_Main.c.
extern int r_viewcluster2; // Owned by gl3_Main.c.
extern int r_oldviewcluster; // Owned by gl3_Main.c.
extern int r_oldviewcluster2; // Owned by gl3_Main.c.

static int r_visframecount; // Bumped when going to a new PVS // Q2: defined in gl_rmain.c //mxd. Moved here & made static.
static qboolean multitexture_mode; // H2 (gl3: "multitexture" means the single-pass lightmapped programs; name kept for gl1 parity).

static vec3_t modelorg; // Relative to viewpoint.

static msurface_t* r_alpha_surfaces;

// gl3: replaces gl1's ambient glColor4f(1.0f, 1.0f, 1.0f, 0.25f) set by R_DrawInlineBModel() for RF_TRANS_ANY brush models.
static float r_bmodel_alpha = 1.0f;

#pragma region ========================== BACKEND HELPERS ==========================

#define MAX_POLY_VERTS	64 // R_SubdividePolygon() emits at most 62 verts; world faces stay well below.

static gl3_3D_vtx_t poly_vtx[MAX_POLY_VERTS];
static gl3_alias_vtx_t poly_color_vtx[MAX_POLY_VERTS];

// 1x1 white texture for untextured color-poly draws (gl_drawflat, H2 tallwalls,
// gl_lightmap debug mode, gl_flashblend coronas). Lazily (re-)created: the name
// dies with the GL context on vid_restart, which glIsTexture() detects.
GLuint GL3_WhiteTexture(void)
{
	static GLuint white_texture;

	if (white_texture == 0 || !glIsTexture(white_texture))
	{
		static const byte pixel[4] = { 255, 255, 255, 255 };

		glGenTextures(1, &white_texture);

		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, white_texture);
		gl3state.currenttexture = white_texture;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	}

	return white_texture;
}

// YQ2. Uploads the 4 lightstyle scales to the given lightmapped program when they changed.
// The program must be in use (GL3_UseProgram()).
static void UpdateLMscales(const hmm_vec4 lmScales[MAX_LIGHTMAPS_PER_SURFACE], gl3ShaderInfo_t* si)
{
	qboolean has_changed = false;

	for (int i = 0; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
	{
		if (has_changed)
		{
			si->lmScales[i] = lmScales[i];
		}
		else if (si->lmScales[i].R != lmScales[i].R || si->lmScales[i].G != lmScales[i].G ||
				 si->lmScales[i].B != lmScales[i].B || si->lmScales[i].A != lmScales[i].A)
		{
			si->lmScales[i] = lmScales[i];
			has_changed = true;
		}
	}

	if (has_changed)
		glUniform4fv(si->uniLmScalesOrTime, MAX_LIGHTMAPS_PER_SURFACE, si->lmScales[0].Elements);
}

// H2 lightstyles -> lmScales: gl1 rebuilt the lightmap texels on lightstyle change
// (R_BuildLightMap() with scale = gl_modulate * lightstyle.rgb); gl3 scales the 4 raw
// style sub-lightmaps in the fragment shader instead (yq2), so gl_modulate is baked
// into the scales here.
static void R_SetSurfaceLmScales(const msurface_t* surf, gl3ShaderInfo_t* si)
{
	hmm_vec4 lmScales[MAX_LIGHTMAPS_PER_SURFACE] = { 0 };
	lmScales[0] = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f); // Fullbright fallback for surfaces without light data (styles[0] == 255).

	for (int map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
	{
		lmScales[map].R = r_newrefdef.lightstyles[surf->styles[map]].rgb[0] * gl_modulate->value;
		lmScales[map].G = r_newrefdef.lightstyles[surf->styles[map]].rgb[1] * gl_modulate->value;
		lmScales[map].B = r_newrefdef.lightstyles[surf->styles[map]].rgb[2] * gl_modulate->value;
		lmScales[map].A = 1.0f;
	}

	GL3_UseProgram(si->shaderProgram);
	UpdateLMscales(lmScales, si);
}

// Converts a gl1-layout poly (float[7]: xyz s1t1 s2t2) to gl3_3D_vtx_t scratch verts.
// Normal comes from the surface plane (yq2), lightFlags from the dlight marking
// (yq2 SetLightFlags(); gl_dynamic gates dynamic lights like gl1's lightmap-update logic did).
static const gl3_3D_vtx_t* R_BuildPolyVerts(const msurface_t* surf, const glpoly_t* p)
{
	if (p->numverts > MAX_POLY_VERTS)
		ri.Sys_Error(ERR_DROP, "R_BuildPolyVerts: too many verts (%i)", p->numverts);

	vec3_t normal;
	VectorCopy(surf->plane->normal, normal);

	if (surf->flags & SURF_PLANEBACK)
		VectorScale(normal, -1.0f, normal); // YQ2: invert, so it's usable for the shader.

	GLuint light_flags = 0;
	if ((int)gl_dynamic->value && surf->dlightframe == r_framecount)
		light_flags = (GLuint)surf->dlightbits;

	gl3_3D_vtx_t* vtx = &poly_vtx[0];
	const float* v = p->verts[0];

	for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE, vtx++)
	{
		VectorCopy(v, vtx->pos);
		vtx->texCoord[0] = v[3];
		vtx->texCoord[1] = v[4];
		vtx->lmTexCoord[0] = v[5];
		vtx->lmTexCoord[1] = v[6];
		VectorCopy(normal, vtx->normal);
		vtx->lightFlags = light_flags;
	}

	return poly_vtx;
}

// Q2 counterpart. Draws fa->polys with the currently selected program / uni3D state.
static void R_DrawGLPoly(const msurface_t* fa)
{
	GL3_BindVAO(gl3state.vao3D);
	GL3_BindVBO(gl3state.vbo3D);

	GL3_BufferAndDraw3D(R_BuildPolyVerts(fa, fa->polys), fa->polys->numverts, GL_TRIANGLE_FAN);
}

// Draws an untextured constant-color poly (gl1: glDisable(GL_TEXTURE_2D) + glColor + glVertex):
// white texture + per-vertex color through the alias vertex layout.
// Caller selects the program (si3Dsprite) and binds GL3_WhiteTexture().
static void R_DrawColorPoly(const glpoly_t* p, const paletteRGBA_t color)
{
	if (p->numverts > MAX_POLY_VERTS)
		ri.Sys_Error(ERR_DROP, "R_DrawColorPoly: too many verts (%i)", p->numverts);

	gl3_alias_vtx_t* vtx = &poly_color_vtx[0];
	const float* v = p->verts[0];

	for (int i = 0; i < p->numverts; i++, v += VERTEXSIZE, vtx++)
	{
		VectorCopy(v, vtx->pos);
		vtx->texCoord[0] = 0.0f;
		vtx->texCoord[1] = 0.0f;

		for (int c = 0; c < 4; c++)
			vtx->color[c] = (float)color.c_array[c] / 255.0f;
	}

	GL3_BindVAO(gl3state.vaoAlias);
	GL3_BindVBO(gl3state.vboAlias);

	glBufferData(GL_ARRAY_BUFFER, p->numverts * (int)sizeof(gl3_alias_vtx_t), poly_color_vtx, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLE_FAN, 0, p->numverts);
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

//TODO: logic identical to for loop logic in R_DrawEntitiesOnList(). Move to gl_rmain as R_DrawEntity and replace said logic?
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
	if (memcmp(&gl3state.uni3DData.transModelMat4, &gl3_identityMat4, sizeof(hmm_mat4)) != 0)
	{
		gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
		GL3_UpdateUBO3D();
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR

	GL3_BindTexnum(fa->texinfo->image->texnum);
	c_brush_polys += 1;

	float alpha;
	if (fa->texinfo->flags & SURF_TRANS33)
		alpha = gl_trans33->value;
	else if (fa->texinfo->flags & SURF_TRANS66)
		alpha = gl_trans66->value;
	else
		alpha = 1.0f;

	// gl1: glColor4f(inverse_intensity x3, alpha) + GL_MODULATE (inverse_intensity == 1.0 in gl3 - intensity is fixed at 1.0)
	// + glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.05f).
	if (fa->flags & SURF_DRAWTURB)
	{
		GL3_UpdateSurfState(gl3state.uni3DData.scroll, alpha, 0.05f); // R_EmitWaterPolys() sets the scroll itself.
		R_EmitWaterPolys(fa, fa->flags & SURF_UNDULATE);
	}
	else
	{
		GL3_UpdateSurfState(0.0f, alpha, 0.05f);
		GL3_UseProgram(gl3state.si3Dtrans.shaderProgram);
		R_DrawGLPoly(fa);
	}

	glDisable(GL_BLEND);
	GL3_UpdateSurfState(0.0f, 1.0f, -1.0f); // gl1: R_AlphaFunc(GL_GREATER, 0.666f); the gl3 ambient 3D state is "alpha test off".
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

// This routine takes all the given lightmapped surfaces in the world and blends them into the framebuffer.
// gl3: gl1's dynamic-lightmap rebuild machinery is gone (lightstyles are shader-side lmScales, dynamic
// lights live in the uniLights UBO); what remains is the H2 tallwall tint pass (always) and the
// static-lightmap multiply pass over the lightmapchains (populated by the gl_drawflat / fallback paths).
static void R_BlendLightmaps(const model_t* mdl) //mxd. Original logic uses 'currentmodel' global var.
{
	// Don't bother if we're set to fullbright.
	if ((int)r_fullbright->value || r_worldmodel->lightdata == NULL)
		return;

	// Don't bother writing Z.
	glDepthMask(GL_FALSE);

	// Set the appropriate blending mode unless we're only looking at the lightmaps.
	if (!(int)gl_lightmap->value)
	{
		glEnable(GL_BLEND);

		if ((int)gl_saturatelighting->value)
			glBlendFunc(GL_ONE, GL_ONE);
		else
			glBlendFunc(GL_ZERO, GL_SRC_COLOR); //mxd. Skipping gl_monolightmap logic
	}

	if (mdl == r_worldmodel)
		c_visible_lightmaps = 0;

	// H2: fog values scaled by r_fog_lightmap_adjust for this pass (gl1 glFogf() with adjusted
	// start/end/density). gl3: scale the uni3D fog block instead, and neutralize fogLightmapAdjust
	// meanwhile so the lightmap term in si3Dlm isn't adjusted twice.
	gl3Uni3D_t* u3d = &gl3state.uni3DData;
	const qboolean render_fog = (u3d->fogMode >= 0);

	const float fog_start = u3d->fogStart;
	const float fog_end = u3d->fogEnd;
	const float fog_density = u3d->fogDensity;
	const float fog_lm_adjust = u3d->fogLightmapAdjust;

	if (render_fog)
	{
		u3d->fogStart *= fog_lm_adjust;
		u3d->fogEnd *= fog_lm_adjust;
		u3d->fogDensity *= fog_lm_adjust;
		u3d->fogLightmapAdjust = 1.0f;

		GL3_UpdateUBO3D();
	}

	// H2: draw tallwalls (untextured polys tinted by the surface's lightstyle bytes,
	// multiplied into the framebuffer).
	if (gl_lms.tallwall_lightmaptexturenum > 0)
	{
		GL3_UpdateSurfState(0.0f, 1.0f, -1.0f);
		GL3_UseProgram(gl3state.si3Dsprite.shaderProgram);
		GL3_BindTexnum(GL3_WhiteTexture());

		for (int i = 0; i < gl_lms.tallwall_lightmaptexturenum; i++)
		{
			// gl1 iterates 'tallwall_lightmap_surfaces[0] + i' (msurface_t pointer arithmetic on the
			// FIRST array entry) - only correct for contiguous surfaces; using [i] here (the obvious intent).
			const msurface_t* surf = gl_lms.tallwall_lightmap_surfaces[i];

			paletteRGBA_t color;
			color.r = surf->styles[0];
			color.g = surf->styles[1];
			color.b = surf->styles[2];
			color.a = surf->styles[3];

			R_DrawColorPoly(surf->polys, color);
		}
	}

	// Render static lightmaps (gl3: si3Dlm with a white base texture outputs just the styled,
	// dynamically lit lightmap term - equivalent to gl1's untextured lightmap blend pass,
	// with lightstyles and dynamic lights handled by the shader).
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (gl_lms.lightmap_surfaces[i] == NULL)
			continue;

		if (mdl == r_worldmodel)
			c_visible_lightmaps++;

		GL3_UpdateSurfState(0.0f, 1.0f, -1.0f);
		GL3_BindTexnum(GL3_WhiteTexture());
		GL3_BindLightmap(i);

		for (const msurface_t* surf = gl_lms.lightmap_surfaces[i]; surf != NULL; surf = surf->lightmapchain)
		{
			if (surf->polys == NULL)
				continue;

			R_SetSurfaceLmScales(surf, &gl3state.si3Dlm);

			for (const glpoly_t* p = surf->polys; p != NULL; p = p->chain)
			{
				GL3_BindVAO(gl3state.vao3D);
				GL3_BindVBO(gl3state.vbo3D);
				GL3_BufferAndDraw3D(R_BuildPolyVerts(surf, p), p->numverts, GL_TRIANGLE_FAN);
			}
		}
	}

	// H2: restore fog values.
	if (render_fog)
	{
		u3d->fogStart = fog_start;
		u3d->fogEnd = fog_end;
		u3d->fogDensity = fog_density;
		u3d->fogLightmapAdjust = fog_lm_adjust;

		GL3_UpdateUBO3D();
	}

	// Restore state.
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR.
	glDepthMask(GL_TRUE);
}

//mxd. Similar to Q2's GL_RenderLightmappedPoly. gl1-H2 note: "missing SURF_FLOWING logic" - restored
// here via the si3DlmFlow program (yq2 technique; original H2 lost Q2's scrolling on lit surfaces).
static void R_RenderLightmappedPoly(const entity_t* ent, msurface_t* surf) //mxd. Added 'ent' arg.
{
	c_brush_polys++;

	// gl1's lightstyle-change / dynamic-light glTexSubImage2D lightmap updates are gone:
	// lightstyles scale the 4 style sub-lightmaps via lmScales, dynamic lights are applied
	// per-fragment from the uniLights UBO (yq2 model).

	// gl_lightmap: show only the lightmap term (gl1: GL_TEXTURE1 GL_REPLACE in R_DrawTextureChains()).
	if ((int)gl_lightmap->value)
		GL3_BindTexnum(GL3_WhiteTexture());
	else
		GL3_BindTexnum(R_TextureAnimation(ent, surf->texinfo)->texnum); // H2: GL_MBind -> GL_MBindImage

	GL3_BindLightmap(surf->lightmaptexturenum);

	if (surf->texinfo->flags & SURF_FLOWING)
	{
		float scroll = -64.0f * ((r_newrefdef.time / 40.0f) - floorf(r_newrefdef.time / 40.0f)); // YQ2
		if (scroll == 0.0f)
			scroll = -64.0f;

		GL3_UpdateSurfState(scroll, 1.0f, -1.0f);
		R_SetSurfaceLmScales(surf, &gl3state.si3DlmFlow);
	}
	else
	{
		GL3_UpdateSurfState(0.0f, 1.0f, -1.0f);
		R_SetSurfaceLmScales(surf, &gl3state.si3Dlm);
	}

	for (const glpoly_t* p = surf->polys; p != NULL; p = p->chain)
	{
		GL3_BindVAO(gl3state.vao3D);
		GL3_BindVBO(gl3state.vbo3D);
		GL3_BufferAndDraw3D(R_BuildPolyVerts(surf, p), p->numverts, GL_TRIANGLE_FAN);
	}
}

static void R_RenderBrushPoly(const entity_t* ent, msurface_t* fa) //mxd. Added 'ent' arg.
{
	c_brush_polys++;

	GL3_BindTexnum(R_TextureAnimation(ent, fa->texinfo)->texnum); // Q2: GL_Bind().

	// gl1 relied on the ambient glColor alpha (1.0, or 0.25 for RF_TRANS_ANY bmodels) here;
	// set the equivalent uni3D state explicitly so the underwater / quake paths are deterministic.
	GL3_UpdateSurfState(gl3state.uni3DData.scroll, r_bmodel_alpha, -1.0f);

	// H2: new cl_camera_under_surface logic.
	if ((int)cl_camera_under_surface->value)
	{
		R_EmitUnderwaterPolys(fa);
		return;
	}

	// H2: new quake_amount logic.
	if ((int)quake_amount->value)
	{
		R_EmitQuakeFloorPolys(fa);
		return;
	}

	if (fa->flags & SURF_DRAWTURB)
	{
		// Warp texture, no lightmaps.
		// gl1: GL_MODULATE + glColor4f(inverse_intensity x3, 1.0f); inverse_intensity == 1.0 in gl3.
		GL3_UpdateSurfState(gl3state.uni3DData.scroll, 1.0f, -1.0f); // R_EmitWaterPolys() sets the scroll itself.
		R_EmitWaterPolys(fa, fa->flags & SURF_UNDULATE);

		return;
	}

	// Textured pass without lightmap (gl1: GL_REPLACE + R_DrawGLPoly()).
	// H2: missing SURF_FLOWING flag logic.
	// r_bmodel_alpha is 0.25 for RF_TRANS_ANY brush models (blend enabled by R_DrawInlineBModel()), 1.0 otherwise.
	GL3_UpdateSurfState(0.0f, r_bmodel_alpha, -1.0f);
	GL3_UseProgram(gl3state.si3Dtrans.shaderProgram);
	R_DrawGLPoly(fa);

	// gl3: gl1's "check for lightmap modification" / dynamic-lightmap branches collapse -
	// lightstyles and dynamic lights are shader-side. Only the blend-pass chains remain.

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

	GL3_UpdateSurfState(0.0f, 1.0f, -1.0f);
	GL3_UseProgram(gl3state.si3Dsprite.shaderProgram);
	GL3_BindTexnum(GL3_WhiteTexture());
	R_DrawColorPoly(fa->polys, color);

	// Done when gl_drawflat == 1.
	if ((int)gl_drawflat->value == 1)
		return;

	// Chain lightmaps (gl_drawflat >= 2) for the R_BlendLightmaps() multiply pass.
	// gl3: the dynamic-lightmap update branches of gl1 collapse (see R_RenderBrushPoly()).
	if (!(fa->texinfo->flags & SURF_TALL_WALL))
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}

static void R_DrawTextureChains(const entity_t* ent) // Q2: DrawTextureChains(). //mxd. Added 'ent' arg.
{
	c_visible_textures = 0;

	if (gl_zfix->value) // YQ2
		glEnable(GL_POLYGON_OFFSET_FILL);

	// YQ2: alpha-to-coverage for alpha-tested world surfaces (gl1 also set R_AlphaFunc(GL_GREATER, 0.25f),
	// but GL_ALPHA_TEST is disabled during the 3D world pass - A2C works off the fragment alpha directly).
	if (r_msaa_samples->value > 0)
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);

	// H2: extra gl_sortmulti logic (gl3: the single-pass lightmapped programs replace gl1's
	// GL_TEXTURE0 GL_REPLACE x GL_TEXTURE1 GL_MODULATE multitexture setup):
	if (multitexture_mode)
	{
		image_t* image = &gltextures[0];
		for (int i = 0; i < numgltextures; i++, image++)
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
	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		if (!image->registration_sequence || image->texturechain == NULL)
			continue;

		c_visible_textures++;

		for (msurface_t* s = image->texturechain; s != NULL; s = s->texturechain)
			if (!(s->flags & SURF_DRAWTURB))
				render_brush_poly(ent, s); // H2: new gl_drawflat logic.
	}

	if (r_msaa_samples->value > 0)
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

	// Render warping (water) surfaces (no lightmaps).
	image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		if (!image->registration_sequence || image->texturechain == NULL)
			continue;

		for (msurface_t* s = image->texturechain; s != NULL; s = s->texturechain)
			if (s->flags & SURF_DRAWTURB)
				render_brush_poly(ent, s); // H2: new gl_drawflat logic.

		image->texturechain = NULL;
	}

	if (gl_zfix->value) // YQ2
		glDisable(GL_POLYGON_OFFSET_FILL);
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

// gl1 R_RotateForEntity() (gl1_Misc.c) as a matrix (yq2 GL3_RotateForEntity technique):
// translate + H2 RAD_TO_ANGLE-scaled rotations, composed onto the current model matrix.
static void R_RotateForEntity(const entity_t* e)
{
	hmm_mat4 mat = HMM_Translate(HMM_Vec3(e->origin[0], e->origin[1], e->origin[2]));

	mat = HMM_MultiplyMat4(mat, HMM_Rotate(e->angles[1] * RAD_TO_ANGLE, HMM_Vec3(0.0f, 0.0f, 1.0f)));
	mat = HMM_MultiplyMat4(mat, HMM_Rotate(-e->angles[0] * RAD_TO_ANGLE, HMM_Vec3(0.0f, 1.0f, 0.0f)));
	mat = HMM_MultiplyMat4(mat, HMM_Rotate(-e->angles[2] * RAD_TO_ANGLE, HMM_Vec3(1.0f, 0.0f, 0.0f)));

	gl3state.uni3DData.transModelMat4 = HMM_MultiplyMat4(gl3state.uni3DData.transModelMat4, mat);
	GL3_UpdateUBO3D();
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
	// gl3: gl1 kept the lightmapped multitexture path with glColor4f(1,1,1,0.25) + GL_MODULATE;
	// the si3Dlm program has no constant alpha, so translucent bmodels go through the trans
	// program (alpha 0.25, no lightmap) instead.
	const qboolean trans_ent = (ent->flags & RF_TRANS_ANY) != 0;
	if (trans_ent)
	{
		glEnable(GL_BLEND);
		r_bmodel_alpha = 0.25f;
	}

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
	{
		glDisable(GL_BLEND);
		r_bmodel_alpha = 1.0f;
	}
}

void R_DrawBrushModel(entity_t* ent)
{
	const model_t* mdl = *ent->model; //mxd. Original logic uses 'currentmodel' global var instead.

	if (mdl->nummodelsurfaces == 0)
		return;

	// H2: missing: currententity = ent;
	// gl1 reset gl_state.currenttextures[] here (multitexture unit cache invalidation) -
	// the gl3state texture cache stays valid across draws, nothing to reset.

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
	const hmm_mat4 old_model_mat = gl3state.uni3DData.transModelMat4;

	ent->angles[0] *= -1.0f; // stupid quake bug.
	ent->angles[2] *= -1.0f; // stupid quake bug.
	R_RotateForEntity(ent);
	ent->angles[0] *= -1.0f; // stupid quake bug.
	ent->angles[2] *= -1.0f; // stupid quake bug.

	// gl1's R_EnableMultitexture(true) + REPLACE/MODULATE texenv setup is implicit in the si3Dlm program.
	R_DrawInlineBModel(ent);

	//mxd. Skip H2 gl_drawmode logic.
	gl3state.uni3DData.transModelMat4 = old_model_mat;
	GL3_UpdateUBO3D();
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

	memset((void*)gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));
	gl_lms.tallwall_lightmaptexturenum = 0; // H2
	multitexture_mode = false; // H2

	R_ClearSkyBox();

	// H2: new r_fullbright and gl_drawflat cvar checks (gl1 wrapped R_RecursiveWorldNode() in
	// R_EnableMultitexture(true) + texenv setup here; implicit in the gl3 programs).
	R_RecursiveWorldNode(&ent, r_worldmodel->nodes);

	// Theoretically nothing should happen in the next two functions if multitexture is enabled.

	// H2: new gl_drawflat cvar logic (gl1: glDisable(GL_TEXTURE_2D) around the call -
	// the gl3 flat-shaded path uses the white texture instead).
	R_DrawTextureChains(&ent);

	R_BlendLightmaps(r_worldmodel);

	//mxd. Skip H2 gl_drawmode logic.
	R_DrawSkyBox();

	// gl1: R_DrawTriangleOutlines() (gl_showtris) - unused in gl3.
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

#pragma region ========================== FRAME SETUP (from gl1_Main.c) ==========================

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
// before R_MarkLeaves(). The rest of R_SetupFrame() lives in gl3_Main.c.
void GL3_SetViewClusters(void)
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
