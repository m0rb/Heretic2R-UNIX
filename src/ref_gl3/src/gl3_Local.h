//
// gl3_Local.h -- renderer-wide header for the OpenGL 3.2 core renderer (ref_gl3).
//
// H2 rendering semantics from ref_gl1 (gl1_Local.h is the semantic reference),
// backend architecture from yquake2 8.60 gl3 (local.h is the backend reference).
//
// Copyright 1998 Raven Software
//

#pragma once

#include <glad/glad.h> // Must be included before SDL (SDL headers are only included by gl3_SDL.c).
#include "client/ref.h"
#include "client/vid.h" // viddef_t / extern viddef (engine global, resolved at dlopen).
#include "HandmadeMath.h"

#define REF_TITLE			"OpenGL 3.2"

#define MAX_GLTEXTURES		2048 // Same as gl1.

#pragma region ========================== CVARS ==========================

// Same extern set as gl1_Local.h (registered in R_Register(), gl3_Main.c).
// Cvar pointer variables marked "engine global" below the list live in the engine
// executable and are resolved at dlopen (see gl1_Main.c externs).

extern cvar_t* r_norefresh;
extern cvar_t* r_fullbright;
extern cvar_t* r_drawentities;
extern cvar_t* r_drawworld;
extern cvar_t* r_novis;
extern cvar_t* r_nocull;
extern cvar_t* r_lerpmodels;
extern cvar_t* r_vsync; // YQ2
extern cvar_t* r_anisotropic; // YQ2
extern cvar_t* r_msaa_samples; // YQ2
extern cvar_t* gl_zfix; // YQ2

extern cvar_t* r_lightlevel;

extern cvar_t* r_farclipdist;
extern cvar_t* r_fog;
extern cvar_t* r_fog_mode;
extern cvar_t* r_fog_density;
extern cvar_t* r_fog_startdist;
extern cvar_t* r_fog_lightmap_adjust;
extern cvar_t* r_fog_underwater;
extern cvar_t* r_fog_underwater_lightmap_adjust;
extern cvar_t* r_frameswap;
extern cvar_t* r_references;

extern cvar_t* gl_noartifacts;

extern cvar_t* gl_modulate;
extern cvar_t* gl_lightmap;
extern cvar_t* gl_dynamic;
extern cvar_t* gl_nobind; // unused in gl3
extern cvar_t* gl_showtris; // unused in gl3
extern cvar_t* gl_flashblend;
extern cvar_t* gl_texturemode;
extern cvar_t* gl_lockpvs;
extern cvar_t* gl_minlight; // YQ2

extern cvar_t* gl_drawflat;
extern cvar_t* gl_trans33;
extern cvar_t* gl_trans66;
extern cvar_t* gl_bookalpha;

extern cvar_t* gl_drawbuffer;
extern cvar_t* gl_saturatelighting;

extern cvar_t* vid_gamma;		// engine global
extern cvar_t* vid_brightness;	// engine global
extern cvar_t* vid_contrast;	// engine global

extern cvar_t* vid_ref;			// engine global

extern cvar_t* vid_mode;					// engine global
extern cvar_t* menus_active;				// engine global
extern cvar_t* cl_camera_under_surface;		// engine global
extern cvar_t* quake_amount;				// engine global

#pragma endregion

#pragma region ========================== IMAGES ==========================

typedef enum //mxd. Changed in H2
{
	it_skin = 1,
	it_sprite = 2,
	it_wall = 4,
	it_pic = 5,
	it_sky = 6
} imagetype_t;

// Ported VERBATIM from gl1_Local.h (H2 image struct). texnum stays; GL3 image
// upload code (gl3_Image.c module port) works with GL-generated texture names.
typedef struct image_s //mxd. Changed in H2. Original size: 104 bytes
{
	struct image_s* next;
	char name[MAX_QPATH];				// Game path, including extension.
	imagetype_t type;
	int width;
	int height;
	int registration_sequence;			// 0 = free
	struct msurface_s* texturechain;	// For sort-by-texture world drawing.
	struct msurface_s* multitexturechain;
	int texnum;							// gl texture binding.
	byte has_alpha;
	byte num_frames;
	struct paletteRGB_s* palette;		// .M8 palette.
} image_t;

extern image_t gltextures[MAX_GLTEXTURES];
extern int numgltextures;

extern image_t* r_notexture;
extern image_t* r_particletexture;
extern image_t* r_aparticletexture;
extern image_t* r_reflecttexture;
extern image_t* r_font1;
extern image_t* r_font2;

#pragma endregion

typedef enum
{
	RSERR_OK,
	RSERR_INVALID_MODE
} rserr_t;

#pragma region ========================== GL3 BACKEND (from yq2 gl3 local.h) ==========================

// Wrappers around glVertexAttribPointer() to stay sane
// (caller doesn't have to cast to GLintptr and then void*). // YQ2
static inline void qglVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset)
{
	glVertexAttribPointer(index, size, type, normalized, stride, (const void*)offset);
}

static inline void qglVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
	glVertexAttribIPointer(index, size, type, stride, (void*)offset);
}

// Attribute locations for vertex shaders. // YQ2
enum
{
	GL3_ATTRIB_POSITION   = 0,
	GL3_ATTRIB_TEXCOORD   = 1, // For normal texture.
	GL3_ATTRIB_LMTEXCOORD = 2, // For lightmap.
	GL3_ATTRIB_COLOR      = 3, // Per-vertex color.
	GL3_ATTRIB_NORMAL     = 4, // Vertex normal.
	GL3_ATTRIB_LIGHTFLAGS = 5  // uint, each set bit means "dyn light i affects this surface".
};

// Lightmap atlas config. // YQ2: 4 big atlases with 4 style sub-lightmaps each.
enum
{
	BLOCK_WIDTH = 1024,
	BLOCK_HEIGHT = 512,
	LIGHTMAP_BYTES = 4,
	MAX_LIGHTMAPS = 4,
	MAX_LIGHTMAPS_PER_SURFACE = 4 // == MAXLIGHTMAPS from qfiles.h.
};

// Vertex layout used for brush surfaces (world geometry): 10 floats + 1 uint. // YQ2
typedef struct gl3_3D_vtx_s
{
	vec3_t pos;
	float texCoord[2];
	float lmTexCoord[2]; // Lightmap texture coordinate (sometimes unused).
	vec3_t normal;
	GLuint lightFlags; // Bit i set means: dynlight i affects surface.
} gl3_3D_vtx_t;

// Vertex layout used for flexmodels/sprites/particle quads: 9 floats. // YQ2 (gl3_alias_vtx_t)
typedef struct gl3_alias_vtx_s
{
	GLfloat pos[3];
	GLfloat texCoord[2];
	GLfloat color[4];
} gl3_alias_vtx_t;

typedef struct
{
	const char* renderer_string;
	const char* vendor_string;
	const char* version_string;
	const char* glsl_version_string;

	int major_version;
	int minor_version;

	qboolean anisotropic;	// Is GL_EXT_texture_filter_anisotropic supported?
	qboolean stencil;		// Do we have a stencil buffer?

	qboolean useBigVBO;		// YQ2: workaround for AMD's Windows/proprietary driver for fewer calls to glBufferData().

	float max_anisotropy;	// YQ2
} gl3config_t;

typedef struct
{
	GLuint shaderProgram;
	GLint uniLmScalesOrTime;	// For lightmapped 3D shaders it's lmScales (else unused/-1).
	hmm_vec4 lmScales[4];		// Cached lmScales values (lightstyle scales for the 4 lightmap units).
} gl3ShaderInfo_t;

// ---------------------------------------------------------------------------
// UBO data structs. All use std140 layout; entries of std140 UBOs are aligned
// to multiples of their own size (vec4 to 16, mat4 columns to 16, float/int
// to 4). The GLSL blocks in gl3_Shaders.c declare *identical* members
// (including the explicit padding floats!) so that the driver-computed
// GL_UNIFORM_BLOCK_DATA_SIZE always equals sizeof() of these structs.
// ---------------------------------------------------------------------------

// uniCommon: shared between ALL shaders (2D and 3D).
// H2 extension: brightness + contrast (vid_brightness/vid_contrast) for the
// shader-side H2 color grading (see R_InitGammaTable() port in gl3_Shaders.c).
typedef struct
{
	GLfloat gamma;			// offset  0: vid_gamma (H2: pow() exponent used directly, default 0.5!)
	GLfloat brightness;		// offset  4: vid_brightness (H2, 0..1, 0.5 = neutral)
	GLfloat contrast;		// offset  8: vid_contrast (H2, 0..1, 0.5 = neutral)
	GLfloat intensity;		// offset 12: texel intensity scale (yq2 heritage, 1.0 in H2)
	GLfloat time;			// offset 16: r_newrefdef.time (auto-animation in shaders)

	GLfloat _padding[3];	// offsets 20/24/28: pad block to 32 bytes (vec4 multiple; also declared in GLSL!)
} gl3UniCommon_t;			// total: 32 bytes.

// uni2D: shared between 2D shaders.
typedef struct
{
	hmm_mat4 transMat4;		// offset 0: orthographic transform for 2D drawing.
} gl3Uni2D_t;				// total: 64 bytes.

// uni3D: shared between all 3D shaders.
// H2 extensions: fog block + alphaTestRef (gl1 R_Fog()/R_WaterFog()/glAlphaFunc() semantics).
typedef struct
{
	hmm_mat4 transProjMat4;		// offset   0: projection matrix.
	hmm_mat4 transViewMat4;		// offset  64: view matrix (kept separate from proj so shaders can reach eye space for fog/sphere-map).
	hmm_mat4 transModelMat4;	// offset 128: per-entity model matrix.

	hmm_vec4 fogColor;			// offset 192: fog color (r_fog_color_* / r_fog_underwater_color_* cvars).

	GLfloat scroll;				// offset 208: for SURF_FLOWING.
	GLfloat alpha;				// offset 212: for translucent surfaces (gl_trans33/gl_trans66, RF_TRANS_*).
	GLfloat alphaTestRef;		// offset 216: discard when texel.a <= alphaTestRef; < 0.0 disables the test.
								//             gl1 glAlphaFunc(GL_GREATER, x) thresholds: 0.666 world, 0.05 UI/sprites, 0.0 additive.
	GLint fogMode;				// offset 220: -1 = fog off, 0 = GL_LINEAR, 1 = GL_EXP, 2 = GL_EXP2 (gl1 fog_modes[]).
	GLfloat fogDensity;			// offset 224: r_fog_density / r_fog_underwater_density.
	GLfloat fogStart;			// offset 228: r_fog_startdist / r_fog_underwater_startdist (linear mode).
	GLfloat fogEnd;				// offset 232: r_farclipdist (linear mode).
	GLfloat fogLightmapAdjust;	// offset 236: r_fog_lightmap_adjust: scales start/end/density for the lightmap term
								//             (gl1 R_BlendLightmaps() fog adjustment).
	GLint fogSkipAdditive;		// offset 240: 1 = suppress fog for the current (additive) draw (gl1 glDisable(GL_FOG) around aparticles).

	GLfloat _padding[3];		// offsets 244/248/252: pad block to 256 bytes (vec4 multiple; also declared in GLSL!)
} gl3Uni3D_t;					// total: 256 bytes.

extern const hmm_mat4 gl3_identityMat4;

typedef struct
{
	vec3_t origin;
	GLfloat _padding;
	vec3_t color;
	GLfloat intensity;
} gl3UniDynLight;

typedef struct
{
	gl3UniDynLight dynLights[MAX_DLIGHTS];	// MAX_DLIGHTS == 32 (ref.h).
	GLuint numDynLights;
	GLfloat _padding[3];
} gl3UniLights_t;

typedef struct
{
	qboolean fullscreen;
	qboolean minlight_set; // YQ2. True when gl_minlight > 0 (gl1 parity).

	int prev_mode;

	// Each lightmap atlas consists of MAX_LIGHTMAPS_PER_SURFACE style sub-lightmaps (H2 lightstyles).
	GLuint lightmap_textureIDs[MAX_LIGHTMAPS][MAX_LIGHTMAPS_PER_SURFACE];

	GLuint currenttexture;	// Bound to GL_TEXTURE0.
	int currentlightmap;	// lightmap_textureIDs[currentlightmap] bound to GL_TEXTURE1..4.
	GLuint currenttmu;		// GL_TEXTURE0 .. GL_TEXTURE4.

	GLuint currentVAO;
	GLuint currentVBO;
	GLuint currentEBO;
	GLuint currentShaderProgram;
	GLuint currentUBO;

	// Shader program set (CONTRACT.md). NOTE: keep si2D the FIRST and siParticle
	// the LAST gl3ShaderInfo_t member - GL3_ShutdownShaders() iterates the range!
	gl3ShaderInfo_t si2D;			// 2D textured (HUD, console, menus, books, cinematics).
	gl3ShaderInfo_t si2Dcolor;		// 2D flat color (Draw_Fill, Draw_FadeScreen).

	gl3ShaderInfo_t si3Dlm;			// Opaque world faces with lightmap (x4 units).
	gl3ShaderInfo_t si3DlmFlow;		// Flowing/scrolling world faces with lightmap.
	gl3ShaderInfo_t si3Dtrans;		// Translucent faces (trans33/66) - always without lightmap.
	gl3ShaderInfo_t si3Dturb;		// Warped surfaces (water, lava, ...) - analytic turbsin.
	gl3ShaderInfo_t si3Dsky;		// Skybox.
	gl3ShaderInfo_t si3Dsprite;		// Sprites (per-vertex color quads).
	gl3ShaderInfo_t si3Dflex;		// Flexmodels: alias-style, per-vertex color lit.
	gl3ShaderInfo_t si3DflexSphere;	// Flexmodel sphere-map variant (FMNI_USE_REFLECT / RF_REFLECTION).
	gl3ShaderInfo_t siParticle;		// H2 particles: textured atlas quads (NOT yq2 point sprites).

	// Streaming VAO/VBO trio: 3D world verts / alias 9-float verts.
	// Particle quads share the alias layout+buffers (CONTRACT.md).
	GLuint vao3D, vbo3D;	// For brushes etc.: 10 floats and one uint as vertex input (x,y,z, s,t, lms,lmt, normX,normY,normZ ; lightFlags).

	// The next two are for gl3config.useBigVBO == true.
	int vbo3Dsize;
	int vbo3DcurOffset;

	GLuint vaoAlias, vboAlias, eboAlias; // For flexmodels/sprites/particle quads: 9 floats as (x,y,z, s,t, r,g,b,a).

	// UBOs and their data.
	gl3UniCommon_t uniCommonData;
	gl3Uni2D_t uni2DData;
	gl3Uni3D_t uni3DData;
	gl3UniLights_t uniLightsData;
	GLuint uniCommonUBO;
	GLuint uni2DUBO;
	GLuint uni3DUBO;
	GLuint uniLightsUBO;

	hmm_mat4 projMat3D;
	hmm_mat4 viewMat3D;
} gl3state_t;

extern gl3config_t gl3config;
extern gl3state_t gl3state;

#pragma endregion

#pragma region ========================== GLOBALS (gl1 parity) ==========================

extern float gldepthmin;
extern float gldepthmax;

extern refdef_t r_newrefdef;

extern int r_framecount;

extern vec3_t vup;
extern vec3_t vpn;
extern vec3_t vright;
extern vec3_t r_origin;

extern int c_brush_polys;
extern int c_alias_polys;

#pragma endregion

#pragma region ========================== STATE-CACHING INLINE HELPERS (YQ2) ==========================

static inline void GL3_UseProgram(const GLuint shaderProgram)
{
	if (shaderProgram != gl3state.currentShaderProgram)
	{
		gl3state.currentShaderProgram = shaderProgram;
		glUseProgram(shaderProgram);
	}
}

static inline void GL3_BindVAO(const GLuint vao)
{
	if (vao != gl3state.currentVAO)
	{
		gl3state.currentVAO = vao;
		glBindVertexArray(vao);
	}
}

static inline void GL3_BindVBO(const GLuint vbo)
{
	if (vbo != gl3state.currentVBO)
	{
		gl3state.currentVBO = vbo;
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
	}
}

static inline void GL3_BindEBO(const GLuint ebo)
{
	if (ebo != gl3state.currentEBO)
	{
		gl3state.currentEBO = ebo;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	}
}

static inline void GL3_SelectTMU(const GLenum tmu)
{
	if (gl3state.currenttmu != tmu)
	{
		glActiveTexture(tmu);
		gl3state.currenttmu = tmu;
	}
}

#pragma endregion

#pragma region ========================== CROSS-FILE PROTOTYPES ==========================

struct model_s; // Opaque until the gl3_Model.c module port lands.

// --- gl3_Main.c ---
extern void GL3_BufferAndDraw3D(const gl3_3D_vtx_t* verts, int numVerts, GLenum drawMode);
extern void R_ScreenShot_f(void);
extern void R_Strings_f(void);

// --- gl3_SDL.c ---
extern void RI_EndFrame(void);
extern qboolean RI_InitContext(void* win);
extern void RI_ShutdownContext(void);
extern int RI_PrepareForWindow(void);
extern void R_SetVsync(void);
extern void GL3_GetDrawableSize(int* width, int* height);

// --- gl3_Shaders.c ---
extern qboolean GL3_InitShaders(void);
extern void GL3_ShutdownShaders(void);
extern qboolean GL3_RecreateShaders(void);
extern void GL3_UpdateUBOCommon(void);
extern void GL3_UpdateUBO2D(void);
extern void GL3_UpdateUBO3D(void);
extern void GL3_UpdateUBOLights(void);

// ---------------------------------------------------------------------------
// Module functions. During the foundation pass ALL of these are provided as
// no-ops by gl3_Stubs.c; the module ports (gl3_Image.c, gl3_Draw.c, ...)
// will replace them one by one. Prototypes mirror gl1_*.h.
// ---------------------------------------------------------------------------

// --- gl3_Model.c (stubbed) ---
extern void RI_BeginRegistration(const char* map);
extern struct model_s* RI_RegisterModel(const char* name);
extern void RI_EndRegistration(void);
extern int RI_GetReferencedID(const struct model_s* model);
extern void Mod_Init(void);
extern void Mod_FreeAll(void);
extern void Mod_Modellist_f(void);

// --- gl3_Image.c (stubbed) ---
extern void R_InitImages(void);
extern void R_ShutdownImages(void);
extern void R_ImageList_f(void);
extern void R_TextureMode(const char* string);
extern struct image_s* RI_RegisterSkin(const char* name, qboolean* retval);

// --- gl3_Draw.c (stubbed) ---
extern void Draw_InitLocal(void);
extern void ShutdownFonts(void);
extern image_t* Draw_FindPic(const char* name);
extern void Draw_GetPicSize(int* w, int* h, const char* name);
extern void Draw_Pic(int x, int y, int scale, const char* name, float alpha);
extern void Draw_StretchPic(int x, int y, int w, int h, const char* name, float alpha, DrawStretchPicScaleMode_t mode);
extern void Draw_Char(int x, int y, int scale, int c, paletteRGBA_t color, qboolean draw_shadow);
extern void Draw_TileClear(int x, int y, int w, int h, const char* name);
extern void Draw_Fill(int x, int y, int w, int h, paletteRGBA_t color);
extern void Draw_FadeScreen(paletteRGBA_t color);
extern void Draw_Name(const vec3_t origin, const char* name, paletteRGBA_t color);

// --- gl3_DrawBook.c (stubbed) ---
extern void Draw_BigFont(int x, int y, const char* text, float alpha);
extern int BF_Strlen(const char* text);
extern void Draw_BookPic(const char* name, float scale, float alpha);

// --- gl3_DrawCinematic.c (stubbed) ---
extern void Draw_InitCinematic(int width, int height);
extern void Draw_CloseCinematic(void);
extern void Draw_Cinematic(const byte* data, const paletteRGB_t* palette);
extern void Draw_InitCinematicRGBA(int width, int height);
extern void Draw_CinematicRGBA(const byte* rgba);

// --- gl3_Sky.c (stubbed) ---
extern void RI_SetSky(const char* name, float rotate, const vec3_t axis);

#ifdef _DEBUG
// --- gl3_Debug.c (stubbed) --- //mxd. Debug draw logic.
extern void RI_AddDebugBox(const vec3_t center, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugBbox(const vec3_t mins, const vec3_t maxs, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugEntityBbox(const edict_t* ent, paletteRGBA_t color);
extern void RI_AddDebugLabel(const vec3_t origin, paletteRGBA_t color, float lifetime, const char* label);
extern void RI_AddDebugEntityLabel(const edict_t* ent, paletteRGBA_t color, const char* label);
extern void RI_AddDebugLine(const vec3_t start, const vec3_t end, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugArrow(const vec3_t start, const vec3_t end, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugDirection(const vec3_t start, const vec3_t direction, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugAngles(const vec3_t start, const vec3_t angles, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugAnglesRad(const vec3_t start, const vec3_t angles, float size, paletteRGBA_t color, float lifetime);
extern void RI_AddDebugMarker(const vec3_t center, float size, paletteRGBA_t color, float lifetime);
extern void R_FreeDebugPrimitives(void);
#endif

#pragma endregion

#pragma region ========================== IMPORTED FUNCTIONS ==========================

extern refimport_t ri;

#pragma endregion
