/*
 * vid_sdl3.c -- SDL3 video backend for Heretic2R Unix port
 * Copyright (C) 2010-2024 Yamagi Quake 2 Contributors (GPLv2)
 * Unix port by morb
 */

#include <SDL3/SDL.h>
#include "../../../qcommon/qcommon.h"
#include "../client/client.h"
#include "../client/cl_skeletons.h"
#include "vid_dll.h"
#include "../../../ref_gl1/src/gl1_Local.h"

static SDL_Window *window = NULL;
static SDL_GLContext gl_context = NULL;

cvar_t *vid_fullscreen;
cvar_t *vid_width;
cvar_t *vid_height;
cvar_t *vid_gamma;
cvar_t *vid_brightness;
cvar_t *vid_contrast;
cvar_t *vid_ref;
cvar_t *vid_mode;
qboolean vid_restart_required;

// Video mode and renderer info (for menu_video.c compatibility)
vidmode_t *vid_modes = NULL;
int num_vid_modes = 0;
reflib_info_t reflib_infos[MAX_REFLIBS];
int num_reflib_infos = 0;

// External declaration of the renderer export structure
extern refexport_t re;

// Forward declarations of local functions
qboolean VID_GetModeInfo(int *width, int *height, int mode);
qboolean VID_InitGraphics(int width, int height);

// Forward declaration of GetRefAPI from the renderer
refexport_t GetRefAPI(refimport_t rimp);

// Initialize the renderer directly (no DLL loading on Unix)
static qboolean VID_LoadRefresh(void)
{
    refimport_t ri;

    Com_Printf("------- Loading renderer -------\n");

    // Set up the refimport_t structure with function pointers
    ri.Sys_Error = VID_Error;
    ri.Com_Error = Com_Error;
    ri.Con_Printf = VID_Printf;
    ri.Cvar_Get = Cvar_Get;
    ri.Cvar_Set = Cvar_Set;
    ri.Cvar_SetValue = Cvar_SetValue;
    ri.Cmd_AddCommand = Cmd_AddCommand;
    ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
    ri.FS_LoadFile = FS_LoadFile;
    ri.FS_FreeFile = FS_FreeFile;
    ri.Vid_GetModeInfo = VID_GetModeInfo;
    ri.GLimp_InitGraphics = VID_InitGraphics;
    ri.skeletalJoints = skeletal_joints;
    ri.jointNodes = joint_nodes;
    ri.Is_Screen_Flashing = Is_Screen_Flashing;
    ri.Deactivate_Screen_Flash = Deactivate_Screen_Flash;
    ri.Vid_WriteScreenshot = NULL; // TODO: implement Unix screenshot
#ifdef _DEBUG
    ri.DBG_IDEPrint = DBG_IDEPrint;
    ri.DBG_HudPrint = DBG_HudPrint;
#endif

    // Call GetRefAPI directly to get the renderer exports
    re = GetRefAPI(ri);

    if (re.api_version != REF_API_VERSION)
    {
        Com_Printf("Renderer has incompatible api_version %i!\n", re.api_version);
        return false;
    }

    if (!re.Init())
    {
        Com_Printf("Failed to initialize renderer!\n");
        return false;
    }

    Com_Printf("--------------------------------\n");
    return true;
}

void VID_Init(void)
{
    vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
    vid_width = Cvar_Get("vid_width", "640", CVAR_ARCHIVE);
    vid_height = Cvar_Get("vid_height", "480", CVAR_ARCHIVE);
    
    // Load the renderer
    VID_LoadRefresh();
}

qboolean VID_GetModeInfo(int *width, int *height, int mode)
{
    // If the mode list hasn't been populated yet (VID_InitGraphics hasn't run),
    // fall back to the vid_width/vid_height cvars so R_SetMode can succeed on
    // the first call during renderer init (chicken-and-egg: mode list is built
    // inside VID_InitGraphics which is called by R_SetMode itself).
    if (num_vid_modes == 0 || mode < 0 || mode >= num_vid_modes)
    {
        *width  = (int)vid_width->value;
        *height = (int)vid_height->value;
        return true;
    }

    *width  = vid_modes[mode].width;
    *height = vid_modes[mode].height;
    return true;
}

SDL_Window* VID_GetSDLWindow(void)
{
    return window;
}

void VID_Shutdown(void)
{
    if (gl_context) {
        SDL_GL_DestroyContext(gl_context);
        gl_context = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

qboolean VID_InitGraphics(int width, int height)
{
    // Force X11 (GLX) over Wayland (EGL). Legacy OpenGL 1.x compatibility
    // contexts don't work reliably via EGL (NVIDIA EGL crashes on glTexImage2D).
    // Must be set before SDL_InitSubSystem.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");

    // Safety: destroy any existing window that wasn't cleaned up by the caller.
    // Normally VID_CheckChanges calls re.ShutdownContext() + destroys window first.
    if (window != NULL)
    {
        Com_Printf("VID_InitGraphics: destroying old window before creating new one\n");
        SDL_DestroyWindow(window);
        window = NULL;
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        Com_Printf("VID_InitGraphics: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Populate the video mode list once (mirrors glimp_sdl3.c::InitDisplayModes).
    // On Linux we never call GLimp_Init(), so VID_InitModes() would otherwise never run.
    if (num_vid_modes == 0)
    {
        const SDL_DisplayID disp = SDL_GetPrimaryDisplay();
        int num_modes = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(disp, &num_modes);
        if (modes != NULL && num_modes > 0)
        {
            viddef_t* valid_modes = malloc(sizeof(viddef_t) * (num_modes + 1));
            int num_valid = 0;

            // Mode 0 = desktop resolution.
            const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(disp);
            if (desktop)
            {
                valid_modes[0].width  = desktop->w;
                valid_modes[0].height = desktop->h;
                num_valid = 1;
            }

            for (int i = 0; i < num_modes; i++)
            {
                if (modes[i]->w < 640 || modes[i]->h < 480)
                    continue;
                qboolean dup = false;
                for (int c = 0; c < num_valid; c++)
                    if (valid_modes[c].width == modes[i]->w && valid_modes[c].height == modes[i]->h)
                        { dup = true; break; }
                if (!dup)
                {
                    valid_modes[num_valid].width  = modes[i]->w;
                    valid_modes[num_valid].height = modes[i]->h;
                    num_valid++;
                }
            }
            SDL_free(modes);

            if (num_valid > 0)
                VID_InitModes(valid_modes, num_valid);

            free(valid_modes);
        }
    }

    // Request a compatibility profile: the renderer uses legacy immediate-mode
    // OpenGL (glBegin/glEnd/glVertex3fv etc.) which are removed in core profiles.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int flags = SDL_WINDOW_OPENGL;
    if (vid_fullscreen->value) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    window = SDL_CreateWindow("Heretic2R",
                              width,
                              height,
                              flags);

    if (!window) {
        Com_Printf("VID_InitGraphics: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    // Initialize the rendering context (loads OpenGL functions via GLAD)
    if (!re.InitContext(window)) {
        Com_Printf("VID_InitGraphics: InitContext failed\n");
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }

    // SDL3 window size in pixels may differ from the requested logical size
    // (e.g. HiDPI scaling or Wayland compositor scaling). Update viddef so
    // glViewport covers the full framebuffer rather than the bottom-left corner.
    int actual_w, actual_h;
    SDL_GetWindowSizeInPixels(window, &actual_w, &actual_h);
    if (actual_w != width || actual_h != height)
        Com_Printf("VID_InitGraphics: drawable %dx%d vs requested %dx%d\n", actual_w, actual_h, width, height);
    viddef.width  = actual_w;
    viddef.height = actual_h;

    return true;
}

void VID_BeginFrame(float camera_separation)
{
    // Nothing to do
}

void VID_EndFrame(void)
{
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

// Error reporting function
void VID_Error(int err_level, const char* fmt, ...)
{
    va_list argptr;
    char msg[1024];
    
    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    
    Com_Error(err_level, "%s", msg);
}

// Printf function for video system
void VID_Printf(int print_level, const char* fmt, ...)
{
    va_list argptr;
    char msg[1024];
    
    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    
    Com_Printf("%s", msg);
}

// Check for video changes (called each frame).
// Mirrors win32/vid_dll.c::VID_CheckChanges but without DLL reloading.
void VID_CheckChanges(void)
{
    if (!vid_restart_required && !vid_ref->modified && !vid_mode->modified && !vid_fullscreen->modified)
        return;

    vid_restart_required = false;
    vid_ref->modified = false;
    vid_mode->modified = false;
    vid_fullscreen->modified = false;

    cl.force_refdef = true;
    cl.refresh_prepped = false;
    cls.disable_screen = true;

    if (se.StopAllSounds != NULL)
        se.StopAllSounds();

    // Destroy the GL context first (it lives in gl1_SDL.c).
    // Must happen before we destroy the SDL window, and before re.Init()
    // tries to create a new one (which would duplicate the window otherwise).
    re.ShutdownContext();

    // Destroy the SDL window. VID_InitGraphics will create a new one when
    // re.Init() calls R_SetMode() -> ri.GLimp_InitGraphics().
    if (window != NULL)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    // Full renderer reinit: sets mode (creates new window + GL context), reloads textures.
    if (!re.Init())
        Com_Error(ERR_FATAL, "VID_CheckChanges: re.Init() failed after mode change");

    cls.disable_screen = false;
}

// Initialize video modes
void VID_InitModes(viddef_t* modes, int num_modes)
{
	// Convert viddef_t array to vidmode_t array
	if (vid_modes)
		free(vid_modes);
	
	vid_modes = (vidmode_t*)malloc(sizeof(vidmode_t) * num_modes);
	num_vid_modes = num_modes;
	
	for (int i = 0; i < num_modes; i++)
	{
		vid_modes[i].width = modes[i].width;
		vid_modes[i].height = modes[i].height;
		vid_modes[i].mode = i;
		snprintf(vid_modes[i].description, sizeof(vid_modes[i].description), 
			"%dx%d", modes[i].width, modes[i].height);
	}
}
