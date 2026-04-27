//
// sys_unix.c -- UNIX system functions
//
// Copyright (C) 1997-2001 Id Software, Inc.
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include "Quake2Main.h"
#include "qcommon.h"
#include "input.h"
#include "ResourceManager.h"
#include "SinglyLinkedList.h"

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#include <sys/select.h>

static qboolean stdin_active = true;

static char console_text[256];

// No DllMain; initialize node manager here instead.
extern ResourceManager_t sllist_nodes_mgr;

H2R_NORETURN void Sys_Error(const char* error, ...)
{
	va_list argptr;
	char text[1024];

	va_start(argptr, error);
	vsnprintf(text, sizeof(text), error, argptr);
	va_end(argptr);

	CL_Shutdown();

	fprintf(stderr, "Error: %s\n", text);

	exit(1);
}

H2R_NORETURN void Sys_Quit(void)
{
	CL_Shutdown();

	exit(0);
}

qboolean Sys_LoadGameDll(const char* dll_name, HINSTANCE* hinst, DWORD* checksum)
{
	char name[MAX_OSPATH];
	char* path = NULL;
	struct stat st;

	*hinst = NULL;
	*checksum = 0;

	while (true)
	{
		path = FS_NextPath(path);
		if (path == NULL)
			break;

		Com_sprintf(name, sizeof(name), "%s/%s.so", path, dll_name);
		Com_DDPrintf(2, "Trying to load %s\n", name);
		*hinst = dlopen(name, RTLD_NOW);
		if (*hinst != NULL)
		{
			if (stat(name, &st) == 0)
				*checksum = (DWORD)st.st_size;
			else
				*checksum = 0;
			Com_DDPrintf(2, "dlopen (%s)\n", name);
			return true;
		}
		const char* dlerror_msg = dlerror();
		Com_Printf("Failed to load %s: %s\n", name, dlerror_msg ? dlerror_msg : "unknown error");

		Com_sprintf(name, sizeof(name), "%s/%s.dylib", path, dll_name);
		Com_DDPrintf(2, "Trying to load %s\n", name);
		*hinst = dlopen(name, RTLD_NOW);
		if (*hinst != NULL)
		{
			if (stat(name, &st) == 0)
				*checksum = (DWORD)st.st_size;
			else
				*checksum = 0;
			Com_DDPrintf(2, "dlopen (%s)\n", name);
			return true;
		}
	}

	return false;
}

void Sys_UnloadGameDll(const char* name, HINSTANCE* hinst)
{
	if (*hinst != NULL)
	{
		if (dlclose(*hinst) != 0)
			Sys_Error("Failed to unload %s: %s", name, dlerror());

		*hinst = NULL;
	}
}

void* Sys_GetProcAddress(HINSTANCE hinst, const char* name)
{
	if (hinst == NULL)
		return NULL;

	return dlsym(hinst, name);
}

void Sys_Init(void)
{
	Set_Com_Printf(Com_Printf);
	ResMngr_Con(&sllist_nodes_mgr, SLL_NODE_SIZE, SLL_NODE_BLOCK_SIZE);

	if ((int)dedicated->value)
		stdin_active = true;
}

char* Sys_ConsoleInput(void)
{
	fd_set fdset;
	struct timeval timeout;

	if (dedicated == NULL || !(int)dedicated->value)
		return NULL;

	if (!stdin_active)
		return NULL;

	FD_ZERO(&fdset);
	FD_SET(STDIN_FILENO, &fdset);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(STDIN_FILENO, &fdset))
		return NULL;

	int len = read(STDIN_FILENO, console_text, sizeof(console_text) - 1);

	if (len < 1)
		return NULL;

	console_text[len] = 0;

	if (console_text[len - 1] == '\n')
		console_text[len - 1] = 0;

	return console_text;
}

void Sys_ConsoleOutput(const char* string)
{
	fputs(string, stdout);
	fflush(stdout);
}

long long Sys_Microseconds(void)
{
#ifdef __APPLE__
	static mach_timebase_info_data_t info;
	if (info.denom == 0)
		mach_timebase_info(&info);
	return mach_absolute_time() * info.numer / info.denom / 1000;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000LL + (long long)ts.tv_nsec / 1000LL;
#endif
}

int Sys_Milliseconds(void)
{
	return (int)(Sys_Microseconds() / 1000LL);
}

void Sys_Mkdir(const char* path)
{
	if (mkdir(path, 0755) == -1 && errno != EEXIST)
	{
		Sys_Error("Failed to create directory: %s", path);
	}
}

qboolean Sys_IsDir(const char* path)
{
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode);
	return false;
}

qboolean Sys_IsFile(const char* path)
{
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISREG(st.st_mode);
	return false;
}

qboolean Sys_GetWorkingDir(char* buffer, size_t len)
{
	if (getcwd(buffer, len) != NULL)
		return true;
	return false;
}

static DIR* finddir = NULL;
static char findpath[MAX_OSPATH];
static char findpattern[MAX_OSPATH];
static int finddir_len;
static unsigned findmusthave;
static unsigned findcanthave;

static qboolean FindMatchAttributes(const struct dirent* d)
{
	if (d->d_name[0] == '.' && (d->d_name[1] == '\0' || (d->d_name[1] == '.' && d->d_name[2] == '\0')))
		return false;

	qboolean is_dir = false;
	{
		char full[MAX_OSPATH];
		struct stat st;
		snprintf(full, sizeof(full), "%.*s/%s", finddir_len, findpath, d->d_name);
		if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
			is_dir = true;
	}

	const qboolean is_hidden = (d->d_name[0] == '.');

	if ((findcanthave & SFF_HIDDEN) && is_hidden)
		return false;
	if ((findmusthave & SFF_SUBDIR) && !is_dir)
		return false;
	if ((findcanthave & SFF_SUBDIR) && is_dir)
		return false;

	return true;
}

static void NormalizePattern(const char* raw, char* out, size_t outsz)
{
	strncpy(out, raw, outsz - 1);
	out[outsz - 1] = '\0';
	const size_t len = strlen(out);
	if (len >= 2 && out[len - 1] == '.' && out[len - 2] == '*')
		out[len - 1] = '\0';
}

char* Sys_FindFirst(const char* path, unsigned musthave, unsigned canthave)
{
	struct dirent* d;
	char* p;

	if (finddir)
		Sys_Error("Sys_FindFirst without close");

	findmusthave = musthave;
	findcanthave = canthave;

	strcpy(findpath, path);
	p = strrchr(findpath, '/');
	if (p)
	{
		*p = 0;
		char raw[MAX_OSPATH];
		strcpy(raw, p + 1);
		NormalizePattern(raw, findpattern, sizeof(findpattern));
	}
	else
	{
		strcpy(findpattern, "*");
	}

	finddir_len = (int)strlen(findpath);

	finddir = opendir(findpath);
	if (!finddir)
		return NULL;

	while ((d = readdir(finddir)) != NULL)
	{
		if (!FindMatchAttributes(d))
			continue;

		const qboolean is_plain = (*findpattern && !strpbrk(findpattern, "*?["));
		const qboolean match = !*findpattern
			|| !strcmp(findpattern, "*")
			|| (is_plain ? (strcasecmp(findpattern, d->d_name) == 0) : !fnmatch(findpattern, d->d_name, 0));
		if (match)
		{
			sprintf(findpath + finddir_len, "/%s", d->d_name);
			return findpath;
		}
	}

	return NULL;
}

char* Sys_FindNext(unsigned musthave, unsigned canthave)
{
	struct dirent* d;

	if (!finddir)
		return NULL;

	while ((d = readdir(finddir)) != NULL)
	{
		if (!FindMatchAttributes(d))
			continue;

		const qboolean is_plain = (*findpattern && !strpbrk(findpattern, "*?["));
		const qboolean match = !*findpattern
			|| !strcmp(findpattern, "*")
			|| (is_plain ? (strcasecmp(findpattern, d->d_name) == 0) : !fnmatch(findpattern, d->d_name, 0));
		if (match)
		{
			sprintf(findpath + finddir_len, "/%s", d->d_name);
			return findpath;
		}
	}

	return NULL;
}

void Sys_FindClose(void)
{
	if (finddir)
	{
		closedir(finddir);
		finddir = NULL;
	}
}

char* Sys_GetHomeDir(void)
{
	static char homedir[MAX_OSPATH];
	char* home = getenv("HOME");

	if (!home)
		return NULL;

	snprintf(homedir, sizeof(homedir), "%s/.heretic2", home);

	if (!Sys_IsDir(homedir))
		Sys_Mkdir(homedir);

	return homedir;
}

qboolean Sys_GetOSUserDir(char* buffer, size_t len)
{
	char* home = getenv("HOME");
	if (!home)
		return false;

	snprintf(buffer, len, "%s/.Heretic2R", home);

	if (!Sys_IsDir(buffer))
		Sys_Mkdir(buffer);

	return true;
}
