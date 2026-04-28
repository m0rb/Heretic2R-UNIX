//
// vid_dll.h -- SDL3 video backend
//
// Copyright (C) 1997-2001 Id Software, Inc.
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb

#ifndef _VID_DLL_H_
#define _VID_DLL_H_

#include "../../../qcommon/qcommon.h"

extern void VID_Error(int err_level, const char* fmt, ...);
extern void VID_Printf(int print_level, const char* fmt, ...);

extern cvar_t *vid_gamma;
extern cvar_t *vid_brightness;
extern cvar_t *vid_contrast;
extern cvar_t *vid_mode;

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
extern SDL_Window* VID_GetSDLWindow(void);

#endif /* _VID_DLL_H_ */
