# ref_vk — Architecture Contract (binding for all port work)

ref_vk is Heretic2R's Vulkan renderer: **H2 rendering semantics from
`src/ref_gl1` (and the already-validated H2 ports in `src/ref_gl3`), on the
yquake2remaster vk backend** (`~/build/yquake2remaster/src/client/refresh/vk/`,
vkQuake2-derived). Builds as a self-contained dlopen'd module `ref_vk.so`
exporting only `GetRefAPI`.

## Golden rules
1. **gl1 is the semantic authority; gl3 is the H2-decisions reference.**
   `src/ref_gl3/src/` already contains validated H2 ports of every subsystem
   (flexmodels, books, cinematics, particles-as-quads, fog rules, image
   loading). CPU-side logic should match gl3 nearly verbatim; only draw
   submission/state differs (Vulkan pipelines instead of GL).
2. **Keep gl1/gl3 function names** for ported functions (`R_*`, `RI_*`,
   `Draw_*`, `Mod_*`, `LM_*`, `BF_*`). New backend helpers use `QVk_`/`Vkimp_`
   prefixes (yq2remaster convention).
3. File layout: `vk_Local.h`, `vk_Main.c`, `vk_Image.c`, `vk_Model.c`,
   `vk_Draw.c`, `vk_DrawBook.c`, `vk_DrawCinematic.c`, `vk_Surface.c`,
   `vk_Light.c`, `vk_Lightmap.c`, `vk_Sprite.c`, `vk_Sky.c`, `vk_Warp.c`,
   `vk_FlexModel.c`, `vk_Misc.c`, `vk_SDL.c`, plus the backend core ported
   from yq2remaster: `vk_common.c`, `vk_device.c`, `vk_swapchain.c`,
   `vk_cmd.c`, `vk_buffer.c`, `vk_pipeline.c`, `vk_shaders.c`,
   `vk_validation.c`, `vk_util.c` (keep their internal structure; strip
   yq2-refimport calls in favor of H2R's `ri.*`).
4. **Reuse renderer-agnostic gl1 sources directly** via CMake (same set as
   ref_gl3): `Hunk.c`, `anormtab.c`, `Skeletons/r_Skeletons.c`,
   `Skeletons/r_SkeletonLerp.c`, `gl1_Matrix4.c`. Do not copy them.
5. Every .c starts with `#include "compat.h"`. Vulkan comes via vendored
   **volk** (`src/ref_vk/volk/`) — never link libvulkan directly; volk loads
   it. SDL3 only (`SDL_Vulkan_GetInstanceExtensions`/`SDL_Vulkan_CreateSurface`).
6. API is H2R's `src/quake2/src/client/ref.h` (REF_API_VERSION 4).
   `re.title = "Vulkan"`. GetRefAPI fills exactly what gl1/gl3 fill.
   **There is no EndWorldRenderpass in H2R's API** — `RI_RenderFrame` ends the
   world render pass internally after the 3D flow (post R_ScreenFlash);
   all 2D lands in the UI pass. `RI_BeginFrame`/`RI_EndFrame` bracket the
   frame (acquire → submit/present), mirroring yq2remaster's Begin/EndFrame.
7. `viddef` is an engine global (`extern viddef_t viddef;`). Same dlopen
   resolution model as gl1/gl3.

## Backend design (adopted from yq2remaster vk)
- **Window**: `RI_PrepareForWindow` returns `SDL_WINDOW_VULKAN` (no GL
  attributes). `RI_InitContext(void* win)` = volk init, instance (with SDL
  instance extensions), surface, device pick (`vk_device` cvar, -1 auto),
  swapchain, render passes, per-frame resources. Handle
  `VK_ERROR_OUT_OF_DATE_KHR`/suboptimal by swapchain recreation.
- **Render passes**: RP_WORLD (offscreen color+depth+msaa resolve),
  RP_WORLD_WARP (fullscreen underwater distortion pass), RP_UI (swapchain).
  MSAA from `r_msaa_samples` maps to RP_WORLD sample count (clamped to
  device limits, fallback chain like yq2remaster).
- **Pipelines**: port yq2remaster's pipeline inventory, adapted to H2's needs:
  2D textured / 2D tinted / 2D color quad; world lightmapped (H2: 4 lightstyle
  sub-lightmaps like gl3 — extend the lmap descriptor set + shader to 4
  samplers with lmScales, matching gl3's si3Dlm semantics); world unlit /
  trans33/66; warp (liquid); sky; flexmodel (per-vertex color, blend/alpha
  variants mirroring R_HandleTransparency's state matrix: standard/additive/
  ghost, depth-hack variants); sprite; particle quads (H2 atlas, classic +
  additive blend variants); nullmodel; screen flash (color quad).
  Alpha test = shader discard with push-constant ref (0.666/0.05/0.0/off),
  exactly gl3's alphaTestRef semantics.
- **Shaders**: GLSL 4.50 sources in `src/ref_vk/shaders/` (seeded from
  yq2remaster; modify for H2), compiled by `src/ref_vk/shaders/compile.sh`
  (glslangValidator → `src/ref_vk/src/spirv/*.c` C arrays, committed to the
  repo so builds don't need glslang). H2 modifications:
  - **Color grading** (gamma/brightness/contrast, gl3's H2ColorGrade formula):
    graded per-fragment in every RP_WORLD and UI shader (gl3 parity - additive
    effects must grade before compositing, so `grade(a)+grade(b)` saturates to
    white). `postprocess.frag` is a plain blit. Push constants carry the values.
  - **Fog**: 3-mode (linear/exp/exp2) + underwater variants + lightmap adjust
    + additive suppression — same rules as gl3's uni3D fog block, via UBO.
  - **4-sampler lightmap** frag with lmScales (H2 lightstyles).
  - **world_warp**: keyed on H2's `cl_camera_under_surface` (this implements
    the underwater screen distortion — a known gap in the port; make the
    strength/speed cvar-tunable: `r_underwater_warp` default 1).
- **Buffers**: yq2remaster's streaming vertex/index/uniform staging system
  (triple-buffered per swapchain image) ported as-is.
- **Images**: H2 `.m8/.m32` loading identical to gl3_Image.c (own mip chains
  → upload all mip levels; no runtime mip gen needed), samplers per
  gl_texturemode + anisotropy, it_pic = nearest. Cinematic streaming texture.
- **FlexModel/particles/sprites/books**: CPU logic from gl3 counterparts,
  vertex streams into the vk streaming buffers.

## Cvars
Same registration set as gl3 (gl1 mirror + `r_msaa_samples`, `r_anisotropic`),
plus vk-specific: `vk_device` (-1 auto), `vk_validation` (0), `r_underwater_warp` (1).
GL-only toggles registered-but-ignored for config compatibility.

## Definition of done per module
Compiles under the repo flags; no yq2 refimport usage; semantics traceable to
gl1/gl3; validation-layer clean at `vk_validation 1` for the paths exercised.
