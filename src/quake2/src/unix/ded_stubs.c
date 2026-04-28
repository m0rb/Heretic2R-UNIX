//
// ded_stubs.c -- Dedicated server client-side stubs
//
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb

#include "Quake2Main.h"
#include "qcommon.h"
#include "client.h"
#include "console.h"
#include "screen.h"
#include "keys.h"
#include "cl_skeletons.h"

void SND_Init(void);
void SND_InitNull(void);
void SND_Shutdown(void);

client_static_t cls;
client_state_t  cl;

console_t con;

cvar_t* cl_timedemo;
cvar_t* cl_maxfps;
cvar_t* cl_minfps;

char client_string[128] = "Heretic II v1.06";

CL_SkeletalJoint_t skeletal_joints[MAX_ARRAYED_SKELETAL_JOINTS];

void SK_CreateSkeleton(int structure, int root_index) {}

void Con_Init(void)
{
	cl_timedemo = Cvar_Get("timedemo",  "0",  0);
	cl_maxfps   = Cvar_Get("cl_maxfps", "30", 0);
	cl_minfps   = Cvar_Get("cl_minfps",  "5", 0);
}

void Con_Print(const char* txt) {} 
void Con_ClearNotify(void) {}

void CL_Init(void)
{
	Con_Init();
	SND_InitNull();
}

void CL_Drop(void)         {}
H2R_NORETURN void CL_Disconnect_f(void) { Sys_Quit(); }
void CL_Shutdown(void)     {}

void CL_Frame(const int packetdelta, const int renderdelta, const int timedelta,
              const qboolean packetframe, const qboolean renderframe) {}

void CL_MusicGetCurrentTrackInfo(int* track, uint* track_pos, qboolean* looping)
{
	if (track)     *track     = 0;
	if (track_pos) *track_pos = 0;
	if (looping)   *looping   = false;
}

void Cmd_ForwardToServer(void) {}

void SCR_BeginLoadingPlaque(void) {}
void SCR_EndLoadingPlaque(void)   {}
void SCR_DebugGraph(float value, uint color) {}
void Reset_Screen_Shake(void) {}

void Key_Init(void) {}
void IN_Init(void) {}
void IN_Shutdown(void) {}
void IN_Update(void) {}
void In_FlushQueue(void) {}
void IN_Move(usercmd_t* cmd) {}
void IN_GetClipboardText(char* out, size_t n) { if (out && n) out[0] = '\0'; }

void Sys_CpuPause(void) {}

void VID_Init(void)         {}
void VID_Shutdown(void)     {}
void VID_CheckChanges(void) {}

void SND_Init(void)     { SND_InitNull(); }
void SND_InitNull(void) { Com_Printf("SND_InitNull: no sound on dedicated server\n"); }
void SND_Shutdown(void) {}

void CLFX_LoadDll(void)   {}
void CLFX_UnloadDll(void) {}
