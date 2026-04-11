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

ifeq ($(UNAME),Darwin)
  CFLAGS += -DMACOS_X -arch $(ARCH)
  LDFLAGS := -arch $(ARCH)
  SHARED_EXT := .dylib
  EXE_EXT :=
endif

# SDL3 configuration
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null || echo "-I/usr/include/SDL3")
SDL3_LIBS := $(shell pkg-config --libs sdl3 2>/dev/null || echo "-lSDL3")

# OpenGL configuration
GL_CFLAGS :=
GL_LIBS := -lGL

# Additional libraries
DL_LIBS := -ldl
MATH_LIBS := -lm
PTHREAD_LIBS := -lpthread

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
	$(CLIENT_DIR)/cl_view.c \
	$(CLIENT_DIR)/console.c \
	$(CLIENT_DIR)/glimp_sdl3.c \
	$(CLIENT_DIR)/input_sdl3.c \
	$(CLIENT_DIR)/keys.c \
	$(CLIENT_DIR)/menu.c \
	$(wildcard $(CLIENT_DIR)/menus/*.c) \
	$(INCLUDE_DIR)/libsmacker/smacker.c \
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
	$(UNIX_DIR)/snd_dll.c \
	$(UNIX_DIR)/clfx_dll.c

# Renderer source files (ref_gl1)
RENDERER_SRCS := \
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
	src/ref_gl1/src/gl1_Matrix4.c \
	src/ref_gl1/src/anormtab.c \
	src/ref_gl1/src/Hunk.c \
	src/ref_gl1/src/Skeletons/r_SkeletonLerp.c \
	src/ref_gl1/src/Skeletons/r_Skeletons.c \
	$(INCLUDE_DIR)/glad-GL1.3/glad.c

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
COMMON_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
CLIENT_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CLIENT_SRCS))
SERVER_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SERVER_SRCS))
UNIX_OBJS     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(UNIX_SRCS))
RENDERER_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(RENDERER_SRCS))
GAME_OBJS     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(GAME_C_SRCS)) \
                 $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(GAME_CPP_SRCS))
PLAYER_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(PLAYER_SRCS))
CLFX_OBJS     := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CLFX_SRCS)) \
                 $(patsubst %.c,$(BUILD_DIR)/clfx_extra/%.o,$(CLFX_EXTRA_SRCS))

# All object files for the main executable
ALL_EXE_OBJS := $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) $(UNIX_OBJS) $(RENDERER_OBJS)

# Targets
.PHONY: all clean game player client server clfx
# Note: "Client Effects.so" has a space in its name, so it cannot be a Make file target.
# The clfx rule is phony and links directly to the correctly-named output.

all: client game player clfx

# Main client executable (replaces Heretic2R.exe)
client: $(BUILD_DIR)/heretic2r$(EXE_EXT)

$(BUILD_DIR)/heretic2r$(EXE_EXT): $(ALL_EXE_OBJS)
	@echo "  LINK    $@"
	@$(CC) $(CFLAGS) -Wl,--export-dynamic -o $@ $^ $(LIBS)

# Game DLL
game: $(BUILD_DIR)/base/gamex86$(SHARED_EXT)

$(BUILD_DIR)/base/gamex86$(SHARED_EXT): $(GAME_OBJS)
	@echo "  LINK    $@"
	@mkdir -p $(BUILD_DIR)/base
	@$(CXX) $(CXXFLAGS) -shared -o $@ $(GAME_OBJS) $(LIBS)

# Player DLL
player: $(BUILD_DIR)/base/Player$(SHARED_EXT)

$(BUILD_DIR)/base/Player$(SHARED_EXT): $(PLAYER_OBJS)
	@echo "  CC      $@"
	@mkdir -p $(BUILD_DIR)/base
	@$(CC) $(filter-out -fvisibility=hidden,$(CFLAGS)) -fPIC -DPLAYER_DLL -shared -o $@ $(PLAYER_OBJS) $(LIBS)

# Client Effects DLL
clfx: $(CLFX_OBJS)
	@echo "  LINK    Client Effects$(SHARED_EXT)"
	@mkdir -p $(BUILD_DIR)/base
	@$(CC) $(CLFX_CFLAGS) -shared -o "$(BUILD_DIR)/base/Client Effects$(SHARED_EXT)" $(CLFX_OBJS) $(LIBS)

CLFX_CFLAGS := $(filter-out -fvisibility=hidden,$(CFLAGS)) -fPIC -DCLIENT_EFFECTS_DLL \
	-include ./src/quake2/src/unix/compat.h

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
	@$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Generic compile rule for all remaining .c files
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -fPIC -MMD -MP -c -o $@ $<

# Include auto-generated header dependencies
-include $(ALL_EXE_OBJS:.o=.d)
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
	@cp $(BUILD_DIR)/base/gamex86$(SHARED_EXT) /usr/local/games/heretic2r/base/
	@cp $(BUILD_DIR)/base/Player$(SHARED_EXT) /usr/local/games/heretic2r/base/
	@cp $(BUILD_DIR)/base/ClientEffects$(SHARED_EXT) /usr/local/games/heretic2r/base/

# Help
help:
	@echo "Heretic2R Unix/Linux Build System"
	@echo "================================="
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build client, game, player, and clfx (default)"
	@echo "  client    - Build the main client executable"
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
