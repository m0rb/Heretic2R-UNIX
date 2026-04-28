//
// dll_io.h -- Windows-specific dll loading/unloading logic.
//
// Copyright 1998 Raven Software
//

#pragma once

#ifdef _WIN32
#include <windows.h>

extern void Sys_LoadGameDll(const char* dll_name, HINSTANCE* hinst, DWORD* checksum);
extern void Sys_UnloadGameDll(const char* name, HINSTANCE* hinst);
#endif // _WIN32
