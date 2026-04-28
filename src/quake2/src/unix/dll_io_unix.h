//
// dll_io_unix.h -- Shared library loading interface
//
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb

#pragma once

#include <dlfcn.h>
#include "../../../qcommon/q_Typedef.h"

#ifndef DWORD
typedef unsigned long DWORD;
#endif

void* Sys_LoadLibrary(const char* name);
void* Sys_GetProcAddress(void* library, const char* name);
void Sys_FreeLibrary(void* library);

qboolean Sys_LoadGameDll(const char* name, void** library, DWORD* checksum);
void Sys_UnloadGameDll(const char* name, void** library);
