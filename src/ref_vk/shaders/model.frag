#version 450

// H2: flexmodels - texture modulated by per-vertex color (gl1 glColor* +
// GL_MODULATE), push-constant alpha test (gl1 glAlphaFunc(GL_GREATER, x)) and
// the uni3D-style fog block. Additive passes (GL_GHOST etc.) suppress fog via
// ubo.fogSkipAdditive, mirroring gl1's glDisable(GL_FOG) for those.

layout(push_constant) uniform PushConstant
{
	// vertex shader owns the first 17 floats; 68..76 = H2ColorGrade trio.
	layout(offset = 68) float gamma;
	layout(offset = 72) float brightness;
	layout(offset = 76) float contrast;
	layout(offset = 80) float alphaTestRef; // discard when a <= ref; < 0.0 disables
} pc;

layout(set = 0, binding = 0) uniform sampler2D sTexture;

// Same block as model.vert (one dynamic UBO shared by both stages).
layout(set = 1, binding = 0) uniform UniformBufferObject
{
	mat4 model;
	vec4 fogColor;
	int fogMode;
	float fogDensity;
	float fogStart;
	float fogEnd;
	float fogLightmapAdjust;
	int fogSkipAdditive;
	int textured;
} ubo;

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in flat int textured;
layout(location = 3) in float fogDist;

layout(location = 0) out vec4 fragmentColor;

// gl1_Image.c R_InitGammaTable() math (see basic.frag). Applied per-fragment
// before the additive blend (gl1 baked-texture gamma / gl3 in-shader parity).
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

// 3-mode fog factor, gl1 R_Fog()/R_WaterFog() semantics (see polygon_lmap.frag).
float H2FogFactor(void)
{
	if (ubo.fogMode < 0 || ubo.fogSkipAdditive != 0)
		return 1.0;

	float f;
	if (ubo.fogMode == 0)
		f = (ubo.fogEnd - fogDist) / (ubo.fogEnd - ubo.fogStart);
	else if (ubo.fogMode == 1)
		f = exp(-ubo.fogDensity * fogDist);
	else
		f = exp(-pow(ubo.fogDensity * fogDist, 2.0));

	return clamp(f, 0.0, 1.0);
}

void main()
{
	if (textured != 0)
		fragmentColor = texture(sTexture, texCoord) * clamp(color, 0.0, 1.0);
	else
		fragmentColor = color;

	if (fragmentColor.a <= pc.alphaTestRef)
		discard;

	fragmentColor.rgb = mix(ubo.fogColor.rgb, fragmentColor.rgb, H2FogFactor());
	fragmentColor.rgb = H2ColorGrade(fragmentColor.rgb);
}
