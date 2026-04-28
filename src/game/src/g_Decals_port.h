//
// Decals.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "q_Typedef.h"

// Check if a decal can be applied to the target surface
extern qboolean IsDecalApplicable(const edict_t* target, const vec3_t origin, const csurface_t* surface, const cplane_t* plane, vec3_t plane_dir);