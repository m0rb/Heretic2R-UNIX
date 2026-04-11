//
// clfx_stub.c -- Minimal Client Effects stub for Unix.
//
// Provides entity rendering (AddPacketEntities) without particle/visual effects.
// This is sufficient for basic gameplay: player model, items, world entities all
// render correctly. Particle effects, tracers, etc. are absent.
//
// Implements the client_fx_export_t interface defined by Raven Software (Copyright 1998).
// This file is original work by morb for the Heretic2R Unix port.
//

#include <string.h>
#include "../client/client.h"
#include "../../../qcommon/qcommon.h"
#include "../../../qcommon/Skeletons.h"
#include "../../../qcommon/Vector.h"
#include "../../../qcommon/EffectFlags.h"
#include "../../../qcommon/ResourceManager.h"

#ifndef __linux__
#define __attribute__(x)
#endif

#define CLFX_DECLSPEC __attribute__((visibility("default")))

static client_fx_import_t fxi;

static char clfx_string[128] = "Heretic II v1.06";

// ============================================================
// Light style state (mirrors CL_LightStyles / LightStyles.c)
// ============================================================

typedef struct
{
	int   length;
	float map[MAX_QPATH];
} stub_lightstyle_t;

static stub_lightstyle_t stub_lightstyles[MAX_LIGHTSTYLES];
static int stub_ls_lastofs = -1;

// Compute current light style values from pattern + time, then push into
// fxi.cls->r_lightstyles so the renderer can use them.
static void Stub_RunAndAddLightStyles(void)
{
	// Sample at 10 Hz (100 ms buckets) like the real CL_RunLightStyles.
	const int ofs = (int)(*fxi.leveltime * 10.0f);

	if (ofs != stub_ls_lastofs)
	{
		stub_ls_lastofs = ofs;

		for (int i = 0; i < MAX_LIGHTSTYLES; i++)
		{
			const stub_lightstyle_t* ls = &stub_lightstyles[i];
			float value;
			if (ls->length == 0)
				value = 1.0f;
			else if (ls->length == 1)
				value = ls->map[0];
			else
				value = ls->map[ofs % ls->length];

			lightstyle_t* rls = &fxi.cls->r_lightstyles[i];
			rls->rgb[0] = value;
			rls->rgb[1] = value;
			rls->rgb[2] = value;
			rls->white  = value * 3.0f;
		}
	}
}

// ============================================================
// Stub implementations
// ============================================================

static void Stub_Init(void) {}
static void Stub_ShutDown(void) {}
static void Stub_Clear(void)
{
	memset(stub_lightstyles, 0, sizeof(stub_lightstyles));
	stub_ls_lastofs = -1;
}
static void Stub_RegisterSounds(void) {}
static void Stub_RegisterModels(void) {}
static void Stub_RemoveClientEffects(centity_t* cent) { (void)cent; }
static void Stub_AddEffects(qboolean freeze) { (void)freeze; Stub_RunAndAddLightStyles(); }
static void Stub_UpdateEffects(void) { Stub_RunAndAddLightStyles(); }
static void Stub_SetLightstyle(const int i)
{
	const char* s = fxi.cl->configstrings[i + CS_LIGHTS];
	const int len = (int)strlen(s);
	stub_lightstyles[i].length = len < MAX_QPATH ? len : MAX_QPATH - 1;
	for (int j = 0; j < stub_lightstyles[i].length; j++)
		stub_lightstyles[i].map[j] = (float)(s[j] - 'a') / (float)('m' - 'a');
	stub_ls_lastofs = -1; // Force recompute on next frame.
}
static level_map_info_t* Stub_GetLMI(void) { return NULL; }
static int Stub_GetLMIMax(void) { return 0; }

// Format strings for each FX type (indexed by FX_Type_t enum from FX.h).
// Used to skip/consume the correct number of bytes from the network message.
// Extracted from src/client effects/Client Effects.c - clientEffectSpawners[].formatString.
static const char* fx_format_strings[] = {
    "s",         // [0]  FX_REMOVE_EFFECTS
    NULL,        // [1]  FX_TEST
    NULL,        // [2]  FX_EXPLOSION1
    NULL,        // [3]  FX_EXPLOSION2
    "b",         // [4]  FX_SPLASH
    NULL,        // [5]  FX_GIB_TRAIL
    "ub",        // [6]  FX_BLOOD
    "d",         // [7]  FX_BLOOD_TRAIL
    "bb",        // [8]  FX_LINKED_BLOOD
    "d",         // [9]  FX_GENERIC_SPARKS
    NULL,        // [10] FX_PLAYER_TELEPORT_IN
    NULL,        // [11] FX_PLAYER_TELEPORT_OUT
    "b",         // [12] FX_HEALTH_PICKUP
    "b",         // [13] FX_WEAPON_PICKUP
    "bv",        // [14] FX_DEFENSE_PICKUP
    "b",         // [15] FX_PUZZLE_PICKUP
    "t",         // [16] FX_AMMO_PICKUP
    "d",         // [17] FX_FLYINGFIST
    NULL,        // [18] FX_FLYINGFIST_EXPLODE
    NULL,        // [19] FX_BLUE_RING
    NULL,        // [20] FX_METEOR_BARRIER
    NULL,        // [21] FX_METEOR_BARRIER_TRAVEL
    NULL,        // [22] FX_METEOR_BARRIER_EXPLODE
    NULL,        // [23] FX_LIGHTNING_SHIELD
    "d",         // [24] FX_SOA
    NULL,        // [25] FX_SOA_GLOWBALLS
    "s",         // [26] FX_SOA_EXPLODE
    "s",         // [27] FX_SOA_POWER
    "db",        // [28] FX_SOA_PLAYER_EXPLODE
    "xbb",       // [29] FX_MAGIC_MISSILE
    "db",        // [30] FX_MAGIC_MISSILE_EXPLODE
    "ss",        // [31] FX_BLAST
    "d",         // [32] FX_RED_RAIN_MISSILE
    "sssssss",   // [33] FX_RED_RAIN
    "t",         // [34] FX_RED_RAIN_GLOW
    NULL,        // [35] FX_MACEBALL
    "b",         // [36] FX_MACEBALL_BOUNCE
    NULL,        // [37] FX_MACEBALL_EXPLODE
    "d",         // [38] FX_PHOENIX_MISSILE
    "d",         // [39] FX_PHOENIX_EXPLODE
    "t",         // [40] FX_MORPH_MISSILE
    "td",        // [41] FX_MORPH_MISSILE_INITIAL
    "bb",        // [42] FX_MORPH_EXPLODE
    "bssssss",   // [43] FX_FIRE_WAVE
    "d",         // [44] FX_FIRE_WAVE_WORM
    "ss",        // [45] FX_FIRE_BURST
    "t",         // [46] FX_RIPPER_EXPLODE
    "ss",        // [47] FX_WATER_ENTRY_SPLASH
    "vbssssssss",// [48] FX_WATER_RIPPLES
    "bd",        // [49] FX_WATER_WAKE
    NULL,        // [50] FX_BUBBLER
    "sbv",       // [51] FX_SCORCHMARK
    "b",         // [52] FX_DEBRIS
    "d",         // [53] FX_FLESH_DEBRIS
    "bbdb",      // [54] FX_SHADOW
    "bdb",       // [55] FX_PLAYER_SHADOW
    "f",         // [56] FX_ANIMATE
    "bbbv",      // [57] FX_FOUNTAIN
    "vsb",       // [58] FX_WATERFALL_BASE
    "bbb",       // [59] FX_DRIPPER
    "bb",        // [60] FX_MIST
    "b",         // [61] FX_PLAGUE_MIST
    "vb",        // [62] FX_PLAGUE_MIST_EXPLODE
    "b",         // [63] FX_SPELLHANDS
    "b",         // [64] FX_LENS_FLARE
    "bbbf",      // [65] FX_STAFF
    "bb",        // [66] FX_SPOO
    NULL,        // [67] FX_HALO
    NULL,        // [68] FX_REMOTE_CAMERA
    "s",         // [69] FX_HELLBOLT
    "t",         // [70] FX_HELLBOLT_EXPLODE
    "d",         // [71] FX_HELLSTAFF_POWER
    "tb",        // [72] FX_HELLSTAFF_POWER_BURN
    "t",         // [73] FX_SPELL_CHANGE
    "db",        // [74] FX_STAFF_CREATE
    NULL,        // [75] FX_STAFF_CREATE_POOF
    NULL,        // [76] FX_STAFF_REMOVE
    NULL,        // [77] FX_DUST_PUFF_ON_GROUND
    NULL,        // [78] FX_FIRE
    "b",         // [79] FX_SOUND
    "bbbb",      // [80] FX_PICKUP
    NULL,        // [81] FX_HIT_PUFF
    "db",        // [82] FX_DUST
    "bdb",       // [83] FX_ENV_SMOKE
    "bdbbb",     // [84] FX_SPOO_SPLAT
    "d",         // [85] FX_BODYPART
    "ssbbb",     // [86] FX_PLAYER_PERSISTANT
    NULL,        // [87] FX_PLAYER_TORCH
    NULL,        // [88] FX_TOME_OF_POWER
    NULL,        // [89] FX_FIRE_ON_ENTITY
    NULL,        // [90] FX_FLAREUP
    "bbb",       // [91] FX_SHRINE_PLAYER_EFFECT
    NULL,        // [92] FX_SHRINE_MANA_EFFECT
    "b",         // [93] FX_SHRINE_LUNGS_EFFECT
    NULL,        // [94] FX_SHRINE_LIGHT_EFFECT
    NULL,        // [95] FX_SHRINE_SPEED_EFFECT
    NULL,        // [96] FX_SHRINE_ARMOR_EFFECT
    NULL,        // [97] FX_SHRINE_HEALTH_EFFECT
    NULL,        // [98] FX_SHRINE_STAFF_EFFECT
    NULL,        // [99] FX_SHRINE_GHOST_EFFECT
    NULL,        // [100] FX_SHRINE_REFLECT_EFFECT
    NULL,        // [101] FX_SHRINE_POWERUP_EFFECT
    NULL,        // [102] FX_ROPE
    NULL,        // [103] FX_FIRE_HANDS
    "ssbvvv",    // [104] FX_SHRINE_BALL
    "b",         // [105] FX_SHRINE_BALL_EXPLODE
    "db",        // [106] FX_OGLE_HIT_PUFF
    "db",        // [107] FX_HP_MISSILE
    "v",         // [108] FX_INSECT_EFFECTS
    "vb",        // [109] FX_CHICKEN_EXPLODE
    "bv",        // [110] FX_TELEPORT_PAD
    NULL,        // [111] FX_TPORT_SMOKE
    "df",        // [112] FX_WATER_PARTICLES
    NULL,        // [113] FX_M_EFFECTS
    "bbb",       // [114] FX_FLAMETHROWER
    "vbb",       // [115] FX_FLAMETEST
    "vb",        // [116] FX_QUAKE
    NULL,        // [117] FX_LIGHTNING
    NULL,        // [118] FX_POWER_LIGHTNING
    NULL,        // [119] FX_HP_STAFF
    "bv",        // [120] FX_RAND_WATER_BUBBLE
    "bs",        // [121] FX_BUBBLE
    NULL,        // [122] FX_MAGIC_PORTAL
    "vbb",       // [123] FX_TB_EFFECTS
    "bv",        // [124] FX_TEST_BBOX
    "fff",       // [125] FX_SSITHRA_ARROW
    "ssbbb",     // [126] FX_PE_SPELL
    "bv",        // [127] FX_LIGHTNING_HIT
    "bv",        // [128] FX_NULL_EFFECT
    "t",         // [129] FX_STAFF_STRIKE
    NULL,        // [130] FX_ARMOR_HIT
    NULL,        // [131] FX_BARREL_EXPLODE
    "db",        // [132] FX_CWATCHER_EFFECTS
    "d",         // [133] FX_CORPSE_REMOVE
    NULL,        // [134] FX_LEADER
    "bv",        // [135] FX_TORNADO
    NULL,        // [136] FX_TORNADO_BALL
    NULL,        // [137] FX_TORNADO_BALL_EXPLODE
    NULL,        // [138] FX_FEET_TRAIL
    NULL,        // [139] FX_WATER_SPLASH
    NULL,        // [140]
    NULL,        // [141]
    "d",         // [142]
    NULL,        // [143]
    NULL,        // [144]
    NULL,        // [145]
    NULL,        // [146]
    NULL,        // [147]
    NULL,        // [148]
    NULL,        // [149]
    NULL,        // [150]
    NULL,        // [151]
    NULL,        // [152]
    NULL,        // [153]
    NULL,        // [154]
    NULL,        // [155]
    NULL,        // [156]
    NULL,        // [157]
    NULL,        // [158]
    NULL,        // [159]
    NULL,        // [160]
};

// Minimal sizebuf_t read helpers (MSG_* have hidden visibility from the main exe).
static inline int SB_ReadByte(sizebuf_t* sb)
{
    if (sb->readcount + 1 > sb->cursize) { sb->readcount++; return -1; }
    return sb->data[sb->readcount++];
}
static inline int SB_ReadShort(sizebuf_t* sb)
{
    if (sb->readcount + 2 > sb->cursize) { sb->readcount += 2; return -1; }
    int c = (short)(sb->data[sb->readcount] + (sb->data[sb->readcount + 1] << 8));
    sb->readcount += 2;
    return c;
}
static inline void SB_Skip(sizebuf_t* sb, int n)
{
    sb->readcount += n;
}

// Skip format-string bytes for one effect without spawning anything.
// Byte counts per format char match the MSG_Read* implementations in netmsg_read.c.
static void SkipEffectParams(sizebuf_t* msg_read, const int flags, const int effect)
{
    if (effect < 0 || effect >= (int)(sizeof(fx_format_strings) / sizeof(fx_format_strings[0])))
        return;

    const char* format = fx_format_strings[effect];
    if (format == NULL)
        return;

    for (int i = 0; format[i] != 0; i++)
    {
        switch (format[i])
        {
            case 'b': SB_Skip(msg_read, 1); break; // byte
            case 's': SB_Skip(msg_read, 2); break; // short
            case 'i': SB_Skip(msg_read, 4); break; // long
            case 'f': SB_Skip(msg_read, 4); break; // float
            case 'p':
            case 'v': SB_Skip(msg_read, 6); break; // pos: 3x short (MSG_ReadPos)
            case 'u': SB_Skip(msg_read, 2); break; // dir+mag: 2 bytes (MSG_ReadDirMag)
            case 'd': SB_Skip(msg_read, 1); break; // dir: 1 byte index (MSG_ReadDir)
            case 'x': SB_Skip(msg_read, 2); break; // yaw+pitch: 2 bytes (MSG_ReadYawPitch)
            case 't': SB_Skip(msg_read, 4); break; // short yaw+pitch: 2x short (MSG_ReadShortYawPitch)
            default:  break;
        }
    }
    (void)flags;
}

// Consume a client effects message from the network stream (when owner=NULL)
// or from an entity's clientEffects buffer (when owner!=NULL), without spawning
// any visual effects.
// Mirrors ParseEffects in Main.c.
static void ParseClientEffects(centity_t* owner)
{
    sizebuf_t* msg_read;
    sizebuf_t temp_buf;
    EffectsBuffer_t* fx_buf = NULL;
    int num;

    if (owner != NULL)
    {
        if (*fxi.cl_effectpredict == 0)
            fx_buf = &owner->current.clientEffects;
        else
            fx_buf = fxi.clientPredEffects;

        num = fx_buf->numEffects;

        msg_read = &temp_buf;
        memset(msg_read, 0, sizeof(*msg_read));
        msg_read->data = fx_buf->buf;
        msg_read->cursize = msg_read->maxsize = fx_buf->bufSize;
    }
    else
    {
        msg_read = fxi.net_message;
        num = SB_ReadByte(msg_read);
    }

    if (num < 0)
        return;

    for (int i = 0; i < num; i++)
    {
        if (owner != NULL)
            msg_read->readcount = fx_buf->freeBlock;

        int effect = (ushort)SB_ReadShort(msg_read);
        int flags = 0;

        if (effect & EFFECT_PRED_INFO)
        {
            SB_Skip(msg_read, 1); // event_id
            effect &= ~EFFECT_PRED_INFO;
        }

        if (effect & EFFECT_FLAGS)
        {
            flags = SB_ReadByte(msg_read);
            effect &= ~EFFECT_FLAGS;
        }

        if (flags & (CEF_BROADCAST | CEF_MULTICAST))
        {
            if (flags & CEF_ENTNUM16)
                SB_Skip(msg_read, 2);
            else
                SB_Skip(msg_read, 1);
        }

        if (!(flags & CEF_OWNERS_ORIGIN))
            SB_Skip(msg_read, 6); // position: 3x short (MSG_ReadPos)

        if (owner != NULL && !(flags & (CEF_BROADCAST | CEF_MULTICAST)))
            fx_buf->freeBlock = msg_read->readcount;

        SkipEffectParams(msg_read, flags, effect);

        if (owner != NULL && !(flags & (CEF_BROADCAST | CEF_MULTICAST)))
            fx_buf->freeBlock = msg_read->readcount;
    }

    // Free entity effect buffer (mirrors original logic).
    if (owner != NULL)
    {
        fx_buf->freeBlock = 0;
        if (fxi.FXBufMngr != NULL && fx_buf->buf != NULL)
            ResMngr_DeallocateResource(fxi.FXBufMngr, fx_buf->buf, ENTITY_FX_BUF_SIZE * sizeof(char));
        fx_buf->buf = NULL;
        fx_buf->numEffects = 0;
        fx_buf->bufSize = 0;
    }
}

// ============================================================
// AddPacketEntities -- core entity rendering
// ============================================================

static entity_t sv_ents[MAX_SERVER_ENTITIES];
static fmnodeinfo_t sv_ents_fmnodeinfos[MAX_SERVER_ENTITIES][MAX_FM_MESH_NODES];

static void AddServerEntities(const frame_t* frame)
{
	const int maxclients = Q_atoi(fxi.cl->configstrings[CS_MAXCLIENTS]);

	const int leveltime_ms = (int)(*fxi.leveltime * 1000.0f);
	const int autoanim = leveltime_ms / 500;

	fxi.cl->PIV = 0;

	memset(sv_ents, 0, sizeof(sv_ents));

	int num_ents = frame->num_entities;
	if (num_ents > MAX_SERVER_ENTITIES)
		num_ents = MAX_SERVER_ENTITIES;

	entity_t* ent = &sv_ents[0];
	for (int pnum = 0; pnum < num_ents; pnum++, ent++)
	{
		entity_state_t* s1 = &fxi.parse_entities[(frame->parse_entities + pnum) & (MAX_PARSE_ENTITIES - 1)];
		centity_t* cent = &fxi.server_entities[s1->number];

		cent->s1 = s1;

		const int effects = s1->effects;
		const int renderfx = s1->renderfx;

		// Set frame.
		if (effects & EF_ANIM_ALL)
			ent->frame = autoanim;
		else if (effects & EF_ANIM_ALLFAST)
			ent->frame = leveltime_ms / 100;
		else
			ent->frame = s1->frame;

		ent->oldframe = cent->prev.frame;
		ent->backlerp = 1.0f - fxi.cl->lerpfrac;

		// Handle flex-model nodes.
		ent->fmnodeinfo = sv_ents_fmnodeinfos[pnum];
		memcpy(ent->fmnodeinfo, s1->fmnodeinfo, sizeof(s1->fmnodeinfo));

		// Interpolate origin.
		{
			vec3_t dist;
			VectorSubtract(cent->current.origin, cent->prev.origin, dist);

			if (VectorLengthSquared(dist) <= 100.0f * 100.0f)
				VectorMA(cent->prev.origin, 1.0f - ent->backlerp, dist, ent->origin);
			else
				VectorCopy(cent->current.origin, ent->origin);
		}

		VectorCopy(ent->origin, ent->oldorigin);
		VectorCopy(cent->origin, cent->lerp_origin);

		// Set model.
		if (s1->modelindex == 255)
		{
			// Player model: use client info.
			clientinfo_t* ci = &fxi.cl->clientinfo[s1->clientnum];
			const int skinnum = (s1->skinnum < SKIN_MAX ? s1->skinnum : 0);

			ent->model = ci->model;
			ent->skin = ci->skin[skinnum];
			ent->skins = &ci->skin[0];

			if (ent->skin == NULL || ent->model == NULL)
			{
				ent->model = fxi.cl->baseclientinfo.model;
				ent->skin = fxi.cl->baseclientinfo.skin[0];
				ent->skins = &fxi.cl->baseclientinfo.skin[0];
			}
		}
		else
		{
			ent->model = &fxi.cl->model_draw[s1->modelindex];
			ent->skin = NULL;
		}

		ent->scale = s1->scale;

		// Use white color if entity has none set.
		if (s1->color.c != 0)
			ent->color = s1->color;
		else
		{
			ent->color.r = 255;
			ent->color.g = 255;
			ent->color.b = 255;
			ent->color.a = 255;
		}

		ent->absLight.r = s1->absLight.r;
		ent->absLight.g = s1->absLight.g;
		ent->absLight.b = s1->absLight.b;

		ent->flags = renderfx;

		// Interpolate angles.
		for (int i = 0; i < 3; i++)
		{
			const float a1 = cent->current.angles[i];
			const float a2 = cent->prev.angles[i];
			ent->angles[i] = LerpAngle(a2, a1, fxi.cl->lerpfrac);
		}
		VectorDegreesToRadians(ent->angles, ent->angles);
		VectorCopy(ent->angles, cent->lerp_angles);

		ent->rootJoint = ((effects & EF_JOINTED) ? s1->rootJoint : NULL_ROOT_JOINT);

		// Handle swap frame.
		ent->swapFrame = NO_SWAP_FRAME;
		ent->oldSwapFrame = NO_SWAP_FRAME;
		if ((effects & EF_SWAPFRAME) && s1->swapFrame != s1->frame)
		{
			ent->swapFrame = s1->swapFrame;
			ent->oldSwapFrame = cent->prev.swapFrame;
			if (ent->oldSwapFrame == NO_SWAP_FRAME)
				ent->oldSwapFrame = ent->oldframe;
		}

		ent->referenceInfo = cent->referenceInfo;

		// Drain per-entity client effects buffer so numEffects doesn't overflow.
		// The real client effects DLL calls ParseEffects(cent) here; our stub's
		// ParseClientEffects(owner) reads and discards the buffer then resets it.
		if (cent->current.clientEffects.numEffects > 0)
		{
			*fxi.cl_effectpredict = 0;
			ParseClientEffects(cent);
		}

		// Track players in view bitmask.
		if (s1->number > 0 && s1->number <= maxclients)
		{
			fxi.cl->PIV |= 1 << (s1->number - 1);
			VectorCopy(ent->origin, fxi.cl->clientinfo[s1->number - 1].origin);
		}

		// Set PlayerEntPtr for the local player.
		if (s1->number == fxi.cl->playernum + 1)
			*fxi.PlayerEntPtr = ent;

		// Add entity to render list.
		if (ent->model == NULL || *ent->model == NULL)
			continue; // Model not set or failed to load - skip rather than drawing diamond placeholder.

		if (ent->flags & RF_TRANS_ANY)
		{
			if (fxi.cls->r_num_alpha_entities < MAX_ALPHA_ENTITIES)
				fxi.cls->r_alpha_entities[fxi.cls->r_num_alpha_entities++] = ent;
		}
		else
		{
			if (fxi.cls->r_numentities < MAX_ENTITIES)
				fxi.cls->r_entities[fxi.cls->r_numentities++] = ent;
		}
	}
}

// ============================================================
// GetfxAPI -- library entry point
// ============================================================

CLFX_DECLSPEC client_fx_export_t GetfxAPI(const client_fx_import_t import)
{
	fxi = import;

	client_fx_export_t export;
	memset(&export, 0, sizeof(export));

	export.api_version = FX_API_VERSION;

	export.Init                = Stub_Init;
	export.ShutDown            = Stub_ShutDown;
	export.Clear               = Stub_Clear;
	export.RegisterSounds      = Stub_RegisterSounds;
	export.RegisterModels      = Stub_RegisterModels;
	export.ParseClientEffects  = ParseClientEffects;
	export.RemoveClientEffects = Stub_RemoveClientEffects;
	export.AddPacketEntities   = AddServerEntities;
	export.AddEffects          = Stub_AddEffects;
	export.UpdateEffects       = Stub_UpdateEffects;
	export.SetLightstyle       = Stub_SetLightstyle;
	export.GetLMI              = Stub_GetLMI;
	export.GetLMIMax           = Stub_GetLMIMax;
	export.client_string       = clfx_string;

	return export;
}
