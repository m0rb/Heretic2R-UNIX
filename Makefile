# Heretic2R Unix/Linux Makefile
# Based on Yamagi Quake 2 build system patterns
#
# Copyright (c) 2025 Unix port based on Yamagi Quake 2
# Yamagi Quake 2 is licensed under GPLv2

# Detect OS
UNAME := $(shell uname -s)
ARCH := $(shell uname -m)

# Normalize architecture names
ifeq ($(ARCH),i686)
  ARCH := i386
endif
ifeq ($(ARCH),amd64)
  ARCH := x86_64
endif

# Compiler and flags
CC ?= gcc
CXX ?= g++
# Auto-discover include paths for deep directory trees
# Note: src/clfx is a symlink to src/client\ effects (avoids Make space-in-path issues).
GAME_INCLUDES   := $(shell find src/game/src -type d | sed 's|^|-I./|')
CLFX_INCLUDES   := $(shell find src/clfx/src -type d | sed 's|^|-I./|')
PLAYER_INCLUDES := $(shell find src/Player/src -type d | sed 's|^|-I./|')

CFLAGS := -std=c17 -Wall -Wno-error -Wno-incompatible-pointer-types -fno-strict-aliasing -fwrapv -fvisibility=hidden -fsigned-char
CFLAGS += -DQUAKE2_DLL -DH2COMMON
CFLAGS += -I./include \
          -I./src \
          -I./src/qcommon \
          -I./src/quake2/src \
          -I./src/quake2/src/cs_shared \
          -I./src/quake2/src/client \
          -I./src/quake2/src/server \
          -I./src/H2Common/src \
          -I./src/Player/src \
          -I./src/quake2/src/unix \
          -I./src/ref_gl1/src \
          $(GAME_INCLUDES) \
          $(CLFX_INCLUDES) \
          $(PLAYER_INCLUDES)
CXXFLAGS := -std=c++17 -Wall -Wno-error -fno-strict-aliasing -fvisibility=hidden
CXXFLAGS += -DQUAKE2_DLL -DH2COMMON
CXXFLAGS += -I./include \
            -I./src \
            -I./src/qcommon \
            -I./src/quake2/src \
            -I./src/quake2/src/cs_shared \
            -I./src/quake2/src/client \
            -I./src/quake2/src/server \
            -I./src/H2Common/src \
            -I./src/Player/src \
            -I./src/quake2/src/unix \
            -I./src/ref_gl1/src \
            $(GAME_INCLUDES) \
            $(CLFX_INCLUDES) \
            $(PLAYER_INCLUDES)
# Linux needs _POSIX_C_SOURCE for POSIX extensions; FreeBSD and macOS are POSIX-compliant by default.
ifeq ($(UNAME),Linux)
  CFLAGS += -D_POSIX_C_SOURCE=200809L
  CXXFLAGS += -D_POSIX_C_SOURCE=200809L
endif

# Debug/Release build
ifdef DEBUG
  CFLAGS += -g -O0
  BUILD_DIR := build/debug
else
  CFLAGS += -O2 -DNDEBUG
  BUILD_DIR := build/release
endif

# Platform-specific flags
ifeq ($(UNAME),Linux)
  CFLAGS += -DLINUX
  LDFLAGS := -lm -ldl -rdynamic
  SHARED_EXT := .so
  EXE_EXT :=
endif

ifeq ($(UNAME),FreeBSD)
  CFLAGS += -DFREEBSD
  LDFLAGS := -lm -lexecinfo
  SHARED_EXT := .so
  EXE_EXT :=
endif

ifeq ($(UNAME),OpenBSD)
  CFLAGS += -DOPENBSD -I/usr/X11R6/include
  LDFLAGS := -lm -lexecinfo -L/usr/X11R6/lib
  SHARED_EXT := .so
  EXE_EXT :=
endif

ifeq ($(UNAME),Darwin)
  CFLAGS += -DMACOS_X -arch $(ARCH)
  LDFLAGS := -arch $(ARCH)
  # Symbols referenced by plugins are resolved at dlopen time from the host exe.
  SHARED_LDFLAGS := -undefined dynamic_lookup
  SHARED_EXT := .dylib
  EXE_EXT :=
endif

ifeq ($(UNAME),Haiku)
  CFLAGS += -DHAIKU
  # $ORIGIN rpath: binary looks for libSDL3.so.0 in its own directory first.
  LDFLAGS := -lnetwork -lexecinfo -Wl,-rpath,$$ORIGIN
  SHARED_EXT := .so
  EXE_EXT :=
endif

ifeq ($(UNAME),NetBSD)
  CFLAGS += -DNETBSD -I/usr/X11R7/include -I/usr/pkg/include
  LDFLAGS := -lm -lexecinfo -Wl,-rpath,$$ORIGIN -Wl,-rpath-link,/usr/X11R7/lib -L/usr/X11R7/lib -L/usr/pkg/lib
  SHARED_EXT := .so
  EXE_EXT :=
endif

ifeq ($(UNAME),SunOS)
  # Oracle Solaris and OpenIndiana (illumos); both report SunOS.
  # Sun ld does not support --export-dynamic; networking is in separate libs.
  CFLAGS += -DSUNOS -D__EXTENSIONS__ -I/usr/X11R6/include
  LDFLAGS := -lm -lsocket -lnsl -Wl,-rpath,$$ORIGIN -L/usr/X11R6/lib
  SHARED_EXT := .so
  EXE_EXT :=
endif

# SDL3 configuration
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null || echo "-I/usr/include/SDL3")
SDL3_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")

# OpenGL configuration
GL_CFLAGS :=
ifeq ($(UNAME),Darwin)
  GL_LIBS := -framework OpenGL
else ifeq ($(UNAME),NetBSD)
  # Embed the search paths so they precede -lGL regardless of LIBS ordering.
  GL_LIBS := -L/usr/X11R7/lib -L/usr/pkg/lib -lGL
else
  GL_LIBS := -lGL
endif

# Vulkan configuration. Only the headers are needed at build time - volk
# dlopen()s libvulkan at runtime and it is never linked - so the Vulkan renderer
# is built wherever <vulkan/vulkan.h> can be found and skipped elsewhere
# (Haiku, Solaris, ...). Point at a custom SDK with VULKAN_CFLAGS=-I/path,
# or skip it explicitly with NO_VULKAN=1.
VULKAN_CFLAGS ?=

# Additional libraries
# dl is part of libc on OpenBSD, Darwin, Haiku, and NetBSD.
ifeq ($(UNAME),Darwin)
  DL_LIBS :=
else ifeq ($(UNAME),OpenBSD)
  DL_LIBS :=
else ifeq ($(UNAME),Haiku)
  DL_LIBS :=
else ifeq ($(UNAME),NetBSD)
  DL_LIBS :=
else
  DL_LIBS := -ldl
endif
MATH_LIBS := -lm
# pthread is part of libroot on Haiku; -lpthread is not needed.
ifeq ($(UNAME),Haiku)
  PTHREAD_LIBS :=
else
  PTHREAD_LIBS := -lpthread
endif

# Combine flags
CFLAGS += $(SDL3_CFLAGS) $(GL_CFLAGS)
LIBS := $(SDL3_LIBS) $(GL_LIBS) $(DL_LIBS) $(MATH_LIBS) $(PTHREAD_LIBS) $(LDFLAGS)

# Source directories
CS_SHARED_DIR := src/quake2/src/cs_shared
CLIENT_DIR    := src/quake2/src/client
SERVER_DIR    := src/quake2/src/server
QCOMMON_DIR   := src/qcommon
GAME_DIR      := src/game/src
PLAYER_DIR    := src/Player/src
H2COMMON_DIR  := src/H2Common/src
UNIX_DIR      := src/quake2/src/unix
CLFX_DIR      := src/clfx/src
INCLUDE_DIR   := include

# Common source files (shared between client and server)
COMMON_SRCS := \
	$(CS_SHARED_DIR)/cmd.c \
	$(CS_SHARED_DIR)/cmodel.c \
	$(CS_SHARED_DIR)/common.c \
	$(CS_SHARED_DIR)/cvar.c \
	$(CS_SHARED_DIR)/Debug.c \
	$(CS_SHARED_DIR)/files.c \
	$(CS_SHARED_DIR)/md4.c \
	$(CS_SHARED_DIR)/net_chan.c \
	$(CS_SHARED_DIR)/pmove.c \
	$(CS_SHARED_DIR)/tokens.c \
	$(QCOMMON_DIR)/netmsg_read.c \
	$(QCOMMON_DIR)/netmsg_write.c \
	$(QCOMMON_DIR)/Reference.c \
	$(QCOMMON_DIR)/Skeletons.c \
	$(QCOMMON_DIR)/Message.c \
	$(QCOMMON_DIR)/turbsin.c \
	$(QCOMMON_DIR)/anorms.c \
	$(UNIX_DIR)/p_dll_unix.c \
	$(GAME_DIR)/q_Shared.c \
	$(H2COMMON_DIR)/ByteOrder.c \
	$(H2COMMON_DIR)/Common.c \
	$(H2COMMON_DIR)/Console.c \
	$(H2COMMON_DIR)/InfoStrings.c \
	$(H2COMMON_DIR)/Math.c \
	$(H2COMMON_DIR)/Matrix.c \
	$(H2COMMON_DIR)/Motion.c \
	$(H2COMMON_DIR)/Random.c \
	$(H2COMMON_DIR)/ResourceManager.c \
	$(H2COMMON_DIR)/SinglyLinkedList.c \
	$(H2COMMON_DIR)/SurfaceProps.c \
	$(H2COMMON_DIR)/TextPalette.c \
	$(H2COMMON_DIR)/Vector.c \
	$(H2COMMON_DIR)/q_Physics.c

# Client source files
CLIENT_SRCS := \
	$(CLIENT_DIR)/cl_camera.c \
	$(CLIENT_DIR)/cl_demo.c \
	$(CLIENT_DIR)/cl_effects.c \
	$(CLIENT_DIR)/cl_entities.c \
	$(CLIENT_DIR)/cl_globals.c \
	$(CLIENT_DIR)/cl_input.c \
	$(CLIENT_DIR)/cl_inventory.c \
	$(CLIENT_DIR)/cl_main.c \
	$(CLIENT_DIR)/cl_messages.c \
	$(CLIENT_DIR)/cl_parse.c \
	$(CLIENT_DIR)/cl_player.c \
	$(CLIENT_DIR)/cl_prediction.c \
	$(CLIENT_DIR)/cl_screen.c \
	$(CLIENT_DIR)/cl_skeletons.c \
	$(CLIENT_DIR)/cl_smk.c \
	$(CLIENT_DIR)/cl_mpeg.c \
	$(CLIENT_DIR)/cl_view.c \
	$(CLIENT_DIR)/console.c \
	$(CLIENT_DIR)/glimp_sdl3.c \
	$(CLIENT_DIR)/input_sdl3.c \
	$(CLIENT_DIR)/keys.c \
	$(CLIENT_DIR)/menu.c \
	$(wildcard $(CLIENT_DIR)/menus/*.c) \
	$(INCLUDE_DIR)/libsmacker/smacker.c \
	$(INCLUDE_DIR)/pl_mpeg/pl_mpeg.c \
	src/snd_sdl3/src/snd_main.c \
	src/snd_sdl3/src/snd_sdl3.c \
	src/snd_sdl3/src/snd_wav.c \
	src/snd_sdl3/src/snd_ogg.c \
	src/snd_sdl3/src/snd_LowpassFilter.c

# Server source files
SERVER_SRCS := \
	$(SERVER_DIR)/sv_ccmds.c \
	$(SERVER_DIR)/sv_effects.c \
	$(SERVER_DIR)/sv_entities.c \
	$(SERVER_DIR)/sv_game.c \
	$(SERVER_DIR)/sv_init.c \
	$(SERVER_DIR)/sv_main.c \
	$(SERVER_DIR)/sv_send.c \
	$(SERVER_DIR)/sv_user.c \
	$(SERVER_DIR)/sv_world.c

# Unix-specific source files (replaces win32/*.c)
UNIX_SRCS := \
	$(UNIX_DIR)/sys_unix.c \
	$(UNIX_DIR)/net_udp.c \
	$(UNIX_DIR)/q_shunix.c \
	$(UNIX_DIR)/main.c \
	$(UNIX_DIR)/vid_sdl3.c \
	$(UNIX_DIR)/vid_screenshot.c \
	$(UNIX_DIR)/snd_dll.c \
	$(UNIX_DIR)/clfx_dll.c

# ---------------------------------------------------------------------------
# Renderer modules
#
# Each renderer is a loadable "ref_<id>" shared object placed next to the
# executable; the engine discovers them by scanning its own directory
# (VID_InitReflibInfos()) and dlopen()s the one vid_ref selects. This mirrors
# the CMake build - keep the two in sync when adding a renderer.
# ---------------------------------------------------------------------------

# Renderer-agnostic sources shared by every module. gl1_FindSurface.c is
# deliberately NOT shared: FindSurface is bound to NULL in gl3/vk and the
# glpoly_t layout differs there.
REF_SHARED_SRCS := \
	src/ref_gl1/src/Hunk.c \
	src/ref_gl1/src/anormtab.c \
	src/ref_gl1/src/gl1_Matrix4.c \
	src/ref_gl1/src/Skeletons/r_Skeletons.c \
	src/ref_gl1/src/Skeletons/r_SkeletonLerp.c

# OpenGL 1.3 renderer
GL1_SRCS := \
	src/ref_gl1/src/gl1_Main.c \
	src/ref_gl1/src/gl1_Draw.c \
	src/ref_gl1/src/gl1_DrawBook.c \
	src/ref_gl1/src/gl1_DrawCinematic.c \
	src/ref_gl1/src/gl1_FindSurface.c \
	src/ref_gl1/src/gl1_FlexModel.c \
	src/ref_gl1/src/gl1_Image.c \
	src/ref_gl1/src/gl1_Light.c \
	src/ref_gl1/src/gl1_Lightmap.c \
	src/ref_gl1/src/gl1_Misc.c \
	src/ref_gl1/src/gl1_Model.c \
	src/ref_gl1/src/gl1_SDL.c \
	src/ref_gl1/src/gl1_Sky.c \
	src/ref_gl1/src/gl1_Sprite.c \
	src/ref_gl1/src/gl1_Surface.c \
	src/ref_gl1/src/gl1_Warp.c \
	$(REF_SHARED_SRCS) \
	$(INCLUDE_DIR)/glad-GL1.3/glad.c

# OpenGL 3.2 renderer
GL3_SRCS := \
	$(wildcard src/ref_gl3/src/*.c) \
	$(REF_SHARED_SRCS) \
	$(INCLUDE_DIR)/glad-GL3.2/src/glad.c

# Vulkan renderer. volk dlopen()s libvulkan at runtime, so only the Vulkan
# headers are needed at build time and libvulkan is never linked.
# src/ref_vk/src/spirv/*.c are #included by vk_shaders.c - do not glob them.
VK_SRCS := \
	$(wildcard src/ref_vk/src/*.c) \
	src/ref_vk/volk/volk.c \
	$(REF_SHARED_SRCS)

# Game DLL sources (recursive)
GAME_C_SRCS   := $(shell find "$(GAME_DIR)" -name "*.c")
GAME_CPP_SRCS := $(shell find "$(GAME_DIR)" -name "*.cpp")

# Player DLL sources
PLAYER_SRCS := $(wildcard $(PLAYER_DIR)/*.c)

# Client Effects DLL sources (recursive) + qcommon files the SO needs (can't rely
# on the main exe's symbols since they have hidden visibility).
CLFX_SRCS := $(shell find "$(CLFX_DIR)" -name "*.c")
CLFX_EXTRA_SRCS := \
	$(QCOMMON_DIR)/turbsin.c \
	$(QCOMMON_DIR)/netmsg_read.c \
	$(QCOMMON_DIR)/anorms.c

# Object files
# q_Shared.c lives under src/game/src, so the game-DLL pattern rule would claim
# it (hidden visibility, -DGAME_DLL) and the exe would share that object. The exe
# needs its own copy built with EXE_CFLAGS so symbols like BoxOnPlaneSide are
# exported for the renderer modules. COMMON_SRCS itself is left intact for the
# dedicated server, which builds its own objects under $(BUILD_DIR)/ded/.
EXE_EXTRA_SRCS := $(GAME_DIR)/q_Shared.c
COMMON_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(filter-out $(EXE_EXTRA_SRCS),$(COMMON_SRCS))) \
                 $(patsubst %.c,$(BUILD_DIR)/exe_extra/%.o,$(EXE_EXTRA_SRCS))
CLIENT_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CLIENT_SRCS))
SERVER_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SERVER_SRCS))
UNIX_OBJS     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(UNIX_SRCS))
# Renderer modules get their own object trees: each is compiled with different
# include paths/defines, so the same shared source must not collide.
GL1_OBJS := $(patsubst %.c,$(BUILD_DIR)/ref_gl1_mod/%.o,$(GL1_SRCS))
GL3_OBJS := $(patsubst %.c,$(BUILD_DIR)/ref_gl3_mod/%.o,$(GL3_SRCS))
VK_OBJS  := $(patsubst %.c,$(BUILD_DIR)/ref_vk_mod/%.o,$(VK_SRCS))
GAME_OBJS     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(GAME_C_SRCS)) \
                 $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(GAME_CPP_SRCS))
PLAYER_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(PLAYER_SRCS))
CLFX_OBJS     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CLFX_SRCS)) \
                 $(patsubst %.c,$(BUILD_DIR)/clfx_extra/%.o,$(CLFX_EXTRA_SRCS))

# All object files for the main executable. The renderers are NOT linked in -
# they are loadable modules built separately (see "Renderer modules" above).
ALL_EXE_OBJS := $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) $(UNIX_OBJS)

# Dedicated server source files (no client, renderer, or SDL/GL)
DED_UNIX_SRCS := \
	$(UNIX_DIR)/sys_unix.c \
	$(UNIX_DIR)/net_udp.c \
	$(UNIX_DIR)/q_shunix.c \
	$(UNIX_DIR)/main.c \
	$(UNIX_DIR)/ded_stubs.c

DED_SRCS := $(COMMON_SRCS) $(SERVER_SRCS) $(DED_UNIX_SRCS)
DED_OBJS := $(patsubst %.c,$(BUILD_DIR)/ded/%.o,$(DED_SRCS))

# Targets
.PHONY: all clean game player client clfx ded server dedicated renderers
# Note: "Client Effects.so" has a space in its name, so it cannot be a Make file target.
# The clfx rule is phony and links directly to the correctly-named output.

all: client renderers game player clfx ded

# Main client executable (replaces Heretic2R.exe)
client: $(BUILD_DIR)/heretic2r$(EXE_EXT)

ifeq ($(filter $(UNAME),Darwin SunOS),)
  EXPORT_DYNAMIC := -Wl,--export-dynamic
endif

$(BUILD_DIR)/heretic2r$(EXE_EXT): $(ALL_EXE_OBJS)
	@echo "  LINK    $@"
	@$(CC) $(CFLAGS) $(EXPORT_DYNAMIC) -o $@ $^ $(LIBS)

# Dedicated server executable
ded server dedicated: $(BUILD_DIR)/heretic2r-server$(EXE_EXT)

DED_CFLAGS := $(filter-out $(SDL3_CFLAGS),$(CFLAGS)) -DDEDICATED_ONLY
DED_LIBS   := $(MATH_LIBS) $(PTHREAD_LIBS)
ifeq ($(UNAME),Linux)
  DED_LIBS += -ldl -lm
endif
ifeq ($(UNAME),FreeBSD)
  DED_LIBS += -lexecinfo
endif
ifeq ($(UNAME),OpenBSD)
  DED_LIBS += -lexecinfo -L/usr/X11R6/lib
endif
ifeq ($(UNAME),Haiku)
  DED_LIBS += -lnetwork -lexecinfo
endif
ifeq ($(UNAME),NetBSD)
  DED_LIBS += -lexecinfo -L/usr/X11R7/lib -L/usr/pkg/lib
endif
ifeq ($(UNAME),SunOS)
  DED_LIBS += -lsocket -lnsl
endif

$(BUILD_DIR)/heretic2r-server$(EXE_EXT): $(DED_OBJS)
	@echo "  LINK    $@"
	@mkdir -p $(BUILD_DIR)
	@$(CC) $(DED_CFLAGS) $(EXPORT_DYNAMIC) -o $@ $^ $(DED_LIBS)

$(BUILD_DIR)/ded/%.o: %.c
	@echo "  CC (ded) $<"
	@mkdir -p $(dir $@)
	@$(CC) $(DED_CFLAGS) -fPIC -MMD -MP -c -o $@ $<

# Game DLL
game: $(BUILD_DIR)/base/gamex86$(SHARED_EXT)

$(BUILD_DIR)/base/gamex86$(SHARED_EXT): $(GAME_OBJS)
	@echo "  LINK    $@"
	@mkdir -p $(BUILD_DIR)/base
	@$(CXX) $(CXXFLAGS) -shared $(SHARED_LDFLAGS) -o $@ $(GAME_OBJS) $(LIBS)

# Player DLL
player: $(BUILD_DIR)/base/Player$(SHARED_EXT)

$(BUILD_DIR)/base/Player$(SHARED_EXT): $(PLAYER_OBJS)
	@echo "  CC      $@"
	@mkdir -p $(BUILD_DIR)/base
	@$(CC) $(filter-out -fvisibility=hidden,$(CFLAGS)) -fPIC -DPLAYER_DLL -shared $(SHARED_LDFLAGS) -o $@ $(PLAYER_OBJS) $(LIBS)

# Client Effects DLL
clfx: $(CLFX_OBJS)
	@echo "  LINK    Client Effects$(SHARED_EXT)"
	@mkdir -p $(BUILD_DIR)/base
	@$(CC) $(CLFX_CFLAGS) -shared $(SHARED_LDFLAGS) -o "$(BUILD_DIR)/base/Client Effects$(SHARED_EXT)" $(CLFX_OBJS) $(LIBS)

CLFX_CFLAGS := $(filter-out -fvisibility=hidden,$(CFLAGS)) -fPIC -DCLIENT_EFFECTS_DLL \
	-include ./src/quake2/src/unix/compat.h

# ---- Renderer modules ----
# GetRefAPI carries __attribute__((visibility("default"))), so the global
# -fvisibility=hidden is kept: only that entry point is exported and everything
# else stays private to the module.
REF_CFLAGS := $(CFLAGS) -fPIC
GL1_CFLAGS := $(REF_CFLAGS)
GL3_CFLAGS := $(REF_CFLAGS) -I./src/ref_gl3/src -I./$(INCLUDE_DIR)/glad-GL3.2/include -DGL3_MODULES_READY
VK_CFLAGS  := $(REF_CFLAGS) -I./src/ref_vk/src -I./src/ref_vk $(VULKAN_CFLAGS) -DVK_MODULES_READY

# Probe volk.h with the real build flags. Uses -include rather than piping a
# source line in: make turns '\#' into a literal backslash-hash, which the
# preprocessor accepts as plain text, so the old probe always succeeded.
ifdef NO_VULKAN
  HAVE_VULKAN :=
else
  HAVE_VULKAN := $(shell $(CC) $(VK_CFLAGS) -E -include volk/volk.h -x c /dev/null >/dev/null 2>&1 && echo yes)
endif

# Renderers to build: gl1 and gl3 everywhere, vk only where headers were found.
REF_MODULES := $(BUILD_DIR)/ref_gl1$(SHARED_EXT) $(BUILD_DIR)/ref_gl3$(SHARED_EXT)
ifeq ($(HAVE_VULKAN),yes)
  REF_MODULES += $(BUILD_DIR)/ref_vk$(SHARED_EXT)
endif

renderers: $(REF_MODULES)

$(BUILD_DIR)/ref_gl1$(SHARED_EXT): $(GL1_OBJS)
	@echo "  LINK    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(GL1_CFLAGS) -shared $(SHARED_LDFLAGS) -o $@ $(GL1_OBJS) $(LIBS)

$(BUILD_DIR)/ref_gl3$(SHARED_EXT): $(GL3_OBJS)
	@echo "  LINK    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(GL3_CFLAGS) -shared $(SHARED_LDFLAGS) -o $@ $(GL3_OBJS) $(LIBS)

$(BUILD_DIR)/ref_vk$(SHARED_EXT): $(VK_OBJS)
	@echo "  LINK    $@"
	@mkdir -p $(dir $@)
	@$(CC) $(VK_CFLAGS) -shared $(SHARED_LDFLAGS) -o $@ $(VK_OBJS) $(LIBS)

$(BUILD_DIR)/ref_gl1_mod/%.o: %.c
	@echo "  CC      $< (gl1)"
	@mkdir -p $(dir $@)
	@$(CC) $(GL1_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/ref_gl3_mod/%.o: %.c
	@echo "  CC      $< (gl3)"
	@mkdir -p $(dir $@)
	@$(CC) $(GL3_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/ref_vk_mod/%.o: %.c
	@echo "  CC      $< (vk)"
	@mkdir -p $(dir $@)
	@$(CC) $(VK_CFLAGS) -MMD -MP -c -o $@ $<

# ---- Compile rules ----

# Game C files
$(BUILD_DIR)/$(GAME_DIR)/%.o: $(GAME_DIR)/%.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -DGAME_DLL -fPIC -MMD -MP -c -o $@ $<

$(BUILD_DIR)/$(GAME_DIR)/%.o: $(GAME_DIR)/%.cpp
	@echo "  CXX     $<"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -DGAME_DLL -fPIC -MMD -MP -c -o $@ $<

# Player DLL files
$(BUILD_DIR)/$(PLAYER_DIR)/%.o: $(PLAYER_DIR)/%.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(filter-out -fvisibility=hidden,$(CFLAGS)) -fPIC -DPLAYER_DLL -MMD -MP -c -o $@ $<

# Main-exe copies of sources that another pattern rule would otherwise claim.
$(BUILD_DIR)/exe_extra/%.o: %.c
	@echo "  CC      $< (exe)"
	@mkdir -p $(dir $@)
	@$(CC) $(EXE_CFLAGS) -fPIC -MMD -MP -c -o $@ $<

# Client Effects extra qcommon files (compiled with CLFX flags, not main exe flags)
$(BUILD_DIR)/clfx_extra/%.o: %.c
	@echo "  CC      $< (clfx)"
	@mkdir -p $(dir $@)
	@$(CC) $(CLFX_CFLAGS) -MMD -MP -c -o $@ $<

# Client Effects files
$(BUILD_DIR)/$(CLFX_DIR)/%.o: $(CLFX_DIR)/%.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CLFX_CFLAGS) -MMD -MP -c -o $@ $<

# p_dll_unix.c: part of the main exe, no GAME_DLL
$(BUILD_DIR)/$(UNIX_DIR)/p_dll_unix.o: $(UNIX_DIR)/p_dll_unix.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(EXE_CFLAGS) -MMD -MP -c -o $@ $<

# Generic compile rule for the main executable's objects.
# -fvisibility=hidden is filtered out here: the renderer modules resolve ~75
# engine symbols (turbsin, AngleVectors, Com_sprintf, ...) from the executable
# at dlopen() time, and hidden symbols cannot be exported even with
# --export-dynamic. The modules themselves keep hidden visibility - only their
# GetRefAPI entry point is marked visibility("default").
EXE_CFLAGS := $(filter-out -fvisibility=hidden,$(CFLAGS))

$(BUILD_DIR)/%.o: %.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(EXE_CFLAGS) -fPIC -MMD -MP -c -o $@ $<

# Include auto-generated header dependencies
-include $(ALL_EXE_OBJS:.o=.d)
-include $(GL1_OBJS:.o=.d)
-include $(GL3_OBJS:.o=.d)
-include $(VK_OBJS:.o=.d)
-include $(GAME_OBJS:.o=.d)
-include $(PLAYER_OBJS:.o=.d)
-include $(CLFX_OBJS:.o=.d)

# Clean build artifacts
clean:
	@echo "  CLEAN"
	@rm -rf $(BUILD_DIR)

# Install (placeholder - needs proper installation logic)
install: all
	@echo "Installing Heretic2R..."
	@mkdir -p /usr/local/games/heretic2r
	@cp $(BUILD_DIR)/heretic2r$(EXE_EXT) /usr/local/games/heretic2r/
	@cp $(REF_MODULES) /usr/local/games/heretic2r/
	@cp $(BUILD_DIR)/base/gamex86$(SHARED_EXT) /usr/local/games/heretic2r/base/
	@cp $(BUILD_DIR)/base/Player$(SHARED_EXT) /usr/local/games/heretic2r/base/
	@cp $(BUILD_DIR)/base/ClientEffects$(SHARED_EXT) /usr/local/games/heretic2r/base/

# Help
help:
	@echo "Heretic2R Unix/Linux Build System"
	@echo "================================="
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build client, renderers, game, player, and clfx (default)"
	@echo "  client    - Build the main client executable"
	@echo "  renderers - Build the ref_gl1/ref_gl3/ref_vk renderer modules"
	@echo "  game      - Build the game DLL"
	@echo "  player    - Build the player DLL"
	@echo "  clfx      - Build the client effects DLL"
	@echo "  clean     - Remove all build artifacts"
	@echo "  install   - Install to /usr/local/games/heretic2r"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1   - Build with debug symbols and assertions"
	@echo ""
	@echo "Platform: $(UNAME) $(ARCH)"
	@echo "Compiler: $(CC)"
