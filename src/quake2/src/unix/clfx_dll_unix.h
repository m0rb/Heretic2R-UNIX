//
// clfx_dll_unix.h -- Client Effects library interface (Unix version).
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#pragma once

#include "client.h"

extern client_fx_import_t fxi;
extern GetfxAPI_t GetfxAPI;
extern void* clfx_library; // Unix: void* instead of HINSTANCE
extern qboolean fxapi_initialized;

extern void CLFX_Init(void);
extern void CLFX_LoadDll(void);