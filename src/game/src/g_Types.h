//
// g_Types.h -- Basic type definitions for game module.
//
// Copyright 1998 Raven Software
//

#pragma once

#include "q_shared.h" // For byte, vec3_t, qboolean, uint, trace_t
#include "p_types.h"  // For physicsType_t, deadState_t
#include "g_items.h"  // For gitem_t

// These types are needed by multiple headers and must be defined before any edict_t definition.

// edict->solid values.
typedef enum
{
	SOLID_NOT,		// No interaction with other objects.
	SOLID_TRIGGER,	// Only touch when inside, after moving.
	SOLID_BBOX,		// Touch on edge.
	SOLID_BSP		// BSP clip, touch on edge.
} solid_t;

// edict->takedamage values.
typedef enum
{
	DAMAGE_NO,
	DAMAGE_YES, // Will take damage if hit.
	DAMAGE_AIM, // Auto targeting recognizes this.
	DAMAGE_NO_RADIUS, // Will not take damage from radius blasts.
} damage_t;

// Only used for entity area links.
typedef struct link_s
{
	struct link_s* prev;
	struct link_s* next;
} link_t;

// Forward declarations for types used in function pointers
struct trace_s;
struct edict_s;

typedef struct edict_s edict_t;
typedef struct alertent_s alertent_t;

// Forward declaration for animmove_t (full definition in g_local.h)
typedef struct animmove_s animmove_t;
