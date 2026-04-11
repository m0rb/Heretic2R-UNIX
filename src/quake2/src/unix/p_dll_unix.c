//
// p_dll.c -- Player DLL interface (Unix version)
//
// Copyright 1998 Raven Software
// Unix port by morb
//

#include <dlfcn.h>
#include "p_dll.h"
#ifdef GAME_DLL
	#include "g_local.h"
#else
	#include "dll_io_unix.h"
#endif
#include "qcommon.h"

// Structure containing functions and data pointers exported from the player DLL.
player_export_t	playerExport __attribute__((visibility("default")));

// Handle to player DLL.
static void* player_library = NULL;

// Define pointers to all the .so functions which other code will dynamically link with.
#ifndef PLAYER_DLL
void (*P_Init)(void);
void (*P_Shutdown)(void);
#endif

void (*P_PlayerReleaseRope)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_KnockDownPlayer)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayFly)(playerinfo_t* playerinfo, float dist) __attribute__((visibility("default")));
void (*P_PlaySlap)(playerinfo_t* playerinfo, float dist) __attribute__((visibility("default")));
void (*P_PlayScratch)(playerinfo_t* playerinfo, float dist) __attribute__((visibility("default")));
void (*P_PlaySigh)(playerinfo_t* playerinfo, float dist) __attribute__((visibility("default")));
void (*P_SpawnDustPuff)(playerinfo_t* playerinfo, float dist) __attribute__((visibility("default")));
void (*P_PlayerInterruptAction)(playerinfo_t* playerinfo) __attribute__((visibility("default")));

qboolean (*P_BranchCheckDismemberAction)(playerinfo_t* playerinfo, int weapon) __attribute__((visibility("default")));

void (*P_TurnOffPlayerEffects)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_AnimUpdateFrame)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerFallingDamage)(playerinfo_t* playerinfo) __attribute__((visibility("default")));

void (*P_PlayerBasicAnimReset)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerAnimReset)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerAnimSetLowerSeq)(playerinfo_t* playerinfo, int seq) __attribute__((visibility("default")));
void (*P_PlayerAnimSetUpperSeq)(playerinfo_t* playerinfo, int seq) __attribute__((visibility("default")));
void (*P_PlayerAnimUpperIdle)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerAnimLowerIdle)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerAnimUpperUpdate)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerAnimLowerUpdate)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerAnimSetVault)(playerinfo_t* playerinfo, int seq) __attribute__((visibility("default")));
void (*P_PlayerPlayPain)(playerinfo_t* playerinfo, int type) __attribute__((visibility("default")));

void (*P_PlayerIntLand)(playerinfo_t* playerinfo, float landspeed) __attribute__((visibility("default")));

void (*P_PlayerInit)(playerinfo_t* playerinfo, int complete_reset) __attribute__((visibility("default")));
void (*P_PlayerClearEffects)(const playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerUpdate)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerUpdateCmdFlags)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
void (*P_PlayerUpdateModelAttributes)(playerinfo_t* playerinfo) __attribute__((visibility("default")));

void (*P_Weapon_Ready)(playerinfo_t* playerinfo, gitem_t* Weapon) __attribute__((visibility("default")));
void (*P_Weapon_EquipSpell)(playerinfo_t* playerinfo, gitem_t* Weapon) __attribute__((visibility("default")));
void (*P_Weapon_EquipSwordStaff)(playerinfo_t* playerinfo, gitem_t* Weapon) __attribute__((visibility("default")));
void (*P_Weapon_EquipHellStaff)(playerinfo_t* playerinfo, gitem_t* Weapon) __attribute__((visibility("default")));
void (*P_Weapon_EquipBow)(playerinfo_t* playerinfo, gitem_t* Weapon) __attribute__((visibility("default")));
void (*P_Weapon_EquipArmor)(playerinfo_t* playerinfo, gitem_t* Weapon) __attribute__((visibility("default")));
int (*P_Weapon_CurrentShotsLeft)(playerinfo_t* playerinfo) __attribute__((visibility("default")));
int (*P_Defence_CurrentShotsLeft)(playerinfo_t* playerinfo, int intent) __attribute__((visibility("default")));

int (*P_GetItemIndex)(const gitem_t* item) __attribute__((visibility("default")));
gitem_t* (*P_GetItemByIndex)(int index) __attribute__((visibility("default")));
gitem_t* (*P_FindItemByClassname)(const char* classname) __attribute__((visibility("default")));
gitem_t* (*P_FindItem)(const char* pickup_name) __attribute__((visibility("default")));
void (*P_InitItems)(void) __attribute__((visibility("default")));

void P_Freelib(void) __attribute__((visibility("default")));
void P_Freelib(void)
{
	if (player_library != NULL)
	{
#ifndef PLAYER_DLL
		P_Shutdown();
#endif

#ifdef GAME_DLL
		gi.Sys_UnloadGameDll("Player", &player_library);
#else
		Sys_UnloadGameDll("Player", &player_library);
#endif

		player_library = NULL;

		// Nullify exported data pointers so CL_PredictMovement doesn't access freed memory
		// before P_Load() is called again.
		playerExport.PlayerSeqData = NULL;
		playerExport.PlayerChickenData = NULL;
	}
}

uint P_Load(char *name)
{
	DWORD playerdll_chksum;

	// If already loaded, skip unload/reload to avoid invalidating server entity
	// classname pointers that point into Player.so static data (g_items.c sets
	// ent->classname = item->classname, a pointer into the loaded .so).
	if (player_library != NULL)
	{
#ifdef GAME_DLL
		Com_Printf("---------- %s already loaded ----------\n", name);
#else
		Com_ColourPrintf(P_HEADER, "---------- %s already loaded ----------\n", name);
#endif
		return 0;
	}

	P_Freelib();

#ifdef GAME_DLL
	Com_Printf("---------- Loading %s ----------\n", name);
	gi.Sys_LoadGameDll(name, &player_library, &playerdll_chksum);
#else
	Com_ColourPrintf(P_HEADER, "---------- Loading %s ----------\n", name);
	Sys_LoadGameDll(name, &player_library, &playerdll_chksum);
#endif

	const GetPlayerAPI_t P_GetPlayerAPI = (GetPlayerAPI_t)dlsym(player_library, "GetPlayerAPI");
	if (P_GetPlayerAPI == NULL)
		Sys_Error("dlsym failed on GetPlayerAPI for library %s: %s", name, dlerror());

#ifndef PLAYER_DLL
	P_Init = (void (*)(void))dlsym(player_library, "P_Init");
	if (P_Init == NULL)
		Sys_Error("dlsym failed on P_Init for library %s: %s", name, dlerror());

	P_Shutdown = (void (*)(void))dlsym(player_library, "P_Shutdown");
	if (P_Shutdown == NULL)
		Sys_Error("dlsym failed on P_Shutdown for library %s: %s", name, dlerror());
#endif

	P_PlayerReleaseRope = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerReleaseRope");
	if (P_PlayerReleaseRope == NULL)
		Sys_Error("dlsym failed on P_PlayerReleaseRope for library %s: %s", name, dlerror());

	P_KnockDownPlayer = (void (*)(playerinfo_t*))dlsym(player_library, "KnockDownPlayer");
	if (P_KnockDownPlayer == NULL)
		Sys_Error("dlsym failed on P_KnockDownPlayer for library %s: %s", name, dlerror());

	P_PlayFly = (void (*)(playerinfo_t*, float))dlsym(player_library, "PlayFly");
	if (P_PlayFly == NULL)
		Sys_Error("dlsym failed on P_PlayFly for library %s: %s", name, dlerror());

	P_PlaySlap = (void (*)(playerinfo_t*, float))dlsym(player_library, "PlaySlap");
	if (P_PlaySlap == NULL)
		Sys_Error("dlsym failed on P_PlaySlap for library %s: %s", name, dlerror());

	P_PlayScratch = (void (*)(playerinfo_t*, float))dlsym(player_library, "PlayScratch");
	if (P_PlayScratch == NULL)
		Sys_Error("dlsym failed on P_PlayScratch for library %s: %s", name, dlerror());

	P_PlaySigh = (void (*)(playerinfo_t*, float))dlsym(player_library, "PlaySigh");
	if (P_PlaySigh == NULL)
		Sys_Error("dlsym failed on P_PlaySigh for library %s: %s", name, dlerror());

	P_SpawnDustPuff = (void (*)(playerinfo_t*, float))dlsym(player_library, "SpawnDustPuff");
	if (P_SpawnDustPuff == NULL)
		Sys_Error("dlsym failed on P_SpawnDustPuff for library %s: %s", name, dlerror());

	P_PlayerInterruptAction = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerInterruptAction");
	if (P_PlayerInterruptAction == NULL)
		Sys_Error("dlsym failed on P_PlayerInterruptAction for library %s: %s", name, dlerror());

	P_BranchCheckDismemberAction = (qboolean (*)(playerinfo_t*, int))dlsym(player_library, "BranchCheckDismemberAction");
	if (P_BranchCheckDismemberAction == NULL)
		Sys_Error("dlsym failed on P_BranchCheckDismemberAction for library %s: %s", name, dlerror());

	P_TurnOffPlayerEffects = (void (*)(playerinfo_t*))dlsym(player_library, "TurnOffPlayerEffects");
	if (P_TurnOffPlayerEffects == NULL)
		Sys_Error("dlsym failed on P_TurnOffPlayerEffects for library %s: %s", name, dlerror());

	P_AnimUpdateFrame = (void (*)(playerinfo_t*))dlsym(player_library, "AnimUpdateFrame");
	if (P_AnimUpdateFrame == NULL)
		Sys_Error("dlsym failed on P_AnimUpdateFrame for library %s: %s", name, dlerror());

	P_PlayerFallingDamage = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerFallingDamage");
	if (P_PlayerFallingDamage == NULL)
		Sys_Error("dlsym failed on P_PlayerFallingDamage for library %s: %s", name, dlerror());

	P_PlayerBasicAnimReset = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerBasicAnimReset");
	if (P_PlayerBasicAnimReset == NULL)
		Sys_Error("dlsym failed on P_PlayerBasicAnimReset for library %s: %s", name, dlerror());

	P_PlayerAnimReset = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerAnimReset");
	if (P_PlayerAnimReset == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimReset for library %s: %s", name, dlerror());

	P_PlayerAnimSetLowerSeq = (void (*)(playerinfo_t*, int))dlsym(player_library, "PlayerAnimSetLowerSeq");
	if (P_PlayerAnimSetLowerSeq == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimSetLowerSeq for library %s: %s", name, dlerror());

	P_PlayerAnimSetUpperSeq = (void (*)(playerinfo_t*, int))dlsym(player_library, "PlayerAnimSetUpperSeq");
	if (P_PlayerAnimSetUpperSeq == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimSetUpperSeq for library %s: %s", name, dlerror());

	P_PlayerAnimUpperIdle = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerAnimUpperIdle");
	if (P_PlayerAnimUpperIdle == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimUpperIdle for library %s: %s", name, dlerror());

	P_PlayerAnimLowerIdle = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerAnimLowerIdle");
	if (P_PlayerAnimLowerIdle == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimLowerIdle for library %s: %s", name, dlerror());

	P_PlayerAnimUpperUpdate = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerAnimUpperUpdate");
	if (P_PlayerAnimUpperUpdate == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimUpperUpdate for library %s: %s", name, dlerror());

	P_PlayerAnimLowerUpdate = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerAnimLowerUpdate");
	if (P_PlayerAnimLowerUpdate == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimLowerUpdate for library %s: %s", name, dlerror());

	P_PlayerAnimSetVault = (void (*)(playerinfo_t*, int))dlsym(player_library, "PlayerAnimSetVault");
	if (P_PlayerAnimSetVault == NULL)
		Sys_Error("dlsym failed on P_PlayerAnimSetVault for library %s: %s", name, dlerror());

	P_PlayerPlayPain = (void (*)(playerinfo_t*, int))dlsym(player_library, "PlayerPlayPain");
	if (P_PlayerPlayPain == NULL)
		Sys_Error("dlsym failed on P_PlayerPlayPain for library %s: %s", name, dlerror());

	P_PlayerIntLand = (void (*)(playerinfo_t*, float))dlsym(player_library, "PlayerIntLand");
	if (P_PlayerIntLand == NULL)
		Sys_Error("dlsym failed on P_PlayerIntLand for library %s: %s", name, dlerror());

	P_PlayerInit = (void (*)(playerinfo_t*, int))dlsym(player_library, "PlayerInit");
	if (P_PlayerInit == NULL)
		Sys_Error("dlsym failed on P_PlayerInit for library %s: %s", name, dlerror());

	P_PlayerClearEffects = (void (*)(const playerinfo_t*))dlsym(player_library, "PlayerClearEffects");
	if (P_PlayerClearEffects == NULL)
		Sys_Error("dlsym failed on P_PlayerClearEffects for library %s: %s", name, dlerror());

	P_PlayerUpdate = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerUpdate");
	if (P_PlayerUpdate == NULL)
		Sys_Error("dlsym failed on P_PlayerUpdate for library %s: %s", name, dlerror());

	P_PlayerUpdateCmdFlags = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerUpdateCmdFlags");
	if (P_PlayerUpdateCmdFlags == NULL)
		Sys_Error("dlsym failed on P_PlayerUpdateCmdFlags for library %s: %s", name, dlerror());

	P_PlayerUpdateModelAttributes = (void (*)(playerinfo_t*))dlsym(player_library, "PlayerUpdateModelAttributes");
	if (P_PlayerUpdateModelAttributes == NULL)
		Sys_Error("dlsym failed on P_PlayerUpdateModelAttributes for library %s: %s", name, dlerror());

	P_Weapon_Ready = (void (*)(playerinfo_t*, gitem_t*))dlsym(player_library, "Weapon_Ready");
	if (P_Weapon_Ready == NULL)
		Sys_Error("dlsym failed on P_Weapon_Ready for library %s: %s", name, dlerror());

	P_Weapon_EquipSpell = (void (*)(playerinfo_t*, gitem_t*))dlsym(player_library, "Weapon_EquipSpell");
	if (P_Weapon_EquipSpell == NULL)
		Sys_Error("dlsym failed on P_Weapon_EquipSpell for library %s: %s", name, dlerror());

	P_Weapon_EquipSwordStaff = (void (*)(playerinfo_t*, gitem_t*))dlsym(player_library, "Weapon_EquipSwordStaff");
	if (P_Weapon_EquipSwordStaff == NULL)
		Sys_Error("dlsym failed on P_Weapon_EquipSwordStaff for library %s: %s", name, dlerror());

	P_Weapon_EquipHellStaff = (void (*)(playerinfo_t*, gitem_t*))dlsym(player_library, "Weapon_EquipHellStaff");
	if (P_Weapon_EquipHellStaff == NULL)
		Sys_Error("dlsym failed on P_Weapon_EquipHellStaff for library %s: %s", name, dlerror());

	P_Weapon_EquipBow = (void (*)(playerinfo_t*, gitem_t*))dlsym(player_library, "Weapon_EquipBow");
	if (P_Weapon_EquipBow == NULL)
		Sys_Error("dlsym failed on P_Weapon_EquipBow for library %s: %s", name, dlerror());

	P_Weapon_EquipArmor = (void (*)(playerinfo_t*, gitem_t*))dlsym(player_library, "Weapon_EquipArmor");
	if (P_Weapon_EquipArmor == NULL)
		Sys_Error("dlsym failed on P_Weapon_EquipArmor for library %s: %s", name, dlerror());

	P_Weapon_CurrentShotsLeft = (int (*)(playerinfo_t*))dlsym(player_library, "Weapon_CurrentShotsLeft");
	if (P_Weapon_CurrentShotsLeft == NULL)
		Sys_Error("dlsym failed on P_Weapon_CurrentShotsLeft for library %s: %s", name, dlerror());

	P_Defence_CurrentShotsLeft = (int (*)(playerinfo_t*, int))dlsym(player_library, "Defence_CurrentShotsLeft");
	if (P_Defence_CurrentShotsLeft == NULL)
		Sys_Error("dlsym failed on P_Defence_CurrentShotsLeft for library %s: %s", name, dlerror());

	P_GetItemIndex = (int (*)(const gitem_t*))dlsym(player_library, "GetItemIndex");
	if (P_GetItemIndex == NULL)
		Sys_Error("dlsym failed on P_GetItemIndex for library %s: %s", name, dlerror());

	P_GetItemByIndex = (gitem_t* (*)(int))dlsym(player_library, "GetItemByIndex");
	if (P_GetItemByIndex == NULL)
		Sys_Error("dlsym failed on P_GetItemByIndex for library %s: %s", name, dlerror());

	P_FindItemByClassname = (gitem_t* (*)(const char*))dlsym(player_library, "FindItemByClassname");
	if (P_FindItemByClassname == NULL)
		Sys_Error("dlsym failed on P_FindItemByClassname for library %s: %s", name, dlerror());

	P_FindItem = (gitem_t* (*)(const char*))dlsym(player_library, "FindItem");
	if (P_FindItem == NULL)
		Sys_Error("dlsym failed on P_FindItem for library %s: %s", name, dlerror());

	P_InitItems = (void (*)(void))dlsym(player_library, "InitItems");
	if (P_InitItems == NULL)
		Sys_Error("dlsym failed on P_InitItems for library %s: %s", name, dlerror());

#ifndef PLAYER_DLL
	P_Init();
#endif

	playerExport = P_GetPlayerAPI();

#ifdef GAME_DLL
	Com_Printf("------------------------------------\n");
#else
	Com_ColourPrintf(P_HEADER, "------------------------------------\n");
#endif

	return playerdll_chksum;
}