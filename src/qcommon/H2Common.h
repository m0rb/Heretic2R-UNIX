//
// H2Common.h
//
// Copyright 1998 Raven Software
//

#pragma once

#ifdef _WIN32
	#ifdef H2COMMON
		#define H2COMMON_API __declspec(dllexport)
	#else
		#define H2COMMON_API __declspec(dllimport)
	#endif
#else
	#ifdef H2COMMON
		#define H2COMMON_API __attribute__((visibility("default")))
	#else
		#define H2COMMON_API
	#endif
#endif
