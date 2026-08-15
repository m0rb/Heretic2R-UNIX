#include "compat.h"
//
// gl3_SDL.c -- SDL3-only context layer for the OpenGL 3.2 core renderer.
//
// Modeled on yq2 gl3_sdl.c (SDL3 branches only) and gl1_SDL.c structure.
//
// Copyright 1998 Raven Software
//

#include "gl3_Local.h"
#include <SDL3/SDL.h>

static SDL_Window* window = NULL;
static SDL_GLContext context = NULL;

// Swaps the buffers and shows the next frame.
void RI_EndFrame(void) //mxd. GLimp_EndFrame in original logic.
{
	if (gl3config.useBigVBO)
	{
		// YQ2: this is a good point to orphan the world VBO and get a fresh one.
		GL3_BindVAO(gl3state.vao3D);
		GL3_BindVBO(gl3state.vbo3D);
		glBufferData(GL_ARRAY_BUFFER, gl3state.vbo3Dsize, NULL, GL_STREAM_DRAW);
		gl3state.vbo3DcurOffset = 0;
	}

	SDL_GL_SwapWindow(window);
}

// This function returns the flags used at the SDL window creation by GLimp_InitGraphics().
// In case of error -1 is returned.
int RI_PrepareForWindow(void)
{
	// Set GL context attributes bound to the window: GL 3.2 core, forward-compatible,
	// depth 24, stencil 8, doublebuffer (yq2 gl3_sdl.c GL3_PrepareForWindow, desktop GL path).
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	gl3config.stencil = (SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8) ? true : false);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

	// Let's see if the driver supports MSAA.
	const cvar_t* msaa_samples = ri.Cvar_Get("r_msaa_samples", "0", CVAR_ARCHIVE);
	const int msaa = (int)msaa_samples->value;

	if (msaa > 0)
	{
		if (!SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1))
		{
			ri.Con_Printf(PRINT_ALL, "MSAA is unsupported: %s\n", SDL_GetError());
			ri.Cvar_SetValue("r_msaa_samples", 0.0f);

			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		}
		else if (!SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa))
		{
			ri.Con_Printf(PRINT_ALL, "MSAA %ix is unsupported: %s\n", msaa, SDL_GetError());
			ri.Cvar_SetValue("r_msaa_samples", 0.0f);

			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		}
	}
	else
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	}

	return SDL_WINDOW_OPENGL;
}

// Enables or disables the vsync. Ported from gl1_SDL.c unchanged.
void R_SetVsync(void)
{
	// Make sure that the user given value is SDL compatible...
	int vsync = 0;

	if (r_vsync->value == 1.0f)
		vsync = 1;
	else if (r_vsync->value == 2.0f)
		vsync = -1;

	if (!SDL_GL_SetSwapInterval(vsync) && vsync == -1)
	{
		// Not every system supports adaptive VSync, fallback to normal VSync.
		ri.Con_Printf(PRINT_ALL, "Failed to set adaptive VSync, reverting to normal VSync.\n");
		SDL_GL_SetSwapInterval(1);
	}

	if (!SDL_GL_GetSwapInterval(&vsync))
		ri.Con_Printf(PRINT_ALL, "Failed to get VSync state, assuming no VSync.\n");

	// Tell the frame loop whether presentation is vblank-limited (so it paces to the
	// display refresh instead of a flat vid_maxfps that beats against vblank).
	ri.Cvar_SetValue("vid_vsync_active", (vsync != 0) ? 1.0f : 0.0f);
}

// Initializes the OpenGL context.
qboolean RI_InitContext(void* win)
{
	if (win == NULL)
	{
		ri.Sys_Error(ERR_FATAL, "RI_InitContext() called with NULL argument!");
		return false;
	}

	window = (SDL_Window*)win;

	// Initialize GL context.
	context = SDL_GL_CreateContext(window);

	if (context == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to create OpenGL context: %s\n", SDL_GetError());
		window = NULL;

		return false;
	}

	// Check if we've got the requested MSAA.
	if (r_msaa_samples != NULL && (int)r_msaa_samples->value > 0)
	{
		int msaa = 0;
		if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &msaa))
			ri.Cvar_SetValue("r_msaa_samples", (float)msaa);
	}

	// Check if we've got at least 8 stencil bits.
	if (gl3config.stencil)
	{
		int stencil_bits = 0;
		if (!SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil_bits) || stencil_bits < 8)
			gl3config.stencil = false;
	}

	// Load OpenGL function pointers through GLAD.
	if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): failed to load OpenGL function pointers!\n");
		return false;
	}

	// Check OpenGL version.
	if (!GLAD_GL_VERSION_3_2)
	{
		ri.Con_Printf(PRINT_ALL, "RI_InitContext(): unsupported OpenGL version. Expected 3.2, got %i.%i!\n", GLVersion.major, GLVersion.minor);
		return false;
	}

	gl3config.major_version = GLVersion.major;
	gl3config.minor_version = GLVersion.minor;
	gl3config.anisotropic = (GLAD_GL_EXT_texture_filter_anisotropic != 0);

	ri.Con_Printf(PRINT_ALL, "GL: vendor=%s renderer=%s version=%s\n",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

	R_SetVsync();

	// NOTE: no vid_gamma->modified forcing here (gl1 did that to rebuild its gamma
	// table + re-upload textures) - gamma/brightness/contrast are shader-side now.

	return true;
}

// Shuts the GL context down.
void RI_ShutdownContext(void)
{
	if (window != NULL && context != NULL)
	{
		SDL_GL_DestroyContext(context);
		context = NULL;
	}
}

// Fills the actual size of the drawable into width and height.
void GL3_GetDrawableSize(int* width, int* height)
{
	SDL_GetWindowSizeInPixels(window, width, height);
}
