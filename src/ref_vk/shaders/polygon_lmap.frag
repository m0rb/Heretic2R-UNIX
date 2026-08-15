#version 450

// H2: lightmapped world faces - 4-sampler lightmap variant (the 4 lightstyle
// sub-lightmaps weighted by lmScales, gl3 si3Dlm semantics) + the uni3D-style
// fog block. gl1 rendered the base texture pass with normal fog and blended
// the lightmap pass (GL_ZERO, GL_SRC_COLOR) with fog scaled by
// r_fog_lightmap_adjust (R_BlendLightmaps()); the single-pass equivalent is
// the product of the two individually fogged terms (gl3 parity).

layout(push_constant) uniform PushConstant
{
    // vertex shader owns the first 17 floats; 68..76 = H2ColorGrade trio.
    layout(offset = 68) float gamma;
    layout(offset = 72) float brightness;
    layout(offset = 76) float contrast;
} pc;

layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(set = 2, binding = 0) uniform sampler2D sLightmap[4];

// Same block as polygon_lmap.vert (one dynamic UBO shared by both stages).
layout(set = 1, binding = 0) uniform UniformBufferObject
{
    mat4 model;
    vec4 lmScales[4];
    vec4 fogColor;
    int fogMode;
    float fogDensity;
    float fogStart;
    float fogEnd;
    float fogLightmapAdjust;
    int fogSkipAdditive;
    float viewLightmaps;
} ubo;

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 texCoordLmap;
layout(location = 2) in float fogDist;

layout(location = 0) out vec4 fragmentColor;

// gl1_Image.c R_InitGammaTable() math (see basic.frag).
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

// 3-mode fog factor, gl1 R_Fog()/R_WaterFog() semantics (fog_modes[] =
// LINEAR/EXP/EXP2). 'adjust' scales start/end/density - used with
// fogLightmapAdjust to replicate gl1's weaker fog on the lightmap term.
float H2FogFactor(float adjust)
{
    if (ubo.fogMode < 0 || ubo.fogSkipAdditive != 0)
        return 1.0;

    float f;
    if (ubo.fogMode == 0)
        f = (ubo.fogEnd * adjust - fogDist) / (ubo.fogEnd * adjust - ubo.fogStart * adjust);
    else if (ubo.fogMode == 1)
        f = exp(-(ubo.fogDensity * adjust) * fogDist);
    else
        f = exp(-pow(ubo.fogDensity * adjust * fogDist, 2.0));

    return clamp(f, 0.0, 1.0);
}

void main()
{
    vec4 color = texture(sTexture, texCoord);

    // H2 lightstyles: sum of the 4 sub-lightmaps weighted by lmScales.
    vec3 lmTex = vec3(0.0);
    for (int i = 0; i < 4; i++)
        lmTex += ubo.lmScales[i].rgb * texture(sLightmap[i], texCoordLmap).rgb;

    vec3 texFogged = mix(ubo.fogColor.rgb, color.rgb, H2FogFactor(1.0));
    vec3 lmFogged = mix(ubo.fogColor.rgb, lmTex, H2FogFactor(ubo.fogLightmapAdjust));

    // gl_lightmap: show the (fogged) lightmaps only.
    vec3 lit = mix(texFogged * lmFogged, lmFogged, ubo.viewLightmaps);
    fragmentColor = vec4(H2ColorGrade(lit), 1.0);
}
