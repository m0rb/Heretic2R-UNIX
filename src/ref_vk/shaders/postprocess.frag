#version 450
#extension GL_ARB_separate_shader_objects : enable

// H2: blits the (optionally underwater-warped) world view into the swapchain at
// the start of RP_UI. The H2 color grade now runs per-fragment in the RP_WORLD
// shaders (gl1 baked-texture gamma / gl3 in-shader parity), so additive effects
// grade before compositing and saturate correctly - this pass only copies.

layout(push_constant) uniform PushConstant
{
	// vertex shader owns the first 17 floats. 68..76 (grade trio) unused here
	// now, kept so the fragment PC layout matches the shared push range.
	layout(offset = 80) float scrWidth;
	layout(offset = 84) float scrHeight;
	layout(offset = 88) float offsetX;
	layout(offset = 92) float offsetY;
} pc;

layout(set = 0, binding = 0) uniform sampler2D sTexture; // unnormalized sampler (S_NEAREST_UNNORMALIZED)

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragmentColor;

void main()
{
	vec2 unnormTexCoord = texCoord * vec2(pc.scrWidth, pc.scrHeight) + vec2(pc.offsetX, pc.offsetY);
	fragmentColor = vec4(textureLod(sTexture, unnormTexCoord, 0.0).rgb, 1.0);
}
