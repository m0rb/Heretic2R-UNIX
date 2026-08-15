#include "compat.h"
//
// vk_Draw.c -- 2D/UI drawing for the Vulkan renderer.
//
// Ported from gl3_Draw.c (the validated H2 port of gl1_Draw.c - CPU-side logic
// kept nearly verbatim); draw submission goes through the batched 2D rect
// renderer in vk_common.c (QVk_DrawTexRect / QVk_DrawTexRectTinted /
// QVk_DrawColorRect, yq2remaster vk_draw.c technique) instead of the gl3
// streaming quad VBO. Backend deviations honored:
//  - the batcher expects x/y/w/h normalized to the 0..1 screen range
//    (yq2: pixel coords divided by vid.width/height);
//  - gl3's per-vertex colors become the tinted 2D pipeline's per-batch tint
//    (basic_tinted.vert UBO tintColor); plain white/no-blend draws
//    (Draw_TileClear) use the untinted pipeline;
//  - gl1's glEnable(GL_BLEND) state is baked into the pipelines:
//    tinted/color-quad blend, untinted textured quad does not (gl1 parity);
//  - gl1 R_AlphaFunc(GL_GREATER, 0.05) and the H2ColorGrade trio are push
//    constants applied by QVk_Draw2DCallsRender() (basic.frag semantics);
//  - H2R's API has no EndWorldRenderpass export, so every 2D entry point
//    forces the RP_WORLD -> RP_WORLD_WARP -> RP_UI transition first
//    (Draw_Begin2D() -> R_EndWorldRenderpass(), idempotent) - required for
//    frames where RI_RenderFrame() never runs (cinematics, console/menu-only,
//    loading screens). Draw_TileClear is the one exception: SCR_TileClear()
//    legitimately runs BEFORE V_RenderView()/RI_RenderFrame() and must draw
//    into the still-open world pass (gl1 drew it to the same framebuffer).
//
// Copyright 1998 Raven Software
//

#include "vk_Draw_internal.h"
#include "client/vid.h"

#include <assert.h>

// NOTE: unlike gl1_Draw.c, the shared image pointers (r_notexture, r_font1, ...)
// are DEFINED by vk_Stubs.c (vk_Image.c once the module ports are integrated) -
// this file only assigns them in Draw_InitLocal().
image_t* draw_chars;

//mxd. Each font contains 224 char definitions.
glxy_t* font1; // H2
glxy_t* font2; // H2

#pragma region ========================== QUAD BACKEND (VK, two-dee drawing) ==========================

// See vk_Draw_internal.h. Kept as a helper (instead of calling
// R_EndWorldRenderpass() inline) so the ordering rules are documented once.
qboolean Draw_Begin2D(void)
{
	return R_EndWorldRenderpass();
}

// gl3 GL3_Draw_EmitQuad() equivalent: batches one textured quad through the
// tinted 2D pipeline (blend + 0.05 alpha test + H2ColorGrade, per-batch tint).
// Corners in pixels, UVs normalized; the batcher wants origin + size in the
// 0..1 screen range.
void Draw_EmitQuad(const float xl, const float yt, const float xr, const float yb,
	const float sl, const float tl, const float sh, const float th,
	const float color[4], const image_t* image)
{
	const float inv_w = 1.0f / (float)viddef.width;
	const float inv_h = 1.0f / (float)viddef.height;

	QVk_DrawTexRectTinted(xl * inv_w, yt * inv_h, (xr - xl) * inv_w, (yb - yt) * inv_h,
		sl, tl, sh - sl, th - tl,
		color[0], color[1], color[2], color[3],
		&image->vk_texture);
}

// paletteRGBA_t (bytes) -> float color (gl1 glColor4ub* equivalence).
static void PaletteColorToFloat(const paletteRGBA_t color, float* out)
{
	out[0] = (float)color.r / 255.0f;
	out[1] = (float)color.g / 255.0f;
	out[2] = (float)color.b / 255.0f;
	out[3] = (float)color.a / 255.0f;
}

#pragma endregion

static void InitFonts(void) // H2
{
	ri.FS_LoadFile("pics/misc/font1.fnt", (void**)&font1);
	ri.FS_LoadFile("pics/misc/font2.fnt", (void**)&font2);
}

void ShutdownFonts(void) // H2
{
	if (font1 != NULL)
	{
		ri.FS_FreeFile(font1);
		font1 = NULL;
	}

	if (font2 != NULL)
	{
		ri.FS_FreeFile(font2);
		font2 = NULL;
	}
}

image_t* Draw_FindPic(const char* name)
{
	if (name[0] != '/' && name[0] != '\\')
	{
		char fullname[MAX_QPATH];
		Com_sprintf(fullname, sizeof(fullname), "pics/%s", name); // Q2: pics/%s.pcx

		return R_FindImage(fullname, it_pic);
	}

	return R_FindImage(name + 1, it_pic);
}

static image_t* Draw_FindPicFilter(const char* name) // H2 //TODO: same as Draw_FindPic(), except for 'it_sky' image type...
{
	if (name[0] != '/' && name[0] != '\\')
	{
		char fullname[MAX_QPATH];
		Com_sprintf(fullname, sizeof(fullname), "pics/%s", name);

		return R_FindImage(fullname, it_sky);
	}

	return R_FindImage(name + 1, it_sky);
}

void Draw_InitLocal(void)
{
	// NOTE: no buffer setup here (gl3 created its streaming quad VAO/VBO) - the
	// batched 2D rect renderer and its buffers live in the QVk core (vk_common.c).

	r_notexture = NULL;
	r_notexture = R_FindImage("textures/general/notex.m8", it_wall);
	if (r_notexture == NULL)
	{
		// Create a fallback texture if the file is not found (for demo assets)
		ri.Con_Printf(PRINT_ALL, "Draw_InitLocal: could not find textures/general/notex.m8, creating fallback texture\n");
		r_notexture = R_CreateFallbackTexture("*notex_fallback", it_wall);
	}

	draw_chars = Draw_FindPic("misc/conchars.m32");
	r_particletexture = Draw_FindPicFilter("misc/particle.m32");
	r_aparticletexture = Draw_FindPicFilter("misc/aparticle.m8");
	r_font1 = Draw_FindPic("misc/font1.m32");
	r_font2 = Draw_FindPic("misc/font2.m32");
	r_reflecttexture = Draw_FindPicFilter("misc/reflect.m32");

	InitFonts();
}

// Draws one 8*8 graphics character with 0 being transparent.
// It can be clipped to the top of the screen to allow the console to be smoothly scrolled off.
static void Draw_Char_impl(const int x, const int y, const int scale, int c, const paletteRGBA_t color)
{
#define CELL_SIZE	0.0625f // 16 chars per row/column (0.0625 == 1 / 16).

	c &= 255;

	const int char_size = CONCHAR_SIZE * scale; //mxd

	// Skip when whitespace char or totally off-screen.
	if ((c & 127) == 32 || y <= -char_size)
		return;

	const float frow = (float)(c >> 4) * CELL_SIZE;
	const float fcol = (float)(c & 15) * CELL_SIZE;

	float vcolor[4];
	PaletteColorToFloat(color, vcolor); // gl1 glColor4ub() + GL_MODULATE -> tinted pipeline tint.

	// gl1 glEnable(GL_BLEND) + glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
	// (H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR) is baked into the tinted
	// 2D pipeline; gl1 R_AlphaFunc(GL_GREATER, 0.05f) is the batcher's
	// alphaTestRef push constant (basic.frag).
	Draw_EmitQuad((float)x, (float)y, (float)(x + char_size), (float)(y + char_size),
		fcol, frow, fcol + CELL_SIZE, frow + CELL_SIZE, vcolor, draw_chars);
}

void Draw_Char(const int x, const int y, const int scale, const int c, const paletteRGBA_t color, const qboolean draw_shadow)
{
	if (draw_chars == NULL || !Draw_Begin2D())
		return;

	if (draw_shadow)
	{
		const paletteRGBA_t shade_color = { .r = 32, .g = 32, .b = 32, .a = (byte)((float)color.a * 0.75f) }; //mxd
		Draw_Char_impl(x + scale, y + scale, scale, c, shade_color);
	}

	Draw_Char_impl(x, y, scale, c, color);
}

void Draw_GetPicSize(int* w, int* h, const char* name)
{
	const image_t* image = R_FindImage(name, it_pic);

	if (image != r_notexture)
	{
		*w = image->width;
		*h = image->height;
	}
	else
	{
		*w = 0; // Q2: -1
		*h = 0; // Q2: -1
	}
}

void Draw_Render(const int x, const int y, const int w, const int h, const image_t* image, const float alpha)
{
	const float vcolor[4] = { 1.0f, 1.0f, 1.0f, alpha }; // gl1 glColor4f(1, 1, 1, alpha) + GL_MODULATE.

	// gl1 blend enable + R_AlphaFunc(GL_GREATER, 0.05f): tinted 2D pipeline + batcher push constants.
	Draw_EmitQuad((float)x, (float)y, (float)(x + w), (float)(y + h), 0.0f, 0.0f, 1.0f, 1.0f, vcolor, image);
}

void Draw_StretchPic(int x, int y, int w, int h, const char* name, const float alpha, const DrawStretchPicScaleMode_t mode)
{
	if (!Draw_Begin2D())
		return;

	const image_t* image = Draw_FindPicFilter(name);

	switch (mode)
	{
		case DSP_SCALE_SCREEN:
		{
			const int xr = x + w;
			const int yb = y + h;

			x = viddef.width * x / DEF_WIDTH;
			y = viddef.height * y / DEF_HEIGHT;
			w = viddef.width * xr / DEF_WIDTH - x;
			h = viddef.height * yb / DEF_HEIGHT - y;
		} break;

		case DSP_SCALE_4x3:
		{
			const int xr = x + w;
			const int yb = y + h;

			const int screen_width = viddef.height * 4 / 3;
			const int screen_offset_x = (viddef.width - screen_width) / 2;

			x = (screen_width * x / DEF_WIDTH) + screen_offset_x;
			y = viddef.height * y / DEF_HEIGHT;
			w = (screen_width * xr / DEF_WIDTH - x) + screen_offset_x;
			h = viddef.height * yb / DEF_HEIGHT - y;
		} break;

		default: // DSP_NONE.
			break;
	}

	Draw_Render(x, y, w, h, image, alpha);
}

void Draw_Pic(const int x, const int y, const int scale, const char* name, const float alpha) //mxd. +scale arg.
{
	if (!Draw_Begin2D())
		return;

	const image_t* pic = Draw_FindPic(name);
	Draw_Render(x, y, pic->width * scale, pic->height * scale, pic, alpha);
}

//mxd. Used in SCR_TileClear frame border drawing logic. //TODO: remove?
void Draw_TileClear(const int x, const int y, const int w, const int h, const char* pic)
{
	// NOTE: no Draw_Begin2D() here! SCR_TileClear() runs BEFORE
	// V_RenderView()/RI_RenderFrame(), so this must draw into the CURRENT pass
	// (the still-open RP_WORLD - gl1 drew the border tiles to the same
	// framebuffer the 3D view rendered into; the postprocess blit carries them
	// to the swapchain).
	if (!vk_frameStarted)
		return;

	const image_t* image = Draw_FindPic(pic);

	//mxd. Skip gl_alphatest_broken cvar logic.
	// NOTE: gl1 drew this with the ambient GL_REPLACE texenv + 0.666 alpha test
	// and NO blending -> untinted 2D pipeline (blend disabled); the batcher's
	// 0.05 alpha test is identical for the opaque tile pics this is used with
	// (gl3 parity).

	//mxd. Divided by 64 in Q2.
	const float sl = (float)x / 128.0f;
	const float sr = (float)(x + w) / 128.0f;
	const float tt = (float)y / 128.0f;
	const float tb = (float)(y + h) / 128.0f;

	const float inv_w = 1.0f / (float)viddef.width;
	const float inv_h = 1.0f / (float)viddef.height;

	QVk_DrawTexRect((float)x * inv_w, (float)y * inv_h, (float)w * inv_w, (float)h * inv_h,
		sl, tt, sr - sl, tb - tt, &image->vk_texture);
}

// Fills a box of pixels with a single color.
void Draw_Fill(const int x, const int y, const int w, const int h, const paletteRGBA_t color)
{
	assert(color.a > 0); //mxd

	if (!Draw_Begin2D())
		return;

	float vcolor[4];
	PaletteColorToFloat(color, vcolor); //mxd. qglColor4f -> qglColor4ubv (gl1); H2: color components divided by 256.0

	// gl1 glDisable(GL_TEXTURE_2D) -> color-only 2D pipeline. gl3 enabled
	// blending only when color.a < 255; the color quad pipeline always blends -
	// identical output for opaque colors (src alpha 1.0).
	const float inv_w = 1.0f / (float)viddef.width;
	const float inv_h = 1.0f / (float)viddef.height;

	QVk_DrawColorRect((float)x * inv_w, (float)y * inv_h, (float)w * inv_w, (float)h * inv_h,
		vcolor[0], vcolor[1], vcolor[2], vcolor[3], vk_state.current_renderpass);
}

void Draw_FadeScreen(const paletteRGBA_t color)
{
	if (!Draw_Begin2D())
		return;

	float vcolor[4];
	PaletteColorToFloat(color, vcolor);

	// gl1 blend enable (H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR) is baked
	// into the color quad pipeline.
	// NOTE: gl1 also enabled alpha test (GL_GREATER, 0.05) on this untextured quad;
	// the color quad path has no alpha test - only differs for color.a <= 12
	// (invisible either way) (gl3 parity).

	const int x = r_newrefdef.x;
	const int y = viddef.height - r_newrefdef.y - r_newrefdef.height;
	const int w = r_newrefdef.width;
	const int h = r_newrefdef.height;

	const float inv_w = 1.0f / (float)viddef.width;
	const float inv_h = 1.0f / (float)viddef.height;

	QVk_DrawColorRect((float)x * inv_w, (float)y * inv_h, (float)w * inv_w, (float)h * inv_h,
		vcolor[0], vcolor[1], vcolor[2], vcolor[3], vk_state.current_renderpass);
}

#pragma region ========================== DRAW NAME ==========================

// gl1 read r_world_matrix / r_projection_matrix back from GL after R_SetupGL;
// gl3 used the copies captured in gl3state by R_SetupGL3D(). The vk foundation
// skeleton captures no frame matrices yet, so recompute them here from
// r_newrefdef with the exact R_SetupGL3D()/R_SetPerspective() math from
// gl3_Main.c (GL conventions, column-major - R_PointToScreen()'s window
// mapping below assumes GL clip space, NOT Vulkan's).
// TODO(integration): switch to the frame matrices captured by the vk_Main.c
// 3D flow once the frame module port lands (mind the Vulkan clip-space
// differences if those are Vulkan-convention matrices).

// Column-major 4x4 multiply (GL layout: element (row, col) at [col * 4 + row]).
static void MatrixMultiply4(float out[16], const float a[16], const float b[16])
{
	for (int col = 0; col < 4; col++)
	{
		for (int row = 0; row < 4; row++)
		{
			out[col * 4 + row] =
				a[0 * 4 + row] * b[col * 4 + 0] +
				a[1 * 4 + row] * b[col * 4 + 1] +
				a[2 * 4 + row] * b[col * 4 + 2] +
				a[3 * 4 + row] * b[col * 4 + 3];
		}
	}
}

// gluPerspective-style projection matrix; gl1 R_SetPerspective() parameters
// (zNear 1.0, zFar r_farclipdist) on yq2 GL3_SetPerspective() matrix math
// (gl3_Main.c R_SetPerspective()).
static void SetPerspectiveMatrix(const double fovy, float proj[16])
{
	static const double z_near = 1.0; // Q2: 4.0
	const double z_far = (double)r_farclipdist->value;
	const double aspect_ratio = (double)r_newrefdef.width / (double)r_newrefdef.height;

	// Traditional gluPerspective calculations.
	const double top = z_near * tan(fovy * M_PI / 360.0);
	const double right = top * aspect_ratio;

	const double bottom = -top;
	const double left = -right;

	// The following emulates glFrustum(left, right, bottom, top, zNear, zFar).
	const float a = (float)((right + left) / (right - left));
	const float b = (float)((top + bottom) / (top - bottom));
	const float c = (float)(-(z_far + z_near) / (z_far - z_near));
	const float d = (float)(-(2.0 * z_far * z_near) / (z_far - z_near));

	memset(proj, 0, sizeof(float) * 16);
	proj[0] = (float)(2.0 * z_near / (right - left));
	proj[5] = (float)(2.0 * z_near / (top - bottom));
	proj[8] = a;
	proj[9] = b;
	proj[10] = c;
	proj[11] = -1.0f;
	proj[14] = d;
}

// Equivalent to R_x * R_y * R_z where R_x is the rotation matrix around the X axis for aroundXdeg. // YQ2
// (gl3_Main.c rotAroundAxisXYZ(), hmm_mat4 -> column-major float[16].)
static void RotAroundAxisXYZ(const float around_x_deg, const float around_y_deg, const float around_z_deg, float rot[16])
{
	const float alpha = around_x_deg * (float)(M_PI / 180.0);
	const float beta = around_y_deg * (float)(M_PI / 180.0);
	const float gamma = around_z_deg * (float)(M_PI / 180.0);

	const float sin_a = sinf(alpha);
	const float cos_a = cosf(alpha);
	const float sin_b = sinf(beta);
	const float cos_b = cosf(beta);
	const float sin_g = sinf(gamma);
	const float cos_g = cosf(gamma);

	const float m[16] = // Column-major.
	{
		 cos_b * cos_g,  sin_a * sin_b * cos_g + cos_a * sin_g, -cos_a * sin_b * cos_g + sin_a * sin_g, 0.0f, // First *column*.
		-cos_b * sin_g, -sin_a * sin_b * sin_g + cos_a * cos_g,  cos_a * sin_b * sin_g + sin_a * cos_g, 0.0f,
		 sin_b,         -sin_a * cos_b,                          cos_a * cos_b,                         0.0f,
		 0.0f,           0.0f,                                   0.0f,                                  1.0f
	};

	memcpy(rot, m, sizeof(m));
}

// Recomputes this frame's view + projection matrices from r_newrefdef
// (gl3_Main.c R_SetupGL3D() view matrix setup, verbatim math).
static qboolean ComputeFrameMatrices(float view[16], float proj[16])
{
	if (r_newrefdef.width <= 0 || r_newrefdef.height <= 0)
		return false; // No 3D view rendered this frame.

	// Set up projection matrix.
	SetPerspectiveMatrix((double)r_newrefdef.fov_y, proj);

	// Set up view matrix (world coordinates -> eye coordinates). // YQ2 SetupGL()
	// First put Z axis going up.
	static const float base[16] = // Column-major.
	{
		 0.0f, 0.0f, -1.0f, 0.0f, // First *column*.
		-1.0f, 0.0f,  0.0f, 0.0f,
		 0.0f, 1.0f,  0.0f, 0.0f,
		 0.0f, 0.0f,  0.0f, 1.0f
	};

	// Now rotate by view angles.
	float rot[16];
	RotAroundAxisXYZ(-r_newrefdef.viewangles[2], -r_newrefdef.viewangles[0], -r_newrefdef.viewangles[1], rot);

	float tmp[16];
	MatrixMultiply4(tmp, base, rot);

	// .. and apply translation for current position.
	float trans[16] =
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		-r_newrefdef.vieworg[0], -r_newrefdef.vieworg[1], -r_newrefdef.vieworg[2], 1.0f
	};

	MatrixMultiply4(view, tmp, trans);

	return true;
}

//mxd. Map object coordinates to window coordinates (slightly modified version of glhProjectf() from https://wikis.khronos.org/opengl/GluProject_and_gluUnProject_code).
// Ported from gl1_Misc.c via gl3_Draw.c (same values, same column-major layout).
// Kept static to avoid clashing with a future vk_Misc.c port of the same function.
static qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos)
{
	float view_matrix[16];
	float projection_matrix[16];

	if (!ComputeFrameMatrices(view_matrix, projection_matrix))
		return false;

	const float* r_world_matrix = view_matrix;
	const float* r_projection_matrix = projection_matrix;

	// Transformation vectors.
	float tmp[8];

	// Modelview transform.
	tmp[0] = r_world_matrix[0] * pos[0] + r_world_matrix[4] * pos[1] + r_world_matrix[8] *  pos[2] + r_world_matrix[12]; // w is always 1.
	tmp[1] = r_world_matrix[1] * pos[0] + r_world_matrix[5] * pos[1] + r_world_matrix[9] *  pos[2] + r_world_matrix[13];
	tmp[2] = r_world_matrix[2] * pos[0] + r_world_matrix[6] * pos[1] + r_world_matrix[10] * pos[2] + r_world_matrix[14];
	tmp[3] = r_world_matrix[3] * pos[0] + r_world_matrix[7] * pos[1] + r_world_matrix[11] * pos[2] + r_world_matrix[15];

	// Projection transform, the final row of projection matrix is always [0 0 -1 0], so we optimize for that.
	tmp[4] = r_projection_matrix[0] * tmp[0] + r_projection_matrix[4] * tmp[1] + r_projection_matrix[8] *  tmp[2] + r_projection_matrix[12] * tmp[3];
	tmp[5] = r_projection_matrix[1] * tmp[0] + r_projection_matrix[5] * tmp[1] + r_projection_matrix[9] *  tmp[2] + r_projection_matrix[13] * tmp[3];
	tmp[6] = r_projection_matrix[2] * tmp[0] + r_projection_matrix[6] * tmp[1] + r_projection_matrix[10] * tmp[2] + r_projection_matrix[14] * tmp[3];

	// The result normalizes between -1 and 1.
	if (tmp[2] == 0.0f) // The w value.
		return false;

	tmp[7] = 1.0f / -tmp[2];

	// Perspective division.
	tmp[4] *= tmp[7];
	tmp[5] *= tmp[7];
	tmp[6] *= tmp[7];

	// Window coordinates. Map x, y to range 0 - 1.
	screen_pos[0] = (tmp[4] * 0.5f + 0.5f) * (float)r_newrefdef.width +  (float)r_newrefdef.x;
	screen_pos[1] = (tmp[5] * 0.5f + 0.5f) * (float)r_newrefdef.height + (float)r_newrefdef.y;
	screen_pos[2] = (1.0f + tmp[6]) * 0.5f; // This is only correct when glDepthRange(0.0, 1.0).

	//mxd. y-coord needs flipping...
	screen_pos[1] = (float)r_newrefdef.height - screen_pos[1];

	return true;
}

void Draw_Name(const vec3_t origin, const char* name, const paletteRGBA_t color) //mxd. Rewritten logic to use R_PointToScreen() (fixes somewhat incorrect name positioning in widescreen).
{
	if (!Draw_Begin2D())
		return;

	vec3_t screen_pos;
	if (!R_PointToScreen(origin, screen_pos) || screen_pos[2] <= 0.0f || screen_pos[2] > 1.0f)
		return; // Can't project or not within frustum.

	// Replicate SCR_UpdateUIScale() logic...
	const int ui_scale = min((int)(roundf((float)viddef.width / DEF_WIDTH)), (int)(roundf((float)viddef.height / DEF_HEIGHT)));
	const int ui_char_size = CONCHAR_SIZE * ui_scale;

	// Setup label coords.
	const int len = (int)strlen(name);
	const int ui_len = len * ui_char_size;

	const int sx = (int)screen_pos[0] - ui_len / 2;
	const int ex = (int)screen_pos[0] + ui_len / 2;
	const int sy = (int)screen_pos[1] - ui_char_size / 2;
	const int ey = (int)screen_pos[1] + ui_char_size / 2;

	// Not on screen.
	if (sx >= viddef.width || ex <= 0 || sy >= viddef.height || ey <= 0)
		return;

	// Draw label.
	int x = sx;
	for (int i = 0; i < len; i++, x += ui_char_size)
		Draw_Char(x, sy, ui_scale, name[i], color, true);
}

#pragma endregion
