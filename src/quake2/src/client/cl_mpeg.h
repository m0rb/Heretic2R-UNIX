//
// cl_mpeg.h -- MPEG-1 cinematic backend (PL_MPEG), for the Loki .mpg assets.
//
// Heretic2R UNIX port by morb
//

#pragma once

#include "../../../qcommon/qcommon.h"

// Backend ops driven by the SCR_*Cinematic dispatch in cl_smk.c. The path
// passed to MPEG_Open is the already case-resolved on-disk filename. --morb
qboolean MPEG_Open(const char* fullpath);
void MPEG_Shutdown(void);
void MPEG_Run(void);  // Advance playback by real elapsed time; ends itself.
void MPEG_Draw(void); // Blit the most recently decoded frame.
