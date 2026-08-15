#include "compat.h"
//
// gl3_Image.c -- image loading and texture management for the OpenGL 3.2 core renderer.
//
// H2 semantics ported from gl1_Image.c (palette handling, .m8/.m32 loading, name hash,
// image types/filtering, registration-sequence GC); backend technique from yq2 gl3_image.c
// (GL3_Bind/GL3_BindLightmap state caching, sized internal formats).
//
// Deviations from gl1 (per CONTRACT.md):
// - NO texture-baked gamma: R_InitGammaTable()/GrabPalette() gamma pass/R_ApplyGamma32()/
//   R_RefreshImage()/R_GammaAffect() are removed - H2 gamma/brightness/contrast run in
//   the fragment shaders (uniCommon), so images upload raw and never need re-uploading.
// - Fixed-function state helpers (R_EnableMultitexture/R_TexEnv/R_BlendFunc/R_AlphaFunc)
//   are removed - texenv/alpha test are shader-side, blend state is set by the draw code.
// - GL_TEXTURE_MAX_LEVEL is clamped to the mip range actually uploaded (GL3 core
//   completeness; gl1 relied on the files always shipping full mip chains).
//
// Copyright 1998 Raven Software
//

#include "gl3_Image_internal.h"
#include "qfiles.h"

#define GL_TEX_SOLID_FORMAT GL_RGBA8 // gl1: GL_RGBA (unsized); sized formats are the GL3 core convention (yq2: gl3_solid_format).
#define GL_TEX_ALPHA_FORMAT GL_RGBA8 // gl1: GL_RGBA.

image_t gltextures[MAX_GLTEXTURES];
int numgltextures;

#define NUM_HASHED_GLTEXTURES	256
static image_t* gltextures_hashed[NUM_HASHED_GLTEXTURES]; // H2

int gl_filter_min = GL_NEAREST_MIPMAP_LINEAR; // Q2: GL_LINEAR_MIPMAP_NEAREST; H2: GL_NEAREST.
int gl_filter_max = GL_LINEAR;

extern image_t* draw_chars; // Defined in gl3_Draw.c (gl1: gl1_Draw.c).

extern void R_InitMinlight(void); // YQ2. Implemented in gl3_Light.c (gl1: gl1_Light.c).

//mxd
static paletteRGBA_t* upload_buffer = NULL;
static uint upload_buffer_size = 0;

typedef struct
{
	char* name;
	int	minimize;
	int maximize;
} glmode_t;

static glmode_t modes[] =
{
	{ "GL_NEAREST", GL_NEAREST, GL_NEAREST },
	{ "GL_LINEAR", GL_LINEAR, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

#define NUM_GL_MODES ((int)(sizeof(modes) / sizeof(glmode_t))) //mxd. Added int cast.

// NOTE: gl1's R_InitGammaTable() is intentionally gone: the exact same math now runs
// per-fragment in the shaders (H2ColorGrade() in gl3_Shaders.c, fed from uniCommon).

//mxd. Part of GL_LoadPic logic in Q2
image_t* R_GetFreeImage(void) // H2: GL_GetFreeImage().
{
	int index;
	image_t* image;

	// Find a free image_t
	for (index = 0, image = &gltextures[0]; index < numgltextures; index++, image++)
		if (image->registration_sequence == 0)
			break;

	if (index == numgltextures)
	{
		if (numgltextures == MAX_GLTEXTURES)
			ri.Sys_Error(ERR_DROP, "R_GetFreeImage: no free image_t slots!\n"); //mxd. Sys_Error() -> ri.Sys_Error().

		numgltextures++;
	}

	memset(image, 0, sizeof(image_t));

	return image;
}

#pragma region ========================== STATE-CACHED TEXTURE BINDING (YQ2) ==========================

// gl1: R_Bind() / R_BindImage(). Always binds to GL_TEXTURE0 (yq2 gl3 backend style).
// gl1's gl_nobind performance option is a no-op in gl3 (cvar registered but ignored).
void GL3_Bind(const GLuint texnum)
{
	if (gl3state.currenttexture != texnum)
	{
		gl3state.currenttexture = texnum;
		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texnum);
	}
}

void GL3_BindImage(const image_t* image) // gl1: R_BindImage().
{
	GL3_Bind((GLuint)image->texnum);
}

// gl1-parity name; Draw/cinematic code calls this.
void R_BindImage(const image_t* image)
{
	GL3_BindImage(image);
}

// GL3_BindLightmap() lives in gl3_Lightmap.c (also restores TMU0 after binding).

#pragma endregion

#pragma region ========================== TEXTURE FILTERING ==========================

void R_SetTexAnisotropy(void) // YQ2
{
	if (!gl3config.anisotropic || gl3config.max_anisotropy < 2.0f)
		return;

	GLfloat aniso = r_anisotropic->value;
	if (aniso < 1.0f)
		aniso = 1.0f;
	else if (aniso > gl3config.max_anisotropy)
		aniso = gl3config.max_anisotropy;

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
}

void R_TextureMode(const char* string) // Q2: GL_TextureMode()
{
	int cur_mode;

	for (cur_mode = 0; cur_mode < NUM_GL_MODES; cur_mode++)
		if (!Q_stricmp(modes[cur_mode].name, string))
			break;

	if (cur_mode == NUM_GL_MODES)
	{
		ri.Con_Printf(PRINT_ALL, "Bad texture filter name\n"); // H2: text change.
		return;
	}

	gl_filter_min = modes[cur_mode].minimize;
	gl_filter_max = modes[cur_mode].maximize;

	// Change all the existing mipmap texture objects.
	image_t* glt = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, glt++)
	{
		if (glt->registration_sequence == 0) // gl3: skip free slots (gl1 binds texnum 0 for them and pointlessly sets its params).
			continue;

		if (glt->type != it_pic && glt->type != it_sky) // Mipmapped texture.
		{
			GL3_BindImage(glt); // Q2: GL_Bind(glt->texnum)

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min); // H2_1.07: GL_TEXTURE_MIN_FILTER -> 0x84fe //mxd. Q2/H2: qglTexParameterf
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max); // H2_1.07: GL_TEXTURE_MAG_FILTER -> 0x84fe //mxd. Q2/H2: qglTexParameterf
			R_SetTexAnisotropy(); // YQ2
		}
	}
}

void R_SetFilter(const image_t* image)
{
	//mxd. Q2/H2: qglTexParameterf
	switch (image->type)
	{
		case it_pic:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // H2_1.07: GL_LINEAR
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // H2_1.07: GL_LINEAR
			break;

		case it_sky:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max); // H2_1.07: GL_TEXTURE_MIN_FILTER -> 0x84fe
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max); // H2_1.07: GL_TEXTURE_MAG_FILTER -> 0x84fe
			break;

		default:
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			R_SetTexAnisotropy(); // YQ2
			break;
	}
}

#pragma endregion

void R_ImageList_f(void) // Q2: GL_ImageList_f()
{
	int tex_count = 0;
	int tex_texels = 0;
	int sky_count = 0;
	int sky_texels = 0;
	int skin_count = 0;
	int skin_texels = 0;
	int sprite_count = 0;
	int sprite_texels = 0;
	int pic_count = 0;
	int pic_texels = 0;

	const char* palstrings[] = { "RGB", "PAL" };

	ri.Con_Printf(PRINT_ALL, "---------------------------\n"); //mxd. Com_Printf() -> ri.Con_Printf() (here and below).

	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		switch (image->type)
		{
			case it_skin:
				ri.Con_Printf(PRINT_ALL, "M");
				skin_count++;
				skin_texels += image->width * image->height;
				break;

			case it_sprite:
				ri.Con_Printf(PRINT_ALL, "S");
				sprite_count++;
				sprite_texels += image->width * image->height;
				break;

			//mxd. Original code also handles types 3 and 7 here. These aren't used anywhere else in the code.
			case it_wall:
				ri.Con_Printf(PRINT_ALL, "W");
				tex_count++;
				tex_texels += (image->width * image->height * 4) / 3;
				break;

			case it_pic:
				ri.Con_Printf(PRINT_ALL, "P");
				pic_count++;
				pic_texels += image->width * image->height;
				break;

			case it_sky:
				ri.Con_Printf(PRINT_ALL, "K"); //mxd. Was also "P" in original logic.
				sky_count++;
				sky_texels += image->width * image->height;
				break;

			default: //mxd. Added to silence compiler warning.
				ri.Con_Printf(PRINT_ALL, "U%i", image->type, image->name);
				break;
		}

		ri.Con_Printf(PRINT_ALL, " %3i %3i %s %s\n", image->width, image->height, palstrings[image->palette != NULL], image->name);
	}

	ri.Con_Printf(PRINT_ALL, "-------------------------------\n");
	ri.Con_Printf(PRINT_ALL, "Total skin   : %i (%i texels)\n", skin_count, skin_texels);
	ri.Con_Printf(PRINT_ALL, "Total world  : %i (%i texels)\n", tex_count, tex_texels);
	ri.Con_Printf(PRINT_ALL, "Total sky    : %i (%i texels)\n", sky_count, sky_texels);
	ri.Con_Printf(PRINT_ALL, "Total sprite : %i (%i texels)\n", sprite_count, sprite_texels);
	ri.Con_Printf(PRINT_ALL, "Total pic    : %i (%i texels)\n", pic_count, pic_texels);
	ri.Con_Printf(PRINT_ALL, "-------------------------------\n");
}

#pragma region ========================== .M8 LOADING ==========================

//mxd. Somewhat similar to Q2's GL_Upload8()
void R_UploadPaletted(const int level, const byte* data, const paletteRGB_t* palette, const int width, const int height) // H2: GL_UploadPaletted().
{
	const uint src_size = width * height;
	const uint dst_size = src_size * sizeof(paletteRGBA_t);

	//mxd. Use dynamically allocated buffer (original logic uses fixed-size 65536 (256 * 256) buffer instead).
	if (dst_size > upload_buffer_size)
	{
		upload_buffer = realloc(upload_buffer, dst_size);

		if (upload_buffer == NULL)
			ri.Sys_Error(ERR_DROP, "R_UploadPaletted: failed to allocate upload buffer for %i x %i image!\n", width, height);

		upload_buffer_size = dst_size;
	}

	for (uint i = 0; i < src_size; i++)
	{
		const paletteRGB_t* src_p = &palette[data[i]];
		paletteRGBA_t* dst_p = &upload_buffer[i];

		// Copy rgb components.
		dst_p->r = src_p->r;
		dst_p->g = src_p->g;
		dst_p->b = src_p->b;
		dst_p->a = 255;
	}

	glTexImage2D(GL_TEXTURE_2D, level, GL_TEX_SOLID_FORMAT, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, upload_buffer);
}

// Upload a tightly-packed 32-bit RGBA frame (used by the MPEG cinematic path). --morb
void R_UploadRGBA(const int level, const byte* rgba, const int width, const int height)
{
	glTexImage2D(GL_TEXTURE_2D, level, GL_TEX_SOLID_FORMAT, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void GrabPalette(const paletteRGB_t* src, paletteRGB_t* dst) // H2
{
	// gl1 pushed every palette entry through gammatable[] here; gl3 applies the H2
	// gamma/brightness/contrast grade in the fragment shaders instead (CONTRACT.md),
	// so the palette is kept raw.
	memcpy(dst, src, sizeof(paletteRGB_t) * PAL_SIZE);
}

//mxd. Gross hack to fix lensflare sprites. //TODO: fix image files instead?
static void FixPalette(const image_t* image)
{
	if (image->type != it_sprite || strstr(image->name, "Sprites/lens/flare") == NULL)
		return;

	if (strstr(image->name, "flare1_0.m8") != NULL) // Remap BG color from [7 7 5] to [0 0 0].
	{
		memset(&image->palette[192], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare2_0.m8") != NULL) // Remap BG colors from [5 5 3] and [20 20 12] to [0 0 0].
	{
		memset(&image->palette[187], 0, sizeof(paletteRGB_t));
		memset(&image->palette[188], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare3_0.m8") != NULL) // Remap BG color from [6 6 4] to [0 0 0].
	{
		memset(&image->palette[208], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare4_0.m8") != NULL) // Remap BG color from [7 7 5] to [0 0 0].
	{
		memset(&image->palette[198], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare5_0.m8") != NULL) // Remap BG colors from [20 20 16] and [22 22 18] to [0 0 0].
	{
		memset(&image->palette[184], 0, sizeof(paletteRGB_t));
		memset(&image->palette[185], 0, sizeof(paletteRGB_t));
	}
}

static void R_UploadM8(miptex_t* mt, const int filesize, const image_t* image) // H2: GL_Upload8M().
{
	int mip;
	for (mip = 0; mip < MIPLEVELS && mt->width[mip] > 0 && mt->height[mip] > 0; mip++)
	{
		const uint mip_size = mt->width[mip] * mt->height[mip];
		if ((int)(mt->offsets[mip] + mip_size) > filesize) // Bounds check --morb
		{
			ri.Con_Printf(PRINT_ALL, "R_UploadM8: mip %i offset %u out of bounds (filesize %i) for '%s'\n", mip, mt->offsets[mip], filesize, image->name);
			break;
		}

		R_UploadPaletted(mip, (byte*)mt + mt->offsets[mip], image->palette, (int)mt->width[mip], (int)mt->height[mip]);
	}

	// GL3 core: clamp the sampled mip range to what was actually uploaded, so mipmap
	// min filters can't make the texture incomplete (gl1 relied on complete file chains).
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max(mip - 1, 0));

	R_SetFilter(image);
}

// Loads .M8 image.
static image_t* R_LoadM8(const char* name, const imagetype_t type) // H2: GL_LoadWal().
{
	miptex_t* mt;
	const int filesize = ri.FS_LoadFile(name, (void**)&mt);

	if (mt == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return NULL;
	}

	if (mt->version != MIP_VERSION)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s': invalid version (%i)\n", name, mt->version); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	if (strlen(name) >= MAX_QPATH)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s': filename too long\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	paletteRGB_t* palette = malloc(sizeof(paletteRGB_t) * 256);
	GrabPalette(mt->palette, palette);

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = (int)mt->width[0];
	image->height = (int)mt->height[0];
	image->type = type;
	image->palette = palette;
	image->has_alpha = false;
	image->num_frames = (byte)mt->value;

	FixPalette(image); //mxd

	glGenTextures(1, (GLuint*)&image->texnum); // gl1 pre-set texnum to TEXNUM_IMAGES + slot; gl3 texnums are always driver-generated.
	GL3_BindImage(image);
	R_UploadM8(mt, filesize, image);
	ri.FS_FreeFile(mt);

	return image;
}

#pragma endregion

#pragma region ========================== .M32 LOADING ==========================

// NOTE: gl1's R_ApplyGamma32() is intentionally gone (shader-side color grading).

static void R_UploadM32(miptex32_t* mt, const int filesize, const image_t* img) // H2: GL_Upload32M().
{
	int mip;
	for (mip = 0; mip < MIPLEVELS && mt->width[mip] > 0 && mt->height[mip] > 0; mip++)
	{
		const uint mip_size = mt->width[mip] * mt->height[mip];
		if ((int)(mt->offsets[mip] + mip_size * sizeof(paletteRGBA_t)) > filesize) //Bounds check --morb
		{
			ri.Con_Printf(PRINT_ALL, "R_UploadM32: mip %i offset %u out of bounds (filesize %i) for '%s'\n", mip, mt->offsets[mip], filesize, img->name);
			break;
		}

		glTexImage2D(GL_TEXTURE_2D, mip, GL_TEX_ALPHA_FORMAT, (int)mt->width[mip], (int)mt->height[mip], 0, GL_RGBA, GL_UNSIGNED_BYTE, (byte*)mt + mt->offsets[mip]);
	}

	// GL3 core: clamp the sampled mip range to what was actually uploaded (see R_UploadM8).
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max(mip - 1, 0));

	R_SetFilter(img);
}

// Loads .M32 image.
static image_t* R_LoadM32(const char* name, const imagetype_t type) // H2: GL_LoadWal32()
{
	miptex32_t* mt;

	const int filesize = ri.FS_LoadFile(name, (void**)&mt);
	if (mt == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return NULL;
	}

	if (mt->version != MIP32_VERSION)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s': invalid version (%i)\n", name, mt->version); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	if (strlen(name) >= MAX_QPATH)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s': filename too long\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	// gl1: R_ApplyGamma32(mt, filesize) - removed, pixels upload raw (shader-side grading).

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = (int)mt->width[0];
	image->height = (int)mt->height[0];
	image->type = type;
	image->palette = NULL;
	image->has_alpha = 1;
	image->num_frames = (byte)mt->num_frames;

	glGenTextures(1, (GLuint*)&image->texnum);
	GL3_BindImage(image);
	R_UploadM32(mt, filesize, image);
	ri.FS_FreeFile(mt);

	return image;
}

#pragma endregion

// Creates a fallback texture programmatically (e.g., checkerboard pattern)
image_t* R_CreateFallbackTexture(const char* name, const imagetype_t type)
{
	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = 64;
	image->height = 64;
	image->type = type;
	image->palette = NULL;
	image->has_alpha = false;
	image->num_frames = 1;

	glGenTextures(1, (GLuint*)&image->texnum);

	// Generate a simple 64x64 checkerboard pattern (magenta/black)
	byte pixels[64 * 64 * 4];
	for (int y = 0; y < 64; y++)
	{
		for (int x = 0; x < 64; x++)
		{
			int idx = (y * 64 + x) * 4;
			qboolean white = ((x / 8) + (y / 8)) & 1;
			pixels[idx + 0] = white ? 255 : 0;   // R
			pixels[idx + 1] = 0;                   // G
			pixels[idx + 2] = white ? 255 : 128;   // B
			pixels[idx + 3] = 255;                 // A
		}
	}

	GL3_BindImage(image);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_TEX_SOLID_FORMAT, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0); // GL3 core: single mip level (see R_UploadM8).
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Add to hash
	const uint len = strlen(image->name);
	const byte hash = image->name[len - 7] + image->name[len - 5] * image->name[len - 6];
	image->next = gltextures_hashed[hash];
	gltextures_hashed[hash] = image;

	return image;
}

// Now with name hashing. When no texture found, returns r_notexture instead of NULL.
image_t* R_FindImage(const char* name, const imagetype_t type) // H2: GL_FindImage()
{
	if (name == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Invalid null name\n"); //mxd. Com_Printf() -> ri.Con_Printf().
		return r_notexture;
	}

	const uint len = strlen(name);

	if (len < 8)
	{
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Name too short (%s)\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return r_notexture;
	}

	// Check for hashed image first.
	const byte hash = name[len - 7] + name[len - 5] * name[len - 6];
	image_t* image = gltextures_hashed[hash];

	while (image != NULL)
	{
		if (strcmp(name, image->name) == 0)
		{
			image->registration_sequence = registration_sequence;
			return image;
		}

		image = image->next;
	}

	// Not hashed. Load image from disk.
	if (strcmp(name + len - 3, ".m8") == 0)
		image = R_LoadM8(name, type);
	else if (strcmp(name + len - 4, ".m32") == 0)
		image = R_LoadM32(name, type);
	else
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Extension not recognized in '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().

	if (image == NULL)
		return r_notexture;

	// Add image to hash.
	image->next = gltextures_hashed[hash];
	gltextures_hashed[hash] = image;

	return image;
}

// H2: new 'retval' arg.
struct image_s* RI_RegisterSkin(const char* name, qboolean* retval)
{
	image_t* img = R_FindImage(name, it_skin);
	if (retval != NULL)
		*retval = (img != r_notexture);

	return img;
}

static void R_FreeImage(image_t* image) // H2: GL_FreeImage()
{
	// Delete GL texture.
	glDeleteTextures(1, (GLuint*)&image->texnum);
	if (image->palette != NULL)
	{
		free(image->palette);
		image->palette = NULL;
	}

	// Remove from hash.
	const uint len = strlen(image->name);
	const byte hash = image->name[len - 7] + image->name[len - 5] * image->name[len - 6];

	image_t** tgt = &gltextures_hashed[hash];
	for (image_t* img = gltextures_hashed[hash]; img != image; img = img->next)
		tgt = &img->next;

	*tgt = image->next;
	image->registration_sequence = 0;

	//BUGFIX: otherwise GL3_Bind() may not re-bind next texture with the same texnum... -- mxd.
	if (gl3state.currenttexture == (GLuint)image->texnum)
		gl3state.currenttexture = 0;
}

void R_FreeImageNoHash(image_t* image) // H2: GL_FreeImageNoHash()
{
	glDeleteTextures(1, (GLuint*)&image->texnum);
	if (image->palette != NULL)
	{
		free(image->palette);
		image->palette = NULL;
	}

	image->registration_sequence = 0;

	//BUGFIX: otherwise GL3_Bind() may not re-bind the next texture with the same texnum... -- mxd.
	if (gl3state.currenttexture == (GLuint)image->texnum)
		gl3state.currenttexture = 0;
}

void R_FreeUnusedImages(void)
{
	// Never free r_notexture or particle texture. H2: extra never-to-free textures.
	// gl3: NULL checks added - these are set by Draw_InitLocal() (gl3_Draw.c module
	// port), which may not have run/landed yet when a map load triggers GC.
	image_t* permanent[] = { r_notexture, r_particletexture, r_aparticletexture, r_reflecttexture, draw_chars, r_font1, r_font2 };
	for (uint i = 0; i < sizeof(permanent) / sizeof(permanent[0]); i++)
		if (permanent[i] != NULL)
			permanent[i]->registration_sequence = registration_sequence;

	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
	{
		// Used in this sequence.
		if (image->registration_sequence == registration_sequence)
			continue;

		// Free image_t slot.
		if (image->registration_sequence == 0)
			continue;

		// Missing: it_pic check

		// fix for nameless self-managed images --morb
		if (!image->name[0])
			continue;

		// Free it.
		R_FreeImage(image);
	}
}

void R_InitImages(void) // Q2: GL_InitImages()
{
	registration_sequence = 1;
	// gl1: gl_state.inverse_intensity = 1.0f - gl3 keeps intensity in uniCommon (1.0, set by the foundation).

	for (int i = 0; i < numgltextures; i++)
	{
		if (gltextures[i].palette != NULL)
		{
			free(gltextures[i].palette);
			gltextures[i].palette = NULL;
		}
	}
	memset(gltextures, 0, sizeof(gltextures));
	memset(gltextures_hashed, 0, sizeof(gltextures_hashed));
	numgltextures = 0;

	// Invalidate texture binding caches - a freshly (re)created GL context has
	// texture 0 bound on TMU0 (gl1 parity: currenttextures[] = -1 reset).
	glActiveTexture(GL_TEXTURE0);
	gl3state.currenttmu = GL_TEXTURE0;
	gl3state.currenttexture = 0;
	gl3state.currentlightmap = -1;

	// gl1 also reset its R_BlendFunc()/R_AlphaFunc() caches here - no such caches in gl3
	// (blend state is set directly by draw code, alpha test is shader-side).

	R_InitMinlight(); // YQ2
}

void R_ShutdownImages(void) // Q2: GL_ShutdownImages()
{
	image_t* image = &gltextures[0];
	for (int i = 0; i < numgltextures; i++, image++)
		// same crash -- nameless cin_frame segfault --morb
		//if (image->registration_sequence != 0)
		if (image->registration_sequence != 0 && image->name[0])
			R_FreeImage(image);

	//mxd. Free upload_buffer.
	if (upload_buffer != NULL)
	{
		free(upload_buffer);
		upload_buffer = NULL;
		upload_buffer_size = 0;
	}
}

// NOTE: gl1's R_RefreshImage() and R_GammaAffect() are intentionally gone: with
// shader-side color grading nothing ever needs to be re-uploaded on gamma change
// (gl3_Main.c consumes/resets vid_textures_refresh_required as a no-op).

void R_DisplayHashTable(void)
{
	int total_count = 0;
	int hashed_count = 0;

	image_t** gl = gltextures_hashed;
	for (int i = 0; i < NUM_HASHED_GLTEXTURES; i++, gl++)
	{
		const image_t* image = *gl;
		if (image != NULL)
		{
			while (image != NULL)
			{
				image = image->next;
				total_count++;
			}

			hashed_count++;
		}
	}

	ri.Con_Printf(PRINT_ALL, "Hash entries: %d, Total images: %d\n", hashed_count, total_count); //mxd. Com_Printf() -> ri.Con_Printf().
}
