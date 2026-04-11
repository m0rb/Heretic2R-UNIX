//
// clfx_dll.c -- Client Effects library interface (Unix version).
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#include "compat.h"
#include "../client/client.h"
#include "clfx_dll_unix.h"
#include "qcommon.h"

// Client effects API variables
client_fx_import_t fxi;
client_fx_export_t fxe;
GetfxAPI_t GetfxAPI;
void* clfx_library = NULL;
qboolean fxapi_initialized = false;

// Initialize client effects system.
// Mirrors the Windows version: fills in renderer-provided imports and calls GetfxAPI to populate fxe.
void CLFX_Init(void)
{
	fxi.FindSurface    = (void*)re.FindSurface; // const vs non-const vec3_t mismatch
	fxi.GetReferencedID = re.GetReferencedID;

	fxe = GetfxAPI(fxi);

	// morb was here. copy fxe.client_string → client_string so CL_ParseServerData can compare it
	// at connect time (line 387). On Windows, CLFX_LoadDll does this after loading the DLL.
	// On Unix, CLFX_LoadDll is server-only and runs before GetfxAPI is available, so we do it here
	// on the client path once GetfxAPI has populated fxe.
	strcpy_s(client_string, sizeof(client_string), fxe.client_string);

	Com_Printf("CLFX_Init: Unix client effects system initialized\n");
}

// Load client effects DLL (stub for Unix)
void CLFX_LoadDll(void)
{
	// morb was here. set client_string so sv_user.c writes the correct version string into
	// the server data packet. On Windows, CLFX_LoadDll loads the DLL, calls GetfxAPI, then
	// strcpy_s(client_string, fxe.client_string). On Unix we are statically linked and
	// GetfxAPI is only wired up on the client path, so set the known constant directly.
	// On Unix, we link directly instead of loading a DLL.
	// The client effects functions are already linked into the executable.
	strcpy_s(client_string, sizeof(client_string), "Heretic II v1.06");

	Com_Printf("CLFX_LoadDll: Unix client effects loaded\n");
}