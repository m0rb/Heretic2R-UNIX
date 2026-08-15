#include "compat.h"
//
// gl3_Shaders.c -- shader corpus and UBO machinery for the OpenGL 3.2 core renderer.
//
// Ported wholesale from yq2 gl3_shaders.c (#version 150 desktop GL only, GLES3
// paths dropped) with the H2 modifications from CONTRACT.md:
//  - uniCommon carries gamma + brightness + contrast + intensity + time; every
//    fragment shader ends with H2ColorGrade(), a GLSL translation of gl1_Image.c
//    R_InitGammaTable() (NO texture-baked gamma anymore).
//  - uni3D carries the H2 fog block (3 fog modes, gl1 R_Fog()/R_WaterFog()
//    semantics, r_fog_lightmap_adjust, additive fog suppression) + alphaTestRef.
//  - H2 program set: 2D texture/color, 3D lm/lmFlow/trans/turb/sky/sprite,
//    flexmodel (+ sphere-map variant), particle atlas quads.
//
// Copyright 1998 Raven Software
//

#include "gl3_Local.h"

#define eprintf(...) ri.Con_Printf(PRINT_ALL, __VA_ARGS__)

static GLuint CompileShader(const GLenum shaderType, const char* shaderSrc, const char* shaderSrc2)
{
	GLuint shader = glCreateShader(shaderType);

	const char* version = "#version 150\n";
	const char* sources[3] = { version, shaderSrc, shaderSrc2 };
	const int numSources = (shaderSrc2 != NULL ? 3 : 2);

	glShaderSource(shader, numSources, sources, NULL);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);

		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

		if (infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength + 1);
			bufLen = infoLogLength + 1;

			if (bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CompileShader(), malloc(%d) failed!\n", infoLogLength + 1);
			}
		}

		glGetShaderInfoLog(shader, bufLen, NULL, bufPtr);

		const char* shaderTypeStr = "";
		switch (shaderType)
		{
			case GL_VERTEX_SHADER:   shaderTypeStr = "Vertex"; break;
			case GL_FRAGMENT_SHADER: shaderTypeStr = "Fragment"; break;
			default: break;
		}

		eprintf("ERROR: Compiling %s Shader failed: %s\n", shaderTypeStr, bufPtr);
		glDeleteShader(shader);

		if (bufPtr != buf)
			free(bufPtr);

		return 0;
	}

	return shader;
}

static GLuint CreateShaderProgram(const int numShaders, const GLuint* shaders)
{
	GLuint shaderProgram = glCreateProgram();

	if (shaderProgram == 0)
	{
		eprintf("ERROR: Couldn't create a new Shader Program!\n");
		return 0;
	}

	for (int i = 0; i < numShaders; i++)
		glAttachShader(shaderProgram, shaders[i]);

	// Make sure all shaders use the same attribute locations for common attributes
	// (so the same VAO can easily be used with different shaders).
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_TEXCOORD, "texCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LMTEXCOORD, "lmTexCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_COLOR, "vertColor");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_NORMAL, "normal");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LIGHTFLAGS, "lightFlags");

	glLinkProgram(shaderProgram);

	GLint status;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);

		GLint infoLogLength;
		glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &infoLogLength);

		if (infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength + 1);
			bufLen = infoLogLength + 1;

			if (bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CreateShaderProgram(), malloc(%d) failed!\n", infoLogLength + 1);
			}
		}

		glGetProgramInfoLog(shaderProgram, bufLen, NULL, bufPtr);
		eprintf("ERROR: Linking shader program failed: %s\n", bufPtr);

		glDeleteProgram(shaderProgram);

		if (bufPtr != buf)
			free(bufPtr);

		return 0;
	}

	for (int i = 0; i < numShaders; i++)
	{
		// After linking, they don't need to be attached anymore.
		glDetachShader(shaderProgram, shaders[i]);
	}

	return shaderProgram;
}

#define MULTILINE_STRING(...) #__VA_ARGS__

// ############## shaders for 2D rendering (HUD, menus, console, books, cinematics, ..) #####################

// NOTE: comments inside MULTILINE_STRING() are stripped by the C preprocessor
// before stringification, so they never reach the GLSL compiler - safe to use.

static const char* vertexSrc2D = MULTILINE_STRING(

		in vec2 position; // GL3_ATTRIB_POSITION
		in vec2 texCoord; // GL3_ATTRIB_TEXCOORD
		in vec4 vertColor; // GL3_ATTRIB_COLOR - H2: per-vertex color/alpha (Draw_Char colors, Draw_Pic alpha, ..)

		// for UBO shared between 2D shaders
		layout (std140) uniform uni2D
		{
			mat4 trans;
		};

		out vec2 passTexCoord;
		out vec4 passColor;

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
			passTexCoord = texCoord;
			passColor = vertColor;
		}
);

// 2D color-only rendering: Draw_Fill(), Draw_FadeScreen().
static const char* vertexSrc2Dcolor = MULTILINE_STRING(

		in vec2 position; // GL3_ATTRIB_POSITION
		in vec4 vertColor; // GL3_ATTRIB_COLOR - H2: fill color comes per-vertex (uniCommon has no color member)

		// for UBO shared between 2D shaders
		layout (std140) uniform uni2D
		{
			mat4 trans;
		};

		out vec4 passColor;

		void main()
		{
			gl_Position = trans * vec4(position, 0.0, 1.0);
			passColor = vertColor;
		}
);

// Common fragment shader prefix for the 2D programs: the uniCommon block and
// the H2 color grading function.
//
// H2ColorGrade() is a faithful GLSL translation of gl1_Image.c R_InitGammaTable():
//
//     contrast = 1.0f - vid_contrast;
//     contrast = (contrast > 0.5f) ? powf(contrast + 0.5f, 3.0f) : powf(contrast + 0.5f, 0.5f);
//     gammatable[0] = 0;
//     for (i = 1; i < 256; i++) {
//         inf = 255.0f * powf((i + 0.5f) / 255.5f, vid_gamma) + 0.5f;    // gamma pow
//         if (inf < 128.0f) { inf = 128.0f - inf; sign = -1; }           // fold around midpoint 128
//         else              { inf -= 128.0f;      sign =  1; }
//         inf = (vid_brightness * 160.0f - 80.0f)                        // brightness offset
//             + (powf(inf / 128.0f, contrast) * sign + 1.0f) * 128.0f;   // contrast curve around mid
//         gammatable[i] = clamp(inf, 0, 255);
//     }
//
// Byte-table math mapped to normalized floats (c ~ i/255, out ~ inf/255):
//   - the byte rounding biases are kept exactly: inf = 255*pow((c*255 + 0.5)/255.5, gamma) + 0.5;
//   - midpoint 128 -> 128.0/255.0; deviation dev = (inf - 128)/128;
//   - brightness offset (vid_brightness*160 - 80) -> /255;
//   - contrast exponent derived from the raw vid_contrast cvar exactly as above;
//   - gammatable[0] = 0 (pure black stays black) kept via step();
//   - final clamp(0, 255) -> clamp(0.0, 1.0).
static const char* fragmentCommon2D = MULTILINE_STRING(

		// for UBO shared between all shaders (incl. 2D)
		layout (std140) uniform uniCommon
		{
			float gamma;      // vid_gamma: H2 uses the value directly as pow() exponent (default 0.5)
			float brightness; // vid_brightness (0..1, 0.5 = neutral)
			float contrast;   // vid_contrast (0..1, 0.5 = neutral)
			float intensity;  // texel intensity scale (1.0 in H2)
			float time;       // r_newrefdef.time

			float _padC_1; float _padC_2; float _padC_3; // pad to vec4 multiple, keep in sync with gl3UniCommon_t!
		};

		vec3 H2ColorGrade(vec3 color)
		{
			float ce = 1.0 - contrast;
			ce = (ce > 0.5) ? pow(ce + 0.5, 3.0) : pow(ce + 0.5, 0.5);

			vec3 inf = 255.0 * pow((color * 255.0 + 0.5) * (1.0 / 255.5), vec3(gamma)) + 0.5;
			vec3 dev = (inf - 128.0) * (1.0 / 128.0);
			vec3 graded = vec3((brightness * 160.0 - 80.0) * (1.0 / 255.0))
			            + (pow(abs(dev), vec3(ce)) * sign(dev) + 1.0) * (128.0 / 255.0);

			graded = mix(graded, vec3(0.0), step(color, vec3(0.0)));
			return clamp(graded, 0.0, 1.0);
		}
);

static const char* fragmentSrc2D = MULTILINE_STRING(

		in vec2 passTexCoord;
		in vec4 passColor;

		uniform sampler2D tex;

		out vec4 outColor;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord) * passColor;

			// gl1 2D drawing used glEnable(GL_ALPHA_TEST) + glAlphaFunc(GL_GREATER, 0.05)
			// (gl1_Draw.c / gl1_DrawBook.c), this does the same
			if(texel.a <= 0.05)
				discard;

			outColor.rgb = H2ColorGrade(texel.rgb);
			outColor.a = texel.a; // alpha is not modified by gamma/brightness/contrast
		}
);

static const char* fragmentSrc2Dcolor = MULTILINE_STRING(

		in vec4 passColor;

		out vec4 outColor;

		void main()
		{
			// NOTE: gl1 only baked gamma into *textures*, so flat fills were ungraded there;
			// CONTRACT.md mandates grading at the end of EVERY fragment shader instead.
			outColor.rgb = H2ColorGrade(passColor.rgb);
			outColor.a = passColor.a;
		}
);

// ############## shaders for 3D rendering #####################

static const char* vertexCommon3D = MULTILINE_STRING(

		in vec3 position;   // GL3_ATTRIB_POSITION
		in vec2 texCoord;   // GL3_ATTRIB_TEXCOORD
		in vec2 lmTexCoord; // GL3_ATTRIB_LMTEXCOORD
		in vec4 vertColor;  // GL3_ATTRIB_COLOR
		in vec3 normal;     // GL3_ATTRIB_NORMAL
		in uint lightFlags; // GL3_ATTRIB_LIGHTFLAGS

		out vec2 passTexCoord;

		// for UBO shared between all 3D shaders - keep in sync with gl3Uni3D_t (gl3_Local.h)!
		layout (std140) uniform uni3D
		{
			mat4 transProj;   // projection matrix
			mat4 transView;   // view matrix (world -> eye)
			mat4 transModel;  // model matrix (entity -> world)

			vec4 fogColor;

			float scroll;     // for SURF_FLOWING
			float alpha;      // for translucent surfaces (trans33/66)
			float alphaTestRef;     // discard when texel.a <= alphaTestRef; < 0.0 disables
			int   fogMode;          // -1 off, 0 linear, 1 exp, 2 exp2
			float fogDensity;
			float fogStart;
			float fogEnd;
			float fogLightmapAdjust; // r_fog_lightmap_adjust (scales fog for the lightmap term)
			int   fogSkipAdditive;   // 1 = no fog on current (additive) draw

			float _pad3D_1; float _pad3D_2; float _pad3D_3; // pad to vec4 multiple
		};
);

static const char* fragmentCommon3D = MULTILINE_STRING(

		in vec2 passTexCoord;

		out vec4 outColor;

		// for UBO shared between all shaders (incl. 2D) - keep in sync with gl3UniCommon_t!
		layout (std140) uniform uniCommon
		{
			float gamma;
			float brightness;
			float contrast;
			float intensity;
			float time;

			float _padC_1; float _padC_2; float _padC_3;
		};

		// for UBO shared between all 3D shaders - keep in sync with gl3Uni3D_t!
		layout (std140) uniform uni3D
		{
			mat4 transProj;
			mat4 transView;
			mat4 transModel;

			vec4 fogColor;

			float scroll;
			float alpha;
			float alphaTestRef;
			int   fogMode;
			float fogDensity;
			float fogStart;
			float fogEnd;
			float fogLightmapAdjust;
			int   fogSkipAdditive;

			float _pad3D_1; float _pad3D_2; float _pad3D_3;
		};

		// H2 color grading - see the detailed derivation comment above fragmentCommon2D
		// (gl1_Image.c R_InitGammaTable(): gamma pow, brightness offset, contrast pow around mid).
		vec3 H2ColorGrade(vec3 color)
		{
			float ce = 1.0 - contrast;
			ce = (ce > 0.5) ? pow(ce + 0.5, 3.0) : pow(ce + 0.5, 0.5);

			vec3 inf = 255.0 * pow((color * 255.0 + 0.5) * (1.0 / 255.5), vec3(gamma)) + 0.5;
			vec3 dev = (inf - 128.0) * (1.0 / 128.0);
			vec3 graded = vec3((brightness * 160.0 - 80.0) * (1.0 / 255.0))
			            + (pow(abs(dev), vec3(ce)) * sign(dev) + 1.0) * (128.0 / 255.0);

			graded = mix(graded, vec3(0.0), step(color, vec3(0.0)));
			return clamp(graded, 0.0, 1.0);
		}

		// Reconstruct the (positive) eye-space depth from gl_FragCoord.z and the
		// projection matrix: z_ndc = 2*fragZ - 1; z_eye = -P[3][2] / (z_ndc + P[2][2]).
		// Fixed-function GL fog (gl1) used the eye-plane distance |z_eye| as fog coord.
		float H2FogEyeDist()
		{
			float zNdc = 2.0 * gl_FragCoord.z - 1.0;
			return abs(transProj[3][2] / (zNdc + transProj[2][2]));
		}

		// 3-mode fog factor, gl1 R_Fog()/R_WaterFog() semantics (fog_modes[] = LINEAR/EXP/EXP2).
		// 'adjust' scales start/end/density - used with fogLightmapAdjust to replicate
		// gl1 R_BlendLightmaps()'s weaker fog on the lightmap term (see fragmentSrc3Dlm).
		float H2FogFactor(float adjust)
		{
			if(fogMode < 0 || fogSkipAdditive != 0)
				return 1.0;

			float dist = H2FogEyeDist();
			float f;
			if(fogMode == 0)
				f = (fogEnd * adjust - dist) / (fogEnd * adjust - fogStart * adjust);
			else if(fogMode == 1)
				f = exp(-(fogDensity * adjust) * dist);
			else
				f = exp(-pow(fogDensity * adjust * dist, 2.0));

			return clamp(f, 0.0, 1.0);
		}

		vec3 H2ApplyFog(vec3 color)
		{
			return mix(fogColor.rgb, color, H2FogFactor(1.0));
		}

		vec3 H2ApplyFogLM(vec3 color)
		{
			return mix(fogColor.rgb, color, H2FogFactor(fogLightmapAdjust));
		}

		// gl1 glEnable(GL_ALPHA_TEST) + glAlphaFunc(GL_GREATER, alphaTestRef) equivalence.
		// Thresholds mirror gl1: 0.666 world default, 0.05 UI/sprites, 0.0 additive;
		// alphaTestRef < 0.0 means the test is disabled (glDisable(GL_ALPHA_TEST)).
		void H2AlphaTest(float alphaValue)
		{
			if(alphaValue <= alphaTestRef)
				discard;
		}
);

static const char* vertexSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		void main()
		{
			passTexCoord = texCoord;
			gl_Position = transProj * transView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrc3Dtrans = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D
		// scroll is 0.0 for non-flowing surfaces, so this also covers
		// flowing translucent surfaces without a separate program.

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, 0.0);
			gl_Position = transProj * transView * transModel * vec4(position, 1.0);
		}
);

static const char* vertexSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec2 passLMcoord;
		out vec3 passWorldCoord;
		out vec3 passNormal;
		flat out uint passLightFlags;

		void main()
		{
			passTexCoord = texCoord;
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			passWorldCoord = worldCoord.xyz;
			vec4 worldNormal = transModel * vec4(normal, 0.0);
			passNormal = normalize(worldNormal.xyz);
			passLightFlags = lightFlags;

			gl_Position = transProj * transView * worldCoord;
		}
);

static const char* vertexSrc3DlmFlow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec2 passLMcoord;
		out vec3 passWorldCoord;
		out vec3 passNormal;
		flat out uint passLightFlags;

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, 0.0);
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			passWorldCoord = worldCoord.xyz;
			vec4 worldNormal = transModel * vec4(normal, 0.0);
			passNormal = normalize(worldNormal.xyz);
			passLightFlags = lightFlags;

			gl_Position = transProj * transView * worldCoord;
		}
);

// Alias-style vertex shader: flexmodels, sprites and particle quads
// (9-float gl3_alias_vtx_t layout: pos, st, rgba).
static const char* vertexSrcFlex = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec4 passColor;

		void main()
		{
			passColor = vertColor;
			passTexCoord = texCoord;
			gl_Position = transProj * transView * transModel * vec4(position, 1.0);
		}
);

// Sphere-map variant for FlexModel FMNI_USE_REFLECT / RF_REFLECTION:
// GLSL equivalence of fixed-function GL_SPHERE_MAP texgen (GL 1.x spec 2.10.4)
// using the eye-space normal. NOTE: requires a vertex stream with normals
// (GL3_ATTRIB_NORMAL) - the flexmodel module port must provide them.
static const char* vertexSrcFlexSphere = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec4 passColor;

		void main()
		{
			passColor = vertColor;

			vec4 eyePos = transView * transModel * vec4(position, 1.0);
			vec3 eyeNormal = normalize(mat3(transView) * mat3(transModel) * normal);

			// u = normalized eye-space position, r = reflection vector,
			// (s,t) = r.xy / m + 0.5 with m = 2*sqrt(rx^2 + ry^2 + (rz+1)^2)
			vec3 u = normalize(eyePos.xyz);
			vec3 r = reflect(u, eyeNormal);
			float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
			passTexCoord = vec2(r.x / m + 0.5, r.y / m + 0.5);

			gl_Position = transProj * eyePos;
		}
);

static const char* fragmentSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		struct DynLight { // gl3UniDynLight in C
			vec3 lightOrigin;
			float _pad;
			vec4 lightColor; // .a is intensity; this way it also works on OSX
		};

		layout (std140) uniform uniLights
		{
			DynLight dynLights[32];
			uint numDynLights;
			uint _pad1; uint _pad2; uint _pad3;
		};

		uniform sampler2D tex;

		uniform sampler2D lightmap0;
		uniform sampler2D lightmap1;
		uniform sampler2D lightmap2;
		uniform sampler2D lightmap3;

		uniform vec4 lmScales[4];

		in vec2 passLMcoord;
		in vec3 passWorldCoord;
		in vec3 passNormal;
		flat in uint passLightFlags;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			H2AlphaTest(texel.a);

			// apply intensity
			texel.rgb *= intensity;

			// apply lightmap: 4 texture units scaled by the lightstyle scales
			vec4 lmTex = texture(lightmap0, passLMcoord) * lmScales[0];
			lmTex     += texture(lightmap1, passLMcoord) * lmScales[1];
			lmTex     += texture(lightmap2, passLMcoord) * lmScales[2];
			lmTex     += texture(lightmap3, passLMcoord) * lmScales[3];

			if(passLightFlags != 0u)
			{
				// dynamic lights (yq2 model)
				for(uint i = 0u; i < numDynLights; ++i)
				{
					// dyn light number i does not affect this plane, just skip it
					if((passLightFlags & (1u << i)) == 0u)
						continue;

					float intens = dynLights[i].lightColor.a;

					vec3 lightToPos = dynLights[i].lightOrigin - passWorldCoord;
					float distLightToPos = length(lightToPos);
					float fact = max(0.0, intens - distLightToPos - 52.0);

					// move the light source a bit further above the surface
					// => helps if the lightsource is so close to the surface (e.g. grenades)
					//    that the dot product below would return 0
					lightToPos += passNormal * 32.0;

					// also factor in angle between light and point on surface
					fact *= max(0.0, dot(passNormal, normalize(lightToPos)));

					lmTex.rgb += dynLights[i].lightColor.rgb * fact * (1.0 / 256.0);
				}
			}

			// H2 fog: gl1 rendered the base texture pass with normal fog and blended the
			// lightmap pass (GL_ZERO, GL_SRC_COLOR) with fog start/end/density scaled by
			// r_fog_lightmap_adjust (R_BlendLightmaps()). The single-pass equivalent is
			// the product of the two individually fogged terms.
			vec3 texFogged = H2ApplyFog(texel.rgb);
			vec3 lmFogged = H2ApplyFogLM(lmTex.rgb);

			outColor.rgb = H2ColorGrade(texFogged * lmFogged);
			outColor.a = 1.0; // lightmaps aren't used with translucent surfaces
		}
);

static const char* fragmentSrc3Dtrans = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			H2AlphaTest(texel.a);

			texel.rgb *= intensity;

			outColor.rgb = H2ColorGrade(H2ApplyFog(texel.rgb));
			outColor.a = texel.a * alpha; // gl_trans33/gl_trans66 via uni3D alpha
		}
);

static const char* fragmentSrc3Dturb = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D
		// Analytic equivalent of gl1_Warp.c R_EmitWaterPolys():
		//   s = (os + turbsin[(ot*0.125 + time) * TURBSCALE & 255] + scroll) / 64
		// with turbsin[] = 8*sin(angle) halved to +-4 at RI_Init (gl1_Main.c),
		// so: s = (os + 4*sin(ot*0.125 + time) + scroll) / 64.
		// (The H2 'undulate' vertex wobble stays CPU-side in the warp module port.)

		uniform sampler2D tex;

		void main()
		{
			vec2 tc = passTexCoord;
			tc.s += sin(passTexCoord.t * 0.125 + time) * 4.0;
			tc.s += scroll;
			tc.t += sin(passTexCoord.s * 0.125 + time) * 4.0;
			tc *= 1.0 / 64.0; // do this last

			vec4 texel = texture(tex, tc);

			texel.rgb *= intensity;

			outColor.rgb = H2ColorGrade(H2ApplyFog(texel.rgb));
			outColor.a = texel.a * alpha;
		}
);

static const char* fragmentSrc3Dsky = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// no fog on the skybox (drawn at fixed distance; matches yq2)
			outColor.rgb = H2ColorGrade(texel.rgb);
			outColor.a = 1.0;
		}
);

// Shared by si3Dsprite, si3Dflex, si3DflexSphere and siParticle: modulate texture
// by per-vertex color (gl1 glColor* + GL_MODULATE), alpha test, fog, grade.
// Additive passes (aparticles, GL_GHOST etc.) suppress fog via uni3D.fogSkipAdditive,
// mirroring gl1's glDisable(GL_FOG) for those (gl1_Main.c R_DrawParticles()).
static const char* fragmentSrc3DvertexColor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		uniform sampler2D tex;

		in vec4 passColor;

		void main()
		{
			vec4 texel = texture(tex, passTexCoord) * passColor;

			H2AlphaTest(texel.a);

			texel.rgb *= intensity;

			outColor.rgb = H2ColorGrade(H2ApplyFog(texel.rgb));
			outColor.a = texel.a;
		}
);

#undef MULTILINE_STRING

enum
{
	GL3_BINDINGPOINT_UNICOMMON,
	GL3_BINDINGPOINT_UNI2D,
	GL3_BINDINGPOINT_UNI3D,
	GL3_BINDINGPOINT_UNILIGHTS
};

static qboolean initShader2D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders2D[2] = { 0 };
	GLuint prog;

	if (shaderInfo->shaderProgram != 0)
	{
		eprintf("WARNING: calling initShader2D for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScalesOrTime = -1;

	shaders2D[0] = CompileShader(GL_VERTEX_SHADER, vertSrc, NULL);
	if (shaders2D[0] == 0)
		return false;

	shaders2D[1] = CompileShader(GL_FRAGMENT_SHADER, fragmentCommon2D, fragSrc);
	if (shaders2D[1] == 0)
	{
		glDeleteShader(shaders2D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders2D);

	// The shaders aren't needed anymore once they're linked into the program.
	glDeleteShader(shaders2D[0]);
	glDeleteShader(shaders2D[1]);

	if (prog == 0)
		return false;

	shaderInfo->shaderProgram = prog;
	GL3_UseProgram(prog);

	// Bind the buffer objects to the uniform blocks.
	GLuint blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	if (blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize != sizeof(gl3state.uniCommonData))
		{
			eprintf("WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon': %i vs %i\n",
					blockSize, (int)sizeof(gl3state.uniCommonData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else
	{
		eprintf("WARNING: Couldn't find uniform block index 'uniCommon'\n");
		goto err_cleanup;
	}

	blockIndex = glGetUniformBlockIndex(prog, "uni2D");
	if (blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize != sizeof(gl3state.uni2DData))
		{
			eprintf("WARNING: OpenGL driver disagrees with us about UBO size of 'uni2D'\n");
			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNI2D);
	}
	else
	{
		eprintf("WARNING: Couldn't find uniform block index 'uni2D'\n");
		goto err_cleanup;
	}

	// Make sure texture is GL_TEXTURE0.
	const GLint texLoc = glGetUniformLocation(prog, "tex");
	if (texLoc != -1)
		glUniform1i(texLoc, 0);

	return true;

err_cleanup:
	glDeleteProgram(prog);
	shaderInfo->shaderProgram = 0;

	return false;
}

static qboolean initShader3D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders3D[2] = { 0 };
	GLuint prog = 0;
	int i;

	if (shaderInfo->shaderProgram != 0)
	{
		eprintf("WARNING: calling initShader3D for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScalesOrTime = -1;

	shaders3D[0] = CompileShader(GL_VERTEX_SHADER, vertexCommon3D, vertSrc);
	if (shaders3D[0] == 0)
		return false;

	shaders3D[1] = CompileShader(GL_FRAGMENT_SHADER, fragmentCommon3D, fragSrc);
	if (shaders3D[1] == 0)
	{
		glDeleteShader(shaders3D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders3D);

	if (prog == 0)
		goto err_cleanup;

	GL3_UseProgram(prog);

	// Bind the buffer objects to the uniform blocks.
	GLuint blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	if (blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize != sizeof(gl3state.uniCommonData))
		{
			eprintf("WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon'\n");
			eprintf("         driver says %d, we expect %d\n", blockSize, (int)sizeof(gl3state.uniCommonData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else
	{
		eprintf("WARNING: Couldn't find uniform block index 'uniCommon'\n");
		goto err_cleanup;
	}

	blockIndex = glGetUniformBlockIndex(prog, "uni3D");
	if (blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize != sizeof(gl3state.uni3DData))
		{
			eprintf("WARNING: OpenGL driver disagrees with us about UBO size of 'uni3D'\n");
			eprintf("         driver says %d, we expect %d\n", blockSize, (int)sizeof(gl3state.uni3DData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNI3D);
	}
	else
	{
		eprintf("WARNING: Couldn't find uniform block index 'uni3D'\n");
		goto err_cleanup;
	}

	blockIndex = glGetUniformBlockIndex(prog, "uniLights");
	if (blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if (blockSize != sizeof(gl3state.uniLightsData))
		{
			eprintf("WARNING: OpenGL driver disagrees with us about UBO size of 'uniLights'\n");
			eprintf("         driver says %d, we expect %d\n", blockSize, (int)sizeof(gl3state.uniLightsData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNILIGHTS);
	}
	// else: as uniLights is only used in the LM shaders, it's ok if it's missing.

	// Make sure texture is GL_TEXTURE0.
	const GLint texLoc = glGetUniformLocation(prog, "tex");
	if (texLoc != -1)
		glUniform1i(texLoc, 0);

	// ... and the 4 lightmap textures use GL_TEXTURE1..4.
	char lmName[10] = "lightmapX";
	for (i = 0; i < 4; i++)
	{
		lmName[8] = (char)('0' + i);
		const GLint lmLoc = glGetUniformLocation(prog, lmName);
		if (lmLoc != -1)
			glUniform1i(lmLoc, i + 1); // lightmap0 belongs to GL_TEXTURE1, lightmap1 to GL_TEXTURE2 etc.
	}

	const GLint lmScalesLoc = glGetUniformLocation(prog, "lmScales");
	shaderInfo->uniLmScalesOrTime = lmScalesLoc;
	if (lmScalesLoc != -1)
	{
		shaderInfo->lmScales[0] = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f);

		for (i = 1; i < 4; i++)
			shaderInfo->lmScales[i] = HMM_Vec4(0.0f, 0.0f, 0.0f, 0.0f);

		glUniform4fv(lmScalesLoc, 4, shaderInfo->lmScales[0].Elements);
	}

	shaderInfo->shaderProgram = prog;

	// The shaders aren't needed anymore once they're linked into the program.
	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	return true;

err_cleanup:
	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	if (prog != 0)
		glDeleteProgram(prog);

	return false;
}

static void initUBOs(void)
{
	gl3state.uniCommonData.gamma = vid_gamma->value; // H2: used directly as pow() exponent (default 0.5), NOT 1/gamma!
	gl3state.uniCommonData.brightness = vid_brightness->value;
	gl3state.uniCommonData.contrast = vid_contrast->value;
	gl3state.uniCommonData.intensity = 1.0f; // Reserved (yq2 heritage); H2 has no intensity cvar.
	gl3state.uniCommonData.time = 0.0f;

	glGenBuffers(1, &gl3state.uniCommonUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniCommonUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNICOMMON, gl3state.uniCommonUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniCommonData), &gl3state.uniCommonData, GL_DYNAMIC_DRAW);

	// The matrix will be set to something more useful later, before being used.
	gl3state.uni2DData.transMat4 = HMM_Mat4();

	glGenBuffers(1, &gl3state.uni2DUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uni2DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI2D, gl3state.uni2DUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uni2DData), &gl3state.uni2DData, GL_DYNAMIC_DRAW);

	// The matrices will be set to something more useful later, before being used.
	gl3state.uni3DData.transProjMat4 = gl3_identityMat4;
	gl3state.uni3DData.transViewMat4 = gl3_identityMat4;
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	gl3state.uni3DData.fogColor = HMM_Vec4(1.0f, 1.0f, 1.0f, 0.0f);
	gl3state.uni3DData.scroll = 0.0f;
	gl3state.uni3DData.alpha = 1.0f;
	gl3state.uni3DData.alphaTestRef = -1.0f; // Alpha test disabled.
	gl3state.uni3DData.fogMode = -1; // Fog off.
	gl3state.uni3DData.fogDensity = 0.0f;
	gl3state.uni3DData.fogStart = 0.0f;
	gl3state.uni3DData.fogEnd = 4096.0f;
	gl3state.uni3DData.fogLightmapAdjust = 1.0f;
	gl3state.uni3DData.fogSkipAdditive = 0;

	glGenBuffers(1, &gl3state.uni3DUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uni3DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, gl3state.uni3DUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uni3DData), &gl3state.uni3DData, GL_DYNAMIC_DRAW);

	glGenBuffers(1, &gl3state.uniLightsUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniLightsUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNILIGHTS, gl3state.uniLightsUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniLightsData), &gl3state.uniLightsData, GL_DYNAMIC_DRAW);

	gl3state.currentUBO = gl3state.uniLightsUBO;
}

static qboolean createShaders(void)
{
	if (!initShader2D(&gl3state.si2D, vertexSrc2D, fragmentSrc2D))
	{
		eprintf("WARNING: Failed to create shader program for textured 2D rendering!\n");
		return false;
	}

	if (!initShader2D(&gl3state.si2Dcolor, vertexSrc2Dcolor, fragmentSrc2Dcolor))
	{
		eprintf("WARNING: Failed to create shader program for color-only 2D rendering!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3Dlm, vertexSrc3Dlm, fragmentSrc3Dlm))
	{
		eprintf("WARNING: Failed to create shader program for textured 3D rendering with lightmap!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3DlmFlow, vertexSrc3DlmFlow, fragmentSrc3Dlm))
	{
		eprintf("WARNING: Failed to create shader program for scrolling textured 3D rendering with lightmap!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3Dtrans, vertexSrc3Dtrans, fragmentSrc3Dtrans))
	{
		eprintf("WARNING: Failed to create shader program for rendering translucent 3D things!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3Dturb, vertexSrc3D, fragmentSrc3Dturb))
	{
		eprintf("WARNING: Failed to create shader program for water rendering!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3Dsky, vertexSrc3D, fragmentSrc3Dsky))
	{
		eprintf("WARNING: Failed to create shader program for sky rendering!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3Dsprite, vertexSrcFlex, fragmentSrc3DvertexColor))
	{
		eprintf("WARNING: Failed to create shader program for sprite rendering!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3Dflex, vertexSrcFlex, fragmentSrc3DvertexColor))
	{
		eprintf("WARNING: Failed to create shader program for rendering flexmodels!\n");
		return false;
	}

	if (!initShader3D(&gl3state.si3DflexSphere, vertexSrcFlexSphere, fragmentSrc3DvertexColor))
	{
		eprintf("WARNING: Failed to create shader program for rendering reflective (sphere-mapped) flexmodels!\n");
		return false;
	}

	if (!initShader3D(&gl3state.siParticle, vertexSrcFlex, fragmentSrc3DvertexColor))
	{
		eprintf("WARNING: Failed to create shader program for rendering particles!\n");
		return false;
	}

	gl3state.currentShaderProgram = 0;

	return true;
}

qboolean GL3_InitShaders(void)
{
	initUBOs();

	return createShaders();
}

static void deleteShaders(void)
{
	const gl3ShaderInfo_t siZero = { 0 };

	// NOTE: relies on si2D being the first and siParticle the last gl3ShaderInfo_t
	// member of gl3state_t (see gl3_Local.h)!
	for (gl3ShaderInfo_t* si = &gl3state.si2D; si <= &gl3state.siParticle; si++)
	{
		if (si->shaderProgram != 0)
			glDeleteProgram(si->shaderProgram);

		*si = siZero;
	}
}

void GL3_ShutdownShaders(void)
{
	deleteShaders();

	// Let's (ab)use the fact that all 4 UBO handles are consecutive fields of the gl3state struct.
	glDeleteBuffers(4, &gl3state.uniCommonUBO);
	gl3state.uniCommonUBO = gl3state.uni2DUBO = gl3state.uni3DUBO = gl3state.uniLightsUBO = 0;
}

qboolean GL3_RecreateShaders(void)
{
	// Delete and recreate the existing shaders (but not the UBOs).
	deleteShaders();

	return createShaders();
}

static inline void updateUBO(const GLuint ubo, const GLsizeiptr size, const void* data)
{
	if (gl3state.currentUBO != ubo)
	{
		gl3state.currentUBO = ubo;
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	}

	// YQ2: glBufferData() seems to be reasonably fast everywhere
	// (see the discussion in yq2 gl3_shaders.c updateUBO()).
	glBufferData(GL_UNIFORM_BUFFER, size, data, GL_DYNAMIC_DRAW);
}

void GL3_UpdateUBOCommon(void)
{
	updateUBO(gl3state.uniCommonUBO, sizeof(gl3state.uniCommonData), &gl3state.uniCommonData);
}

void GL3_UpdateUBO2D(void)
{
	updateUBO(gl3state.uni2DUBO, sizeof(gl3state.uni2DData), &gl3state.uni2DData);
}

void GL3_UpdateUBO3D(void)
{
	updateUBO(gl3state.uni3DUBO, sizeof(gl3state.uni3DData), &gl3state.uni3DData);
}

void GL3_UpdateUBOLights(void)
{
	updateUBO(gl3state.uniLightsUBO, sizeof(gl3state.uniLightsData), &gl3state.uniLightsData);
}
