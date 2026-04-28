//
// snd_dll.h -- Sound backend
//
// Copyright (C) 1997-2001 Id Software, Inc.
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb

#ifndef _SND_DLL_H_
#define _SND_DLL_H_

#include "../../../qcommon/qcommon.h"

extern void SND_Init(void);
extern void SND_InitNull(void);
extern void SND_Shutdown(void);

#define MAX_SNDLIBS 16
#define DEFAULT_SOUND_LIBRARY_NAME "snd_sdl3"

typedef struct sndlib_info_s
{
	char title[32];
	char id[16];
} sndlib_info_t;

extern sndlib_info_t sndlib_infos[MAX_SNDLIBS];
extern int num_sndlib_infos;
extern cvar_t* snd_dll;

#endif /* _SND_DLL_H_ */
