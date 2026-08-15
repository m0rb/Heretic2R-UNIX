#!/bin/sh
# compile.sh -- compiles the ref_vk GLSL sources into SPIR-V C arrays
# (src/spirv/*.c, committed to the repo so builds don't need glslang).
#
# Naming matches yq2remaster's stuff/shaders/shaders.sh:
#   glslangValidator --variable-name <name>_spv -V <src> -o ../src/spirv/<name>.c
#
# Run from src/ref_vk/shaders/. Requires glslangValidator in PATH.

set -e

OUT=../src/spirv

compile()
{
	# $1 = source file, e.g. basic.vert -> basic_vert.c / basic_vert_spv
	name=$(echo "$1" | tr '.' '_')
	glslangValidator --variable-name "${name}_spv" -V "$1" -o "$OUT/${name}.c"
}

compile basic.vert
compile basic.frag
compile basic_tinted.vert
compile basic_color_quad.vert
compile basic_color_quad.frag
compile model.vert
compile model.frag
compile nullmodel.vert
compile particle.vert
compile point_particle.vert
compile point_particle.frag
compile sprite.vert
compile beam.vert
compile skybox.vert
compile d_light.vert
compile polygon.vert
compile polygon_lmap.vert
compile polygon_lmap.frag
compile polygon_warp.vert
compile shadows.vert
compile postprocess.vert
compile postprocess.frag
compile world_warp.vert
compile world_warp.frag

echo "All shaders compiled to $OUT."
