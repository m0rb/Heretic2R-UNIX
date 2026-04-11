//
// dll_io_unix.h -- Unix DLL loading interface
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#pragma once

#include <dlfcn.h>
#include "../../../qcommon/q_Typedef.h"

// Define DWORD for Unix
#ifndef DWORD
typedef unsigned long DWORD;
#endif

// DLL loading functions for Unix
void* Sys_LoadLibrary(const char* name);
void* Sys_GetProcAddress(void* library, const char* name);
void Sys_FreeLibrary(void* library);

// DLL loading functions
qboolean Sys_LoadGameDll(const char* name, void** library, DWORD* checksum);
void Sys_UnloadGameDll(const char* name, void** library);
