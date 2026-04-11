/*
 * in_sdl3.c -- SDL3 input backend for Heretic2R Unix port
 * Copyright (C) 2010-2024 Yamagi Quake 2 Contributors (GPLv2)
 * Unix port by morb
 */

#include <SDL3/SDL.h>
#include "../../../qcommon/qcommon.h"
#include "../client/client.h"
#include "../client/input.h"

extern cvar_t *in_mouse;
extern cvar_t *in_grab;
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

static int joy_x = 0;
static int joy_y = 0;

// Forward declarations
void IN_ActivateMouse(void);
void IN_DeactivateMouse(void);

void IN_Init(void)
{
    mouseinitialized = true;
    
    // Force mouse grab on start
    if (in_mouse->value)
    {
        IN_ActivateMouse();
    }
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

    if (!mouseactive)
    {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        mouseactive = true;
    }
}

void IN_DeactivateMouse(void)
{
    if (!mouseinitialized)
        return;

    if (mouseactive)
    {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        mouseactive = false;
    }
}

void IN_Frame(void)
{
    if (!mouseinitialized)
        return;

    // Handle mouse grab toggle
    if (in_grab->modified)
    {
        if (in_grab->value)
        {
            IN_ActivateMouse();
        }
        else
        {
            IN_DeactivateMouse();
        }
        in_grab->modified = false;
    }
}

void IN_Move(usercmd_t *cmd)
{
    if (!mouseactive)
        return;

    // Add mouse movement to the command
    cmd->sidemove += m_side->value * mouse_x;
    cmd->forwardmove -= m_forward->value * mouse_y;

    // Clear mouse movement
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

void IN_JoyMove(void)
{
    // SDL2 joystick support - placeholder
    joy_x = 0;
    joy_y = 0;
}

void IN_StartupJoystick(void)
{
    // SDL2 joystick initialization - placeholder
}

void IN_ShutdownJoystick(void)
{
    // SDL2 joystick shutdown - placeholder
}