cmake_minimum_required(VERSION 3.16)
project(heretic2 C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(OPENAL openal)

# ---------------------------------------------------------------------------
# Common include directories (shared by all targets)
# ---------------------------------------------------------------------------
set(INCLUDE_DIRS
    include
    include/SDL3
    include/stb
    include/libsmacker
    src/qcommon                  # H2-specific shared headers (FX.h, Vector.h, …)
    src/quake2/src               # quake2 engine root (client/, server/, cs_shared/, win32/)
    src/quake2/src/cs_shared     # cmd.c / cmodel.c / common.c / …
    src/quake2/src/client        # client.h, console.h, …
    src/quake2/src/server        # server.h, …
    src/game/src                 # Game.h, g_Local.h (referenced by engine too)
    src/Player/src               # Player.h, p_anims.h (referenced by engine too)
    src/H2Common/src             # Math.h, Vector.h, …
    src/quake2/src/unix          # compat.h, dll_io_unix.h, clfx_dll_unix.h
    src/snd_sdl3/src
    src/ref_gl1                  # vid.h
    src/ref_gl1/src              # gl1_*.h use bare-name includes within subdirs
)

# Extra includes only needed by game/scripting code
set(GAME_EXTRA_INCLUDES
    src/game/src/Scripting       # sc_CScript.h and friends use bare-name includes
)

# Extra includes only needed by client effects code
set(CLFX_EXTRA_INCLUDES
    "src/client effects/src"     # ce_Effects.h and friends use bare-name includes
)

# ---------------------------------------------------------------------------
# Source files - Unix platform
# ---------------------------------------------------------------------------
set(UNIX_SOURCES
    src/quake2/src/unix/main.c
    src/quake2/src/unix/sys_unix.c
    src/quake2/src/unix/q_shunix.c
    src/quake2/src/unix/net_udp.c
    src/quake2/src/unix/vid_sdl3.c
    src/quake2/src/unix/vid_screenshot.c
    src/quake2/src/unix/p_dll_unix.c
    src/quake2/src/unix/clfx_dll.c
    src/quake2/src/unix/snd_dll.c
)

# ---------------------------------------------------------------------------
# Source files - qcommon (H2-specific shared, stays in src/qcommon/)
# ---------------------------------------------------------------------------
set(QCOMMON_SOURCES
    src/qcommon/Message.c
    src/qcommon/Reference.c
    src/qcommon/Skeletons.c
    src/qcommon/netmsg_read.c
    src/qcommon/netmsg_write.c
    src/qcommon/turbsin.c
    src/qcommon/anorms.c
)

# ---------------------------------------------------------------------------
# Source files - quake2 cs_shared (engine client+server shared code)
# ---------------------------------------------------------------------------
set(CS_SHARED_SOURCES
    src/quake2/src/cs_shared/cmd.c
    src/quake2/src/cs_shared/cmodel.c
    src/quake2/src/cs_shared/common.c
    src/quake2/src/cs_shared/cvar.c
    src/quake2/src/cs_shared/Debug.c
    src/quake2/src/cs_shared/files.c
    src/quake2/src/cs_shared/md4.c
    src/quake2/src/cs_shared/net_chan.c
    src/quake2/src/cs_shared/pmove.c
    src/quake2/src/cs_shared/tokens.c
)

# ---------------------------------------------------------------------------
# Source files - Client
# ---------------------------------------------------------------------------
set(CLIENT_SOURCES
    src/quake2/src/client/cl_main.c
    src/quake2/src/client/cl_parse.c
    src/quake2/src/client/cl_input.c
    src/quake2/src/client/cl_effects.c
    src/quake2/src/client/cl_entities.c
    src/quake2/src/client/cl_prediction.c
    src/quake2/src/client/cl_camera.c
    src/quake2/src/client/cl_view.c
    src/quake2/src/client/cl_screen.c
    src/quake2/src/client/cl_demo.c
    src/quake2/src/client/cl_smk.c
    src/quake2/src/client/cl_skeletons.c
    src/quake2/src/client/cl_inventory.c
    src/quake2/src/client/cl_messages.c
    src/quake2/src/client/cl_player.c
    src/quake2/src/client/console.c
    src/quake2/src/client/keys.c
    src/quake2/src/client/menu.c
    src/quake2/src/client/glimp_sdl3.c
    src/quake2/src/client/input_sdl3.c
    src/quake2/src/client/cl_globals.c
)

file(GLOB MENU_SOURCES "src/quake2/src/client/menus/*.c")

# ---------------------------------------------------------------------------
# Source files - Server
# ---------------------------------------------------------------------------
set(SERVER_SOURCES
    src/quake2/src/server/sv_main.c
    src/quake2/src/server/sv_init.c
    src/quake2/src/server/sv_send.c
    src/quake2/src/server/sv_user.c
    src/quake2/src/server/sv_world.c
    src/quake2/src/server/sv_ccmds.c
    src/quake2/src/server/sv_effects.c
    src/quake2/src/server/sv_game.c
    src/quake2/src/server/sv_entities.c
)

# ---------------------------------------------------------------------------
# Source files - H2Common
# ---------------------------------------------------------------------------
set(H2COMMON_SOURCES
    src/H2Common/src/ByteOrder.c
    src/H2Common/src/Common.c
    src/H2Common/src/Console.c
    src/H2Common/src/InfoStrings.c
    src/H2Common/src/Math.c
    src/H2Common/src/Matrix.c
    src/H2Common/src/Motion.c
    src/H2Common/src/q_Physics.c
    src/H2Common/src/Random.c
    src/H2Common/src/ResourceManager.c
    src/H2Common/src/SinglyLinkedList.c
    src/H2Common/src/SurfaceProps.c
    src/H2Common/src/TextPalette.c
    src/H2Common/src/Vector.c
)

# ---------------------------------------------------------------------------
# Source files - Game DLL
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SOURCES "src/game/src/*.c" "src/game/src/*.cpp")

# ---------------------------------------------------------------------------
# Source files - Player DLL
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PLAYER_SOURCES "src/Player/src/*.c")

# ---------------------------------------------------------------------------
# Source files - Client Effects DLL
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CLFX_SOURCES "src/client effects/src/*.c")
# qcommon files the .so needs locally (can't rely on main exe's hidden symbols)
set(CLFX_EXTRA_SOURCES
    src/qcommon/turbsin.c
    src/qcommon/netmsg_read.c
    src/qcommon/anorms.c
)

# ---------------------------------------------------------------------------
# Source files - Sound (SDL3)
# ---------------------------------------------------------------------------
set(SOUND_SOURCES
    src/snd_sdl3/src/snd_main.c
    src/snd_sdl3/src/snd_sdl3.c
    src/snd_sdl3/src/snd_wav.c
    src/snd_sdl3/src/snd_ogg.c
    src/snd_sdl3/src/snd_LowpassFilter.c
)

# ---------------------------------------------------------------------------
# Source files - GL1 Renderer
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GL1_SOURCES "src/ref_gl1/src/*.c")

# ---------------------------------------------------------------------------
# Source files - OpenGL 1.3 GLAD
# ---------------------------------------------------------------------------
set(GLAD_SOURCES
    include/glad-GL1.3/glad.c
)

# ---------------------------------------------------------------------------
# Source files - libsmacker
# ---------------------------------------------------------------------------
set(LIBSMACKER_SOURCES
    include/libsmacker/smacker.c
)

# ---------------------------------------------------------------------------
# Platform-specific link libraries
# ---------------------------------------------------------------------------
# dl is part of libc on OpenBSD; only link it explicitly elsewhere.
# backtrace() is in libexecinfo on OpenBSD/FreeBSD, in libc on Linux/glibc.
if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD" OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(EXECINFO_LIBRARY execinfo)
else()
    set(DL_LIBRARY dl)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    set(OPENBSD_LINK_DIRS -L/usr/X11R6/lib)
endif()

# ---------------------------------------------------------------------------
# Common compile options (applied to all targets)
# ---------------------------------------------------------------------------
set(COMMON_CFLAGS
    -Wall
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-parentheses
    -Wno-format-truncation
    -Wno-missing-field-initializers
    -Wno-incompatible-pointer-types
    -fsigned-char
    -fcommon
)

# ---------------------------------------------------------------------------
# Main client/server executable
# ---------------------------------------------------------------------------
set(EXE_SOURCES
    ${UNIX_SOURCES}
    ${QCOMMON_SOURCES}
    ${CS_SHARED_SOURCES}
    ${CLIENT_SOURCES}
    ${MENU_SOURCES}
    ${SERVER_SOURCES}
    ${H2COMMON_SOURCES}
    ${SOUND_SOURCES}
    ${GL1_SOURCES}
    ${GLAD_SOURCES}
    ${LIBSMACKER_SOURCES}
    src/game/src/q_Shared.c
)

add_executable(heretic2 ${EXE_SOURCES})
set_target_properties(heretic2 PROPERTIES OUTPUT_NAME "heretic2r")

target_include_directories(heretic2 PRIVATE ${INCLUDE_DIRS} ${SDL3_INCLUDE_DIRS} ${OPENAL_INCLUDE_DIRS})

target_compile_definitions(heretic2 PRIVATE QUAKE2_DLL H2COMMON _UNIX NDEBUG)
target_compile_options(heretic2 PRIVATE ${COMMON_CFLAGS})

target_link_libraries(heretic2
    ${SDL3_LIBRARIES}
    ${OPENAL_LIBRARIES}
    m
    ${DL_LIBRARY}
    ${EXECINFO_LIBRARY}
    pthread
)
target_link_options(heretic2 PRIVATE ${SDL3_LDFLAGS} ${OPENBSD_LINK_DIRS} -Wl,--export-dynamic)

# ---------------------------------------------------------------------------
# Dedicated server  →  heretic2r-server
# ---------------------------------------------------------------------------
set(DED_UNIX_SOURCES
    src/quake2/src/unix/main.c
    src/quake2/src/unix/sys_unix.c
    src/quake2/src/unix/q_shunix.c
    src/quake2/src/unix/net_udp.c
    src/quake2/src/unix/p_dll_unix.c
    src/quake2/src/unix/ded_stubs.c
)

set(DED_SOURCES
    ${DED_UNIX_SOURCES}
    ${QCOMMON_SOURCES}
    ${CS_SHARED_SOURCES}
    ${SERVER_SOURCES}
    ${H2COMMON_SOURCES}
    src/game/src/q_Shared.c
)

add_executable(heretic2r-server ${DED_SOURCES})

target_include_directories(heretic2r-server PRIVATE ${INCLUDE_DIRS})
target_compile_definitions(heretic2r-server PRIVATE QUAKE2_DLL H2COMMON _UNIX NDEBUG DEDICATED_ONLY)
target_compile_options(heretic2r-server PRIVATE ${COMMON_CFLAGS})

target_link_libraries(heretic2r-server
    m
    ${DL_LIBRARY}
    ${EXECINFO_LIBRARY}
    pthread
)
target_link_options(heretic2r-server PRIVATE ${OPENBSD_LINK_DIRS} -Wl,--export-dynamic)

# ---------------------------------------------------------------------------
# Game DLL  →  base/gamex86.so
# ---------------------------------------------------------------------------
add_library(gamex86 SHARED ${GAME_SOURCES})
set_target_properties(gamex86 PROPERTIES
    OUTPUT_NAME "gamex86"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/base"
)
target_include_directories(gamex86 PRIVATE ${INCLUDE_DIRS} ${GAME_EXTRA_INCLUDES})
target_compile_definitions(gamex86 PRIVATE GAME_DLL H2COMMON _UNIX NDEBUG)
target_compile_options(gamex86 PRIVATE ${COMMON_CFLAGS} -fPIC)
target_link_options(gamex86 PRIVATE ${OPENBSD_LINK_DIRS})

# ---------------------------------------------------------------------------
# Player DLL  →  base/Player.so
# ---------------------------------------------------------------------------
add_library(player SHARED ${PLAYER_SOURCES})
set_target_properties(player PROPERTIES
    OUTPUT_NAME "Player"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/base"
)
target_include_directories(player PRIVATE ${INCLUDE_DIRS})
target_compile_definitions(player PRIVATE PLAYER_DLL H2COMMON _UNIX NDEBUG)
target_compile_options(player PRIVATE ${COMMON_CFLAGS} -fPIC)
target_link_options(player PRIVATE ${OPENBSD_LINK_DIRS})

# ---------------------------------------------------------------------------
# Client Effects DLL  →  "base/Client Effects.so"
# CMake target names can't contain spaces; use 'clfx' as the target name.
# ---------------------------------------------------------------------------
add_library(clfx SHARED ${CLFX_SOURCES} ${CLFX_EXTRA_SOURCES})
set_target_properties(clfx PROPERTIES
    OUTPUT_NAME "Client Effects"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/base"
)
target_include_directories(clfx PRIVATE ${INCLUDE_DIRS} ${CLFX_EXTRA_INCLUDES})
target_compile_definitions(clfx PRIVATE CLIENT_EFFECTS_DLL H2COMMON _UNIX NDEBUG)
target_compile_options(clfx PRIVATE ${COMMON_CFLAGS} -fPIC)
target_link_options(clfx PRIVATE ${OPENBSD_LINK_DIRS})
