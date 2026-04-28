//
// Random.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "H2Common.h"

// Required protos for random functions.
H2COMMON_API __attribute__((visibility("default"))) extern float flrand(float min, float max);
H2COMMON_API __attribute__((visibility("default"))) extern int irand(int min, int max);
