#include "compat.h"
//
// Hunk.c
//
// Copyright 1998 Raven Software
//
// morb was here. fixed for Unix port.
// The mmap-based Unix memory path and per-hunk size-at-base design are based on the
// Yamagi Quake 2 hunk allocator (src/backends/unix/shared/hunk.c).
// Yamagi Quake 2 is Copyright (C) 1997-2001 Id Software, Inc. and YQ2 contributors,
// licensed under the GNU General Public License v2 (GPLv2).
// See: https://github.com/yquake2/yquake2
//

#include "Hunk.h"
#include "gl1_Local.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static byte* membase;

static int hunkcount;
static uint hunkmaxsize; //mxd. int -> uint
static uint cursize; //mxd. int -> uint

// Q2 counterpart
void* Hunk_Begin(const int maxsize)
{
	// morb was here. fixed for Unix port.
	// Reserve a huge chunk of memory, but don't commit any yet.
	// The extra sizeof(uint) stores the mapped size at the base for Hunk_Free (YQ2 convention).
	// The extra 32 bytes are for cacheline rounding in Hunk_Alloc.
	hunkmaxsize = maxsize + sizeof(uint) + 32;
	// cursize starts past the stored-size header so Hunk_Alloc doesn't overwrite it.
	//cursize = 0; // original: no size header, cursize started at 0, return was membase.
	cursize = sizeof(uint);

#ifdef _WIN32
	//membase = VirtualAlloc(NULL, maxsize, MEM_RESERVE, PAGE_NOACCESS); // original: Windows-only, used maxsize (not hunkmaxsize).
	membase = VirtualAlloc(NULL, hunkmaxsize, MEM_RESERVE, PAGE_NOACCESS);
	if (membase == NULL)
		ri.Sys_Error(ERR_DROP, "VirtualAlloc reserve failed"); //mxd. Sys_Error() -> ri.Sys_Error().
#else
	// BUGFIX: mxd. Allocate hunkmaxsize bytes (not maxsize) — Hunk_Alloc's overflow guard
	// allows cursize up to hunkmaxsize, so the mmap must cover the full range.
	membase = mmap(NULL, hunkmaxsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (membase == MAP_FAILED)
		ri.Sys_Error(ERR_DROP, "mmap reserve failed");
#endif

	// BUGFIX: mxd. Store the mapped size at the base so Hunk_Free can munmap with the correct
	// size regardless of which hunk was created most recently (hunkmaxsize is a global that gets
	// overwritten by every Hunk_Begin call, so Hunk_Free must not rely on it).
	*(uint*)membase = hunkmaxsize;

	//return membase; // original: no size header at base.
	return membase + sizeof(uint);
}

// Q2 counterpart
void* Hunk_Alloc(int size)
{
	// Round to cacheline.
	size = (size + 31) & ~31;

#ifdef _WIN32
	// Commit pages as needed.
	void* buf = VirtualAlloc(membase, cursize + size, MEM_COMMIT, PAGE_READWRITE);

	if (buf == NULL)
	{
		char* msg = NULL;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msg, 0, NULL);
		ri.Sys_Error(ERR_DROP, "VirtualAlloc commit failed.\n%s", msg); //mxd. Sys_Error() -> ri.Sys_Error().
	}
#else
	// On Unix, memory is already committed via mmap
#endif

	cursize += size;

	if (cursize > hunkmaxsize)
		ri.Sys_Error(ERR_DROP, "Hunk_Alloc overflow"); //mxd. Sys_Error() -> ri.Sys_Error().

	return membase + cursize - size;
}

// Q2 counterpart
int Hunk_End(void)
{
	hunkcount++;
	// morb was here. fixed for Unix port.
	//return (int)cursize; // original: included header size in returned extradatasize.
	return (int)(cursize - sizeof(uint)); // Subtract header size; callers track data size only.
}

// Q2 counterpart
void Hunk_Free(void* buf)
{
	if (buf != NULL)
	{
		// morb was here. fixed for Unix port.
		// buf points to membase + sizeof(uint); the actual mapping starts sizeof(uint) bytes earlier.
		//if (buf != NULL) VirtualFree(buf, 0, MEM_RELEASE); // original: Windows-only, no size-header adjustment.
		byte* base = (byte*)buf - sizeof(uint);
		const uint size = *(uint*)base; // Stored by Hunk_Begin.
#ifdef _WIN32
		VirtualFree(base, 0, MEM_RELEASE);
#else
		// BUGFIX: mxd. Use the per-hunk size stored at the base rather than the global hunkmaxsize,
		// which always reflects the most recently created hunk and would munmap the wrong amount.
		munmap(base, size);
#endif
	}

	hunkcount--;
}