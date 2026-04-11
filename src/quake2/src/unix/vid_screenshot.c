//
// vid_screenshot.c -- Screenshot support for Heretic2R Unix port
//
// Mirrors win32/vid_Screenshot.c; the logic is identical — only the platform
// headers differ (compat.h provides the _s family on Unix).
//
// Copyright 2025 mxd
// Unix port by morb
//

#include "compat.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb/stb_image_write.h>

#include "qcommon.h"
#include "client/menus/menu_misc.h"

#define MAX_SCREENSHOTS             10000
#define SCREENSHOT_FILENAME_FORMAT  "%sH2R-%04i.%s"

#define PNG_COMPRESSION 8
#define JPG_QUALITY     95

typedef struct ScreenshotSaveBuffer_s
{
	byte* data;
	int size;
	int offset;
} ScreenshotSaveBuffer_t;

static void VID_WriteScreenshotCallback(void* context, void* data, const int size)
{
	ScreenshotSaveBuffer_t* buf = context;

	memcpy_s(&buf->data[buf->offset], buf->size - buf->offset, data, size);
	buf->offset += size;
}

static int VID_GetFreeScreenshotIndex(const char* screenshots_dir)
{
	for (int i = 0; i < MAX_SCREENSHOTS; i++)
	{
		qboolean file_exists = false;

		for (uint c = 0; c < SSF_NUM_FORMATS; c++)
		{
			if (Sys_IsFile(va(SCREENSHOT_FILENAME_FORMAT, screenshots_dir, i, screenshot_formats[c])))
			{
				file_exists = true;
				break;
			}
		}

		if (!file_exists)
			return i;
	}

	return -1;
}

void VID_WriteScreenshot(const int width, const int height, const int comp, const void* data)
{
	char scr_dir[MAX_OSPATH];
	Com_sprintf(scr_dir, sizeof(scr_dir), "%s/screenshots/", FS_Userdir());
	FS_CreatePath(scr_dir);

	const int screenshot_index = VID_GetFreeScreenshotIndex(scr_dir);
	if (screenshot_index == -1)
	{
		Com_Printf("VID_WriteScreenshot: couldn't create a file: too many screenshots!\n");
		return;
	}

	const ScreenshotSaveFormat_t save_format = M_GetCurrentScreenshotSaveFormat();

	stbi_flip_vertically_on_write(true);
	qboolean success;

	const int buf_size = width * height * comp;
	ScreenshotSaveBuffer_t buf = { .offset = 0, .size = buf_size, .data = malloc(buf_size) };

	switch (save_format)
	{
		case SSF_JPG:
		default:
			success = stbi_write_jpg_to_func(VID_WriteScreenshotCallback, &buf, width, height, comp, data, JPG_QUALITY);
			break;

		case SSF_PNG:
			stbi_write_png_compression_level = PNG_COMPRESSION;
			success = stbi_write_png_to_func(VID_WriteScreenshotCallback, &buf, width, height, comp, data, 0);
			break;
	}

	char scr_filepath[MAX_OSPATH];
	Com_sprintf(scr_filepath, sizeof(scr_filepath), SCREENSHOT_FILENAME_FORMAT, scr_dir, screenshot_index, screenshot_formats[save_format]);

	if (success)
	{
		FILE* f;
		int written_size = -1;

		if (fopen_s(&f, scr_filepath, "wb") == 0)
		{
			written_size = (int)fwrite(buf.data, 1, buf.offset, f);
			fclose(f);
		}

		success = (buf.offset == written_size);
	}

	free(buf.data);

	if (success)
	{
		float brightness = 0.0f;
		const int data_size = width * height * comp;
		const byte* pixels = data;

		for (int i = 0; i < data_size; i += comp)
			brightness += (float)(pixels[i] + pixels[i + 1] + pixels[i + 2]);

		brightness = brightness / (float)data_size * 100.0f / 255.0f;

		if (brightness < 2.0f)
			Com_Printf("**WARNING** Overly dark image '%s' (brightness: %2.1f%%)\n", scr_filepath, brightness);
		else if (brightness > 88.0f)
			Com_Printf("**WARNING** Overly bright image '%s' (brightness: %2.1f%%)\n", scr_filepath, brightness);
		else
			Com_Printf("Wrote '%s' (brightness: %2.1f%%)\n", scr_filepath, brightness);
	}
	else
	{
		Com_Printf("VID_WriteScreenshot: couldn't write '%s'!\n", scr_filepath);
	}
}
