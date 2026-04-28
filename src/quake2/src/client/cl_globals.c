//
// cl_globals.c -- Client global variables for Unix
//
// Copyright 1998 Raven Software
//

#include "client.h"
#include "../unix/vid_dll.h"
#include "../unix/snd_dll.h"
#include "../unix/clfx_dll_unix.h"

// Renderer exports (from ref_gl)
refexport_t re;

// Sound exports (from snd_dll)
snd_export_t se;

// Server exports - cls is defined in cl_main.c

// Video definition
viddef_t viddef;

// Client effects exports (from CLFX DLL) - defined in unix/clfx_dll.c
// client_fx_export_t fxe;
// client_fx_import_t fxi;
// GetfxAPI_t GetfxAPI;
// void* clfx_library = NULL;
// qboolean fxapi_initialized = false;

// Sound library info - defined in unix/snd_dll.c
// sndlib_info_t sndlib_infos[MAX_SNDLIBS];
// int num_sndlib_infos = 0;
// cvar_t* snd_dll = NULL;

// Video cvars - defined in unix/vid_sdl3.c
// qboolean vid_restart_required = false;
// cvar_t* vid_gamma = NULL;
// cvar_t* vid_brightness = NULL;
// cvar_t* vid_contrast = NULL;
// cvar_t* vid_ref = NULL;
// cvar_t* vid_mode = NULL;

// Video mode info - defined in unix/vid_sdl3.c
// reflib_info_t reflib_infos[MAX_REFLIBS];
// int num_reflib_infos = 0;
// vidmode_t* vid_modes = NULL;
// int num_vid_modes = 0;

// Input state variables are defined in cl_input.c
