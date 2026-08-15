#version 450
#extension GL_ARB_separate_shader_objects : enable

// H2: underwater screen distortion (RP_WORLD_WARP fullscreen pass), keyed to
// an intensity uniform: cl_camera_under_surface drives 'time' (0 = pass-through
// blit) and r_underwater_warp drives 'intensity' (distortion strength; 0
// disables). yq2's vk_pixel_size render scaling is dropped (H2 foundation).

layout(push_constant) uniform PushConstant
{
	// vertex shader owns the first 17 floats.
	layout(offset = 68) float time;         // r_newrefdef.time while underwater, 0 otherwise
	layout(offset = 72) float intensity;    // r_underwater_warp (distortion strength, default 1)
	layout(offset = 76) float scrWidth;
	layout(offset = 80) float scrHeight;
	layout(offset = 84) float offsetX;
	layout(offset = 88) float offsetY;
	layout(offset = 92) float refdefX;
	layout(offset = 96) float refdefY;
	layout(offset = 100) float refdefWidth;
	layout(offset = 104) float refdefHeight;
} pc;

layout(set = 0, binding = 0) uniform sampler2D sTexture; // unnormalized sampler (S_NEAREST_UNNORMALIZED)

layout(location = 0) out vec4 fragmentColor;

#define PI 3.14159265358979323846

void main()
{
	vec2 scrSize = vec2(pc.scrWidth, pc.scrHeight);
	vec2 fragCoord = (gl_FragCoord.xy - vec2(pc.offsetX, pc.offsetY));
	vec2 uv = fragCoord / scrSize;

	float xMin = pc.refdefX;
	float xMax = pc.refdefX + pc.refdefWidth;
	float yMin = pc.refdefY;
	float yMax = pc.refdefY + pc.refdefHeight;

	if (pc.time > 0 && pc.intensity > 0 &&
		fragCoord.x > xMin && fragCoord.x < xMax && fragCoord.y > yMin && fragCoord.y < yMax)
	{
		// Distortion amplitude fades towards the view edges; overall strength
		// scales with the r_underwater_warp intensity.
		float sx = 1.0 - abs(pc.scrWidth  / 2.0 - fragCoord.x) * 2.0 / pc.scrWidth;
		float sy = 1.0 - abs(pc.scrHeight / 2.0 - fragCoord.y) * 2.0 / pc.scrHeight;
		float xShift = 2.0 * pc.time + uv.y * PI * 10;
		float yShift = 2.0 * pc.time + uv.x * PI * 10;
		vec2 distortion = vec2(sin(xShift) * sx, sin(yShift) * sy) * 0.00666 * pc.intensity;

		uv += distortion;
	}

	uv = clamp(uv * scrSize, vec2(0.0, 0.0), scrSize - vec2(0.5, 0.5));

	fragmentColor = textureLod(sTexture, uv, 0.0);
}
