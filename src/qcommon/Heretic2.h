//
// Heretic2.h
//
// Copyright 1998 Raven Software
//

#pragma once

// Cross-platform DLL export/import macros
#ifdef _WIN32
	#ifdef QUAKE2_DLL
		#define Q2DLL_DECLSPEC __declspec(dllexport)
	#else 
		#define Q2DLL_DECLSPEC __declspec(dllimport)
	#endif
#else
	// Unix/Linux/macOS use GCC/Clang visibility attributes
	#ifdef QUAKE2_DLL
		#define Q2DLL_DECLSPEC __attribute__((visibility("default")))
	#else
		#define Q2DLL_DECLSPEC
	#endif
#endif

// Cross-platform types
#ifndef _WIN32
	#include <stdint.h>
	typedef int BOOL;
	typedef unsigned long DWORD;
	typedef void* HINSTANCE;
	typedef void* HMODULE;
	typedef void* HWND;
	typedef void* LPSTR;
	
	#ifndef TRUE
		#define TRUE 1
	#endif
	#ifndef FALSE
		#define FALSE 0
	#endif
#endif
