#version 450

// H2: flat color quads (Draw_Fill, Draw_FadeScreen, screen flash) - graded
// in-shader with the H2ColorGrade trio.

layout(push_constant) uniform PushConstant
{
	// vertex shader owns the first 17 floats.
	layout(offset = 68) float gamma;
	layout(offset = 72) float brightness;
	layout(offset = 76) float contrast;
} pc;

layout(location = 0) in vec4 color;

layout(location = 0) out vec4 fragmentColor;

// gl1_Image.c R_InitGammaTable() math (see gl3_Shaders.c / basic.frag).
vec3 H2ColorGrade(vec3 c)
{
	float ce = 1.0 - pc.contrast;
	ce = (ce > 0.5) ? pow(ce + 0.5, 3.0) : pow(ce + 0.5, 0.5);

	vec3 inf = 255.0 * pow((c * 255.0 + 0.5) * (1.0 / 255.5), vec3(pc.gamma)) + 0.5;
	vec3 dev = (inf - 128.0) * (1.0 / 128.0);
	vec3 graded = vec3((pc.brightness * 160.0 - 80.0) * (1.0 / 255.0))
	            + (pow(abs(dev), vec3(ce)) * sign(dev) + 1.0) * (128.0 / 255.0);

	graded = mix(graded, vec3(0.0), step(c, vec3(0.0)));
	return clamp(graded, 0.0, 1.0);
}

void main()
{
	fragmentColor = vec4(H2ColorGrade(color.rgb), color.a);
}
