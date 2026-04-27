//
// Quake2Main.h -- Exposes the only function needed by Heretic2R.exe.
//
// Copyright 1998 Raven Software
//

#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include "Heretic2.h"

#ifdef _WIN32
Q2DLL_DECLSPEC int Quake2Main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
#else
Q2DLL_DECLSPEC int Quake2Main(int argc, char** argv);
#endif
