#include "compat.h"
//
// gl3_DrawBook.c -- book pages (mod_book) + big font drawing for the OpenGL 3.2 core renderer.
//
// Ported from gl1_DrawBook.c (semantic authority). All quads go through the
// 2D pipeline (si2D program, per-vertex color, in-shader 0.05 alpha test).
//
// Copyright 1998 Raven Software
//

#include "gl3_Draw_internal.h"
// NOTE(integration): model_t (mod_book layout: extradata + skins) comes from
// gl1_Model.h for now - switch this single include to gl3_Model.h when the
// gl3_Model.c module port lands (it must keep the gl1 model_t member layout,
// or provide its own header).
#include "gl1_Model.h"
#include "qfiles.h"
#include "client/vid.h"

//mxd. Not part of original logic.
static const glxy_t* GetCharDef(const byte c, const glxy_t* font)
{
	const glxy_t* char_def = &font[c - 32];
	return ((char_def->w == 0) ? &font[14] : char_def); // Return dot char when w == 0? //TODO: is this ever triggered?
}

//mxd. Not part of original logic.
static void DrawBigFontChar(const int x, int y, const int offset_x, const int offset_y, const int width, const int height, const glxy_t* char_def, const float* color)
{
	y -= char_def->baseline;

	const int xl = (width * x / DEF_WIDTH) + offset_x;
	const int xr = (width * (char_def->w + x) / DEF_WIDTH) + offset_x;
	const int yt = (height * y / DEF_HEIGHT) + offset_y;
	const int yb = (height * (char_def->h + y) / DEF_HEIGHT) + offset_y;

	GL3_Draw_EmitQuad((float)xl, (float)yt, (float)xr, (float)yb, char_def->xl, char_def->yt, char_def->xr, char_def->yb, color);
}

void Draw_BigFont(const int x, const int y, const char* text, const float alpha)
{
	if (font1 == NULL || font2 == NULL)
		return;

	const float color[4] = { 1.0f, 1.0f, 1.0f, alpha }; // gl1 glColor4f(1, 1, 1, alpha) + GL_MODULATE.

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// gl1 R_AlphaFunc(GL_GREATER, 0.05f) is hardcoded in the 2D fragment shader.

	GL3_UseProgram(gl3state.si2D.shaderProgram);

	const glxy_t* cur_font = font1;
	R_BindImage(r_font1);

	int ox = x;
	int oy = y;

	int vid_w = viddef.width;
	int vid_h = viddef.height;

	int offset_x = 0;
	int offset_y = 0;

	if ((float)vid_w * 0.75f > (float)vid_h) //mxd. Setup for widescreen aspect ratio.
	{
		vid_w = vid_h * 4 / 3;
		offset_x = (viddef.width - vid_w) / 2;
	}

	while (true)
	{
		const byte c = *text++;

		if (c == 0)
			break;

		switch (c)
		{
			case 1: //TODO: unused?
				ox = (DEF_WIDTH - BF_Strlen(text)) / 2; //mxd. viddef.width instead of DEF_WIDTH in original logic.
				break;

			case 2:
				cur_font = font1;
				R_BindImage(r_font1);
				break;
			case 3:
				cur_font = font2;
				R_BindImage(r_font2);
				break;

			case '\t':
				ox += 63;
				break;

			case '\n':
				oy += 18;
				ox = x;
				break;

			case '\r':
				break;

			case 32: // Whitespace char.
				ox += 8;
				break;

			default:
				if (c > 32)
				{
					const glxy_t* char_def = GetCharDef(c, cur_font);
					DrawBigFontChar(ox, oy, offset_x, offset_y, vid_w, vid_h, char_def, color); //mxd. Logic split into helper function.
					ox += char_def->w;
				}
				break;
		}
	}

	glDisable(GL_BLEND);
}

// BigFont_Strlen. Returns width of given text in pixels.
int BF_Strlen(const char* text)
{
	if (font1 == NULL || font2 == NULL)
		return 0;

	int width = 0;
	const glxy_t* cur_font = font1;

	while (true)
	{
		const byte c = *text++;

		switch (c)
		{
			case 0:
			case 1:
			case '\t':
			case '\n':
				return width;

			case 2:
				cur_font = font1;
				break;

			case 3:
				cur_font = font2;
				break;

			case '\r':
				break;

			case 32: // Whitespace char.
				width += 8;
				break;

			default:
				if (c > 32) // When printable char.
				{
					const glxy_t* char_def = GetCharDef(c, cur_font);
					width += char_def->w;
				}
				break;
		}
	}
}

void Draw_BookPic(const char* name, const float scale, const float alpha) //mxd. +alpha arg.
{
	const model_t* mod = RI_RegisterModel(name);

	if (mod == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "Draw_BookPic: can't find book '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return;
	}

	book_t* book = mod->extradata;

	const float header_w = (float)book->bheader.total_w;
	const float header_h = (float)book->bheader.total_h;

	float vid_w = (float)viddef.width;
	float vid_h = (float)viddef.height;

	int offset_x = 0;
	int offset_y = 0;

	if (vid_w * 0.75f > vid_h) //mxd. Setup for widescreen aspect ratio.
	{
		vid_w = vid_h * 4 / 3;
		offset_x = (viddef.width - (int)vid_w) / 2;
	}

	offset_x += (const int)((header_w - header_w * scale) * 0.5f * (vid_w / header_w));
	offset_y += (const int)((header_h - header_h * scale) * 0.5f * (vid_h / header_h));

	bookframe_t* bframe = &book->bframes[0];
	for (int i = 0; i < book->bheader.num_segments; i++, bframe++)
	{
		const int pic_x = (const int)floorf((float)bframe->x * vid_w / header_w * scale);
		const int pic_y = (const int)floorf((float)bframe->y * vid_h / header_h * scale);
		const int pic_w = (const int)ceilf((float)bframe->w * vid_w / header_w * scale);
		const int pic_h = (const int)ceilf((float)bframe->h * vid_h / header_h * scale);

		Draw_Render(offset_x + pic_x, offset_y + pic_y, pic_w, pic_h, mod->skins[i], gl_bookalpha->value * alpha);
	}
}
