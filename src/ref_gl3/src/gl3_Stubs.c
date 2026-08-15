#include "compat.h"
//
// gl3_Stubs.c -- shared module globals without a clear single owner, plus any
// still-unported symbols. Kept minimal now that all module ports have landed.
//
// Copyright 1998 Raven Software
//

#include "gl3_Local.h"

// Permanent image pointers: created by Draw_InitLocal (gl3_Draw.c), freed by
// R_ShutdownImages (gl3_Image.c), read by draw/particle/world code.
extern image_t* draw_chars; // Owned by gl3_Draw.c.
image_t* r_notexture;
image_t* r_particletexture;
image_t* r_aparticletexture;
image_t* r_reflecttexture;
image_t* r_font1;
image_t* r_font2;
