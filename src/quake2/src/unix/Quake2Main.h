//
// Quake2Main.h
//
// Copyright (C) 1997-2001 Id Software, Inc.
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb

#ifndef _QUAKE2MAIN_H_
#define _QUAKE2MAIN_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <assert.h>

#include "../../../qcommon/q_Typedef.h"
#include "../../../qcommon/qcommon.h"

extern int Sys_Milliseconds(void);
extern long long Sys_Microseconds(void);

extern cvar_t *vid_mode;

extern void Qcommon_Init(int argc, char** argv);
extern void Qcommon_Frame(int msec);

#endif /* _QUAKE2MAIN_H_ */