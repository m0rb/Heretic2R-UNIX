//
// clfx_dll.h -- Client Effects library interface.
//
// Copyright 1998 Raven Software
//

#pragma once

#ifdef _WIN32
#include <windows.h>
#include "client/client.h"

extern client_fx_import_t fxi;
extern GetfxAPI_t GetfxAPI;
extern HINSTANCE clfx_library;
extern qboolean fxapi_initialized;
#else
#include "../../unix/clfx_dll_unix.h"
#endif // _WIN32
