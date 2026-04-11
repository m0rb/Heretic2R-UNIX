//
// Quake2Main.h -- Unix main header
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#ifndef _QUAKE2MAIN_H_
#define _QUAKE2MAIN_H_

// Unix-specific includes
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

// Standard types
#include "../../../qcommon/q_Typedef.h"
#include "../../../qcommon/qcommon.h"

// System functions
extern int Sys_Milliseconds(void);
extern long long Sys_Microseconds(void);

// Video mode cvar (declared in vid_sdl3.c)
extern cvar_t *vid_mode;

// Main entry point
extern void Qcommon_Init(int argc, char** argv);
extern void Qcommon_Frame(int msec);

#endif /* _QUAKE2MAIN_H_ */