#include "compat.h"
//
// gl3_Sprite.c -- sprite rendering (gl1_Sprite.c port for the OpenGL 3.2 core renderer).
//
// gl1's immediate-mode quads/polygons become streamed gl3_alias_vtx_t fans drawn
// with the si3Dsprite program; the current gl1 glColor state (set by
// R_HandleTransparency()) is baked into the per-vertex colors.
//
// Copyright 1998 Raven Software
//

#include "gl3_Entity_internal.h"
#include "q_Sprite.h"
#include "Vector.h"

//mxd. Fills a single alias-layout vertex with the current gl1-style draw color.
static void SetSpriteVert(gl3_alias_vtx_t* v, const vec3_t point, const float s, const float t)
{
	VectorCopy(point, v->pos);

	v->texCoord[0] = s;
	v->texCoord[1] = t;

	for (int i = 0; i < 4; i++)
		v->color[i] = gl3_currentDrawColor[i];
}

// Standard square sprite.
static void R_DrawStandardSprite(const entity_t* e, const dsprframe_t* frame, const vec3_t up, const vec3_t right)
{
	gl3_alias_vtx_t verts[4];
	vec3_t point;

	const float xl = (float)-frame->origin_x * e->scale;
	const float xr = (float)(frame->width - frame->origin_x) * e->scale;
	const float yt = (float)-frame->origin_y * e->scale;
	const float yb = (float)(frame->height - frame->origin_y) * e->scale;

	// glBegin(GL_QUADS) -> 4-vertex triangle fan.

	VectorMA(e->origin, yt, up, point);
	VectorMA(point, xl, right, point);
	SetSpriteVert(&verts[0], point, 0.0f, 1.0f);

	VectorMA(e->origin, yb, up, point);
	VectorMA(point, xl, right, point);
	SetSpriteVert(&verts[1], point, 0.0f, 0.0f);

	VectorMA(e->origin, yb, up, point);
	VectorMA(point, xr, right, point);
	SetSpriteVert(&verts[2], point, 1.0f, 0.0f);

	VectorMA(e->origin, yt, up, point);
	VectorMA(point, xr, right, point);
	SetSpriteVert(&verts[3], point, 1.0f, 1.0f);

	GL3_DrawAliasVerts(gl3state.si3Dsprite.shaderProgram, verts, 4, GL_TRIANGLE_FAN);
}

// Sprite with 4 variable verts(x, y scale and s, t); texture must be square.
static void R_DrawDynamicSprite(const entity_t* e, const vec3_t up, const vec3_t right)
{
	gl3_alias_vtx_t verts[4];
	vec3_t point;

	// glBegin(GL_QUADS) -> 4-vertex triangle fan.

	for (int i = 0; i < 4; i++)
	{
		VectorMA(e->origin, e->scale * e->verts[i].y, up, point);
		VectorMA(point, e->scale * e->verts[i].x, right, point);
		SetSpriteVert(&verts[i], point, e->verts[i].s, e->verts[i].t);
	}

	GL3_DrawAliasVerts(gl3state.si3Dsprite.shaderProgram, verts, 4, GL_TRIANGLE_FAN);
}

// Sprite with n variable verts(x, y scale and s, t); texture must be square. //TODO: seems unused, so can't test if it works correctly...
static void R_DrawVariableSprite(const entity_t* e, const vec3_t up, const vec3_t right)
{
	if (e->numVerts < 3) //mxd. Added sanity check (gl1 GL_POLYGON with < 3 verts draws nothing anyway).
		return;

	gl3_alias_vtx_t verts[e->numVerts]; // glBegin(GL_POLYGON) -> triangle fan (convex polygons only, same as GL_POLYGON rasterization).
	vec3_t point;

	const svertex_t* v = &e->verts_p[0];
	for (int i = 0; i < e->numVerts; i++, v++)
	{
		VectorMA(e->origin, e->scale * v->y, up, point);
		VectorMA(point, e->scale * v->x, right, point);
		SetSpriteVert(&verts[i], point, v->s, v->t);
	}

	GL3_DrawAliasVerts(gl3state.si3Dsprite.shaderProgram, verts, e->numVerts, GL_TRIANGLE_FAN);
}

// Long linear semi-oriented sprite with two verts (xyz start and end) and a width.
static void R_DrawLineSprite(const entity_t* e, const vec3_t up)
{
	gl3_alias_vtx_t verts[4];

	vec3_t diff;
	VectorSubtract(e->endpos, e->startpos, diff);

	vec3_t dir;
	CrossProduct(diff, up, dir);
	VectorNormalize(dir);

	vec3_t start_offset;
	VectorScale(dir, e->scale * 0.5f, start_offset);

	vec3_t end_offset;
	VectorScale(dir, e->scale2 * 0.5f, end_offset);

	const float tile = (e->tile > 0.0f ? e->tile : 1.0f);

	vec3_t point;

	// glBegin(GL_QUADS) -> 4-vertex triangle fan.

	VectorSubtract(e->startpos, start_offset, point);
	SetSpriteVert(&verts[0], point, 0.0f, e->tileoffset);

	VectorAdd(e->startpos, start_offset, point);
	SetSpriteVert(&verts[1], point, 1.0f, e->tileoffset);

	VectorAdd(e->endpos, end_offset, point);
	SetSpriteVert(&verts[2], point, 1.0f, e->tileoffset + tile);

	VectorSubtract(e->endpos, end_offset, point);
	SetSpriteVert(&verts[3], point, 0.0f, e->tileoffset + tile);

	GL3_DrawAliasVerts(gl3state.si3Dsprite.shaderProgram, verts, 4, GL_TRIANGLE_FAN);
}

void R_DrawSpriteModel(entity_t* e)
{
	const model_t* mdl = *e->model; //mxd. Original logic uses 'currentmodel' global var instead.

	// Don't even bother culling, because it's just a single polygon without a surface cache.
	// NOTE: sprite verts are emitted in world space - relies on uni3D transModelMat4 being
	// identity between entities (same invariant as gl1's world modelview matrix).
	const dsprite_t* psprite = mdl->extradata;

	//mxd. #if 0-ed in Q2
	if (e->frame < 0 || e->frame >= psprite->numframes)
	{
		ri.Con_Printf(PRINT_DEVELOPER, "R_DrawSpriteModel: sprite '%s' with invalid frame %i!\n", mdl->name, e->frame);
		e->frame = 0;
	}

	e->frame %= psprite->numframes;

	if (mdl->skins[e->frame] == NULL) //mxd. Moved below e->frame sanity checks.
		return;

	// All-new logic from here and down!!!
	// glShadeModel(GL_SMOOTH) + GL_MODULATE: inherent in the sprite shader (texture x per-vertex color).

	R_HandleTransparency(e);
	GL3_BindImage(mdl->skins[e->frame]);

	if (e->flags & RF_NODEPTHTEST)
		glDisable(GL_DEPTH_TEST);

	vec3_t up;
	vec3_t right;

	if (e->flags & RF_FIXED)
	{
		vec3_t dir;
		DirAndUpFromAngles(e->angles, dir, up); //mxd. Original logic uses 'currententity' global var instead.

		CrossProduct(up, dir, right);
		VectorNormalize(right);
	}
	else
	{
		VectorCopy(vup, up);
		VectorCopy(vright, right);
	}

	switch (e->spriteType)
	{
		case SPRITE_EDICT:
		case SPRITE_STANDARD:
			R_DrawStandardSprite(e, &psprite->frames[e->frame], up, right);
			break;

		case SPRITE_DYNAMIC:
			R_DrawDynamicSprite(e, up, right);
			break;

		case SPRITE_VARIABLE:
			R_DrawVariableSprite(e, up, right);
			break;

		case SPRITE_LINE:
			R_DrawLineSprite(e, vpn);
			break;

		default: //mxd. Avoid compiler warnings...
			ri.Sys_Error(ERR_DROP, "R_DrawSpriteModel: unknown sprite type (%i)!", e->spriteType); //mxd. Sys_Error() -> ri.Sys_Error().
			break;
	}

	if (e->flags & RF_NODEPTHTEST)
		glEnable(GL_DEPTH_TEST);

	R_CleanupTransparency(e);

	// GL_REPLACE + glShadeModel(GL_FLAT): no-ops in gl3 (shader-determined).
	GL3_SetCurrentColor(1.0f, 1.0f, 1.0f, 1.0f); // glColor4f(1, 1, 1, 1).
}
