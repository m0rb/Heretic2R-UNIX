# ref_gl3 — Architecture Contract (binding for all port work)

ref_gl3 is Heretic2R's OpenGL 3.2 core renderer: **H2 rendering semantics from
`src/ref_gl1`, re-expressed on the yquake2 8.60 gl3 backend architecture**
(`~/build/quake2-8.60/src/client/refresh/gl3/`). It builds as a self-contained
dlopen'd module `ref_gl3.so` exporting only `GetRefAPI`.

## Golden rules
1. **gl1 is the semantic reference.** For any H2 behavior (flexmodels, fog,
   trans33/66, books, particles, sprites, image formats, surface flags), the
   gl1 source in `src/ref_gl1/src/` is authoritative. yq2 gl3 is the backend
   reference (shaders, UBOs, VBO streaming, state management).
2. **Keep gl1's function names** when porting a gl1 function (`R_*`, `RI_*`,
   `Draw_*`, `Mod_*`, `LM_*`, `BF_*`). The .so is symbol-isolated; no prefixes
   needed. New backend-only helpers use the `GL3_` prefix.
3. File layout mirrors gl1: `gl3_Main.c`, `gl3_Image.c`, `gl3_Draw.c`,
   `gl3_DrawBook.c`, `gl3_DrawCinematic.c`, `gl3_Model.c`, `gl3_Surface.c`,
   `gl3_Light.c`, `gl3_Lightmap.c`, `gl3_Sprite.c`, `gl3_Sky.c`, `gl3_Warp.c`,
   `gl3_FlexModel.c`, `gl3_Misc.c`, `gl3_SDL.c`, `gl3_Shaders.c`, `gl3_Local.h`.
4. **Reuse renderer-agnostic gl1 sources directly** (no copies): `Hunk.c`,
   `anormtab.c`, `Skeletons/r_Skeletons.c`, `Skeletons/r_SkeletonLerp.c`,
   `gl1_Matrix4.c`, `gl1_FindSurface.c` are compiled into ref_gl3 from
   `src/ref_gl1/src/` via CMake. Do not duplicate them.
5. Every .c starts with `#include "compat.h"` (unix shims), then its own
   header. GLAD header (`<glad/glad.h>`, include dir `include/glad-GL3.2/include`)
   must be included before SDL headers.
6. The refexport/refimport API is H2R's `src/quake2/src/client/ref.h`
   (REF_API_VERSION 4) — NOT yquake2's. `GetRefAPI` must fill exactly the same
   members gl1 fills (see gl1_Main.c). `re.title = "OpenGL 3.2"`.
7. `viddef` is an engine global (`extern viddef_t viddef;`) resolved at dlopen.
   Same for the shared H2Common math and `turbsin`/`bytedirs` tables gl1 uses.

## Backend design (adopted from yq2 gl3)
- **Context**: SDL3 only. `RI_PrepareForWindow` sets GL 3.2 core,
  depth 24, stencil 8, doublebuffer, MSAA from `r_msaa_samples`; returns
  `SDL_WINDOW_OPENGL`. `RI_InitContext(void* win)` creates the context, loads
  GLAD (`gladLoadGLLoader(SDL_GL_GetProcAddress)`), requires
  `GLAD_GL_VERSION_3_2`, sets vsync. Engine creates the window (vid_sdl3.c).
- **gl3state**: current program/VAO/VBO/texture-unit caches, UBO handles,
  the shader program set, streaming VBO/VAO trio (3D world verts / alias
  9-float verts / particle quads share the alias layout).
- **UBOs** (std140, binding points fixed): `uniCommon` (gamma, brightness,
  contrast, intensity, time), `uni2D` (transMat4), `uni3D` (transProj,
  transView, transModel, scroll, alpha, alphaTestRef, fog block, flags),
  `uniLights` (dynamic lights, lightstyle scales).
- **Draw batching**: `GL3_BufferAndDraw3D(verts, n, mode)` — orphaning
  streaming VBO. 2D uses a small streaming quad VBO with `si2D` programs.
- **Shaders** (`gl3_Shaders.c`, `#version 150`): port yq2's set, then H2 mods:
  - 3D world (lightmap ×4 units, lmScales), flowing variant, translucent
    (trans33/66 via uni3D alpha), turb/warp (analytic sin), sky, sprite,
    alias-style flexmodel (vertex color lit), particle-quad (atlas textured),
    2D textured, 2D color fill.
  - **Gamma/brightness/contrast in fragment shaders** via uniCommon —
    H2's `R_InitGammaTable` formula (gamma pow, brightness offset, contrast
    pow around 0.5) applied at the end of every fragment shader (2D and 3D).
    NO texture-baked gamma; `vid_textures_refresh_required` becomes a no-op.
  - **Fog**: uni3D fog params: mode (0 linear/1 exp/2 exp2), density,
    start/end, color. Fragment implements all three (gl1 `R_Fog`/`R_WaterFog`
    semantics incl. underwater variants, `r_fog_lightmap_adjust`, and fog
    suppression for additive surfaces as in gl1).
  - **Alpha test**: `discard` when `alpha < alphaTestRef`; thresholds mirror
    gl1 (0.666 world default, 0.05 UI/sprites, 0.0 additive).
  - **Sphere-map reflection** (FlexModel FMNI_USE_REFLECT / RF_REFLECTION):
    GLSL equivalent of GL_SPHERE_MAP texgen using eye-space normal.
- **Particles**: H2 particles are textured atlas quads (NOT yq2 point
  sprites). Port gl1_Main.c's `particle_st_coords` table; batch camera-facing
  quads through the alias vertex path, classic + additive (`aparticles`) passes.
- **Lightmaps**: yq2 gl3 model — 4 lightmap texture units, atlas blocks,
  dynamic lights via uniLights; but preserve gl1/H2 lightmap build logic
  (gl1_Lightmap.c LM_* + H2 lightstyles) and gl_minlight.
- **FlexModel**: CPU pipeline identical to gl1 (FrameLerp, skeleton lerp via
  shared Skeletons sources, fmnodeinfo flags, RF_TRANS_*, GL_GHOST etc.);
  output = interleaved pos/color/st stream drawn with the flexmodel program.
  Node on/off, per-node color/skin/reflection handled CPU-side per mesh node.
- **Cinematics/books/2D**: gl1 logic with glTexSubImage2D streaming texture
  (RGBA conversion CPU-side); books draw through the 2D pipeline.
- **Gamma-affected image init is REMOVED** — images upload raw; palette
  handling (.m8/.m32/pcx/tga/fnt) ported from gl1_Image.c unchanged otherwise.

## Cvars
Register the same cvar set gl1 registers (R_Register in gl1_Main.c) so menus
and configs keep working. GL1-only toggles that have no GL3 meaning
(`gl_ztrick`, `gl_drawbuffer`, `gl_nobind`) are registered but ignored
(comment `// unused in gl3`).

## Definition of done per module
Compiles under `-Wall` with the target's flags; no yq2 refimport calls
(only H2R's `ri.*` members); no fixed-function GL (must compile against
GL 3.2 core glad); semantics traceable line-by-line to the gl1 counterpart.
