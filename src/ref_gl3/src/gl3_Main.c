#include "compat.h"
//
// gl3_Main.c -- refresher setup and main frame flow for the OpenGL 3.2 core renderer.
//
// Cvar registration, mode setting, context bring-up, streaming buffers,
// UBO/frame plumbing, the full H2R refexport_t and the complete H2 frame flow
// (RI_BeginFrame / RI_RenderFrame: fog-aware clear, frustum/view setup, world +
// entity + particle passes, screen flash handshake, r_lightlevel hack).
// World / entity / image / 2D drawing bodies live in the module ports
// (gl3_Image.c, gl3_Draw.c, gl3_Model.c, gl3_Surface.c, ...).
//
// Mirrors gl1_Main.c semantics on the yq2 gl3_main.c backend architecture.
//
// Copyright 1998 Raven Software
//

#define HANDMADE_MATH_IMPLEMENTATION // Must be defined before gl3_Local.h pulls in HandmadeMath.h.
#include "gl3_Local.h"

#define DG_DYNARR_IMPLEMENTATION
#include "DG_dynarr.h"

// Renderer-agnostic H2 model/BSP structs, shared with the gl1 sources compiled
// into ref_gl3 (gl1_FindSurface.c walks the same world structures - CONTRACT.md
// rule 4). Must be included after gl3_Local.h (needs image_t).
#include "gl1_Model.h"

#include "ParticleFlags.h" //mxd
#include "Vector.h"

#include <stddef.h> // offsetof

#ifdef _WIN32
#define REF_DECLSPEC	__declspec(dllexport)
#else
#define REF_DECLSPEC	__attribute__((visibility("default")))
#endif

extern viddef_t viddef; // Defined in cl_globals.c (engine global, resolved at dlopen).
refimport_t ri;

model_t* r_worldmodel; // Assigned by RI_BeginRegistration() (gl3_Model.c module port).

gl3config_t gl3config;
gl3state_t gl3state;

float gldepthmin;
float gldepthmax;

// View origin.
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

// gl1 parity: view/projection matrices as GL-style float[16] for R_PointToScreen()
// (gl3_Misc.c module port). hmm_mat4 is column-major like GL, so these are plain
// copies of gl3state.viewMat3D / gl3state.projMat3D made in R_SetupGL3D().
float r_world_matrix[16];
float r_projection_matrix[16]; //mxd
cplane_t frustum[4];

refdef_t r_newrefdef; // Screen size info.

int r_framecount; // Used for dlight push checking.

int r_viewcluster;
int r_viewcluster2;
int r_oldviewcluster;
int r_oldviewcluster2;

int c_brush_polys;
int c_alias_polys;

static float v_blend[4]; // Final blending color. //mxd. Made static.

const hmm_mat4 gl3_identityMat4 = {{
	{ 1, 0, 0, 0 },
	{ 0, 1, 0, 0 },
	{ 0, 0, 1, 0 },
	{ 0, 0, 0, 1 },
}};

#pragma region ========================== CVARS  ==========================

// Same registration set as gl1_Main.c R_Register() so menus and configs keep
// working. GL1-only toggles that have no GL3 meaning are registered but
// ignored (marked "unused in gl3").

cvar_t* r_norefresh;
cvar_t* r_fullbright;
cvar_t* r_drawentities;
cvar_t* r_drawworld;
cvar_t* r_novis;
cvar_t* r_nocull;
cvar_t* r_lerpmodels;
static cvar_t* r_speeds;
cvar_t* r_vsync; // YQ2
cvar_t* r_anisotropic; // YQ2
cvar_t* r_msaa_samples; // YQ2

cvar_t* r_lightlevel; // FIXME: This is a HACK to get the client's light level

extern cvar_t* r_farclipdist;
extern cvar_t* r_fog;
cvar_t* r_fog_mode;
extern cvar_t* r_fog_density;
cvar_t* r_fog_startdist;
static cvar_t* r_fog_color_r;
static cvar_t* r_fog_color_g;
static cvar_t* r_fog_color_b;
static cvar_t* r_fog_color_a;
cvar_t* r_fog_lightmap_adjust;
cvar_t* r_fog_underwater; // gl1 parity: defined but never registered/used.
cvar_t* r_fog_underwater_lightmap_adjust; // gl3: extern'ed in gl3_Local.h (gl1 declared it in gl1_Local.h but never defined/registered it).
static cvar_t* r_fog_underwater_mode;
static cvar_t* r_fog_underwater_density;
static cvar_t* r_fog_underwater_startdist;
static cvar_t* r_fog_underwater_color_r;
static cvar_t* r_fog_underwater_color_g;
static cvar_t* r_fog_underwater_color_b;
static cvar_t* r_fog_underwater_color_a;
static cvar_t* r_underwater_color;
cvar_t* r_frameswap;
cvar_t* r_references;

cvar_t* gl_noartifacts;

cvar_t* gl_modulate;
cvar_t* gl_lightmap;
cvar_t* gl_dynamic;
cvar_t* gl_nobind; // unused in gl3
cvar_t* gl_showtris; // unused in gl3
static cvar_t* gl_reporthash; // unused in gl3
static cvar_t* gl_ztrick; // unused in gl3
cvar_t* gl_zfix; // YQ2
static cvar_t* gl_finish;
static cvar_t* gl_clear;
static cvar_t* gl_cull;
static cvar_t* gl_polyblend;
cvar_t* gl_flashblend;
cvar_t* gl_texturemode;
cvar_t* gl_lockpvs;
cvar_t* gl_minlight; // YQ2

cvar_t* gl_drawflat;
cvar_t* gl_trans33;
cvar_t* gl_trans66;
cvar_t* gl_bookalpha;

cvar_t* gl_drawbuffer;
cvar_t* gl_saturatelighting;

static cvar_t* gl3_usebigvbo; // YQ2 gl3: workaround for AMD's proprietary driver (see GL3_BufferAndDraw3D()).

extern cvar_t* vid_gamma;
extern cvar_t* vid_brightness;
extern cvar_t* vid_contrast;
static cvar_t* vid_textures_refresh_required; //mxd. No-op in gl3: gamma is shader-side, no texture refresh needed.

extern cvar_t* vid_ref;

extern cvar_t* vid_mode; // gl_mode in Q2
extern cvar_t* menus_active;
extern cvar_t* cl_camera_under_surface;
extern cvar_t* quake_amount;

#pragma endregion

#pragma region ========================== CROSS-MODULE PROTOTYPES (not in gl3_Local.h yet) ==========================

// These mirror the gl1_*.h headers 1:1; the corresponding gl3 module ports
// provide the definitions (ported functions keep their gl1 names - CONTRACT.md).
// ref_gl3.so does NOT link until those module ports land.
// TODO(integration): move these into gl3_Local.h once all module ports landed.

// gl3_Light.c (gl1_Light.h):
extern void R_RenderDlights(void);
extern void R_PushDlights(void);
extern void R_ResetBmodelTransforms(void); //mxd
extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels); //mxd. +check_bmodels arg.

// gl3_Surface.c (gl1_Surface.h):
extern int c_visible_lightmaps;
extern int c_visible_textures;
extern void R_SortAndDrawAlphaSurfaces(void);
extern void R_DrawBrushModel(entity_t* ent);
extern void R_DrawWorld(void);
extern void R_MarkLeaves(void);

// gl3_Sprite.c (gl1_Sprite.h):
extern void R_DrawSpriteModel(entity_t* e);

// gl3_FlexModel.c (gl1_FlexModel.h):
extern void R_DrawFlexModel(entity_t* e);

// gl3_Misc.c (gl1_Misc.h):
extern void R_DrawNullModel(const entity_t* e);
extern paletteRGBA_t R_ModulateRGBA(paletteRGBA_t a, paletteRGBA_t b); //mxd
extern paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, byte alpha); //mxd

#pragma endregion

#pragma region ========================== STREAMING BUFFERS ==========================

static void GL3_InitBuffers(void) // YQ2: GL3_SurfInit() (gl3_surf.c).
{
	// Init the VAO and VBO for the standard 3D vertexdata: 10 floats and 1 uint
	// (X, Y, Z), (S, T), (LMS, LMT), (normX, normY, normZ) ; lightFlags - last two groups for lightmap/dynlights.

	glGenVertexArrays(1, &gl3state.vao3D);
	GL3_BindVAO(gl3state.vao3D);

	glGenBuffers(1, &gl3state.vbo3D);
	GL3_BindVBO(gl3state.vbo3D);

	if (gl3config.useBigVBO)
	{
		gl3state.vbo3Dsize = 5 * 1024 * 1024; // YQ2: a 5MB buffer seems to work well.
		gl3state.vbo3DcurOffset = 0;
		glBufferData(GL_ARRAY_BUFFER, gl3state.vbo3Dsize, NULL, GL_STREAM_DRAW); // Allocate/reserve the data.
	}

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, texCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_LMTEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_LMTEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, lmTexCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_NORMAL);
	qglVertexAttribPointer(GL3_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, normal));

	glEnableVertexAttribArray(GL3_ATTRIB_LIGHTFLAGS);
	qglVertexAttribIPointer(GL3_ATTRIB_LIGHTFLAGS, 1, GL_UNSIGNED_INT, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, lightFlags));

	// Init VAO, VBO and EBO for alias-style vertexdata: 9 floats
	// (X, Y, Z), (S, T), (R, G, B, A).
	// Flexmodels, sprites AND particle quads share this layout (CONTRACT.md).

	glGenVertexArrays(1, &gl3state.vaoAlias);
	GL3_BindVAO(gl3state.vaoAlias);

	glGenBuffers(1, &gl3state.vboAlias);
	GL3_BindVBO(gl3state.vboAlias);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 3 * sizeof(GLfloat));

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(GLfloat), 5 * sizeof(GLfloat));

	glGenBuffers(1, &gl3state.eboAlias);
}

static void GL3_ShutdownBuffers(void) // YQ2: GL3_SurfShutdown() (gl3_surf.c).
{
	glDeleteBuffers(1, &gl3state.vbo3D);
	gl3state.vbo3D = 0;
	glDeleteVertexArrays(1, &gl3state.vao3D);
	gl3state.vao3D = 0;

	glDeleteBuffers(1, &gl3state.eboAlias);
	gl3state.eboAlias = 0;
	glDeleteBuffers(1, &gl3state.vboAlias);
	gl3state.vboAlias = 0;
	glDeleteVertexArrays(1, &gl3state.vaoAlias);
	gl3state.vaoAlias = 0;
}

// Assumes gl3state.v[ab]o3D are bound; buffers and draws gl3_3D_vtx_t vertices.
// drawMode is something like GL_TRIANGLE_STRIP or GL_TRIANGLE_FAN or whatever. // YQ2
void GL3_BufferAndDraw3D(const gl3_3D_vtx_t* verts, const int numVerts, const GLenum drawMode)
{
	if (!gl3config.useBigVBO)
	{
		glBufferData(GL_ARRAY_BUFFER, sizeof(gl3_3D_vtx_t) * numVerts, verts, GL_STREAM_DRAW);
		glDrawArrays(drawMode, 0, numVerts);
	}
	else // gl3config.useBigVBO == true
	{
		// YQ2: AMD's Windows/proprietary drivers choke on lots of small glBufferData()
		// calls; instead allocate one big buffer and stream into different regions of it
		// via unsynchronized glMapBufferRange() (fresh buffer when full / at EndFrame).
		int curOffset = gl3state.vbo3DcurOffset;
		const int neededSize = (int)sizeof(gl3_3D_vtx_t) * numVerts;

		if (curOffset + neededSize > gl3state.vbo3Dsize)
		{
			// Buffer is full, need to start again from the beginning
			// => need to sync or get fresh buffer (getting fresh buffer seems easier).
			glBufferData(GL_ARRAY_BUFFER, gl3state.vbo3Dsize, NULL, GL_STREAM_DRAW);
			curOffset = 0;
		}

		// As we make sure to use a previously unused part of the buffer,
		// doing it unsynchronized should be safe.
		const GLbitfield accessBits = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
		void* data = glMapBufferRange(GL_ARRAY_BUFFER, curOffset, neededSize, accessBits);
		memcpy(data, verts, neededSize);
		glUnmapBuffer(GL_ARRAY_BUFFER);

		glDrawArrays(drawMode, curOffset / (int)sizeof(gl3_3D_vtx_t), numVerts);

		gl3state.vbo3DcurOffset = curOffset + neededSize;
	}
}

// Binds the alias VAO/VBO pair and streams gl3_alias_vtx_t vertices
// (the 9-float layout shared by flexmodels/sprites/particle quads - CONTRACT.md).
static void GL3_BufferAndDrawAlias(const gl3_alias_vtx_t* verts, const int numVerts, const GLenum drawMode)
{
	GL3_BindVAO(gl3state.vaoAlias);
	GL3_BindVBO(gl3state.vboAlias);

	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(gl3_alias_vtx_t) * numVerts), verts, GL_STREAM_DRAW);
	glDrawArrays(drawMode, 0, numVerts);
}

#pragma endregion

#pragma region ========================== ENTITIES / PARTICLES ==========================

// Minimal state-cached texture bind for GL_TEXTURE0 (gl1 R_BindImage() equivalent;
// the full image-aware version belongs to the gl3_Image.c module port).
static void GL3_BindTexture(const GLuint texnum)
{
	GL3_SelectTMU(GL_TEXTURE0);

	if (gl3state.currenttexture != texnum)
	{
		gl3state.currenttexture = texnum;
		glBindTexture(GL_TEXTURE_2D, texnum);
	}
}

// H2: simplified: no separate non-transparent/transparent drawing chains.
static void R_DrawEntitiesOnList(void)
{
	if (!(int)r_drawentities->value)
		return;

	for (int i = 0; i < r_newrefdef.num_entities; i++)
	{
		entity_t* ent = r_newrefdef.entities[i]; //mxd. Original logic uses 'currententity' global var.

		if (ent->model == NULL) // H2: extra sanity check.
		{
			ri.Con_Printf(PRINT_ALL, "Attempt to draw NULL model\n"); //mxd. Com_Printf() -> ri.Con_Printf().
			R_DrawNullModel(ent);

			continue;
		}

		const model_t* mdl = *ent->model; //mxd. Original logic uses 'currentmodel' global var.

		if (mdl == NULL)
		{
			R_DrawNullModel(ent);
			continue;
		}

		// H2: no mod_alias case, new mod_bad and mod_fmdl cases.
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
				ri.Sys_Error(ERR_DROP, "Bad modeltype"); // Q2: ri.Sys_Error //mxd. Sys_Error() -> ri.Sys_Error().
				break;
		}
	}
}

static void R_DrawParticles(const int num_particles, const particle_t* particles, const qboolean alpha_particle)
{
	static GLfloat particle_st_coords[NUM_PARTICLE_TYPES][4] =
	{
		{ 0.00390625f, 0.00390625f, 0.02734375f, 0.02734375f },
		{ 0.03515625f, 0.00390625f, 0.05859375f, 0.02734375f },
		{ 0.06640625f, 0.00390625f, 0.08984375f, 0.02734375f },
		{ 0.09765625f, 0.00390625f, 0.12109375f, 0.02734375f },
		{ 0.00390625f, 0.03515625f, 0.02734375f, 0.05859375f },
		{ 0.03515625f, 0.03515625f, 0.05859375f, 0.05859375f },
		{ 0.06640625f, 0.03515625f, 0.08984375f, 0.05859375f },
		{ 0.09765625f, 0.03515625f, 0.12109375f, 0.05859375f },
		{ 0.00390625f, 0.06640625f, 0.02734375f, 0.08984375f },
		{ 0.03515625f, 0.06640625f, 0.05859375f, 0.08984375f },
		{ 0.06640625f, 0.06640625f, 0.08984375f, 0.08984375f },
		{ 0.09765625f, 0.06640625f, 0.12109375f, 0.08984375f },
		{ 0.00390625f, 0.09765625f, 0.02734375f, 0.12109375f },
		{ 0.03515625f, 0.09765625f, 0.05859375f, 0.12109375f },
		{ 0.06640625f, 0.09765625f, 0.08984375f, 0.12109375f },
		{ 0.09765625f, 0.09765625f, 0.12109375f, 0.12109375f },
		{ 0.12890625f, 0.00390625f, 0.18359375f, 0.05859375f },
		{ 0.19140625f, 0.00390625f, 0.24609375f, 0.05859375f },
		{ 0.12890625f, 0.06640625f, 0.18359375f, 0.12109375f },
		{ 0.19140625f, 0.06640625f, 0.24609375f, 0.12109375f },
		{ 0.00390625f, 0.12890625f, 0.12109375f, 0.24609375f },
		{ 0.12890625f, 0.12890625f, 0.24609375f, 0.24609375f },
		{ 0.25390625f, 0.00390625f, 0.37109375f, 0.12109375f },
		{ 0.37890625f, 0.00390625f, 0.49609375f, 0.12109375f },
		{ 0.25390625f, 0.12890625f, 0.37109375f, 0.24609375f },
		{ 0.37890625f, 0.12890625f, 0.49609375f, 0.24609375f },
		{ 0.00390625f, 0.25390625f, 0.24609375f, 0.49609375f },
		{ 0.25390625f, 0.25390625f, 0.49609375f, 0.49609375f },
		{ 0.50390625f, 0.00390625f, 0.74609375f, 0.24609375f },
		{ 0.75390625f, 0.00390625f, 0.99609375f, 0.24609375f },
		{ 0.50390625f, 0.25390625f, 0.74609375f, 0.49609375f },
		{ 0.75390625f, 0.25390625f, 0.87109375f, 0.37109375f },
		{ 0.87890625f, 0.25390625f, 0.99609375f, 0.37109375f },
		{ 0.75390625f, 0.37890625f, 0.87109375f, 0.49609375f },
		{ 0.87890625f, 0.37890625f, 0.99609375f, 0.49609375f },
		{ 0.00390625f, 0.50390625f, 0.24609375f, 0.74609375f },
		{ 0.00390625f, 0.50390625f, 0.24609375f, 0.74609375f },
		{ 0.25390625f, 0.50390625f, 0.37109375f, 0.62109375f },
		{ 0.37890625f, 0.50390625f, 0.43359375f, 0.55859375f },
		{ 0.44140625f, 0.50390625f, 0.49609375f, 0.55859375f },
		{ 0.37890625f, 0.56640625f, 0.43359375f, 0.62109375f },
		{ 0.44140625f, 0.56640625f, 0.49609375f, 0.62109375f },
		{ 0.25390625f, 0.62890625f, 0.30859375f, 0.68359375f },
		{ 0.31640625f, 0.62890625f, 0.37109375f, 0.68359375f },
		{ 0.25390625f, 0.69140625f, 0.30859375f, 0.74609375f },
		{ 0.31640625f, 0.69140625f, 0.37109375f, 0.74609375f },
		{ 0.37890625f, 0.62890625f, 0.43359375f, 0.68359375f },
		{ 0.44140625f, 0.62890625f, 0.49609375f, 0.68359375f },
		{ 0.37890625f, 0.69140625f, 0.43359375f, 0.74609375f },
		{ 0.44140625f, 0.69140625f, 0.49609375f, 0.74609375f },
		{ 0.00390625f, 0.75390625f, 0.24609375f, 0.99609375f },
		{ 0.25390625f, 0.75390625f, 0.49609375f, 0.99609375f },
		{ 0.50390625f, 0.50390625f, 0.62109375f, 0.62109375f },
		{ 0.62890625f, 0.50390625f, 0.74609375f, 0.62109375f },
		{ 0.50390625f, 0.62890625f, 0.62109375f, 0.74609375f },
		{ 0.62890625f, 0.62890625f, 0.74609375f, 0.74609375f },
		{ 0.75390625f, 0.50390625f, 0.99609375f, 0.74609375f },
		{ 0.50390625f, 0.75390625f, 0.74609375f, 0.99609375f },
		{ 0.75390625f, 0.75390625f, 0.87109375f, 0.87109375f },
		{ 0.87890625f, 0.75390625f, 0.99609375f, 0.87109375f },
		{ 0.75390625f, 0.87890625f, 0.87109375f, 0.99609375f },
		{ 0.87890625f, 0.87890625f, 0.99609375f, 0.99609375f }
	};

	// GL_QUADS doesn't exist in GL 3.2 core: emit 6 verts (2 triangles) per particle quad instead.
	static gl3_alias_vtx_t verts[MAX_PARTICLES * 6];
	static const int corner_indices[6] = { 0, 1, 2, 0, 2, 3 };

	if (num_particles < 1)
		return;

	const image_t* tex = (alpha_particle ? r_aparticletexture : r_particletexture);

	if (tex == NULL)
		return; // Particle atlases not loaded yet (gl3_Image.c / gl3_Draw.c module ports).

	GL3_BindTexture((GLuint)tex->texnum);

	if (alpha_particle)
	{
		glBlendFunc(GL_ONE, GL_ONE);

		// gl1 disabled GL_FOG around additive particles; the particle shader checks fogSkipAdditive.
		if ((int)r_fog->value || (int)cl_camera_under_surface->value) //mxd. Removed gl_fog_broken cvar check.
			gl3state.uni3DData.fogSkipAdditive = 1;

		gl3state.uni3DData.alphaTestRef = -1.0f; // gl1: glDisable(GL_ALPHA_TEST).
	}
	else
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		gl3state.uni3DData.alphaTestRef = 0.05f; // gl1: GL_ALPHA_TEST state inherited from the alpha-surface pass (GL_GREATER, 0.05).
	}

	// Particle quads are emitted in world space.
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	GL3_UpdateUBO3D();

	glEnable(GL_BLEND);

	int num_verts = 0;

	const particle_t* p = &particles[0];
	for (int i = 0; i < num_particles && num_verts + 6 <= (int)ARRAY_SIZE(verts); i++, p++)
	{
		vec3_t p_up;
		VectorScale(vup, p->scale, p_up);

		vec3_t p_right;
		VectorScale(vright, p->scale, p_right);

		paletteRGBA_t c;

		if (p->type & PFL_LM_COLOR) //mxd
			c = R_ModulateRGBA(p->color, R_GetSpriteShadelight(p->origin, p->color.a));
		else
			c = p->color;

		if (alpha_particle)
		{
			c.r = c.r * c.a / 255;
			c.g = c.g * c.a / 255;
			c.b = c.b * c.a / 255;
		}

		const byte p_type = (p->type & PFL_FLAG_MASK); // Strip particle flags.
		const GLfloat* st = particle_st_coords[p_type];

		// gl1 emitted a camera-plane GL_QUADS diamond: +up, +right, -up, -right corners.
		float corners[4][3];
		for (int j = 0; j < 3; j++)
		{
			corners[0][j] = p->origin[j] + p_up[j];
			corners[1][j] = p->origin[j] + p_right[j];
			corners[2][j] = p->origin[j] - p_up[j];
			corners[3][j] = p->origin[j] - p_right[j];
		}

		const float corners_st[4][2] =
		{
			{ st[0], st[1] },
			{ st[2], st[1] },
			{ st[2], st[3] },
			{ st[0], st[3] }
		};

		for (int j = 0; j < 6; j++)
		{
			gl3_alias_vtx_t* v = &verts[num_verts++];
			const int ci = corner_indices[j];

			VectorCopy(corners[ci], v->pos);

			v->texCoord[0] = corners_st[ci][0];
			v->texCoord[1] = corners_st[ci][1];

			v->color[0] = (float)c.r * (1.0f / 255.0f); // gl1: glColor4ubv(c.c_array) + GL_MODULATE.
			v->color[1] = (float)c.g * (1.0f / 255.0f);
			v->color[2] = (float)c.b * (1.0f / 255.0f);
			v->color[3] = (float)c.a * (1.0f / 255.0f);
		}
	}

	GL3_UseProgram(gl3state.siParticle.shaderProgram);
	GL3_BufferAndDrawAlias(verts, num_verts, GL_TRIANGLES);

	if (alpha_particle && gl3state.uni3DData.fogSkipAdditive != 0)
	{
		gl3state.uni3DData.fogSkipAdditive = 0; // gl1: glEnable(GL_FOG).
		GL3_UpdateUBO3D();
	}

	glDisable(GL_BLEND);
}

#pragma endregion

#pragma region ========================== VIEW / MATRIX SETUP ==========================

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

static void R_SetFrustum(void)
{
	RotatePointAroundVector(frustum[0].normal, vup,		vpn, -(90.0f - r_newrefdef.fov_x * 0.5f));	// Rotate VPN right by FOV_X/2 degrees.
	RotatePointAroundVector(frustum[1].normal, vup,		vpn,   90.0f - r_newrefdef.fov_x * 0.5f);	// Rotate VPN left by FOV_X/2 degrees.
	RotatePointAroundVector(frustum[2].normal, vright,	vpn,   90.0f - r_newrefdef.fov_y * 0.5f);	// Rotate VPN up by FOV_X/2 degrees.
	RotatePointAroundVector(frustum[3].normal, vright,	vpn, -(90.0f - r_newrefdef.fov_y * 0.5f));	// Rotate VPN down by FOV_X/2 degrees.

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

// Q2 counterpart
static void R_SetupFrame(void)
{
	r_framecount++;

	// Build the transformation matrix for the given view angles.
	VectorCopy(r_newrefdef.vieworg, r_origin);
	AngleVectors(r_newrefdef.viewangles, vpn, vright, vup);

	// Current viewcluster.
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
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

	for (int i = 0; i < 4; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

	// Clear out the portion of the screen that the NOWORLDMODEL defines.
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		glEnable(GL_SCISSOR_TEST);
		glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
		glScissor(r_newrefdef.x, viddef.height - r_newrefdef.height - r_newrefdef.y, r_newrefdef.width, r_newrefdef.height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glClearColor(1.0f, 0.0f, 0.5f, 0.5f);
		glDisable(GL_SCISSOR_TEST);
	}
}

// Equivalent to R_x * R_y * R_z where R_x is the rotation matrix around the X axis for aroundXdeg. // YQ2
static hmm_mat4 rotAroundAxisXYZ(const float aroundXdeg, const float aroundYdeg, const float aroundZdeg)
{
	const float alpha = HMM_ToRadians(aroundXdeg);
	const float beta = HMM_ToRadians(aroundYdeg);
	const float gamma = HMM_ToRadians(aroundZdeg);

	const float sinA = HMM_SinF(alpha);
	const float cosA = HMM_CosF(alpha);
	const float sinB = HMM_SinF(beta);
	const float cosB = HMM_CosF(beta);
	const float sinG = HMM_SinF(gamma);
	const float cosG = HMM_CosF(gamma);

	const hmm_mat4 ret = {{
		{  cosB * cosG,  sinA * sinB * cosG + cosA * sinG, -cosA * sinB * cosG + sinA * sinG, 0 }, // first *column*
		{ -cosB * sinG, -sinA * sinB * sinG + cosA * cosG,  cosA * sinB * sinG + sinA * cosG, 0 },
		{  sinB,        -sinA * cosB,                       cosA * cosB,                      0 },
		{  0,            0,                                 0,                                1 }
	}};

	return ret;
}

// gluPerspective-style projection matrix; gl1 R_SetPerspective() parameters
// (zNear 1.0, zFar r_farclipdist) on yq2 GL3_SetPerspective() matrix math.
static hmm_mat4 R_SetPerspective(const GLdouble fovy) // YQ2
{
	static const GLdouble zNear = 1.0; // Q2: 4.0
	const GLdouble zFar = (GLdouble)r_farclipdist->value;
	const GLdouble aspectratio = (GLdouble)r_newrefdef.width / r_newrefdef.height;

	// Traditional gluPerspective calculations.
	const GLdouble top = zNear * tan(fovy * M_PI / 360.0);
	const GLdouble right = top * aspectratio;

	const GLdouble bottom = -top;
	const GLdouble left = -right;

	// The following emulates glFrustum(left, right, bottom, top, zNear, zFar).
	const float A = (float)((right + left) / (right - left));
	const float B = (float)((top + bottom) / (top - bottom));
	const float C = (float)(-(zFar + zNear) / (zFar - zNear));
	const float D = (float)(-(2.0 * zFar * zNear) / (zFar - zNear));

	const hmm_mat4 ret = {{
		{ (float)(2.0 * zNear / (right - left)), 0, 0, 0 }, // first *column*
		{ 0, (float)(2.0 * zNear / (top - bottom)), 0, 0 },
		{ A, B, C, -1.0f },
		{ 0, 0, D, 0 }
	}};

	return ret;
}

static void R_SetupGL3D(void) //mxd. Named 'R_SetupGL' in original logic.
{
	//mxd. Removed unneeded integer multiplications/divisions (gl1 parity).
	const int xl = r_newrefdef.x;
	const int xr = r_newrefdef.x + r_newrefdef.width;
	const int yt = viddef.height - r_newrefdef.y;
	const int yb = viddef.height - (r_newrefdef.y + r_newrefdef.height);

	glViewport(xl, yb, xr - xl, yt - yb);

	// Set up projection matrix.
	gl3state.projMat3D = R_SetPerspective((GLdouble)r_newrefdef.fov_y);

	glCullFace(GL_FRONT);

	// Set up view matrix (world coordinates -> eye coordinates). // YQ2 SetupGL()
	{
		// First put Z axis going up.
		hmm_mat4 viewMat = {{
			{  0, 0, -1, 0 }, // first *column* (the matrix is column-major)
			{ -1, 0,  0, 0 },
			{  0, 1,  0, 0 },
			{  0, 0,  0, 1 }
		}};

		// Now rotate by view angles.
		const hmm_mat4 rotMat = rotAroundAxisXYZ(-r_newrefdef.viewangles[2], -r_newrefdef.viewangles[0], -r_newrefdef.viewangles[1]);
		viewMat = HMM_MultiplyMat4(viewMat, rotMat);

		// .. and apply translation for current position.
		const hmm_vec3 trans = HMM_Vec3(-r_newrefdef.vieworg[0], -r_newrefdef.vieworg[1], -r_newrefdef.vieworg[2]);
		viewMat = HMM_MultiplyMat4(viewMat, HMM_Translate(trans));

		gl3state.viewMat3D = viewMat;
	}

	gl3state.uni3DData.transProjMat4 = gl3state.projMat3D;
	gl3state.uni3DData.transViewMat4 = gl3state.viewMat3D;
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;

	GL3_UpdateUBO3D();

	// gl1 glGetFloatv(GL_MODELVIEW/PROJECTION_MATRIX) equivalents for R_PointToScreen()
	// (hmm_mat4 is column-major like GL, so plain copies preserve the layout).
	memcpy(r_world_matrix, gl3state.viewMat3D.Elements, sizeof(r_world_matrix));
	memcpy(r_projection_matrix, gl3state.projMat3D.Elements, sizeof(r_projection_matrix)); //mxd

	// Set drawing parms.
	if ((int)gl_cull->value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

static void R_SetupGL2D(void) //mxd. Named 'R_SetGL2D' in original logic.
{
	// Set 2D virtual screen size.
	glViewport(0, 0, viddef.width, viddef.height);

	gl3state.uni2DData.transMat4 = HMM_Orthographic(0.0f, (float)viddef.width, (float)viddef.height, 0.0f, -99999.0f, 99999.0f);
	GL3_UpdateUBO2D();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	// gl1 also enabled GL_ALPHA_TEST here - that's shader-side now (fragmentSrc2D discard).
}

#pragma endregion

#pragma region ========================== FOG / CLEAR ==========================

static void R_Fog(void) // H2: GL_Fog. Sets the uni3D fog block instead of glFog*().
{
	const int mode = ClampI((int)r_fog_mode->value, 0, 2); //mxd. Added ClampI().

	gl3state.uni3DData.fogMode = mode;
	gl3state.uni3DData.fogStart = r_fog_startdist->value;
	gl3state.uni3DData.fogEnd = r_farclipdist->value;
	gl3state.uni3DData.fogDensity = r_fog_density->value;
	gl3state.uni3DData.fogLightmapAdjust = r_fog_lightmap_adjust->value;
	gl3state.uni3DData.fogColor = HMM_Vec4(r_fog_color_r->value, r_fog_color_g->value, r_fog_color_b->value, r_fog_color_a->value);

	glClearColor(r_fog_color_r->value, r_fog_color_g->value, r_fog_color_b->value, r_fog_color_a->value);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void R_WaterFog(void) // H2: GL_WaterFog. Sets the uni3D fog block instead of glFog*().
{
	const int mode = ClampI((int)r_fog_underwater_mode->value, 0, 2); //mxd. Added ClampI().

	gl3state.uni3DData.fogMode = mode;
	gl3state.uni3DData.fogStart = r_fog_underwater_startdist->value;
	gl3state.uni3DData.fogEnd = r_farclipdist->value;
	gl3state.uni3DData.fogDensity = r_fog_underwater_density->value;
	gl3state.uni3DData.fogLightmapAdjust = r_fog_underwater_lightmap_adjust->value;
	gl3state.uni3DData.fogColor = HMM_Vec4(r_fog_underwater_color_r->value, r_fog_underwater_color_g->value, r_fog_underwater_color_b->value, r_fog_underwater_color_a->value);

	glClearColor(r_fog_underwater_color_r->value, r_fog_underwater_color_g->value, r_fog_underwater_color_b->value, r_fog_underwater_color_a->value);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void R_Clear(void)
{
	// gl1 gl_ztrick logic dropped (registered but ignored - depth tricks make no sense
	// with a fully shader-driven pipeline; the gl1 path also skipped all fog logic).

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
		gl3state.uni3DData.fogMode = -1; // Fog off (gl1: glDisable(GL_FOG); shaders check this).

		if ((int)gl_clear->value)
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear(GL_DEPTH_BUFFER_BIT);
	}

	GL3_UpdateUBO3D(); // Fog state lives in uni3D now.

	gldepthmin = 0.0f;
	gldepthmax = 1.0f;
	glDepthFunc(GL_LEQUAL);

	glDepthRange((double)gldepthmin, (double)gldepthmax);

	if (gl_zfix->value) // YQ2
		glPolygonOffset(gldepthmax > gldepthmin ? 0.05f : -0.05f, gldepthmax > gldepthmin ? 1.0f : -1.0f);
}

#pragma endregion

#pragma region ========================== COMMANDS ==========================

void R_ScreenShot_f(void) // Based on YQ2 logic; Q2: GL_ScreenShot_f(). Ported from gl1_Misc.c.
{
#define SCREENSHOT_COMP	3

	const int buf_size = viddef.width * viddef.height * SCREENSHOT_COMP;
	byte* buffer = malloc(buf_size);

	if (buffer == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_ScreenShot_f: couldn't malloc %i bytes!\n", buf_size);
		return;
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, viddef.width, viddef.height, GL_RGB, GL_UNSIGNED_BYTE, buffer);

	ri.Vid_WriteScreenshot(viddef.width, viddef.height, SCREENSHOT_COMP, buffer);
	free(buffer);
}

void R_Strings_f(void) // Q2: GL_Strings_f(). Ported from gl1_Misc.c.
{
	ri.Con_Printf(PRINT_ALL, "GL_VENDOR: %s\n", gl3config.vendor_string);
	ri.Con_Printf(PRINT_ALL, "GL_RENDERER: %s\n", gl3config.renderer_string);
	ri.Con_Printf(PRINT_ALL, "GL_VERSION: %s\n", gl3config.version_string);
	ri.Con_Printf(PRINT_ALL, "GL_SHADING_LANGUAGE_VERSION: %s\n", gl3config.glsl_version_string);
}

#pragma endregion

static void R_Register(void)
{
	r_norefresh = ri.Cvar_Get("r_norefresh", "0", 0);
	r_fullbright = ri.Cvar_Get("r_fullbright", "0", 0);
	r_drawentities = ri.Cvar_Get("r_drawentities", "1", 0);
	r_drawworld = ri.Cvar_Get("r_drawworld", "1", 0);
	r_novis = ri.Cvar_Get("r_novis", "0", 0);
	r_nocull = ri.Cvar_Get("r_nocull", "0", 0);
	r_lerpmodels = ri.Cvar_Get("r_lerpmodels", "1", 0);
	r_speeds = ri.Cvar_Get("r_speeds", "0", 0);
	r_vsync = ri.Cvar_Get("r_vsync", "1", CVAR_ARCHIVE); // YQ2
	r_anisotropic = ri.Cvar_Get("r_anisotropic", "0", CVAR_ARCHIVE); // YQ2
	r_msaa_samples = ri.Cvar_Get("r_msaa_samples", "0", CVAR_ARCHIVE); // YQ2

	r_lightlevel = ri.Cvar_Get("r_lightlevel", "0", 0);

	// H2:
	r_farclipdist = ri.Cvar_Get("r_farclipdist", "4096.0", 0);
	r_fog = ri.Cvar_Get("r_fog", "0", 0);
	r_fog_mode = ri.Cvar_Get("r_fog_mode", "1", 0);
	r_fog_density = ri.Cvar_Get("r_fog_density", "0.004", 0);
	r_fog_startdist = ri.Cvar_Get("r_fog_startdist", "50.0", 0);
	r_fog_color_r = ri.Cvar_Get("r_fog_color_r", "1.0", 0);
	r_fog_color_g = ri.Cvar_Get("r_fog_color_g", "1.0", 0);
	r_fog_color_b = ri.Cvar_Get("r_fog_color_b", "1.0", 0);
	r_fog_color_a = ri.Cvar_Get("r_fog_color_a", "0.0", 0);
	r_fog_lightmap_adjust = ri.Cvar_Get("r_fog_lightmap_adjust", "5.0", 0);
	// gl3: default 1.0 (neutral) - gl1 never applied a lightmap fog adjust to the underwater fog
	// (its R_BlendLightmaps() adjust only triggered on r_fog, with the surface fog values).
	r_fog_underwater_lightmap_adjust = ri.Cvar_Get("r_fog_underwater_lightmap_adjust", "1.0", 0);
	r_fog_underwater_mode = ri.Cvar_Get("r_fog_underwater_mode", "1", 0);
	r_fog_underwater_density = ri.Cvar_Get("r_fog_underwater_density", "0.0015", 0);
	r_fog_underwater_startdist = ri.Cvar_Get("r_fog_underwater_startdist", "100.0", 0);
	r_fog_underwater_color_r = ri.Cvar_Get("r_fog_underwater_color_r", "1.0", 0);
	r_fog_underwater_color_g = ri.Cvar_Get("r_fog_underwater_color_g", "1.0", 0);
	r_fog_underwater_color_b = ri.Cvar_Get("r_fog_underwater_color_b", "1.0", 0);
	r_fog_underwater_color_a = ri.Cvar_Get("r_fog_underwater_color_a", "0.0", 0);
	r_underwater_color = ri.Cvar_Get("r_underwater_color", "0x70c06000", 0);
	r_frameswap = ri.Cvar_Get("r_frameswap", "1.0", 0);
	r_references = ri.Cvar_Get("r_references", "1.0", 0);

	gl_noartifacts = ri.Cvar_Get("gl_noartifacts", "0", 0); // H2

	gl_modulate = ri.Cvar_Get("gl_modulate", "1", CVAR_ARCHIVE);
	gl_lightmap = ri.Cvar_Get("gl_lightmap", "0", 0);
	gl_dynamic = ri.Cvar_Get("gl_dynamic", "1", 0);
	gl_nobind = ri.Cvar_Get("gl_nobind", "0", 0); // unused in gl3
	gl_showtris = ri.Cvar_Get("gl_showtris", "0", 0); // unused in gl3
	gl_reporthash = ri.Cvar_Get("gl_reporthash", "0", 0); // unused in gl3
	gl_ztrick = ri.Cvar_Get("gl_ztrick", "0", 0); // unused in gl3
	gl_zfix = ri.Cvar_Get("gl_zfix", "0", CVAR_ARCHIVE); // YQ2
	gl_finish = ri.Cvar_Get("gl_finish", "0", 0);
	gl_clear = ri.Cvar_Get("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get("gl_cull", "1", 0);
	gl_polyblend = ri.Cvar_Get("gl_polyblend", "1", 0);
	gl_flashblend = ri.Cvar_Get("gl_flashblend", "0", 0);
	gl_texturemode = ri.Cvar_Get("gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE);
	gl_lockpvs = ri.Cvar_Get("gl_lockpvs", "0", 0);
	gl_minlight = ri.Cvar_Get("gl_minlight", "0", CVAR_ARCHIVE); // YQ2

	// H2:
	gl_drawflat = ri.Cvar_Get("gl_drawflat", "0", 0);
	gl_trans33 = ri.Cvar_Get("gl_trans33", "0.33", 0); // H2_1.07: 0.33 -> 1
	gl_trans66 = ri.Cvar_Get("gl_trans66", "0.66", 0); // H2_1.07: 0.66 -> 1
	gl_bookalpha = ri.Cvar_Get("gl_bookalpha", "1.0", 0);

	gl_drawbuffer = ri.Cvar_Get("gl_drawbuffer", "GL_BACK", 0);
	gl_saturatelighting = ri.Cvar_Get("gl_saturatelighting", "0", 0);

	// YQ2 gl3: 0 = lots of glBufferData() calls, 1 = one big VBO, -1 = auto-detect (see RI_Init()).
	gl3_usebigvbo = ri.Cvar_Get("gl3_usebigvbo", "-1", CVAR_ARCHIVE);

	vid_gamma = ri.Cvar_Get("vid_gamma", "0.5", CVAR_ARCHIVE);
	vid_brightness = ri.Cvar_Get("vid_brightness", "0.5", CVAR_ARCHIVE); // H2
	vid_contrast = ri.Cvar_Get("vid_contrast", "0.5", CVAR_ARCHIVE); // H2
	vid_textures_refresh_required = ri.Cvar_Get("vid_textures_refresh_required", "0", 0); //mxd

	vid_ref = ri.Cvar_Get("vid_ref", "gl", CVAR_ARCHIVE);

	// H2:
	vid_mode = ri.Cvar_Get("vid_mode", "0", CVAR_ARCHIVE); // H2: 3
	menus_active = ri.Cvar_Get("menus_active", "0", 0);
	cl_camera_under_surface = ri.Cvar_Get("cl_camera_under_surface", "0", 0);
	quake_amount = ri.Cvar_Get("quake_amount", "0", 0);

	ri.Cmd_AddCommand("imagelist", R_ImageList_f);
	ri.Cmd_AddCommand("screenshot", R_ScreenShot_f);
	ri.Cmd_AddCommand("modellist", Mod_Modellist_f);
	ri.Cmd_AddCommand("gl_strings", R_Strings_f);

	// NOTE: no R_InitGammaTable() here - the H2 gamma/brightness/contrast table math
	// runs in the fragment shaders now (H2ColorGrade() in gl3_Shaders.c).
}

#pragma region ========================== MODE SETTING ==========================

// Changes the video mode. Ported verbatim from gl1_Main.c.
static rserr_t SetMode_impl(int* pwidth, int* pheight, const int mode) // YQ2
{
	ri.Con_Printf(PRINT_ALL, "Setting mode %d:", mode);

	if (!ri.Vid_GetModeInfo(pwidth, pheight, mode))
	{
		ri.Con_Printf(PRINT_ALL, " invalid mode\n");
		return RSERR_INVALID_MODE;
	}

	ri.Con_Printf(PRINT_ALL, " %dx%d\n", *pwidth, *pheight);

	return (ri.GLimp_InitGraphics(*pwidth, *pheight) ? RSERR_OK : RSERR_INVALID_MODE);
}

static qboolean R_SetMode(void)
{
	rserr_t err = SetMode_impl(&viddef.width, &viddef.height, (int)vid_mode->value);

	if (err == RSERR_OK)
	{
		gl3state.prev_mode = (int)vid_mode->value;
		return true;
	}

	if (err == RSERR_INVALID_MODE)
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::R_SetMode() - invalid mode\n");

		// Trying again would result in a crash anyway, give up already (this would happen if your initing fails at all and your resolution already was 640x480).
		if ((int)vid_mode->value == gl3state.prev_mode)
			return false;

		ri.Cvar_SetValue("vid_mode", (float)gl3state.prev_mode);
		vid_mode->modified = false;
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::R_SetMode() - unknown error %i!\n", err);
		return false;
	}

	// Try setting it back to something safe.
	err = SetMode_impl(&viddef.width, &viddef.height, (int)vid_mode->value);

	if (err != RSERR_OK)
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::R_SetMode() - could not revert to safe mode\n");
		return false;
	}

	return true;
}

#pragma endregion

#pragma region ========================== INIT / SHUTDOWN ==========================

static void GL3_SetDefaultState(void) // Q2: GL_SetDefaultState(); trimmed to GL 3.2 core state.
{
	glClearColor(1.0f, 0.0f, 0.5f, 0.5f);
	glCullFace(GL_FRONT);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	if (r_msaa_samples->value > 0.0f)
		glEnable(GL_MULTISAMPLE);
	else
		glDisable(GL_MULTISAMPLE);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR.

	// gl1's GL_ALPHA_TEST / glShadeModel / GL_TEXTURE_2D enables and texture filter
	// defaults don't exist in core profile - alpha test is shader-side (uni3D
	// alphaTestRef), filtering is set per-texture by R_TextureMode (gl3_Image.c port).
}

static qboolean RI_Init(void)
{
	// NOTE: gl1 halves the engine's shared turbsin[] table here; the gl3 turb shader
	// bakes that 0.5 factor into its analytic sin() (amplitude 4.0), so the table is
	// left untouched (gl3_Warp.c module port keeps CPU-side turbsin use for undulation).

	ri.Con_Printf(PRINT_ALL, "Refresh: "REF_TITLE"\n");
	R_Register();

	// Set our "safe" mode.
	gl3state.prev_mode = 0; // H2: 3.

	// Create the window and set up the context.
	if (!R_SetMode())
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::RI_Init() - could not R_SetMode()\n");
		return false;
	}

	// Get our various GL strings.
	gl3config.vendor_string = (const char*)glGetString(GL_VENDOR);
	ri.Con_Printf(PRINT_ALL, "GL_VENDOR: %s\n", gl3config.vendor_string);

	gl3config.renderer_string = (const char*)glGetString(GL_RENDERER);
	ri.Con_Printf(PRINT_ALL, "GL_RENDERER: %s\n", gl3config.renderer_string);

	gl3config.version_string = (const char*)glGetString(GL_VERSION);
	ri.Con_Printf(PRINT_ALL, "GL_VERSION: %s\n", gl3config.version_string);

	gl3config.glsl_version_string = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
	ri.Con_Printf(PRINT_ALL, "GL_SHADING_LANGUAGE_VERSION: %s\n", gl3config.glsl_version_string);

	// YQ2: Anisotropic texture filtering.
	if (gl3config.anisotropic)
	{
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl3config.max_anisotropy);
		ri.Con_Printf(PRINT_ALL, "Max. anisotropy: %i\n", (int)gl3config.max_anisotropy);
	}
	else
	{
		gl3config.max_anisotropy = 0.0f;
		ri.Con_Printf(PRINT_ALL, "Anisotropic filtering not supported.\n");
	}

	//mxd. Check max. supported texture size. H2R expects at least 512x512 (for cinematics rendering without frame chopping shenanigans).
	int max_texture_size;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	if (max_texture_size < 512)
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::RI_Init() - maximum supported texture size too low! Expected at least 512, got %i\n", max_texture_size);
		return false;
	}

	// YQ2: big-VBO workaround for AMD's proprietary drivers.
	gl3config.useBigVBO = false;
	if (gl3_usebigvbo->value == 1.0f)
	{
		ri.Con_Printf(PRINT_ALL, "Enabling useBigVBO workaround because gl3_usebigvbo = 1\n");
		gl3config.useBigVBO = true;
	}
	else if (gl3_usebigvbo->value == -1.0f)
	{
		// Enable for AMD's proprietary Linux driver (the Windows detection from yq2 is
		// irrelevant for this unix-only port).
		if (gl3config.vendor_string != NULL && strstr(gl3config.vendor_string, "Advanced Micro Devices, Inc.") != NULL)
		{
			ri.Con_Printf(PRINT_ALL, "Detected proprietary AMD GPU driver, enabling useBigVBO workaround\n");
			ri.Con_Printf(PRINT_ALL, "(consider using the open source RadeonSI drivers, they tend to work better overall)\n");
			gl3config.useBigVBO = true;
		}
	}

	// Generate texture handles for all possible lightmaps. // YQ2
	glGenTextures(MAX_LIGHTMAPS * MAX_LIGHTMAPS_PER_SURFACE, gl3state.lightmap_textureIDs[0]);

	GL3_SetDefaultState();

	if (!GL3_InitShaders()) // Also creates the UBOs (initUBOs()).
	{
		ri.Con_Printf(PRINT_ALL, "ref_gl3::RI_Init() - loading shaders failed!\n");
		return false;
	}

	GL3_InitBuffers();

#ifdef GL3_MODULES_READY // Module ports: gl3_Image.c, gl3_Model.c, gl3_Draw.c.
	R_InitImages();
	Mod_Init();
	Draw_InitLocal();
#endif

	const GLenum err = glGetError();
	if (err != GL_NO_ERROR)
	{
		ri.Con_Printf(PRINT_ALL, "glGetError() = 0x%x\n", err);
		return false;
	}

	return true;
}

static void RI_Shutdown(void)
{
#ifdef GL3_MODULES_READY
	ShutdownFonts(); // H2
#endif

	ri.Cmd_RemoveCommand("modellist");
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("imagelist");
	ri.Cmd_RemoveCommand("gl_strings");

	// YQ2: only call into GL if we have a context and loaded function pointers
	// (randomly chose one function that should always be there to test).
	if (glDeleteBuffers != NULL)
	{
#ifdef GL3_MODULES_READY
		Mod_FreeAll();
		R_ShutdownImages();
#endif

		GL3_ShutdownBuffers();
		GL3_ShutdownShaders();
	}

	// Shutdown OS-specific OpenGL stuff like contexts, etc.
	RI_ShutdownContext(); // YQ2
}

#pragma endregion

#pragma region ========================== FRAME FLOW ==========================

static void RI_BeginFrame(const float camera_separation) //TODO: remove camera_separation arg?
{
	(void)camera_separation; // Unused (as in gl1).

	// Changed from gl1: gamma/brightness/contrast are shader-side (uniCommon) now -
	// no gamma table rebuild, no texture re-upload, changes apply instantly.
	if (vid_gamma->modified || vid_brightness->modified || vid_contrast->modified)
	{
		gl3state.uniCommonData.gamma = vid_gamma->value;
		gl3state.uniCommonData.brightness = vid_brightness->value;
		gl3state.uniCommonData.contrast = vid_contrast->value;
		GL3_UpdateUBOCommon();

		vid_gamma->modified = false;
		vid_brightness->modified = false;
		vid_contrast->modified = false;
	}
	else if (vid_textures_refresh_required->value == 1.0f)
	{
		//mxd's gl1 roundtrip to re-gamma all textures after the video menu closes -
		// a no-op in gl3 (no texture-baked gamma), just acknowledge the request.
		ri.Cvar_SetValue("vid_textures_refresh_required", 0.0f);
	}

	// Go into 2D mode.
	R_SetupGL2D();

	// Draw buffer stuff.
	if (gl_drawbuffer->modified)
	{
		glDrawBuffer((Q_stricmp(gl_drawbuffer->string, "GL_FRONT") == 0) ? GL_FRONT : GL_BACK);
		gl_drawbuffer->modified = false;
	}

	// Texturemode stuff.
	if (gl_texturemode->modified || r_anisotropic->modified) // YQ2
	{
		if (r_anisotropic->modified)
		{
			if (!gl3config.anisotropic || gl3config.max_anisotropy < 2.0f)
				ri.Con_Printf(PRINT_ALL, "Anisotropic filtering not supported by this GL context.\n");
			else
				ri.Con_Printf(PRINT_ALL, "Anisotropic filtering: x%i (max x%i).\n",
					ClampI((int)r_anisotropic->value, 1, (int)gl3config.max_anisotropy), (int)gl3config.max_anisotropy);
		}

		R_TextureMode(gl_texturemode->string); // No-op until the gl3_Image.c module port lands (gl3_Stubs.c).
		gl_texturemode->modified = false;
		r_anisotropic->modified = false;
	}

	// Swapinterval stuff.
	if (r_vsync->modified) // YQ2
	{
		R_SetVsync();
		r_vsync->modified = false;
	}

	// Clear screen if desired.
	R_Clear();
}

// Q2 counterpart
static void R_PolyBlend(void)
{
	if (!(int)gl_polyblend->value || v_blend[3] == 0.0f)
		return;

	// gl1 drew a fixed camera-space GL_QUADS overlay; the gl3 equivalent is a quad
	// covering the whole 3D viewport in NDC, drawn through the 2D color program with
	// an identity transform. Clobbering uni2D here is safe: R_SetupGL2D() (called
	// right after R_RenderView() in RI_RenderFrame()) re-sets it before any 2D draw.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST); // gl1 parity: left disabled (R_SetupGL2D()/R_SetupGL3D() manage it).
	glDisable(GL_CULL_FACE);

	gl3state.uni2DData.transMat4 = gl3_identityMat4;
	GL3_UpdateUBO2D();

	gl3_alias_vtx_t verts[4];
	for (int i = 0; i < 4; i++)
	{
		verts[i].pos[0] = ((i & 1) ? 1.0f : -1.0f); // Fullscreen NDC triangle strip.
		verts[i].pos[1] = ((i & 2) ? 1.0f : -1.0f);
		verts[i].pos[2] = 0.0f;

		verts[i].texCoord[0] = 0.0f; // Unused by the 2D color program.
		verts[i].texCoord[1] = 0.0f;

		for (int j = 0; j < 4; j++)
			verts[i].color[j] = v_blend[j];
	}

	GL3_UseProgram(gl3state.si2Dcolor.shaderProgram);
	GL3_BufferAndDrawAlias(verts, 4, GL_TRIANGLE_STRIP);

	glDisable(GL_BLEND);
}

static void R_RenderView(const refdef_t* fd)
{
	if ((int)r_norefresh->value)
		return;

	r_newrefdef = *fd;

	if (r_worldmodel == NULL && !(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		ri.Sys_Error(ERR_DROP, "R_RenderView: NULL worldmodel"); //mxd. Sys_Error() -> ri.Sys_Error().

	if ((int)r_speeds->value)
	{
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	// gl3: update time-based uniforms (uniCommon.time drives turb/auto-animation in shaders).
	gl3state.uniCommonData.time = r_newrefdef.time;
	GL3_UpdateUBOCommon();

	R_PushDlights();

	if ((int)gl_finish->value)
		glFinish();

	R_SetupFrame();
	R_SetFrustum();
	R_SetupGL3D();
	R_MarkLeaves(); // Done here so we know if we're in water.
	R_ResetBmodelTransforms(); //mxd
	R_DrawWorld();
	R_DrawEntitiesOnList();
	R_RenderDlights();

	// Changed in H2:
	glDepthMask(GL_FALSE);
	R_SortAndDrawAlphaSurfaces();
	R_DrawParticles(r_newrefdef.num_particles, r_newrefdef.particles, false);
	R_DrawParticles(r_newrefdef.anum_particles, r_newrefdef.aparticles, true);
	glDepthMask(GL_TRUE);

#ifdef _DEBUG
	//R_DrawDebugPrimitives(); //mxd //TODO: enable with the gl3_Debug.c module port.
#endif

	// Changed in H2: R_Flash() call replaced with R_PolyBlend() call (or optimization?).
	R_PolyBlend();

	if ((int)r_speeds->value)
		ri.Con_Printf(PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n", c_brush_polys, c_alias_polys, c_visible_textures, c_visible_lightmaps); // H2: ri.Con_Printf -> Com_Printf //mxd. Com_Printf() -> ri.Con_Printf().

	// gl1's 'gl_reporthash' R_DisplayHashTable() debug dump not ported (gl_reporthash is registered but unused in gl3).
}

static void R_SetLightLevel(void)
{
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		// Save off light value for server to look at (BIG HACK!).
		vec3_t shadelight;
		R_LightPoint(r_newrefdef.clientmodelorg, shadelight, true); // H2: vieworg -> clientmodelorg

		// Pick the greatest component, which should be the same as the mono value returned by software.
		// Max. shadelight can exceed 1.0 when player is affected by dynamic lights --mxd.
		r_lightlevel->value = max(shadelight[0], max(shadelight[1], shadelight[2])) * 150.0f / gl_modulate->value; //mxd. Undo gl_modulate scaler (to avoid affecting cmd.lightlevel).
	}
}

static void R_ScreenFlash(const paletteRGBA_t color)
{
	glDepthMask(GL_FALSE);
	Draw_FadeScreen(color); // STUB until the gl3_Draw.c module port lands.
	glDepthMask(GL_TRUE);

	ri.Deactivate_Screen_Flash();
}

// H2: return type: void -> int //TODO: useless: always returns 0
static int RI_RenderFrame(const refdef_t* fd)
{
	paletteRGBA_t color;

	if ((int)cl_camera_under_surface->value)
		color.c = strtoul(r_underwater_color->string, NULL, 0);
	else
		color.c = 0;

	if (color.a != 255)
	{
		// Q2 version calls these 3 functions only.
		R_RenderView(fd);
		R_SetLightLevel();
		R_SetupGL2D();

#ifdef _DEBUG
		//R_DrawDebugLabels(); //mxd //TODO: enable with the gl3_Debug.c module port.
#endif

		if (color.a == 0)
			return 0;
	}

	R_ScreenFlash(color);

	return 0;
}

#pragma endregion

REF_DECLSPEC refexport_t GetRefAPI(const refimport_t rimp)
{
	refexport_t re;

	ri = rimp;

	re.api_version = REF_API_VERSION;
	re.title = REF_TITLE; //mxd

	re.BeginRegistration = RI_BeginRegistration;
	re.RegisterModel = RI_RegisterModel;
	re.RegisterSkin = RI_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = RI_SetSky;
	re.EndRegistration = RI_EndRegistration;
	re.GetReferencedID = RI_GetReferencedID;

	re.RenderFrame = RI_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen = Draw_FadeScreen;

	re.DrawBigFont = Draw_BigFont;
	re.BF_Strlen = BF_Strlen;
	re.BookDrawPic = Draw_BookPic;
	re.DrawInitCinematic = Draw_InitCinematic;
	re.DrawCloseCinematic = Draw_CloseCinematic;
	re.DrawCinematic = Draw_Cinematic;
	re.DrawInitCinematicRGBA = Draw_InitCinematicRGBA; // Loki cinematics. --morb
	re.DrawCinematicRGBA = Draw_CinematicRGBA; // Loki cinematics. --morb
	re.Draw_Name = Draw_Name;

	re.Init = RI_Init;
	re.Shutdown = RI_Shutdown;

	re.BeginFrame = RI_BeginFrame;
	re.EndFrame = RI_EndFrame;
	re.FindSurface = NULL; // RI_FindSurface is unused - struct Surface_s is not properly defined (gl1 parity).

	re.PrepareForWindow = RI_PrepareForWindow; // YQ2
	re.InitContext = RI_InitContext; // YQ2
	re.ShutdownContext = RI_ShutdownContext; // YQ2

#ifdef _DEBUG
	//mxd. Debug draw logic (all stubs until a gl3_Debug.c port).
	re.AddDebugBox = RI_AddDebugBox;
	re.AddDebugBbox = RI_AddDebugBbox;
	re.AddDebugEntityBbox = RI_AddDebugEntityBbox;

	re.AddDebugLabel = RI_AddDebugLabel;
	re.AddDebugEntityLabel = RI_AddDebugEntityLabel;

	re.AddDebugLine = RI_AddDebugLine;
	re.AddDebugArrow = RI_AddDebugArrow;
	re.AddDebugDirection = RI_AddDebugDirection;
	re.AddDebugAngles = RI_AddDebugAngles;
	re.AddDebugAnglesRad = RI_AddDebugAnglesRad;
	re.AddDebugMarker = RI_AddDebugMarker;

	re.FreeDebugPrimitives = R_FreeDebugPrimitives;
#endif

	// Unbound: A3D_RenderGeometry();

	return re;
}
