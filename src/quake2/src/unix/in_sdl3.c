//
// in_sdl3.c -- SDL3 input backend
//
// Copyright (C) 1997-2005 Id Software, Inc.
// Copyright (C) 2010 Yamagi Burmeister
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.


#include <SDL3/SDL.h>
#include "../../../qcommon/qcommon.h"
#include "../client/client.h"
#include "../client/input.h"

extern cvar_t *lookstrafe;
extern cvar_t *m_side;
extern cvar_t *m_yaw;
extern cvar_t *m_pitch;
extern cvar_t *m_forward;
extern cvar_t *sensitivity;

static qboolean mouseinitialized = false;
static qboolean mouseactive = false;
static int mouse_x = 0;
static int mouse_y = 0;

void IN_ActivateMouse(void);
void IN_DeactivateMouse(void);

void IN_Init(void)
{
    mouseinitialized = true;
    IN_ActivateMouse();
}

void IN_Shutdown(void)
{
    if (mouseactive)
    {
        IN_DeactivateMouse();
    }
    mouseinitialized = false;
}

void IN_ActivateMouse(void)
{
    if (!mouseinitialized)
        return;

    mouseactive = true;
}

void IN_DeactivateMouse(void)
{
    if (!mouseinitialized)
        return;

    mouseactive = false;
}

void IN_Frame(void){}

void IN_Move(usercmd_t *cmd)
{
    if (!mouseactive)
        return;

    cmd->sidemove += m_side->value * mouse_x;
    cmd->forwardmove -= m_forward->value * mouse_y;

    mouse_x = 0;
    mouse_y = 0;
}

void IN_MouseEvent(int dx, int dy)
{
    if (!mouseactive)
        return;

    mouse_x += dx;
    mouse_y += dy;
}

void IN_JoyMove(void){}

void IN_StartupJoystick(void){}

void IN_ShutdownJoystick(void){}