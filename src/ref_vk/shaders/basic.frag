#version 450

// H2: textured-quad fragment shader - UI quads plus the RP_WORLD polygon/warp/
// sky/particle pipelines. Every caller pushes the real grade (per-fragment
// grading, gl3 parity).
//  - H2ColorGrade(): gamma/brightness/contrast, gl1 R_InitGammaTable() math
//    (see gl3_Shaders.c).
//  - alphaTestRef push constant: gl1 glAlphaFunc(GL_GREATER, x) equivalence
//    (0.666 world, 0.05 UI/sprites, 0.0 additive; < 0.0 disables the test).

layout(push_constant) uniform PushConstant
{
	// vertex shader owns the first 17 floats (mvpMatrix + sprite alpha).
	layout(offset = 68) float gamma;        // vid_gamma: H2 uses the value directly as pow() exponent (default 0.5!)
	layout(offset = 72) float brightness;   // vid_brightness (0..1, 0.5 = neutral)
	layout(offset = 76) float contrast;     // vid_contrast (0..1, 0.5 = neutral)
	layout(offset = 80) float alphaTestRef; // discard when a <= ref; < 0.0 disables
} pc;

layout(set = 0, binding = 0) uniform sampler2D sTexture;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 fragmentColor;

// H2ColorGrade() is a faithful GLSL translation of gl1_Image.c R_InitGammaTable()
// (byte-table math mapped to normalized floats - see gl3_Shaders.c for the derivation).
vec3 H2ColorGrade(vec3 c)
{
	float ce = 1.0 - pc.contrast;
	ce = (ce > 0.5) ? pow(ce + 0.5, 3.0) : pow(ce + 0.5, 0.5);

	vec3 inf = 255.0 * pow((c * 255.0 + 0.5) * (1.0 / 255.5), vec3(pc.gamma)) + 0.5;
	vec3 dev = (inf - 128.0) * (1.0 / 128.0);
	vec3 graded = vec3((pc.brightness * 160.0 - 80.0) * (1.0 / 255.0))
	            + (pow(abs(dev), vec3(ce)) * sign(dev) + 1.0) * (128.0 / 255.0);

	graded = mix(graded, vec3(0.0), step(c, vec3(0.0))); // gammatable[0] = 0 (pure black stays black).
	return clamp(graded, 0.0, 1.0);
}

void main()
{
	fragmentColor = texture(sTexture, texCoord) * color;

	if (fragmentColor.a <= pc.alphaTestRef)
		discard;

	fragmentColor = vec4(H2ColorGrade(fragmentColor.rgb), fragmentColor.a);
}
