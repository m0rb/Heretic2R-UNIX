//
// clfx_dll.c -- Client Effects library interface
//
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb

#include "compat.h"
#include "../client/client.h"
#include "clfx_dll_unix.h"
#include "qcommon.h"

client_fx_import_t fxi;
GetfxAPI_t GetfxAPI;
client_fx_export_t fxe;
void* clfx_library = NULL;
qboolean fxapi_initialized = false;

void CLFX_Init(void)
{
	fxi.FindSurface    = (void*)re.FindSurface;
	fxi.GetReferencedID = re.GetReferencedID;

	fxe = GetfxAPI(fxi);

	strcpy_s(client_string, sizeof(client_string), fxe.client_string);
	Com_Printf("CLFX_Init: Unix client effects system initialized\n");
}

void CLFX_LoadDll(void)
{
	strcpy_s(client_string, sizeof(client_string), "Heretic II v1.06");
	Com_Printf("CLFX_LoadDll: Unix client effects loaded\n");
}