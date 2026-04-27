#include "compat.h"
//
// SurfaceProps.c
//
// Copyright 1998 Raven Software
//

#include "SurfaceProps.h"
#include "p_types.h"

//TODO: mxd. Seems to be used in Player.dll only. Move there?
H2COMMON_API char* GetClientGroundSurfaceMaterialName(const void* info)
{
#define NUM_MATERIALS (sizeof(material_names) / sizeof(material_names[0]))
	static char* material_names[] = { "gravel", "metal", "stone", "wood" };

	const playerinfo_t* pi = (const playerinfo_t*)info;

	if (pi->GroundSurface != NULL)
	{
		const int mat_index = (pi->GroundSurface->flags >> 24);
		return material_names[mat_index % NUM_MATERIALS];
	}

	return NULL;
}
