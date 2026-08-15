#version 450
#extension GL_ARB_separate_shader_objects : enable

// H2: lightmapped world faces. The per-draw UBO carries lmScales for the 4
// lightstyle sub-lightmaps (gl3 si3Dlm parity) and the fog block (gl1
// R_Fog()/R_WaterFog() semantics); the fog distance (eye-plane distance ==
// gl_Position.w for perspective projections) is passed to the fragment stage.
// C-side layout: vklmapubo_t (vk_Local.h).

layout(location = 0) in vec3 inVertex;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec2 inTexCoordLmap;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uint inFlags;

layout(push_constant) uniform PushConstant
{
    mat4 vpMatrix;
} pc;

layout(set = 1, binding = 0) uniform UniformBufferObject
{
    mat4 model;
    vec4 lmScales[4];         // H2 lightstyle scales for the 4 lightmap samplers.
    vec4 fogColor;
    int fogMode;              // -1 = off, 0 = linear, 1 = exp, 2 = exp2
    float fogDensity;
    float fogStart;
    float fogEnd;
    float fogLightmapAdjust;  // r_fog_lightmap_adjust (scales fog for the lightmap term)
    int fogSkipAdditive;      // 1 = no fog on current (additive) draw
    float viewLightmaps;      // 1.0 = gl_lightmap (draw lightmaps only)
} ubo;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 texCoordLmap;
layout(location = 2) out float fogDist;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = pc.vpMatrix * ubo.model * vec4(inVertex, 1.0);
    texCoord = inTexCoord;
    texCoordLmap = inTexCoordLmap;
    fogDist = gl_Position.w; // Eye-plane distance (perspective projection).
}
