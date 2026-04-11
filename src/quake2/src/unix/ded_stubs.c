//
// ded_stubs.c -- Stub implementations of client-side functions for the dedicated server build.
//
// The dedicated server binary does not compile any client, renderer, sound,
// or SDL code. Any server or common code that calls into those subsystems
// goes through this file instead.
//
// Unix port by morb
//

#include "Quake2Main.h"
#include "qcommon.h"
#include "client.h"
#include "console.h"
#include "screen.h"
#include "keys.h"
#include "cl_skeletons.h"

// ---------------------------------------------------------------------------
// Sound (forward declarations — defined at bottom, used by CL_Init above)
// ---------------------------------------------------------------------------

void SND_Init(void);
void SND_InitNull(void);
void SND_Shutdown(void);

// ---------------------------------------------------------------------------
// client_static_t / client_state_t
// common.c reads cls.state; pmove.c reads cl.refdef.time.
// ---------------------------------------------------------------------------

client_static_t cls;
client_state_t  cl;

// ---------------------------------------------------------------------------
// Console struct
// common.c directly accesses con.current_color when printing messages.
// ---------------------------------------------------------------------------

console_t con;

// ---------------------------------------------------------------------------
// Client cvars referenced by common.c frame timing
// ---------------------------------------------------------------------------

cvar_t* cl_timedemo;
cvar_t* cl_maxfps;

// ---------------------------------------------------------------------------
// Version string sent to connecting clients
// ---------------------------------------------------------------------------

char client_string[128] = "Heretic II v1.06";

// ---------------------------------------------------------------------------
// Skeletal animation data referenced by netmsg_read.c
// ---------------------------------------------------------------------------

CL_SkeletalJoint_t skeletal_joints[MAX_ARRAYED_SKELETAL_JOINTS];

void SK_CreateSkeleton(int structure, int root_index) {}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

void Con_Init(void)
{
	cl_timedemo = Cvar_Get("timedemo",  "0",  0);
	cl_maxfps   = Cvar_Get("cl_maxfps", "30", 0);
}

void Con_Print(const char* txt) {} // common.c already calls Sys_ConsoleOutput after Con_Print.
void Con_ClearNotify(void) {}

// ---------------------------------------------------------------------------
// Client subsystem
// ---------------------------------------------------------------------------

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

// sv_user.c calls Cmd_ForwardToServer; no local client to forward to.
void Cmd_ForwardToServer(void) {}

// ---------------------------------------------------------------------------
// Screen / HUD
// ---------------------------------------------------------------------------

void SCR_BeginLoadingPlaque(void) {}
void SCR_EndLoadingPlaque(void)   {}
void SCR_DebugGraph(float value, uint color) {}
void Reset_Screen_Shake(void) {}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Key_Init(void) {}
void IN_Init(void) {}
void IN_Shutdown(void) {}
void IN_Update(void) {}
void In_FlushQueue(void) {}
void IN_Move(usercmd_t* cmd) {}
void IN_GetClipboardText(char* out, size_t n) { if (out && n) out[0] = '\0'; }

// input.h declares this inline; provide the definition for dedicated builds.
void Sys_CpuPause(void) {}

// ---------------------------------------------------------------------------
// Video / renderer
// ---------------------------------------------------------------------------

void VID_Init(void)         {}
void VID_Shutdown(void)     {}
void VID_CheckChanges(void) {}

// ---------------------------------------------------------------------------
// Sound (replaces snd_dll.c which depends on the full client state)
// ---------------------------------------------------------------------------

void SND_Init(void)     { SND_InitNull(); }
void SND_InitNull(void) { Com_Printf("SND_InitNull: no sound on dedicated server\n"); }
void SND_Shutdown(void) {}

// ---------------------------------------------------------------------------
// Client effects DLL (not loaded on a dedicated server)
// ---------------------------------------------------------------------------

void CLFX_LoadDll(void)   {}
void CLFX_UnloadDll(void) {}
