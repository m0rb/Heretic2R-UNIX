#include "compat.h"
//
// vk_Stubs.c -- no-op bodies for every renderer export not yet ported, plus
// shared module globals without a clear single owner. The module ports
// (vk_Image.c, vk_Draw.c, vk_Model.c, vk_Surface.c, ...) will replace these
// one by one - mirror of the original gl3 foundation's gl3_Stubs.c approach.
//
// Copyright 1998 Raven Software
//

#include "vk_Local.h"

#pragma region ========================== SHARED MODULE GLOBALS ==========================

// Texture list: filled by R_InitImages()/Vk_LoadPic() (vk_Image.c module port).
image_t vktextures[MAX_VKTEXTURES];
int numvktextures;

// Permanent image pointers: created by Draw_InitLocal (vk_Draw.c), freed by
// R_ShutdownImages (vk_Image.c), read by draw/particle/world code.
image_t* r_notexture;
image_t* r_particletexture;
image_t* r_aparticletexture;
image_t* r_reflecttexture;
image_t* r_font1;
image_t* r_font2;

#pragma endregion

#ifdef _DEBUG
#pragma region ========================== vk_Debug.c (stubbed) ==========================

void RI_AddDebugBox(const vec3_t center, float size, paletteRGBA_t color, float lifetime)
{
	(void)center; (void)size; (void)color; (void)lifetime;
}

void RI_AddDebugBbox(const vec3_t mins, const vec3_t maxs, paletteRGBA_t color, float lifetime)
{
	(void)mins; (void)maxs; (void)color; (void)lifetime;
}

void RI_AddDebugEntityBbox(const edict_t* ent, paletteRGBA_t color)
{
	(void)ent; (void)color;
}

void RI_AddDebugLabel(const vec3_t origin, paletteRGBA_t color, float lifetime, const char* label)
{
	(void)origin; (void)color; (void)lifetime; (void)label;
}

void RI_AddDebugEntityLabel(const edict_t* ent, paletteRGBA_t color, const char* label)
{
	(void)ent; (void)color; (void)label;
}

void RI_AddDebugLine(const vec3_t start, const vec3_t end, paletteRGBA_t color, float lifetime)
{
	(void)start; (void)end; (void)color; (void)lifetime;
}

void RI_AddDebugArrow(const vec3_t start, const vec3_t end, paletteRGBA_t color, float lifetime)
{
	(void)start; (void)end; (void)color; (void)lifetime;
}

void RI_AddDebugDirection(const vec3_t start, const vec3_t direction, float size, paletteRGBA_t color, float lifetime)
{
	(void)start; (void)direction; (void)size; (void)color; (void)lifetime;
}

void RI_AddDebugAngles(const vec3_t start, const vec3_t angles, float size, paletteRGBA_t color, float lifetime)
{
	(void)start; (void)angles; (void)size; (void)color; (void)lifetime;
}

void RI_AddDebugAnglesRad(const vec3_t start, const vec3_t angles, float size, paletteRGBA_t color, float lifetime)
{
	(void)start; (void)angles; (void)size; (void)color; (void)lifetime;
}

void RI_AddDebugMarker(const vec3_t center, float size, paletteRGBA_t color, float lifetime)
{
	(void)center; (void)size; (void)color; (void)lifetime;
}

void R_FreeDebugPrimitives(void)
{
}

#pragma endregion
#endif // _DEBUG
