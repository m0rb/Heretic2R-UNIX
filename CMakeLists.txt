cmake_minimum_required(VERSION 3.16)
project(heretic2 C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
pkg_check_modules(OPENAL openal)

# ---------------------------------------------------------------------------
# Include directories
# Mirrors the VS project AdditionalIncludeDirectories layout.
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
    src/game/src                 # Game.h, g_Local.h, …
    src/Player/src               # Player.h, p_anims.h, …
    src/H2Common/src             # Math.h, Vector.h, …  (also in qcommon, but let the project use its own)
    src/quake2/src/unix          # compat.h, dll_io_unix.h, clfx_dll_unix.h
    src/snd_sdl3/src
    src/ref_gl1                  # vid.h
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
    src/quake2/src/unix/in_sdl3.c
    src/quake2/src/unix/p_dll_unix.c
    src/quake2/src/unix/clfx_dll.c
    src/quake2/src/unix/clfx_stub.c
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
# Source files - Game DLL (compiled into the main binary for single-process build)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SOURCES "src/game/src/*.c" "src/game/src/*.cpp")

# ---------------------------------------------------------------------------
# Source files - Player DLL (compiled into the main binary)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PLAYER_SOURCES "src/Player/src/*.c")

# ---------------------------------------------------------------------------
# Source files - Client Effects DLL (compiled into the main binary)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CLFX_SOURCES "src/client effects/src/*.c")

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
# Combine all sources and build
# ---------------------------------------------------------------------------
set(ALL_SOURCES
    ${UNIX_SOURCES}
    ${QCOMMON_SOURCES}
    ${CS_SHARED_SOURCES}
    ${CLIENT_SOURCES}
    ${MENU_SOURCES}
    ${SERVER_SOURCES}
    ${H2COMMON_SOURCES}
    ${GAME_SOURCES}
    ${PLAYER_SOURCES}
    ${CLFX_SOURCES}
    ${SOUND_SOURCES}
    ${GL1_SOURCES}
    ${GLAD_SOURCES}
    ${LIBSMACKER_SOURCES}
)

add_executable(heretic2 ${ALL_SOURCES})

target_include_directories(heretic2 PRIVATE
    ${INCLUDE_DIRS}
    ${SDL3_INCLUDE_DIRS}
    ${OPENAL_INCLUDE_DIRS}
)

target_link_libraries(heretic2
    ${SDL3_LIBRARIES}
    ${OPENAL_LIBRARIES}
    m
    dl
    pthread
)

target_compile_definitions(heretic2 PRIVATE
    _UNIX
    NDEBUG
)

target_compile_options(heretic2 PRIVATE
    -Wall
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-parentheses
    -Wno-format-truncation
    -Wno-missing-field-initializers
    -fsigned-char
)

target_link_options(heretic2 PRIVATE
    ${SDL3_LDFLAGS}
)
