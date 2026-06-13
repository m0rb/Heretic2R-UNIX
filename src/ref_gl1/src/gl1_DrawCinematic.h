//
// gl1_DrawCinematic.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "gl1_Local.h"

extern void Draw_InitCinematic(int width, int height);
extern void Draw_CloseCinematic(void);
extern void Draw_Cinematic(const byte* data, const paletteRGB_t* palette);

// Full-color cinematic path (MPEG). RGBA frames, no palette. --morb
extern void Draw_InitCinematicRGBA(int width, int height);
extern void Draw_CinematicRGBA(const byte* rgba);