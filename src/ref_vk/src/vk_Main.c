#include "compat.h"
//
// vk_Main.c -- refresher setup and main frame flow for the Vulkan renderer.
//
// Cvar registration (gl1/gl3 mirror + vk_device/vk_validation/r_underwater_warp),
// mode setting (SetMode/R_SetMode verbatim from gl3_Main.c), context bring-up
// via the QVk core (vk_common.c), the complete H2 frame flow (RI_BeginFrame:
// cvar-modified handling + acquire + begin RP_WORLD + fog-aware clear;
// RI_RenderFrame: frustum/view/matrix setup, world + entity + particle passes,
// polyblend, r_lightlevel hack, screen flash handshake, internal world-pass
// end + underwater warp + graded postprocess blit; RI_EndFrame in vk_SDL.c:
// submit + present) and the complete H2R refexport_t.
//
// World / entity / image / 2D drawing bodies live in the module ports
// (vk_Image.c, vk_Draw.c, vk_Model.c, vk_Surface.c, ...) - the module-calling
// parts of the frame flow are gated on VK_MODULES_READY (vk_Stubs.c mirror of
// the gl3 foundation's GL3_MODULES_READY process).
//
// Mirrors gl1_Main.c semantics (through the validated H2 ports in gl3_Main.c)
// on the yq2remaster vk_main.c backend architecture.
//
// Copyright 1998 Raven Software
//

#include "vk_Local.h"

// Renderer-agnostic H2 model/BSP structs, shared with the gl1 sources compiled
// into ref_vk (CONTRACT.md rule 4). Must be included after vk_Local.h (needs
// image_t). Mod_PointInLeaf() lands with the vk_Model.c module port.
#include "gl1_Model.h"

#include "ParticleFlags.h" //mxd
#include "Vector.h"

#include <stdlib.h> // strtoul

#ifdef _WIN32
#define REF_DECLSPEC	__declspec(dllexport)
#else
#define REF_DECLSPEC	__attribute__((visibility("default")))
#endif

extern viddef_t viddef; // Defined in cl_globals.c (engine global, resolved at dlopen).
refimport_t ri;

model_t* r_worldmodel; // Assigned by RI_BeginRegistration() (vk_Model.c module port).

vkconfig_t vk_config;
vkstate_t vk_state;

// View origin.
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

// gl1 parity: view/projection matrices as GL-style float[16] for R_PointToScreen()
// (vk_Misc.c module port). The yq2 vk matrices are GL-layout column-major, so
// these are plain copies of the (pre-Vulkan-correction) matrices made in
// R_SetupVulkan().
float r_world_matrix[16];
float r_projection_matrix[16]; //mxd

// Premultiplied projection * view matrix in VULKAN clip space (Y flipped,
// depth 0..1) - pushed once per frame as the shared vertex push constant
// [0..15] (mvpMatrix/vpMatrix - vk_Local.h); world-space draws use it as-is,
// per-entity transforms keep their model matrix in the per-draw UBOs
// (yq2 R_SetupVulkan() convention).
float r_viewproj_matrix[16];

static float r_view_matrix[16];

// Correction matrix for perspective in Vulkan (yq2: GL -> Vulkan clip space).
static float r_vulkan_correction[16] = {
	1.0f,  0.0f, 0.0f, 0.0f,
	0.0f, -1.0f, 0.0f, 0.0f,
	0.0f,  0.0f, 0.5f, 0.0f,
	0.0f,  0.0f, 0.5f, 1.0f
};

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

// The single per-frame fog block, gl3 uni3D fog parity. Defined in vk_Surface.c
// and read by both the world (vklmapubo_t) and entity (vkmodelubo_t) per-draw
// UBOs. R_Clear()/R_Fog()/R_WaterFog() below are the frame authority that fills
// it each frame (matching gl1/gl3 R_Clear); vk_Surface.c's own R_SetupFog() is
// the dead duplicate. (Previously this module wrote a private r_world_fog that
// nobody read, so fog never reached the shaders.)
extern vkfogblock_t r_world_fog;

// vid_gamma/vid_brightness/vid_contrast snapshot pushed into the shaders
// (H2ColorGrade trio; refreshed in RI_BeginFrame()). Every RP_WORLD and RP_UI
// draw grades per-fragment with this now (gl1 baked-gamma / gl3 in-shader parity);
// the postprocess blit no longer grades.
float vk_gradePush[3] = { 0.5f, 0.5f, 0.5f };

// Set when the RP_WORLD -> RP_WORLD_WARP -> RP_UI transition happened this
// frame (R_EndWorldRenderpass() guard); reset in RI_BeginFrame().
static qboolean world_rendered;

#pragma region ========================== CVARS ==========================

// Same registration set as gl1_Main.c/gl3_Main.c R_Register() so menus and
// configs keep working. GL-only toggles that have no Vulkan meaning are
// registered but ignored (marked "unused in vk").

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

cvar_t* r_farclipdist;
cvar_t* r_fog;
cvar_t* r_fog_mode;
cvar_t* r_fog_density;
cvar_t* r_fog_startdist;
static cvar_t* r_fog_color_r;
static cvar_t* r_fog_color_g;
static cvar_t* r_fog_color_b;
static cvar_t* r_fog_color_a;
cvar_t* r_fog_lightmap_adjust;
cvar_t* r_fog_underwater; // gl1 parity: defined but never registered/used.
cvar_t* r_fog_underwater_lightmap_adjust;
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
cvar_t* gl_nobind; // unused in vk
cvar_t* gl_showtris; // unused in vk
static cvar_t* gl_reporthash; // unused in vk
static cvar_t* gl_ztrick; // unused in vk
static cvar_t* gl_zfix; // unused in vk (YQ2 GL depth fighting workaround)
static cvar_t* gl_finish; // unused in vk (explicit fencing instead)
cvar_t* gl_clear; // vk: render pass color attachment load op (VK_ATTACHMENT_LOAD_OP_CLEAR).
static cvar_t* gl_cull; // unused in vk (per-pipeline cull mode)
static cvar_t* gl_polyblend;
cvar_t* gl_flashblend;
cvar_t* gl_texturemode;
cvar_t* gl_lockpvs;
cvar_t* gl_minlight; // YQ2

cvar_t* gl_drawflat;
cvar_t* gl_trans33;
cvar_t* gl_trans66;
cvar_t* gl_bookalpha;

cvar_t* gl_drawbuffer; // unused in vk
cvar_t* gl_saturatelighting;

// vk-specific (CONTRACT.md):
cvar_t* vk_device_idx;		// "vk_device": preferred physical device index, -1 = auto.
cvar_t* vk_validation;		// Validation layers (0 = off, 1 = on, 2 = + best practices/verbose).
cvar_t* r_underwater_warp;	// Underwater screen distortion strength (world_warp pass).

extern cvar_t* vid_gamma;
extern cvar_t* vid_brightness;
extern cvar_t* vid_contrast;
static cvar_t* vid_textures_refresh_required; //mxd. No-op in vk: gamma is shader-side, no texture refresh needed.

extern cvar_t* vid_ref;

extern cvar_t* vid_mode; // gl_mode in Q2
extern cvar_t* menus_active;
extern cvar_t* cl_camera_under_surface;
extern cvar_t* quake_amount;

#pragma endregion

#ifdef VK_MODULES_READY
#pragma region ========================== CROSS-MODULE PROTOTYPES (not in vk_Local.h yet) ==========================

// These mirror the gl1_*.h headers 1:1; the corresponding vk module ports
// provide the definitions (ported functions keep their gl1 names - CONTRACT.md).
// ref_vk.so does NOT link until those module ports land (VK_MODULES_READY).
// TODO(integration): move these into vk_Local.h once all module ports landed.

// vk_Light.c (gl1_Light.h):
extern void R_RenderDlights(void);
extern void R_PushDlights(void);
extern void R_ResetBmodelTransforms(void); //mxd
extern void R_LightPoint(const vec3_t p, vec3_t color, qboolean check_bmodels); //mxd. +check_bmodels arg.

// vk_Surface.c (gl1_Surface.h):
extern int c_visible_lightmaps;
extern int c_visible_textures;
extern void R_SortAndDrawAlphaSurfaces(void);
extern void R_DrawBrushModel(entity_t* ent);
extern void R_DrawWorld(void);
extern void R_MarkLeaves(void);

// vk_Sprite.c (gl1_Sprite.h):
extern void R_DrawSpriteModel(entity_t* e);

// vk_FlexModel.c (gl1_FlexModel.h):
extern void R_DrawFlexModel(entity_t* e);

// vk_Misc.c (gl1_Misc.h):
extern void R_DrawNullModel(const entity_t* e);
extern paletteRGBA_t R_ModulateRGBA(paletteRGBA_t a, paletteRGBA_t b); //mxd
extern paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, byte alpha); //mxd

// SHARED CHANGE REQUIRED (vk_common.c/vk_Local.h - see the frame module port
// report): additive (aparticles) variant of vk_drawParticlesPipeline with
// ONE/ONE blend factors (gl1 glBlendFunc(GL_ONE, GL_ONE)) and no depth writes.
extern qvkpipeline_t vk_drawAParticlesPipeline;

#pragma endregion
#endif // VK_MODULES_READY

#pragma region ========================== MATRIX HELPERS (from yq2remaster vk_main.c) ==========================

// GL-layout column-major float[16] matrix helpers (vkQuake2-derived; the
// composition order matches the gl1 glRotatef()/glTranslatef() sequences).
// Non-static so the entity-drawing module ports can reuse them.

void Mat_Identity(float* matrix)
{
	matrix[0] = 1.0f;
	matrix[1] = 0.0f;
	matrix[2] = 0.0f;
	matrix[3] = 0.0f;
	matrix[4] = 0.0f;
	matrix[5] = 1.0f;
	matrix[6] = 0.0f;
	matrix[7] = 0.0f;
	matrix[8] = 0.0f;
	matrix[9] = 0.0f;
	matrix[10] = 1.0f;
	matrix[11] = 0.0f;
	matrix[12] = 0.0f;
	matrix[13] = 0.0f;
	matrix[14] = 0.0f;
	matrix[15] = 1.0f;
}

void Mat_Mul(const float* m1, const float* m2, float* res)
{
	const float mul[16] = {
		m1[0] * m2[0] + m1[1] * m2[4] + m1[2] * m2[8] + m1[3] * m2[12],
		m1[0] * m2[1] + m1[1] * m2[5] + m1[2] * m2[9] + m1[3] * m2[13],
		m1[0] * m2[2] + m1[1] * m2[6] + m1[2] * m2[10] + m1[3] * m2[14],
		m1[0] * m2[3] + m1[1] * m2[7] + m1[2] * m2[11] + m1[3] * m2[15],
		m1[4] * m2[0] + m1[5] * m2[4] + m1[6] * m2[8] + m1[7] * m2[12],
		m1[4] * m2[1] + m1[5] * m2[5] + m1[6] * m2[9] + m1[7] * m2[13],
		m1[4] * m2[2] + m1[5] * m2[6] + m1[6] * m2[10] + m1[7] * m2[14],
		m1[4] * m2[3] + m1[5] * m2[7] + m1[6] * m2[11] + m1[7] * m2[15],
		m1[8] * m2[0] + m1[9] * m2[4] + m1[10] * m2[8] + m1[11] * m2[12],
		m1[8] * m2[1] + m1[9] * m2[5] + m1[10] * m2[9] + m1[11] * m2[13],
		m1[8] * m2[2] + m1[9] * m2[6] + m1[10] * m2[10] + m1[11] * m2[14],
		m1[8] * m2[3] + m1[9] * m2[7] + m1[10] * m2[11] + m1[11] * m2[15],
		m1[12] * m2[0] + m1[13] * m2[4] + m1[14] * m2[8] + m1[15] * m2[12],
		m1[12] * m2[1] + m1[13] * m2[5] + m1[14] * m2[9] + m1[15] * m2[13],
		m1[12] * m2[2] + m1[13] * m2[6] + m1[14] * m2[10] + m1[15] * m2[14],
		m1[12] * m2[3] + m1[13] * m2[7] + m1[14] * m2[11] + m1[15] * m2[15]
	};

	memcpy(res, mul, sizeof(mul));
}

void Mat_Translate(float* matrix, const float x, const float y, const float z)
{
	const float t[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		   x,    y,    z, 1.0f
	};

	Mat_Mul(matrix, t, matrix);
}

void Mat_Rotate(float* matrix, const float deg, const float x, const float y, const float z)
{
	const double c = cos((double)deg * M_PI / 180.0);
	const double s = sin((double)deg * M_PI / 180.0);
	const double cd = 1.0 - c;

	vec3_t r = { x, y, z };
	VectorNormalize(r);

	const float rot[16] = {
		(float)(r[0] * r[0] * cd + c),			(float)(r[1] * r[0] * cd + r[2] * s),	(float)(r[0] * r[2] * cd - r[1] * s),	0.0f,
		(float)(r[0] * r[1] * cd - r[2] * s),	(float)(r[1] * r[1] * cd + c),			(float)(r[1] * r[2] * cd + r[0] * s),	0.0f,
		(float)(r[0] * r[2] * cd + r[1] * s),	(float)(r[1] * r[2] * cd - r[0] * s),	(float)(r[2] * r[2] * cd + c),			0.0f,
		0.0f,									0.0f,									0.0f,									1.0f
	};

	Mat_Mul(matrix, rot, matrix);
}

void Mat_Scale(float* matrix, const float x, const float y, const float z)
{
	const float s[16] = {
		   x, 0.0f, 0.0f, 0.0f,
		0.0f,    y, 0.0f, 0.0f,
		0.0f, 0.0f,    z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	Mat_Mul(matrix, s, matrix);
}

#pragma endregion

#pragma region ========================== COMMANDS ==========================

// R_ScreenShot_f() lives in vk_Misc.c (QVk_ReadPixels-based swapchain readback).

void Vk_Strings_f(void) // yq2 Vk_Strings_f (vk analog of gl_strings/R_Strings_f).
{
	const uint32_t apiVer = vk_device.properties.apiVersion;
	const uint32_t drvVer = vk_device.properties.driverVersion;

	ri.Con_Printf(PRINT_ALL, "VK_INSTANCE_VERSION: %d.%d.%d\n",
		VK_VERSION_MAJOR(vk_config.vk_version), VK_VERSION_MINOR(vk_config.vk_version), VK_VERSION_PATCH(vk_config.vk_version));
	ri.Con_Printf(PRINT_ALL, "VK_DEVICE: %s (%s, %s)\n",
		vk_device.properties.deviceName, vk_config.vendor_name, vk_config.device_type);
	ri.Con_Printf(PRINT_ALL, "VK_API_VERSION: %d.%d.%d, driver %d.%d.%d\n",
		VK_VERSION_MAJOR(apiVer), VK_VERSION_MINOR(apiVer), VK_VERSION_PATCH(apiVer),
		VK_VERSION_MAJOR(drvVer), VK_VERSION_MINOR(drvVer), VK_VERSION_PATCH(drvVer));
	ri.Con_Printf(PRINT_ALL, "VK_PRESENT_MODE: %s\n", (vk_config.present_mode != NULL ? vk_config.present_mode : "<none>"));
	ri.Con_Printf(PRINT_ALL, "VK_BUFFERS: vertex %ukB, index %ukB, uniform %ukB (max usage: %u/%u/%u kB)\n",
		vk_config.vertex_buffer_size / 1024, vk_config.index_buffer_size / 1024, vk_config.uniform_buffer_size / 1024,
		vk_config.vertex_buffer_max_usage / 1024, vk_config.index_buffer_max_usage / 1024, vk_config.uniform_buffer_max_usage / 1024);
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
	// gl3 parity: default 1.0 (neutral) - gl1 never applied a lightmap fog adjust to the underwater fog.
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
	gl_nobind = ri.Cvar_Get("gl_nobind", "0", 0); // unused in vk
	gl_showtris = ri.Cvar_Get("gl_showtris", "0", 0); // unused in vk
	gl_reporthash = ri.Cvar_Get("gl_reporthash", "0", 0); // unused in vk
	gl_ztrick = ri.Cvar_Get("gl_ztrick", "0", 0); // unused in vk
	gl_zfix = ri.Cvar_Get("gl_zfix", "0", CVAR_ARCHIVE); // unused in vk (YQ2)
	gl_finish = ri.Cvar_Get("gl_finish", "0", 0); // unused in vk
	gl_clear = ri.Cvar_Get("gl_clear", "0", 0);
	gl_cull = ri.Cvar_Get("gl_cull", "1", 0); // unused in vk
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

	gl_drawbuffer = ri.Cvar_Get("gl_drawbuffer", "GL_BACK", 0); // unused in vk
	gl_saturatelighting = ri.Cvar_Get("gl_saturatelighting", "0", 0);

	// vk-specific (CONTRACT.md):
	vk_device_idx = ri.Cvar_Get("vk_device", "-1", CVAR_ARCHIVE);
	vk_validation = ri.Cvar_Get("vk_validation", "0", CVAR_ARCHIVE);
	r_underwater_warp = ri.Cvar_Get("r_underwater_warp", "1", CVAR_ARCHIVE);

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
	ri.Cmd_AddCommand("vk_strings", Vk_Strings_f);

	// NOTE: no R_InitGammaTable() here - the H2 gamma/brightness/contrast table math
	// runs in the fragment shaders now (H2ColorGrade() - see shaders/basic.frag).
}

#pragma region ========================== MODE SETTING ==========================

// Changes the video mode. Ported verbatim from gl1_Main.c (gl3_Main.c parity).
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
		vk_state.prev_mode = (int)vid_mode->value;
		return true;
	}

	if (err == RSERR_INVALID_MODE)
	{
		ri.Con_Printf(PRINT_ALL, "ref_vk::R_SetMode() - invalid mode\n");

		// Trying again would result in a crash anyway, give up already (this would happen if your initing fails at all and your resolution already was 640x480).
		if ((int)vid_mode->value == vk_state.prev_mode)
			return false;

		ri.Cvar_SetValue("vid_mode", (float)vk_state.prev_mode);
		vid_mode->modified = false;
	}
	else
	{
		ri.Con_Printf(PRINT_ALL, "ref_vk::R_SetMode() - unknown error %i!\n", err);
		return false;
	}

	// Try setting it back to something safe.
	err = SetMode_impl(&viddef.width, &viddef.height, (int)vid_mode->value);

	if (err != RSERR_OK)
	{
		ri.Con_Printf(PRINT_ALL, "ref_vk::R_SetMode() - could not revert to safe mode\n");
		return false;
	}

	return true;
}

#pragma endregion

#pragma region ========================== INIT / SHUTDOWN ==========================

static qboolean RI_Init(void)
{
	// NOTE: gl1 halves the engine's shared turbsin[] table here; the vk turb
	// shader will bake that 0.5 factor into its analytic sin() like gl3 did,
	// so the table is left untouched (vk_Warp.c module port).

	ri.Con_Printf(PRINT_ALL, "Refresh: "REF_TITLE"\n");
	R_Register();

	// Initial shader-side color grade state (H2ColorGrade trio).
	vk_gradePush[0] = vid_gamma->value;
	vk_gradePush[1] = vid_brightness->value;
	vk_gradePush[2] = vid_contrast->value;
	vid_gamma->modified = false;
	vid_brightness->modified = false;
	vid_contrast->modified = false;

	// Set our "safe" mode.
	vk_state.prev_mode = 0; // H2: 3.

	// Create the window and set up the Vulkan context: R_SetMode() ->
	// ri.GLimp_InitGraphics() -> re.PrepareForWindow() + SDL window +
	// re.InitContext() -> RI_InitContext() (vk_SDL.c) -> QVk_CreateInstance()/
	// QVk_Init() (device pick, swapchain, render passes, descriptor layouts,
	// dynamic/staging buffers, pipelines, samplers - vk_common.c).
	if (!R_SetMode())
	{
		ri.Con_Printf(PRINT_ALL, "ref_vk::RI_Init() - could not R_SetMode()\n");
		return false;
	}

	if (!vk_initialized)
	{
		ri.Con_Printf(PRINT_ALL, "ref_vk::RI_Init() - Vulkan backend not initialized!\n");
		return false;
	}

	// Log device/backend info.
	Vk_Strings_f();

#ifdef VK_MODULES_READY // Module ports: vk_Image.c, vk_Model.c, vk_Draw.c.
	R_InitImages();
	Mod_Init();
	Draw_InitLocal();
#endif

	ri.Con_Printf(PRINT_ALL, "Successfully initialized Vulkan!\n");

	return true;
}

static void RI_Shutdown(void)
{
#ifdef VK_MODULES_READY
	ShutdownFonts(); // H2
#endif

	ri.Cmd_RemoveCommand("modellist");
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("imagelist");
	ri.Cmd_RemoveCommand("vk_strings");

	// Full Vulkan teardown (waits for device idle; frees module resources
	// under VK_MODULES_READY, then all QVk core objects + surface/instance).
	RI_ShutdownContext(); // YQ2
}

#pragma endregion

#pragma region ========================== FOG / CLEAR ==========================

// Draws an immediately-flushed color quad into the world render pass through
// the 2D color-quad pipeline (basic_color_quad.frag grades it once in-shader,
// matching the per-fragment RP_WORLD grade - gl3 parity).
// x/y/w/h are normalized (0..1) window coordinates (QVk_DrawColorRect()).
static void R_DrawWorldColorRect(const float x, const float y, const float w, const float h, const float r, const float g, const float b, const float a)
{
	QVk_DrawColorRect(x, y, w, h, r, g, b, a, RP_WORLD);
	QVk_Draw2DCallsRender();
}

static void R_Fog(void) // H2: GL_Fog. Sets the shared fog block instead of glFog*().
{
	r_world_fog.fogMode = ClampI((int)r_fog_mode->value, 0, 2); //mxd. Added ClampI().
	r_world_fog.fogStart = r_fog_startdist->value;
	r_world_fog.fogEnd = r_farclipdist->value;
	r_world_fog.fogDensity = r_fog_density->value;
	r_world_fog.fogLightmapAdjust = r_fog_lightmap_adjust->value;
	r_world_fog.fogSkipAdditive = 0;
	r_world_fog.fogColor[0] = r_fog_color_r->value;
	r_world_fog.fogColor[1] = r_fog_color_g->value;
	r_world_fog.fogColor[2] = r_fog_color_b->value;
	r_world_fog.fogColor[3] = r_fog_color_a->value;

	// gl1 glClearColor(fog color) + glClear(GL_COLOR_BUFFER_BIT): RP_WORLD's
	// color attachment is loaded, not cleared (vk_common.c CreateRenderpasses(),
	// the depth attachment IS cleared by the render pass begin), so the color
	// clear becomes an explicit full-screen fill.
	R_DrawWorldColorRect(0.0f, 0.0f, 1.0f, 1.0f, r_fog_color_r->value, r_fog_color_g->value, r_fog_color_b->value, 1.0f);
}

static void R_WaterFog(void) // H2: GL_WaterFog. Sets the shared fog block instead of glFog*().
{
	r_world_fog.fogMode = ClampI((int)r_fog_underwater_mode->value, 0, 2); //mxd. Added ClampI().
	r_world_fog.fogStart = r_fog_underwater_startdist->value;
	r_world_fog.fogEnd = r_farclipdist->value;
	r_world_fog.fogDensity = r_fog_underwater_density->value;
	r_world_fog.fogLightmapAdjust = r_fog_underwater_lightmap_adjust->value;
	r_world_fog.fogSkipAdditive = 0;
	r_world_fog.fogColor[0] = r_fog_underwater_color_r->value;
	r_world_fog.fogColor[1] = r_fog_underwater_color_g->value;
	r_world_fog.fogColor[2] = r_fog_underwater_color_b->value;
	r_world_fog.fogColor[3] = r_fog_underwater_color_a->value;

	R_DrawWorldColorRect(0.0f, 0.0f, 1.0f, 1.0f, r_fog_underwater_color_r->value, r_fog_underwater_color_g->value, r_fog_underwater_color_b->value, 1.0f);
}

static void R_Clear(void)
{
	// gl1 gl_ztrick logic dropped (registered but ignored - gl3 parity).

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
		r_world_fog.fogMode = -1; // Fog off (gl1: glDisable(GL_FOG); the world/model shaders check this).
		r_world_fog.fogSkipAdditive = 0;

		if ((int)gl_clear->value)
			R_DrawWorldColorRect(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.5f, 1.0f); // gl1 GL_SetDefaultState() clear color (debug pink).
	}

	// gl1 gldepthmin/gldepthmax + glDepthFunc(GL_LEQUAL) are baked into the
	// pipelines; the depth buffer itself was cleared by the RP_WORLD begin.
	// gl_zfix glPolygonOffset() does not apply (YQ2 GL depth fighting workaround).
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

#ifdef VK_MODULES_READY // Mod_PointInLeaf() lands with the vk_Model.c module port.
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
#endif

	for (int i = 0; i < 4; i++)
		v_blend[i] = r_newrefdef.blend[i];

	c_brush_polys = 0;
	c_alias_polys = 0;

	// Clear out the portion of the screen that the NOWORLDMODEL defines
	// (gl1: scissored glClear(); vk: color rect, like yq2 R_SetupFrame()).
	// NOTE: dead code in H2R - the engine never sets RDF_NOWORLDMODEL.
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		R_DrawWorldColorRect(
			(float)r_newrefdef.x / (float)viddef.width,
			(float)r_newrefdef.y / (float)viddef.height,
			(float)r_newrefdef.width / (float)viddef.width,
			(float)r_newrefdef.height / (float)viddef.height,
			0.3f, 0.3f, 0.3f, 1.0f);
	}
}

// gluPerspective-style projection matrix; gl1 R_SetPerspective() parameters
// (zNear 1.0, zFar r_farclipdist) on yq2 Mat_Perspective()-style output.
// Fills BOTH r_projection_matrix (GL-style, for R_PointToScreen() - gl3
// parity) and 'vkProj' (Vulkan clip space: Y flipped, depth 0..1).
static void R_SetPerspective(const double fovy, float* vkProj) // YQ2
{
	static const double zNear = 1.0; // Q2: 4.0
	const double zFar = (double)r_farclipdist->value;
	const double aspectratio = (double)r_newrefdef.width / r_newrefdef.height;

	// Traditional gluPerspective calculations.
	const double top = zNear * tan(fovy * M_PI / 360.0);
	const double right = top * aspectratio;

	const double bottom = -top;
	const double left = -right;

	// The following emulates glFrustum(left, right, bottom, top, zNear, zFar).
	const float A = (float)((right + left) / (right - left));
	const float B = (float)((top + bottom) / (top - bottom));
	const float C = (float)(-(zFar + zNear) / (zFar - zNear));
	const float D = (float)(-(2.0 * zFar * zNear) / (zFar - zNear));

	memset(r_projection_matrix, 0, sizeof(r_projection_matrix));
	r_projection_matrix[0] = (float)(2.0 * zNear / (right - left));
	r_projection_matrix[5] = (float)(2.0 * zNear / (top - bottom));
	r_projection_matrix[8] = A;
	r_projection_matrix[9] = B;
	r_projection_matrix[10] = C;
	r_projection_matrix[11] = -1.0f;
	r_projection_matrix[14] = D;

	// Convert to the Vulkan coordinate system (yq2 Mat_Perspective():
	// Y flip + 0..1 depth range, applied after the projection).
	Mat_Mul(r_projection_matrix, r_vulkan_correction, vkProj);
}

static void R_SetupVulkan(void) //mxd. Named 'R_SetupGL' in original logic (gl3: R_SetupGL3D); yq2 vk name kept.
{
	// Render old elements before changing the viewport (yq2).
	QVk_Draw2DCallsRender();

	// Set up the 3D viewport/scissor: the r_newrefdef rect, top-left origin
	// (Vulkan convention - the GL Y flip lives in r_vulkan_correction).
	const VkViewport viewport = {
		.x = (float)r_newrefdef.x,
		.y = (float)r_newrefdef.y,
		.width = (float)r_newrefdef.width,
		.height = (float)r_newrefdef.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(vk_activeCmdbuffer, 0, 1, &viewport);

	const VkRect2D scissor = {
		.offset = { r_newrefdef.x, r_newrefdef.y },
		.extent = { (uint32_t)r_newrefdef.width, (uint32_t)r_newrefdef.height }
	};
	vkCmdSetScissor(vk_activeCmdbuffer, 0, 1, &scissor);

	// Set up projection matrix (also stores the GL-style copy in r_projection_matrix).
	float proj[16];
	R_SetPerspective((double)r_newrefdef.fov_y, proj);

	// Set up view matrix (world coordinates -> eye coordinates): the gl1
	// glRotatef()/glTranslatef() sequence on yq2 R_SetupVulkan() matrix math.
	Mat_Identity(r_view_matrix);
	Mat_Translate(r_view_matrix, -r_newrefdef.vieworg[0], -r_newrefdef.vieworg[1], -r_newrefdef.vieworg[2]);
	Mat_Rotate(r_view_matrix, -r_newrefdef.viewangles[1], 0.0f, 0.0f, 1.0f);
	Mat_Rotate(r_view_matrix, -r_newrefdef.viewangles[0], 0.0f, 1.0f, 0.0f);
	Mat_Rotate(r_view_matrix, -r_newrefdef.viewangles[2], 1.0f, 0.0f, 0.0f);
	Mat_Rotate(r_view_matrix, 90.0f, 0.0f, 0.0f, 1.0f);	// Put Z going up.
	Mat_Rotate(r_view_matrix, -90.0f, 1.0f, 0.0f, 0.0f);

	// Precalculate the view-projection matrix.
	Mat_Mul(r_view_matrix, proj, r_viewproj_matrix);

	// The view-projection matrix is always the first vertex push constant item
	// and ALL pipeline layouts declare identical push constant ranges
	// (vk_Local.h), so pushing it once here covers every world-space draw this
	// frame; per-entity transforms keep their model matrix in the per-draw UBOs.
	vkCmdPushConstants(vk_activeCmdbuffer, vk_drawTexQuadPipeline[vk_state.current_renderpass].layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(r_viewproj_matrix), r_viewproj_matrix);

	// gl1 glGetFloatv(GL_MODELVIEW_MATRIX) equivalent for R_PointToScreen()
	// (the yq2 matrices are GL-layout column-major, so a plain copy preserves
	// the layout; r_projection_matrix was filled by R_SetPerspective()).
	memcpy(r_world_matrix, r_view_matrix, sizeof(r_world_matrix));
}

#pragma endregion

#ifdef VK_MODULES_READY
#pragma region ========================== ENTITIES / PARTICLES ==========================

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
	static const float particle_st_coords[NUM_PARTICLE_TYPES][4] =
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

	// GL_QUADS doesn't exist in Vulkan: emit 6 verts (2 triangles) per particle quad instead (gl3 parity).
	static vk_alias_vtx_t verts[MAX_PARTICLES * 6];
	static const int corner_indices[6] = { 0, 1, 2, 0, 2, 3 };

	if (num_particles < 1)
		return;

	const image_t* tex = (alpha_particle ? r_aparticletexture : r_particletexture);

	if (tex == NULL)
		return; // Particle atlases not loaded yet (vk_Image.c / vk_Draw.c module ports).

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
		const float* st = particle_st_coords[p_type];

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
			vk_alias_vtx_t* v = &verts[num_verts++];
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

	// gl1 blend state: classic = GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA, additive
	// (aparticles) = GL_ONE/GL_ONE - baked into the two pipeline variants.
	// NOTE: gl1 also disabled GL_FOG around additive particles; the locked
	// particle pipeline layout carries no fog UBO at all, so neither particle
	// pass receives the fog term (deviation for CLASSIC particles under fog).
	qvkpipeline_t* pipeline = (alpha_particle ? &vk_drawAParticlesPipeline : &vk_drawParticlesPipeline);
	QVk_BindPipeline(pipeline);

	VkBuffer vbo;
	VkDeviceSize vboOffset;
	const VkDeviceSize vertSize = (VkDeviceSize)num_verts * sizeof(vk_alias_vtx_t);
	uint8_t* vertData = QVk_GetVertexBuffer(vertSize, &vbo, &vboOffset);
	memcpy(vertData, verts, vertSize);

	// Particle quads are emitted in world space (gl1: identity model matrix).
	vkCmdPushConstants(vk_activeCmdbuffer, pipeline->layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(r_viewproj_matrix), r_viewproj_matrix);

	// Grade per-fragment (basic.frag) before the additive blend. Alpha test:
	// gl1 additive = glDisable(GL_ALPHA_TEST), classic = GL_ALPHA_TEST state
	// inherited from the alpha-surface pass (GL_GREATER, 0.05) - gl3 parity.
	const float fragPush[4] = { vk_gradePush[0], vk_gradePush[1], vk_gradePush[2], (alpha_particle ? -1.0f : 0.05f) };
	vkCmdPushConstants(vk_activeCmdbuffer, pipeline->layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, PUSH_CONSTANT_VERTEX_SIZE * sizeof(float), sizeof(fragPush), fragPush);

	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, 1, &tex->vk_texture.descriptorSet, 0, NULL);
	vkCmdBindVertexBuffers(vk_activeCmdbuffer, 0, 1, &vbo, &vboOffset);
	vkCmdDraw(vk_activeCmdbuffer, (uint32_t)num_verts, 1, 0, 0);
}

#pragma endregion
#endif // VK_MODULES_READY

#pragma region ========================== FRAME FLOW ==========================

static void RI_BeginFrame(const float camera_separation) //TODO: remove camera_separation arg?
{
	(void)camera_separation; // Unused (as in gl1).

	// World has not rendered yet.
	world_rendered = false;

	/* VK hasn't been initialized yet. I'm pretty sure that we can't get here
	   without having called QVk_Init(), but better safe than sorry. */
	if (!vk_initialized)
	{
		vk_frameStarted = false;
		return;
	}

	// Changed from gl1: gamma/brightness/contrast are shader-side (push
	// constants) now - no gamma table rebuild, no texture re-upload, changes
	// apply instantly.
	if (vid_gamma->modified || vid_brightness->modified || vid_contrast->modified)
	{
		vk_gradePush[0] = vid_gamma->value;
		vk_gradePush[1] = vid_brightness->value;
		vk_gradePush[2] = vid_contrast->value;

		vid_gamma->modified = false;
		vid_brightness->modified = false;
		vid_contrast->modified = false;
	}
	else if (vid_textures_refresh_required->value == 1.0f)
	{
		//mxd's gl1 roundtrip to re-gamma all textures after the video menu closes -
		// a no-op in vk (no texture-baked gamma), just acknowledge the request.
		ri.Cvar_SetValue("vid_textures_refresh_required", 0.0f);
	}

	// Texturemode stuff.
	if (gl_texturemode->modified || r_anisotropic->modified) // YQ2
	{
		R_TextureMode(gl_texturemode->string); // No-op until the vk_Image.c module port lands (vk_Stubs.c).
		gl_texturemode->modified = false;
		r_anisotropic->modified = false;
	}

	// Swapinterval stuff - in Vulkan this means a new swapchain present mode.
	if (r_vsync->modified) // YQ2
	{
		R_SetVsync();
		r_vsync->modified = false;
	}

	// Rebuild the swapchain if the previous frame requested it
	// (VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR / vsync change).
	if (vk_recreateSwapchainNeeded)
	{
		if (!QVk_RecreateSwapchain())
		{
			vk_frameStarted = false;
			return;
		}
	}

	/* If ri.Sys_Error()/Com_Error() had been issued mid-frame, we might end up
	   here without having properly submitted the image, so call QVk_EndFrame()
	   to be safe (no-op if the last frame ended normally). */
	QVk_EndFrame(true);

	// Acquire the next swapchain image and start the world render pass - all
	// 3D drawing (RI_RenderFrame()) lands in RP_WORLD; the transition to
	// RP_WORLD_WARP + RP_UI happens in R_EndWorldRenderpass().
	if (QVk_BeginFrame(&vk_viewport, &vk_scissor) == VK_SUCCESS)
	{
		QVk_BeginRenderpass(RP_WORLD);

		// Clear screen if desired (gl1 parity: fog-aware - the full-screen
		// viewport set by QVk_BeginFrame() is still active here, so the fog/
		// gl_clear fill quads cover the whole screen like gl1's glClear()).
		R_Clear();
	}
}

// Ends the world render pass: RP_WORLD -> RP_WORLD_WARP (fullscreen underwater
// distortion keyed to cl_camera_under_surface / r_underwater_warp) -> RP_UI.
// Idempotent per frame (world_rendered guard). H2R's API has no
// EndWorldRenderpass export - RI_RenderFrame() calls this internally after the
// 3D flow, and RI_EndFrame() calls it as a fallback on 2D-only frames
// (CONTRACT.md).
qboolean R_EndWorldRenderpass(void)
{
	float dummy[PUSH_CONSTANT_VERTEX_SIZE] = { 0 };

	// still some issues?
	if (!vk_frameStarted)
	{
		// we can't start 2D rendering
		return false;
	}

	// 3D world has already rendered and 2D already initialized
	if (world_rendered)
	{
		return true;
	}

	world_rendered = true;

	// Flush any 2D rects still batched for RP_WORLD (the foundation grey clear).
	QVk_Draw2DCallsRender();

	// finish rendering world view to offscreen buffer
	vkCmdEndRenderPass(vk_activeCmdbuffer);

	// apply postprocessing effects to offscreen buffer: underwater view warp
	// if the player is submerged in liquid (H2: cl_camera_under_surface, this
	// is the underwater screen distortion gl1 never had - CONTRACT.md;
	// strength is cvar-tunable via r_underwater_warp).
	QVk_BeginRenderpass(RP_WORLD_WARP);

	const float underwaterTime = ((int)cl_camera_under_surface->value && r_underwater_warp->value > 0.0f) ? r_newrefdef.time : 0.0f;

	float pushConsts[] =
	{
		underwaterTime,
		r_underwater_warp->value,
		(float)viddef.width,
		(float)viddef.height,
		vk_viewport.x,
		vk_viewport.y,
		(float)r_newrefdef.x,
		(float)r_newrefdef.y,
		(float)r_newrefdef.width,
		(float)r_newrefdef.height,
	};
	vkCmdPushConstants(vk_activeCmdbuffer, vk_worldWarpPipeline.layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(dummy), dummy);
	vkCmdPushConstants(vk_activeCmdbuffer, vk_worldWarpPipeline.layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, PUSH_CONSTANT_VERTEX_SIZE * sizeof(float), sizeof(pushConsts), pushConsts);
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_worldWarpPipeline.layout, 0, 1, &vk_colorbuffer.descriptorSet, 0, NULL);
	QVk_BindPipeline(&vk_worldWarpPipeline);
	// Restore full viewport for future steps.
	vkCmdSetViewport(vk_activeCmdbuffer, 0u, 1u, &vk_viewport);
	vkCmdSetScissor(vk_activeCmdbuffer, 0u, 1u, &vk_scissor);
	vkCmdDraw(vk_activeCmdbuffer, 3, 1, 0, 0);
	vkCmdEndRenderPass(vk_activeCmdbuffer);

	// start drawing UI
	QVk_BeginRenderpass(RP_UI);

	return true;
}

// yq2 R_SetVulkan2D: transition to the UI pass and blit the offscreen world
// view to the swapchain, applying the H2 color grade once for the whole 3D
// view (postprocess.frag).
static void R_SetVulkan2D(const VkViewport* viewport, const VkRect2D* scissor)
{
	// yq2: player configuration screen renders a model using the UI renderpass,
	// so skip finishing RP_WORLD twice.
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
		if (!R_EndWorldRenderpass())
			// buffers are not initialized
			return;

	vkCmdSetViewport(vk_activeCmdbuffer, 0, 1, viewport);
	vkCmdSetScissor(vk_activeCmdbuffer, 0, 1, scissor);

	// First, blit the offscreen color buffer with the warped/postprocessed
	// world view. Skip this step in the NOWORLDMODEL case since it uses RP_UI
	// and draws directly to the swapchain.
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		float pushConsts[] = {
			vk_gradePush[0],		// vid_gamma (H2ColorGrade)
			vk_gradePush[1],		// vid_brightness
			vk_gradePush[2],		// vid_contrast
			(float)viddef.width,	// scrWidth
			(float)viddef.height,	// scrHeight
			vk_viewport.x,			// offsetX
			vk_viewport.y			// offsetY
		};
		vkCmdPushConstants(vk_activeCmdbuffer, vk_postprocessPipeline.layout,
			VK_SHADER_STAGE_FRAGMENT_BIT, PUSH_CONSTANT_VERTEX_SIZE * sizeof(float), sizeof(pushConsts), pushConsts);
		vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_postprocessPipeline.layout, 0, 1, &vk_colorbufferWarp.descriptorSet, 0, NULL);
		QVk_BindPipeline(&vk_postprocessPipeline);
		vkCmdDraw(vk_activeCmdbuffer, 3, 1, 0, 0);
	}
}

// Q2 counterpart
static void R_PolyBlend(void)
{
	if (!(int)gl_polyblend->value || v_blend[3] == 0.0f)
		return;

	// gl1 drew a fixed camera-space GL_QUADS overlay; the vk equivalent is a
	// color quad covering the whole 3D viewport in NDC (the 3D viewport set by
	// R_SetupVulkan() is still active), drawn through the 2D color-quad
	// pipeline (yq2 R_PolyBlend()), graded per-fragment like the rest of RP_WORLD.
	R_DrawWorldColorRect(0.0f, 0.0f, 1.0f, 1.0f, v_blend[0], v_blend[1], v_blend[2], v_blend[3]);
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

	// gl3 refreshed uniCommon.time here; in vk the warp/turb time is written
	// into the per-draw UBOs by the surface module port (r_newrefdef.time).

#ifdef VK_MODULES_READY
	R_PushDlights();
#endif

	// gl1's 'gl_finish' glFinish() dropped (explicit fencing in QVk_BeginFrame()).

	R_SetupFrame();
	R_SetFrustum();
	R_SetupVulkan();

#ifdef VK_MODULES_READY
	R_MarkLeaves(); // Done here so we know if we're in water.
	R_ResetBmodelTransforms(); //mxd
	R_DrawWorld();
	R_DrawEntitiesOnList();
	R_RenderDlights();

	// Changed in H2: (gl1 wrapped these in glDepthMask(GL_FALSE/GL_TRUE) -
	// depth writes are disabled per-pipeline on the vk side).
	R_SortAndDrawAlphaSurfaces();
	R_DrawParticles(r_newrefdef.num_particles, r_newrefdef.particles, false);
	R_DrawParticles(r_newrefdef.anum_particles, r_newrefdef.aparticles, true);
#else
	// FOUNDATION SKELETON: clear the 3D view to dark grey to prove the
	// RP_WORLD -> RP_WORLD_WARP -> RP_UI flow works end to end.
	R_DrawWorldColorRect(0.0f, 0.0f, 1.0f, 1.0f, 0.15f, 0.15f, 0.15f, 1.0f);
#endif

#ifdef _DEBUG
	//R_DrawDebugPrimitives(); //mxd //TODO: enable with the vk_Debug.c module port.
#endif

	// Changed in H2: R_Flash() call replaced with R_PolyBlend() call (or optimization?).
	R_PolyBlend();

#ifdef VK_MODULES_READY
	if ((int)r_speeds->value)
		ri.Con_Printf(PRINT_ALL, "%4i wpoly %4i epoly %i tex %i lmaps\n", c_brush_polys, c_alias_polys, c_visible_textures, c_visible_lightmaps); // H2: ri.Con_Printf -> Com_Printf //mxd. Com_Printf() -> ri.Con_Printf().
#endif

	// gl1's 'gl_reporthash' R_DisplayHashTable() debug dump not ported (gl_reporthash is registered but unused in vk).
}

static void R_SetLightLevel(void)
{
#ifdef VK_MODULES_READY // R_LightPoint() lands with the vk_Light.c module port.
	if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		// Save off light value for server to look at (BIG HACK!).
		vec3_t shadelight;
		R_LightPoint(r_newrefdef.clientmodelorg, shadelight, true); // H2: vieworg -> clientmodelorg

		// Pick the greatest component, which should be the same as the mono value returned by software.
		// Max. shadelight can exceed 1.0 when player is affected by dynamic lights --mxd.
		r_lightlevel->value = max(shadelight[0], max(shadelight[1], shadelight[2])) * 150.0f / gl_modulate->value; //mxd. Undo gl_modulate scaler (to avoid affecting cmd.lightlevel).
	}
#endif
}

static void R_ScreenFlash(const paletteRGBA_t color)
{
	// gl1 wrapped this in glDepthMask(GL_FALSE/GL_TRUE) - the vk color-quad
	// pipeline writes no depth. Runs in RP_UI (after R_SetVulkan2D()), so the
	// flash quad is graded in-shader like all UI-path draws.
	Draw_FadeScreen(color); // STUB until the vk_Draw.c module port lands.

	ri.Deactivate_Screen_Flash();
}

// H2: return type: void -> int //TODO: useless: always returns 0
static int RI_RenderFrame(const refdef_t* fd)
{
	if (!vk_frameStarted)
		return 0;

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

		// gl: R_SetupGL2D(); vk: end the world render pass internally after
		// the 3D flow (H2R's API has no EndWorldRenderpass export -
		// CONTRACT.md), run the underwater warp pass and transition to the UI
		// pass (graded world blit). All subsequent 2D lands in RP_UI.
		R_SetVulkan2D(&vk_viewport, &vk_scissor);

#ifdef _DEBUG
		//R_DrawDebugLabels(); //mxd //TODO: enable with the vk_Debug.c module port.
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
	//mxd. Debug draw logic (all stubs until a vk_Debug.c port).
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
