/*
* snd_dll.c -- Sound DLL interface (Unix)
 *
 * Copyright 1998 Raven Software
 * Unix port by morb
 */

#include "../../../qcommon/qcommon.h"
#include "snd_dll.h"
#include "../client/client.h"

// External declaration from snd_main.c
extern snd_export_t GetSoundAPI(const snd_import_t snd_import);

// Sound library info
sndlib_info_t sndlib_infos[MAX_SNDLIBS];
int num_sndlib_infos = 0;
cvar_t* snd_dll = NULL;

// Sound initialization
void SND_Init(void)
{
	Com_Printf("\n------- Initializing sound -------\n");

	snd_dll = Cvar_Get("snd_dll", DEFAULT_SOUND_LIBRARY_NAME, CVAR_ARCHIVE);

	snd_import_t si;

	si.cl = &cl;
	si.cls = &cls;
	si.cl_entities = cl_entities;
	si.cl_parse_entities = cl_parse_entities;

	si.Com_Error = Com_Error;
	si.Com_Printf = Com_Printf;
	si.Com_DPrintf = Com_DPrintf;

	si.Cvar_Get = Cvar_Get;
	si.Cvar_Set = Cvar_Set;
	si.Cmd_AddCommand = Cmd_AddCommand;
	si.Cmd_RemoveCommand = Cmd_RemoveCommand;
	si.Cmd_Argc = Cmd_Argc;
	si.Cmd_Argv = Cmd_Argv;

	si.FS_FOpenFile = FS_FOpenFile;
	si.FS_FCloseFile = FS_FCloseFile;
	si.FS_LoadFile = FS_LoadFile;
	si.FS_FreeFile = FS_FreeFile;
	si.FS_Gamedir = FS_Gamedir;

	si.Z_Malloc = Z_Malloc;
	si.Z_Free = Z_Free;

#ifdef _DEBUG
	si.DBG_IDEPrint = DBG_IDEPrint;
	si.DBG_HudPrint = DBG_HudPrint;
	si.DBG_AddBox = NULL;
	si.DBG_AddBbox = NULL;
	si.DBG_AddEntityBbox = NULL;
	si.DBG_AddLine = NULL;
	si.DBG_AddArrow = NULL;
#endif

	se = GetSoundAPI(si);

	if (se.api_version != SND_API_VERSION)
	{
		Com_Printf("Sound library has incompatible api_version (%d != %d)\n", se.api_version, SND_API_VERSION);
		memset(&se, 0, sizeof(se));
		Com_Printf("------------------------------------\n");
		return;
	}

	se.Init();
	Com_Printf("------------------------------------\n");
}

// Null sound initialization (for dedicated servers)
void SND_InitNull(void)
{
	Com_Printf("SND_InitNull: Null sound system initialized\n");
}

// Sound shutdown
void SND_Shutdown(void)
{
	if (se.Shutdown != NULL)
		se.Shutdown();
}
