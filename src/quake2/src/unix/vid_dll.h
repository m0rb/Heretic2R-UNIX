//
// vid_dll.h -- Video DLL interface stub for Unix
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#ifndef _VID_DLL_H_
#define _VID_DLL_H_

#include "../../../qcommon/qcommon.h"

// Unix video stub - variadic to match fxi interface
extern void VID_Error(int err_level, const char* fmt, ...);
extern void VID_Printf(int print_level, const char* fmt, ...);

// Video cvars (defined in vid_sdl3.c)
extern cvar_t *vid_gamma;
extern cvar_t *vid_brightness;
extern cvar_t *vid_contrast;
extern cvar_t *vid_mode;

// Video mode and renderer info (for menu_video.c compatibility)
typedef struct
{
	int width;
	int height;
	int mode;
	char description[64];
} vidmode_t;

#define MAX_REFLIBS 8
#define MAX_DISPLAYED_VIDMODES 16

typedef struct
{
	char title[64];
	char id[32];
} reflib_info_t;

extern reflib_info_t reflib_infos[MAX_REFLIBS];
extern int num_reflib_infos;
extern vidmode_t *vid_modes;
extern int num_vid_modes;
extern cvar_t *vid_ref;
extern qboolean vid_restart_required;

#include <SDL3/SDL.h>
extern SDL_Window* VID_GetSDLWindow(void); // Returns the active SDL window for input grab etc.

#endif /* _VID_DLL_H_ */
