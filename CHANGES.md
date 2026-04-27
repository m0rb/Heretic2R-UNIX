# Heretic2R Unix/Linux Port - Changes Documentation

## Overview
This document tracks the changes made to port Heretic2R from Windows to Unix/Linux systems.

## Attribution
Unix implementations reference Yamagi Quake 2 (https://github.com/yquake2/quake2), licensed under GPLv2.
Specific files referenced:
- `src/backends/unix/system.c` - System functions, DLL loading, timing
- `src/backends/unix/network.c` - Network socket implementation
- `src/backends/unix/main.c` - Unix entry point

## Build System Changes

### Created Files
- `CMakeLists.txt` - Unix build system replacing Visual Studio solution

### Modified Files
- None yet

## Platform-Specific Code Changes

### Created Files - src/unix/
- `main.c` - Unix entry point (replaces `src/win32/main.c`)
- `sys_unix.c` - Unix system functions (replaces `src/win32/sys_win.c`)
- `net_udp.c` - Unix network implementation (replaces `src/win32/net_wins.c`)
- `q_shunix.c` - Unix filesystem and timing (replaces `src/win32/q_shwin.c`)
- `vid_sdl2.c` - SDL2 video backend (replaces `src/win32/vid_dll.c`)
- `in_sdl2.c` - SDL2 input backend (replaces `src/win32/in_win.c`)
- `p_dll_unix.c` - Unix Player DLL loading (replaces `src/qcommon/p_dll.c`)
- `compat.h` - Windows to Unix compatibility layer
- `Quake2Main.h` - Unix main header (replaces `src/win32/Quake2Main.h`)
- `vid_dll.h` - Unix video interface stub
- `snd_dll.h` - Unix sound interface stub
- `clfx_dll.h` - Unix client effects interface stub
- `dll_io_unix.h` - Unix DLL I/O interface

### Modified Files
- `src/qcommon/qcommon.h` - Added Unix build configuration macros
- `src/qcommon/q_Typedef.h` - Fixed qboolean for Unix
- `src/qcommon/H2Common.h` - Fixed H2COMMON_API for Unix
- `src/qcommon/ArrayedList.h` - Fixed _inline usage
- `src/qcommon/pmove.c` - Added compat.h include
- `src/qcommon/common.c` - Added compat.h include
- `src/qcommon/cmd.c` - Added compat.h include
- `src/qcommon/cmodel.c` - Added compat.h include
- `src/qcommon/files.c` - Added compat.h include
- `src/qcommon/net_chan.c` - Added compat.h include
- `src/client/cl_main.c` - Added compat.h include
- `src/client/cl_input.c` - Added compat.h include
- `src/client/cl_screen.c` - Added compat.h and limits.h includes
- `src/client/cl_smk.c` - Added compat.h include, fixed cl_smk.c type mismatch
- `src/client/cl_view.c` - Added compat.h include
- `src/client/cl_prediction.c` - Added compat.h include
- `src/client/console.c` - Added compat.h include
- `src/client/keys.c` - Added compat.h include
- `src/client/menu.c` - Added compat.h include
- `src/snd_sdl3/src/snd_main.c` - Added compat.h, fixed SNDLIB_DECLSPEC
- `src/snd_sdl3/src/snd_sdl3.c` - Added compat.h include
- `src/snd_sdl3/src/snd_wav.c` - Added compat.h include
- `src/snd_sdl3/src/snd_ogg.c` - Added compat.h include
- `src/server/sv_main.c` - Added compat.h include
- `src/server/sv_user.c` - Added compat.h include
- `src/server/sv_init.c` - Added compat.h include
- `src/server/sv_send.c` - Added compat.h include
- `src/server/sv_ccmds.c` - Added compat.h include
- `src/server/sv_world.c` - Added compat.h include, fixed STRUCT_FROM_LINK macro
- `src/server/sv_game.c` - Added compat.h include, fixed dll_io.h path
- `src/H2Common/Common.c` - Added compat.h include
- `src/H2Common/SurfaceProps.c` - Added compat.h, fixed conflicting types
- Various menu files - Added compat.h includes
- Various ref_gl1 files - Added compat.h includes

## Key Changes Summary

### 1. Dynamic Library Loading
- Windows: `LoadLibrary()`, `GetProcAddress()`, `FreeLibrary()`
- Unix: `dlopen()`, `dlsym()`, `dlclose()`

### 2. Network Code
- Windows: Winsock API (`WSAStartup`, `socket`, `ioctlsocket`)
- Unix: POSIX sockets (`socket`, `fcntl` with `O_NONBLOCK`)

### 3. Timing Functions
- Windows: `QueryPerformanceCounter`, `timeGetTime`
- Unix: `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_REALTIME)`

### 4. File System Operations
- Windows: `_findfirst/_findnext`, `GetFileAttributes`, `SHGetKnownFolderPath`
- Unix: `opendir/readdir`, `stat`, `$HOME` environment variable

### 5. Console I/O
- Windows: `AllocConsole`, `ReadConsoleInput`, `WriteFile`
- Unix: `stdin` with `select()`, `printf`/`fprintf`

### 6. Process/Thread Control
- Windows: `timeBeginPeriod`, `SetPriorityClass`
- Unix: Not needed (POSIX handles this differently)

## Build Instructions

### Prerequisites
- GCC or Clang compiler
- SDL3 development libraries
- OpenGL development libraries
- libdl (for dlopen/dlsym)

### Building
```bash
cd build
cmake ..
make
```

### Installing Dependencies (Debian/Ubuntu)
```bash
sudo apt-get install build-essential cmake libsdl3-dev libgl-dev
```

## Testing Notes
- Tested on: [To be filled after testing]
- Known issues: [To be documented during development]

## Version History
- R4.01.01.0504.01 - Initial Unix port work begins