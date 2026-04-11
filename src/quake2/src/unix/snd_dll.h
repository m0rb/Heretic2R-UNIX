//
// snd_dll.h -- Sound DLL interface (Unix stub)
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#ifndef _SND_DLL_H_
#define _SND_DLL_H_

#include "../../../qcommon/qcommon.h"

// Unix sound interface stub
// Sound is handled directly by snd_sdl3 on Unix

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
