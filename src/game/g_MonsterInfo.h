//
// g_MonsterInfo.h -- Monster info structures
//
// Copyright 1998 Raven Software
//

#pragma once

#include "q_shared.h"
#include "g_Types.h"
#include "g_HitLocation.h"

// Forward declarations
struct edict_s;
typedef struct edict_s edict_t;

typedef struct alertent_s alertent_t;

// This is used to hold information pertaining to an entity's movement.
// NOTE: mxd. Can't change struct size, otherwise compatibility with original game dlls will break!
typedef struct moveinfo_s
{
	// Fixed data.
	vec3_t start_origin;
	vec3_t start_angles;
	vec3_t end_origin;
	vec3_t end_angles;

	int sound_start;
	int sound_middle;
	int sound_end;

	float accel;
	float speed;
	float decel;
	float distance;

	float wait;

	// State data.
	int state;
	vec3_t dir;
	float current_speed;
	float move_speed;
	float next_speed;
	float remaining_distance;
	float decel_distance;
	void (*endfunc)(edict_t*);
} moveinfo_t;

typedef struct
{
	const int framenum;
	void (*const movefunc)(edict_t* self, float var1, float var2, float var3);
	const float var1;
	const float var2;
	const float var3;
	void (*const actionfunc)(edict_t* self, float var4);
	const float var4;
	void (*const thinkfunc)(edict_t* self);
} animframe_t;

#define ANIMMOVE(arr, endfunc)	{ ARRAY_SIZE(arr), arr, endfunc } //mxd. animmove_t initializer macro. Added, so we don't have to type numframes manually.

typedef struct animmove_s
{
	const int numframes;
	const animframe_t* frame;
	void (*const endfunc)(edict_t* self);
} animmove_t;

// NOTE: mxd. Can't change struct size, otherwise compatibility with original game dlls will break!
typedef struct monsterinfo_s
{
	// Not used in new system.
	char* otherenemyname; // ClassName of secondary enemy (other than player). E.g. a Rat's secondary enemy is a gib.

	const animmove_t* currentmove;
	int aiflags;
	int aistate;		// Last order given to the monster (ORD_XXX).
	int currframeindex;	// Index to current monster frame.
	int nextframeindex;	// Used to force the next frameindex.
	float thinkinc;		// Time between thinks for this entity.
	float scale;

	void (*idle)(edict_t* self); //TODO: used, but never assigned?
	void (*search)(edict_t* self); //TODO: unused.
	void (*dodge)(edict_t* self, edict_t* other, float eta); //TODO: unused.
	int (*attack)(edict_t* self); //TODO: unused.
	void (*sight)(edict_t* self, edict_t* other); //TODO: unused.
	void (*dismember)(edict_t* self, int damage, HitLocation_t hl); //mxd. Changed 'hl' arg type from int.
	qboolean (*alert)(edict_t* self, alertent_t* alerter, edict_t* enemy);
	qboolean (*checkattack)(edict_t* self);

	float pausetime;
	float attack_finished;

	union
	{
		float flee_finished; // When a monster is done fleeing.
		qboolean morcalavin_quake_finished; //mxd
		float rope_player_current_swing_speed; //mxd
	};

	union
	{
		float chase_finished;	// When the monster can look for secondary monsters.
		float rope_player_initial_swing_speed; //mxd
	};

	union
	{
		vec3_t saved_goal;
		vec3_t rope_player_swing_direction; //mxd
	};

	union
	{
		float search_time;
		float priestess_attack_delay; //mxd
		float rope_sound_debounce_time; //mxd
	};
	
	float misc_debounce_time;
	vec3_t last_sighting;
	int attack_state;

	union
	{
		int lefty;
		int morcalavin_taunt_counter; //mxd
	};
	
	float idle_time;
	int linkcount;

	int searchType;
	vec3_t nav_goal;

	union
	{
		float jump_time;
		float morcalavin_teleport_attack_time; //mxd
		float ogle_sing_time; //mxd
		float rope_jump_debounce_time; // Delay after jumping from rope before trying to grab another rope -- mxd.
	};

	int morcalavin_battle_phase; //mxd. Named 'stepState' in original logic.
	int ogleflags;			// Ogles have special spawnflags stored in here at spawntime.
	int supporters;			// Number of supporting monsters (with common type) in the area when awoken.
	float sound_finished;	// Amount of time until the monster will be finishing talking (used for voices).

	union
	{
		float sound_start; // The amount of time to wait before playing the pending sound.
		float morcalavin_taunt_time; //mxd
	};
	
	int sound_pending;		// This monster is waiting to make a sound (used for voices) (0 if false, else sound ID).

	// Cinematic fields.
	int c_dist;		// Distance left to move.
	int c_repeat;	// # of times to repeat the anim cycle.
	void (*c_callback)(struct edict_s* self); // Callback function when action is done. Used only by script system --mxd.
	int c_anim_flag;	// Shows if current cinematic anim supports moving, turning, or repeating.
	qboolean c_mode;	// In cinematic mode or not?
	edict_t* c_ent;		// Entity passed from a cinematic command.

	qboolean awake;		// Has found an enemy AND gone after it.
	qboolean roared;	// Gorgon has roared or been woken up by a roar.

	float last_successful_enemy_tracking_time; // Last time successfully saw enemy or found a path to him.
	float coop_check_debounce_time;
} monsterinfo_t;