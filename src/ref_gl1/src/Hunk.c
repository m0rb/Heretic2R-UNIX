#include "compat.h"
//
// Hunk.c
//
// Copyright 1998 Raven Software
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
	// Reserve a huge chunk of memory, but don't commit any yet.
	// YQ2: plus 32 bytes for cacheline
	hunkmaxsize = maxsize + sizeof(uint) + 32;
	// cursize 0 returns membase on most platforms. --morb
	//cursize = 0; 
	cursize = sizeof(uint);

#ifdef _WIN32
	//membase = VirtualAlloc(NULL, maxsize, MEM_RESERVE, PAGE_NOACCESS);
	membase = VirtualAlloc(NULL, hunkmaxsize, MEM_RESERVE, PAGE_NOACCESS);
	if (membase == NULL)
		ri.Sys_Error(ERR_DROP, "VirtualAlloc reserve failed"); //mxd. Sys_Error() -> ri.Sys_Error().
#else
	membase = mmap(NULL, hunkmaxsize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (membase == MAP_FAILED)
		ri.Sys_Error(ERR_DROP, "mmap reserve failed");
#endif

	*(uint*)membase = hunkmaxsize;

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
	// memory is already mmaped
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
	// Subtract header size --morb
	//return (int)cursize;
	return (int)(cursize - sizeof(uint)); 
}

// Q2 counterpart
void Hunk_Free(void* buf)
{
	if (buf != NULL)
	{
		// size adjustment. --morb
		//if (buf != NULL) VirtualFree(buf, 0, MEM_RELEASE); 
		byte* base = (byte*)buf - sizeof(uint);
		const uint size = *(uint*)base;
#ifdef _WIN32
		VirtualFree(base, 0, MEM_RELEASE);
#else
		munmap(base, size);
#endif
	}

	hunkcount--;
}