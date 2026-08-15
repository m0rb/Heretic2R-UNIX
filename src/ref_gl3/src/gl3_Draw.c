#include "compat.h"
//
// gl3_Draw.c -- 2D/UI drawing for the OpenGL 3.2 core renderer.
//
// Ported from gl1_Draw.c (semantic authority); backend technique from yq2
// gl3_draw.c (small streaming quad VBO + si2D/si2Dcolor programs).
// Foundation deviations honored (CONTRACT.md / gl3_Shaders.c):
//  - colors/alpha are PER-VERTEX attributes (uniCommon carries no color);
//  - gl1's glEnable(GL_ALPHA_TEST) + glAlphaFunc(GL_GREATER, 0.05) + GL_MODULATE
//    live in the 2D fragment shader (texel * vertColor, discard when a <= 0.05);
//  - gamma/brightness/contrast grading runs at the end of the fragment shaders.
//
// Copyright 1998 Raven Software
//

#include "gl3_Draw_internal.h"
#include "client/vid.h"

#include <assert.h>
#include <stddef.h> // offsetof

// NOTE: unlike gl1_Draw.c, the shared image pointers (r_notexture, r_font1, ...)
// are DEFINED by the image module (gl3_Stubs.c until gl3_Image.c lands) - this
// file only assigns them in Draw_InitLocal().
image_t* draw_chars;

//mxd. Each font contains 224 char definitions.
glxy_t* font1; // H2
glxy_t* font2; // H2

#pragma region ========================== QUAD BACKEND (GL3, two-dee drawing) ==========================

// Streaming quad buffers for all 2D drawing (YQ2 gl3_draw.c technique).
// One VAO is enough for both 2D programs: attribute locations are shared
// (gl3_Shaders.c glBindAttribLocation) and si2Dcolor simply ignores texCoord.
static GLuint vao2D;
static GLuint vbo2D;

static void GL3_Draw_EnsureBuffers(void)
{
	if (vao2D != 0)
		return;

	glGenVertexArrays(1, &vao2D);
	GL3_BindVAO(vao2D);

	glGenBuffers(1, &vbo2D);
	GL3_BindVBO(vbo2D);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_2D_vtx_t), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_2D_vtx_t), offsetof(gl3_2D_vtx_t, texCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(gl3_2D_vtx_t), offsetof(gl3_2D_vtx_t, color));
}

void GL3_Draw_EmitQuad(const float xl, const float yt, const float xr, const float yb, const float sl, const float tl, const float sh, const float th, const float color[4])
{
	const gl3_2D_vtx_t verts[4] = // Triangle strip (YQ2 drawTexturedRectangle() vertex order).
	{
		{ { xl, yb }, { sl, th }, { color[0], color[1], color[2], color[3] } },
		{ { xl, yt }, { sl, tl }, { color[0], color[1], color[2], color[3] } },
		{ { xr, yb }, { sh, th }, { color[0], color[1], color[2], color[3] } },
		{ { xr, yt }, { sh, tl }, { color[0], color[1], color[2], color[3] } },
	};

	GL3_Draw_EnsureBuffers();

	GL3_BindVAO(vao2D);
	GL3_BindVBO(vbo2D); // YQ2: binding the VAO does NOT implicitly bind the VBO.
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// paletteRGBA_t (bytes) -> per-vertex float color (gl1 glColor4ub* equivalence).
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
	// (Re-)create the streaming 2D quad buffers. Old handles are just dropped:
	// RI_Init() always runs on a fresh GL context (old objects died with it).
	vao2D = 0;
	vbo2D = 0;
	GL3_Draw_EnsureBuffers();

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
	PaletteColorToFloat(color, vcolor); // gl1 glColor4ub() + GL_MODULATE -> per-vertex color.

	GL3_UseProgram(gl3state.si2D.shaderProgram);
	R_BindImage(draw_chars);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR
	// gl1 R_AlphaFunc(GL_GREATER, 0.05f) is hardcoded in the 2D fragment shader.

	GL3_Draw_EmitQuad((float)x, (float)y, (float)(x + char_size), (float)(y + char_size),
		fcol, frow, fcol + CELL_SIZE, frow + CELL_SIZE, vcolor);

	glDisable(GL_BLEND);
}

void Draw_Char(const int x, const int y, const int scale, const int c, const paletteRGBA_t color, const qboolean draw_shadow)
{
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

	GL3_UseProgram(gl3state.si2D.shaderProgram);
	R_BindImage(image);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// gl1 R_AlphaFunc(GL_GREATER, 0.05f) is hardcoded in the 2D fragment shader.

	GL3_Draw_EmitQuad((float)x, (float)y, (float)(x + w), (float)(y + h), 0.0f, 0.0f, 1.0f, 1.0f, vcolor);

	glDisable(GL_BLEND);
}

void Draw_StretchPic(int x, int y, int w, int h, const char* name, const float alpha, const DrawStretchPicScaleMode_t mode)
{
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
	const image_t* pic = Draw_FindPic(name);
	Draw_Render(x, y, pic->width * scale, pic->height * scale, pic, alpha);
}

//mxd. Used in SCR_TileClear frame border drawing logic. //TODO: remove?
void Draw_TileClear(const int x, const int y, const int w, const int h, const char* pic)
{
	const image_t* image = Draw_FindPic(pic);

	//mxd. Skip gl_alphatest_broken cvar logic.
	// NOTE: gl1 drew this with the ambient GL_REPLACE texenv + 0.666 alpha test;
	// the 2D shader modulates by white (== replace) and discards a <= 0.05 -
	// identical for the opaque tile pics this is used with. No blending (gl1 parity).
	const float vcolor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	//mxd. Divided by 64 in Q2.
	const float sl = (float)x / 128.0f;
	const float sr = (float)(x + w) / 128.0f;
	const float tt = (float)y / 128.0f;
	const float tb = (float)(y + h) / 128.0f;

	GL3_UseProgram(gl3state.si2D.shaderProgram);
	R_BindImage(image);

	GL3_Draw_EmitQuad((float)x, (float)y, (float)(x + w), (float)(y + h), sl, tt, sr, tb, vcolor);
}

// Fills a box of pixels with a single color.
void Draw_Fill(const int x, const int y, const int w, const int h, const paletteRGBA_t color)
{
	assert(color.a > 0); //mxd

	float vcolor[4];
	PaletteColorToFloat(color, vcolor); //mxd. qglColor4f -> qglColor4ubv (gl1); H2: color components divided by 256.0

	if (color.a < 255) //mxd. Added transparency support.
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	// gl1 glDisable(GL_TEXTURE_2D) -> color-only 2D program.
	GL3_UseProgram(gl3state.si2Dcolor.shaderProgram);
	GL3_Draw_EmitQuad((float)x, (float)y, (float)(x + w), (float)(y + h), 0.0f, 0.0f, 0.0f, 0.0f, vcolor);

	if (color.a < 255) //mxd. Added transparency support.
		glDisable(GL_BLEND);
}

void Draw_FadeScreen(const paletteRGBA_t color)
{
	float vcolor[4];
	PaletteColorToFloat(color, vcolor);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR
	// NOTE: gl1 also enabled alpha test (GL_GREATER, 0.05) on this untextured quad;
	// si2Dcolor has no alpha test - only differs for color.a <= 12 (invisible either way).

	// gl1 glDisable(GL_TEXTURE_2D) -> color-only 2D program.
	GL3_UseProgram(gl3state.si2Dcolor.shaderProgram);

	const int x = r_newrefdef.x;
	const int y = viddef.height - r_newrefdef.y - r_newrefdef.height;
	const int w = r_newrefdef.width;
	const int h = r_newrefdef.height;

	GL3_Draw_EmitQuad((float)x, (float)y, (float)(x + w), (float)(y + h), 0.0f, 0.0f, 0.0f, 0.0f, vcolor);

	glDisable(GL_BLEND);
}

//mxd. Map object coordinates to window coordinates (slightly modified version of glhProjectf() from https://wikis.khronos.org/opengl/GluProject_and_gluUnProject_code).
// Ported from gl1_Misc.c. gl1 read r_world_matrix / r_projection_matrix back from GL after R_SetupGL;
// gl3 uses the matrices captured in gl3state by R_SetupGL3D() (same values, same column-major layout).
// Kept static to avoid clashing with a future gl3_Misc.c port of the same function.
static qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos)
{
	const float* r_world_matrix = (const float*)gl3state.viewMat3D.Elements;
	const float* r_projection_matrix = (const float*)gl3state.projMat3D.Elements;

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
