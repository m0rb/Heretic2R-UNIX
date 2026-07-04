//
// vid_sdl3.c -- SDL3 video backend
//
// Copyright (C) 1997-2001 Id Software, Inc.
// Copyright (C) 2010 Yamagi Burmeister
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.


#include <SDL3/SDL.h>
#include "../../../qcommon/qcommon.h"
#include "../client/client.h"
#include "../client/cl_skeletons.h"
#include "vid_dll.h"
#include "../client/glimp_sdl3.h"
#include "../../../ref_gl1/src/gl1_Local.h"
#include "../win32/vid_Screenshot.h"

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
cvar_t *vid_highdpiaware;
cvar_t *vid_displayindex;
cvar_t *vid_rate;
qboolean vid_restart_required;

vidmode_t *vid_modes = NULL;
int num_vid_modes = 0;
reflib_info_t reflib_infos[MAX_REFLIBS];
int num_reflib_infos = 0;

extern refexport_t re;

qboolean VID_GetModeInfo(int *width, int *height, int mode);
qboolean VID_InitGraphics(int width, int height);
refexport_t GetRefAPI(refimport_t rimp);

static qboolean VID_LoadRefresh(void)
{
    refimport_t ri;

    Com_Printf("------- Loading renderer -------\n");

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
    ri.Vid_WriteScreenshot = VID_WriteScreenshot;
#ifdef _DEBUG
    ri.DBG_IDEPrint = DBG_IDEPrint;
    ri.DBG_HudPrint = DBG_HudPrint;
#endif

    re = GetRefAPI(ri);

    if (re.api_version != REF_API_VERSION)
    {
        Com_Printf("Renderer has incompatible api_version %i!\n", re.api_version);
        return false;
    }

    if (!re.Init())
    {
        Com_Printf("Failed to initialize renderer!\n");
        memset(&re, 0, sizeof(re)); 
        return false;
    }

    Com_Printf("--------------------------------\n");
    return true;
}

static void VID_Restart_f(void)
{
    vid_restart_required = true;
}

void VID_Init(void)
{
    vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
    vid_width = Cvar_Get("vid_width", "640", CVAR_ARCHIVE);
    vid_height = Cvar_Get("vid_height", "480", CVAR_ARCHIVE);
    vid_highdpiaware = Cvar_Get("vid_highdpiaware", "1", CVAR_ARCHIVE);
    vid_displayindex = Cvar_Get("vid_displayindex", "0", CVAR_ARCHIVE);
    vid_rate = Cvar_Get("vid_rate", "-1", CVAR_ARCHIVE);

    vid_ref = Cvar_Get("vid_ref", "gl1", CVAR_ARCHIVE);

    Cmd_AddCommand("vid_restart", VID_Restart_f);

    VID_LoadRefresh();

    // Force vid_ref to "gl1" for now. Circle back when we have more renderers.
    Cvar_Set("vid_ref", "gl1");
    vid_ref->modified = false; 
}

qboolean VID_GetModeInfo(int *width, int *height, int mode)
{
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

static const struct { int w, h; } vid_std_modes[] = {
    { 320, 240 },   { 400, 300 },   { 512, 384 },   { 640, 400 },
    { 640, 480 },   { 800, 500 },   { 800, 600 },   { 960, 720 },
    { 1024, 480 },  { 1024, 640 },  { 1024, 768 },  { 1152, 768 },
    { 1152, 864 },  { 1280, 800 },  { 1280, 720 },  { 1280, 960 },
    { 1280, 1024 }, { 1366, 768 },  { 1440, 900 },  { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2048, 1536 },
    { 2560, 1080 }, { 2560, 1440 }, { 2560, 1600 }, { 3440, 1440 },
    { 3840, 1600 }, { 3840, 2160 }, { 4096, 2160 }, { 5120, 2880 },
    { 1600, 900 },
};

int VID_GetNumDisplays(void)
{
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids != NULL)
        SDL_free(ids);
    return (count > 0) ? count : 1;
}

static SDL_DisplayID VID_SelectedDisplay(void)
{
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    SDL_DisplayID result = SDL_GetPrimaryDisplay();

    if (ids != NULL)
    {
        const int idx = (int)vid_displayindex->value;
        if (idx >= 0 && idx < count)
            result = ids[idx];
        SDL_free(ids);
    }

    return result;
}

static float VID_GetDisplayDensity(void)
{
    const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(VID_SelectedDisplay());
    return (dm != NULL && dm->pixel_density > 0.0f) ? dm->pixel_density : 1.0f;
}

static void VID_BuildModeList(void)
{
    const SDL_DisplayID disp = VID_SelectedDisplay();
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(disp);
    const float density = (desktop != NULL && desktop->pixel_density > 0.0f) ? desktop->pixel_density : 1.0f;

    const int desk_w = (desktop != NULL) ? (int)(desktop->w * density + 0.5f) : DEF_WIDTH;
    const int desk_h = (desktop != NULL) ? (int)(desktop->h * density + 0.5f) : DEF_HEIGHT;

    const int num_std = (int)(sizeof(vid_std_modes) / sizeof(vid_std_modes[0]));
    viddef_t* list = malloc(sizeof(viddef_t) * (num_std + 1));
    int n = 0;

    list[0].width  = desk_w;
    list[0].height = desk_h;
    n = 1;

    for (int i = 0; i < num_std; i++)
    {
        const int w = vid_std_modes[i].w;
        const int h = vid_std_modes[i].h;

        if (w > desk_w || h > desk_h)
            continue;

        qboolean dup = false;
        for (int c = 0; c < n; c++)
            if (list[c].width == w && list[c].height == h) { dup = true; break; }

        if (!dup)
        {
            list[n].width  = w;
            list[n].height = h;
            n++;
        }
    }

    VID_InitModes(list, n);
    free(list);
}

static SDL_Window* VID_CreateWindow(int w, int h, SDL_WindowFlags flags, SDL_DisplayID disp)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    const Sint64 pos = (Sint64)SDL_WINDOWPOS_CENTERED_DISPLAY(disp);

    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Heretic2R");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, pos);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, pos);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, (Sint64)flags);

    SDL_Window* win = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    return win;
}

qboolean VID_InitGraphics(int width, int height)
{
#if !defined(__APPLE__) && !defined(__HAIKU__)
    if (SDL_getenv("SDL_VIDEODRIVER") == NULL)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif

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

    static SDL_DisplayID mode_list_display = 0;
    const SDL_DisplayID sel_display = VID_SelectedDisplay();
    if (num_vid_modes == 0 || sel_display != mode_list_display)
    {
        VID_BuildModeList();
        mode_list_display = sel_display;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const int msaa = (int)Cvar_VariableValue("r_msaa_samples");
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa > 0 ? msaa : 0);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    switch ((int)vid_fullscreen->value)
    {
        case 1:  flags |= SDL_WINDOW_FULLSCREEN; break;
        case 2:  flags |= SDL_WINDOW_BORDERLESS; break;
        default: break;
    }

    const char* driver = SDL_GetCurrentVideoDriver();
    const qboolean highdpi = (vid_highdpiaware->value != 0) && (driver != NULL) &&
                             (SDL_strcmp(driver, "wayland") == 0 || SDL_strcmp(driver, "x11") == 0);
    if (highdpi)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    int create_w = width, create_h = height;
    if (highdpi)
    {
        const float density = VID_GetDisplayDensity();
        if (density > 1.0f)
        {
            create_w = (int)(width / density + 0.5f);
            create_h = (int)(height / density + 0.5f);
        }
    }

    window = VID_CreateWindow(create_w, create_h, flags, sel_display);

    if (!window && msaa > 0) {
        Com_Printf("VID_InitGraphics: MSAA x%d unavailable, retrying without it: %s\n", msaa, SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        Cvar_SetValue("r_msaa_samples", 0);
        window = VID_CreateWindow(create_w, create_h, flags, sel_display);
    }

    if (!window) {
        Com_Printf("VID_InitGraphics: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    if ((int)vid_fullscreen->value == 1)
    {
        SDL_DisplayMode closest;
        const float rate = vid_rate->value;

        if (SDL_GetClosestFullscreenDisplayMode(sel_display, width, height, (rate > 0.0f) ? rate : 0.0f, false, &closest))
        {
            if (!SDL_SetWindowFullscreenMode(window, &closest))
                Com_Printf("VID_InitGraphics: exclusive mode %dx%d@%g failed, using desktop fullscreen: %s\n",
                           width, height, rate, SDL_GetError());
            else
                SDL_SyncWindow(window);
        }
        else
        {
            Com_Printf("VID_InitGraphics: no exclusive mode near %dx%d@%g, using desktop fullscreen\n", width, height, rate);
        }
    }

    if (!re.InitContext(window)) {
        Com_Printf("VID_InitGraphics: InitContext failed\n");
        SDL_DestroyWindow(window);
        window = NULL;
        return false;
    }

    {
        int got_buffers = 0, got_samples = 0;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &got_buffers);
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &got_samples);
        if (msaa > 0)
            Com_Printf("MSAA: requested x%d, got %d buffer(s) / x%d samples%s\n",
                       msaa, got_buffers, got_samples,
                       (got_samples < msaa) ? " (driver downgraded/denied)" : "");
        else
            Com_Printf("MSAA: disabled\n");
    }

    if (highdpi)
    {
        int draw_w = width, draw_h = height;
        SDL_GetWindowSizeInPixels(window, &draw_w, &draw_h);
        viddef.width  = draw_w;
        viddef.height = draw_h;

        if (draw_w != width || draw_h != height)
            Com_Printf("VID_InitGraphics: high-dpi drawable %dx%d (requested %dx%d)\n",
                       draw_w, draw_h, width, height);
    }
    else
    {
        viddef.width  = width;
        viddef.height = height;
    }

    return true;
}

void VID_BeginFrame(float camera_separation) {}
void VID_EndFrame(void)
{
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

void VID_Error(int err_level, const char* fmt, ...)
{
    va_list argptr;
    char msg[1024];
    
    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    
    Com_Error(err_level, "%s", msg);
}

void VID_Printf(int print_level, const char* fmt, ...)
{
    va_list argptr;
    char msg[1024];
    
    va_start(argptr, fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    
    Com_Printf("%s", msg);
}

// Check for video changes; mirrors win32/vid_dll.c::VID_CheckChanges
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

    if (re.ShutdownContext != NULL)
        re.ShutdownContext();

    if (window != NULL)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    GLimp_ResetGrabState();

    if (re.Init == NULL || !re.Init())
    {
        cls.disable_screen = false;
        return;
    }

    cls.disable_screen = false;

    SCR_UpdateUIScale();
}

void VID_InitModes(viddef_t* modes, int num_modes)
{
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
