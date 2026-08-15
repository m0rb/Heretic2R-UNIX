#version 450
#extension GL_ARB_separate_shader_objects : enable

// H2: flexmodels (per-vertex color lit). The per-draw UBO carries the fog
// block; the fog distance (eye-plane distance == gl_Position.w for perspective
// projections) is passed to the fragment stage.
// C-side layout: vkmodelubo_t (vk_Local.h).

layout(location = 0) in vec3 inVertex;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstant
{
    mat4 vpMatrix;
} pc;

layout(set = 1, binding = 0) uniform UniformBufferObject
{
    mat4 model;
    vec4 fogColor;
    int fogMode;              // -1 = off, 0 = linear, 1 = exp, 2 = exp2
    float fogDensity;
    float fogStart;
    float fogEnd;
    float fogLightmapAdjust;  // unused for models, kept for the shared vkfogblock_t layout
    int fogSkipAdditive;      // 1 = no fog on current (additive/ghost) draw
    int textured;
} ubo;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out flat int textured;
layout(location = 3) out float fogDist;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = pc.vpMatrix * ubo.model * vec4(inVertex, 1.0);
    color = inColor;
    texCoord = inTexCoord;
    textured = ubo.textured;
    fogDist = gl_Position.w; // Eye-plane distance (perspective projection).
}
