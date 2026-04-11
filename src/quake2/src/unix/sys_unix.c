//
// sys_unix.c -- Unix system functions
//
// Copyright 1998 Raven Software
// Copyright (C) 2010-2024 Yamagi Quake 2 Contributors (GPLv2)
// Unix port by morb
//

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

// For dedicated server console input
#include <sys/select.h>

static qboolean stdin_active = true;

#define MAX_NUM_ARGVS 128
static int argc;
static char* argv[MAX_NUM_ARGVS];

static char console_text[256];
static int console_textlen;

// Initialize resource manager for singly linked list nodes (Windows does this in DllMain)
extern ResourceManager_t sllist_nodes_mgr;

#pragma region ========================== SYSTEM IO ==========================

H2R_NORETURN void Sys_Error(const char* error, ...)
{
	va_list argptr;
	char text[1024];

	va_start(argptr, error);
	vsnprintf(text, sizeof(text), error, argptr);
	va_end(argptr);

	// BUGFIX: mxd. CL_Shutdown() calls Z_FreeTags(0), which frees all cvars and dereferences all pointers to them...
	const qboolean is_dedicated = (dedicated != NULL && (int)dedicated->value);

	CL_Shutdown();

	fprintf(stderr, "Error: %s\n", text);

	exit(1);
}

H2R_NORETURN void Sys_Quit(void)
{
	// BUGFIX: mxd. CL_Shutdown() calls Z_FreeTags(0), which frees all cvars and dereferences all pointers to them...
	const qboolean is_dedicated = (dedicated != NULL && (int)dedicated->value);

	CL_Shutdown();

	exit(0);
}

#pragma endregion

#pragma region ========================== DLL HANDLING ==========================

qboolean Sys_LoadGameDll(const char* dll_name, HINSTANCE* hinst, DWORD* checksum)
{
	char name[MAX_OSPATH];
	char* path = NULL;
	struct stat st;

	*hinst = NULL;
	*checksum = 0;

	// Run through the search paths
	while (true)
	{
		path = FS_NextPath(path);
		if (path == NULL)
			break; // Couldn't find one anywhere

		Com_sprintf(name, sizeof(name), "%s/%s.so", path, dll_name);
		Com_DDPrintf(2, "Trying to load %s\n", name);
		*hinst = dlopen(name, RTLD_NOW);
		if (*hinst != NULL)
		{
			// Get file checksum using stat
			if (stat(name, &st) == 0)
			{
				*checksum = (DWORD)st.st_size;
			}
			else
			{
				*checksum = 0;
			}
			Com_DDPrintf(2, "dlopen (%s)\n", name);
			return true;
		}
		// Print the actual error for .so file failure
		const char* dlerror_msg = dlerror();
		Com_Printf("Failed to load %s: %s\n", name, dlerror_msg ? dlerror_msg : "unknown error");

		// Try with .dylib extension (macOS)
		Com_sprintf(name, sizeof(name), "%s/%s.dylib", path, dll_name);
		Com_DDPrintf(2, "Trying to load %s\n", name);
		*hinst = dlopen(name, RTLD_NOW);
		if (*hinst != NULL)
		{
			if (stat(name, &st) == 0)
			{
				*checksum = (DWORD)st.st_size;
			}
			else
			{
				*checksum = 0;
			}
			Com_DDPrintf(2, "dlopen (%s)\n", name);
			return true;
		}
	}

	// Failed to load - return false instead of calling Sys_Error
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

#pragma endregion

void Sys_Init(void)
{
	Set_Com_Printf(Com_Printf); // H2

	// Initialize resource manager for singly linked list nodes
	// On Windows this is done in DllMain, but Unix doesn't have DllMain
	ResMngr_Con(&sllist_nodes_mgr, SLL_NODE_SIZE, SLL_NODE_BLOCK_SIZE);

	if ((int)dedicated->value)
	{
		// For dedicated server, we'll use stdin for input
		stdin_active = true;
	}
}

// Q2 counterpart
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

	// Remove newline
	if (console_text[len - 1] == '\n')
		console_text[len - 1] = 0;

	return console_text;
}

// Q2 counterpart
void Sys_ConsoleOutput(const char* string)
{
	fputs(string, stdout);
	fflush(stdout);
}

// Q2 counterpart
static void ParseCommandLine(int argc_in, char** argv_in)
{
	argc = argc_in;
	for (int i = 0; i < argc && i < MAX_NUM_ARGVS; i++)
	{
		argv[i] = argv_in[i];
	}
}

// Unix entry point is in main.c

// Platform-specific timing functions
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

// File system functions
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

// Directory searching
static DIR* finddir = NULL;
static char findpath[MAX_OSPATH];
static char findpattern[MAX_OSPATH];
static int finddir_len; // Length of the directory-only portion of findpath.
static unsigned findmusthave;
static unsigned findcanthave;

// Check SFF_* attribute flags against a dirent.
// On Windows, "*.""  is the glob for "no extension" and matches bare directory names.
// On Unix we honor it by checking d_type/stat directly.
static qboolean FindMatchAttributes(const struct dirent* d)
{
	// Skip . and .. always.
	if (d->d_name[0] == '.' && (d->d_name[1] == '\0' || (d->d_name[1] == '.' && d->d_name[2] == '\0')))
		return false;

	// Determine if entry is a directory via stat (portable; d_type is not reliable on all FS).
	qboolean is_dir = false;
	{
		char full[MAX_OSPATH];
		struct stat st;
		snprintf(full, sizeof(full), "%.*s/%s", finddir_len, findpath, d->d_name);
		if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
			is_dir = true;
	}

	// Hidden on Unix: name starts with '.'.
	const qboolean is_hidden = (d->d_name[0] == '.');

	if ((findcanthave & SFF_HIDDEN) && is_hidden)
		return false;
	if ((findmusthave & SFF_SUBDIR) && !is_dir)
		return false;
	if ((findcanthave & SFF_SUBDIR) && is_dir)
		return false;

	return true;
}

// On Windows "*.""  matches files/dirs with no extension, which includes bare
// directory names like "male" or "female". Map that to a plain "*" on Unix.
static void NormalizePattern(const char* raw, char* out, size_t outsz)
{
	strncpy(out, raw, outsz - 1);
	out[outsz - 1] = '\0';
	const size_t len = strlen(out);
	if (len >= 2 && out[len - 1] == '.' && out[len - 2] == '*')
		out[len - 1] = '\0'; // Strip trailing dot: "*." -> "*"
}

char* Sys_FindFirst(const char* path, unsigned musthave, unsigned canthave)
{
	struct dirent* d;
	char* p;

	if (finddir)
		Sys_Error("Sys_FindFirst without close");

	findmusthave = musthave;
	findcanthave = canthave;

	// Extract directory and pattern.
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

		// Use strcasecmp for a plain filename (no wildcards), fnmatch for glob patterns.
		// This makes lookups like "bumper.smk" match "Bumper.smk" on case-sensitive Linux FS.
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
			// Write from finddir_len so the previous match's filename is overwritten, not appended to.
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

// Get home directory
char* Sys_GetHomeDir(void)
{
	static char homedir[MAX_OSPATH];
	char* home = getenv("HOME");

	if (!home)
		return NULL;

	snprintf(homedir, sizeof(homedir), "%s/.heretic2", home);

	// Create directory if it doesn't exist
	if (!Sys_IsDir(homedir))
		Sys_Mkdir(homedir);

	return homedir;
}

// Get OS user directory (for saved games, etc.)
qboolean Sys_GetOSUserDir(char* buffer, size_t len)
{
	char* home = getenv("HOME");
	if (!home)
		return false;

	// morb was here. changed from ~/.heretic2 to ~/.Heretic2R (matches binary name, other q2 engines).
	// was: snprintf(buffer, len, "%s/.heretic2", home);
	snprintf(buffer, len, "%s/.Heretic2R", home);

	// Create directory if it doesn't exist
	if (!Sys_IsDir(buffer))
		Sys_Mkdir(buffer);

	return true;
}

// CPU pause for tight loops - implemented in input_sdl3.c using SDL3
