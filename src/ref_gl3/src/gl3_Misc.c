#include "compat.h"
//
// gl3_Misc.c -- gl1_Misc.c port for the OpenGL 3.2 core renderer.
//
// NOTE: R_ScreenShot_f() and R_Strings_f() from gl1_Misc.c already live in the
// (locked) gl3_Main.c foundation - they are NOT duplicated here.
//
// Fixed-function state from gl1 maps to the gl3 backend like this:
//  - glColor*            -> gl3_currentDrawColor[] (baked into per-vertex colors by the emitters).
//  - GL_ALPHA_TEST       -> uni3D alphaTestRef (< 0.0 = test disabled; shaders discard when texel.a <= ref).
//  - GL_FOG en/disable   -> uni3D fogSkipAdditive (fog params themselves are set by R_Fog()/R_WaterFog() in gl3_Main.c).
//  - glPushMatrix/glPopMatrix around R_RotateForEntity() -> uni3D transModelMat4 multiply / GL3_RestoreModelIdentity().
//  - GL_MODULATE/GL_REPLACE + glShadeModel  -> inherent in the vertex-color shaders (no-ops here).
//
// Copyright 1998 Raven Software
//

#include "gl3_Entity_internal.h"
#include "Vector.h"

// gl1 glColor* state mirror (see gl3_Entity_internal.h).
float gl3_currentDrawColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

// Tentative definition (-fcommon): merged with the real definition once the
// gl3_Light.c port (gl1_Light.c R_InitMinlight()) lands.
extern byte minlight[256]; // Owned by gl3_Light.c.

// Lazily created 1x1 white texture: replaces gl1's glDisable(GL_TEXTURE_2D) +
// glColor paths (R_DrawNullModel()) - GL 3.2 core has no untextured fixed-function
// drawing and the shared shader set always samples 'tex'.
static GLuint gl3_whiteTexture;

static GLuint GL3_GetWhiteTexture(void)
{
	if (gl3_whiteTexture == 0)
	{
		static const byte white_pixel[4] = { 255, 255, 255, 255 };

		glGenTextures(1, &gl3_whiteTexture);
		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, gl3_whiteTexture);
		gl3state.currenttexture = gl3_whiteTexture;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
	}

	return gl3_whiteTexture;
}

// Streams alias-layout vertices through vaoAlias/vboAlias and draws them (non-indexed).
// Alias-layout counterpart of GL3_BufferAndDraw3D() (gl3_Main.c); used by sprites and the null model.
void GL3_DrawAliasVerts(const GLuint shaderProgram, const gl3_alias_vtx_t* verts, const int numVerts, const GLenum drawMode)
{
	GL3_UseProgram(shaderProgram);
	GL3_BindVAO(gl3state.vaoAlias);
	GL3_BindVBO(gl3state.vboAlias);

	glBufferData(GL_ARRAY_BUFFER, numVerts * (GLsizeiptr)sizeof(gl3_alias_vtx_t), verts, GL_STREAM_DRAW);
	glDrawArrays(drawMode, 0, numVerts);
}

// glPopMatrix() equivalent for R_RotateForEntity().
void GL3_RestoreModelIdentity(void)
{
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	GL3_UpdateUBO3D();
}

void R_SetDefaultState(void) // Q2: GL_SetDefaultState()
{
	glClearColor(1.0f, 0.0f, 0.5f, 0.5f);
	glCullFace(GL_FRONT);
	// glEnable(GL_TEXTURE_2D): doesn't exist in core profile (texturing is always on in shaders).

	// glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.666f): H2's ambient alpha-test
	// state becomes the uni3D alphaTestRef default (world alpha-tested surfaces rely on it).
	gl3state.uni3DData.alphaTestRef = 0.666f;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	glDepthFunc(GL_LEQUAL); // gl1 sets this in R_Clear(); harmless to establish the default here too.

	if (r_msaa_samples->value > 0)
		glEnable(GL_MULTISAMPLE);
	else
		glDisable(GL_MULTISAMPLE);

	GL3_SetCurrentColor(1.0f, 1.0f, 1.0f, 1.0f); // glColor4f(1, 1, 1, 1).

	// glPolygonMode(GL_FRONT_AND_BACK, GL_FILL) is the core-profile default; glShadeModel doesn't exist.

	R_TextureMode(gl_texturemode->string);

	// gl1's global GL_TEXTURE_MIN/MAG_FILTER + GL_TEXTURE_WRAP defaults are per-texture-object
	// state in gl3 - R_TextureMode()/texture upload (gl3_Image.c module port) handle them.

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR.

	// R_TexEnv(GL_REPLACE): no-op (shader-determined).

	if (gl3state.uni3DUBO != 0) // UBOs exist only after GL3_InitShaders().
		GL3_UpdateUBO3D();

	// The GL context may have been recreated - drop the cached white texture so it's rebuilt.
	gl3_whiteTexture = 0;
}

// Q2 counterpart
void R_DrawNullModel(const entity_t* e) //mxd. Original logic uses 'currententity' global var.
{
	vec3_t shadelight;

	if (e->flags & RF_FULLBRIGHT)
		VectorSet(shadelight, 1.0f, 1.0f, 1.0f);
	else
		R_LightPoint(e->origin, shadelight, false);

	// glPushMatrix() + R_RotateForEntity(e).
	R_RotateForEntity(e);

	// glDisable(GL_TEXTURE_2D) + glColor3fv(shadelight): white texture + per-vertex color in gl3.
	GL3_BindTexnum(GL3_GetWhiteTexture());

	gl3_alias_vtx_t verts[6];

	for (int i = 0; i < 6; i++)
	{
		verts[i].texCoord[0] = 0.0f;
		verts[i].texCoord[1] = 0.0f;

		VectorCopy(shadelight, verts[i].color);
		verts[i].color[3] = 1.0f;
	}

	// Bottom fan.
	VectorSet(verts[0].pos, 0.0f, 0.0f, -16.0f);
	for (int i = 0; i < 5; i++)
		VectorSet(verts[i + 1].pos, 16.0f * cosf((float)i * ANGLE_90), 16.0f * sinf((float)i * ANGLE_90), 0.0f); //mxd. M_PI/2 -> ANGLE_90

	GL3_DrawAliasVerts(gl3state.si3Dflex.shaderProgram, verts, 6, GL_TRIANGLE_FAN);

	// Top fan.
	VectorSet(verts[0].pos, 0.0f, 0.0f, 16.0f);
	for (int i = 4; i > -1; i--)
		VectorSet(verts[5 - i].pos, 16.0f * cosf((float)i * ANGLE_90), 16.0f * sinf((float)i * ANGLE_90), 0.0f); //mxd. M_PI/2 -> ANGLE_90

	GL3_DrawAliasVerts(gl3state.si3Dflex.shaderProgram, verts, 6, GL_TRIANGLE_FAN);

	GL3_SetCurrentColor(1.0f, 1.0f, 1.0f, 1.0f); // glColor3f(1, 1, 1).
	GL3_RestoreModelIdentity(); // glPopMatrix().
}

// Transforms vector to screen space?
void R_TransformVector(const vec3_t v, vec3_t out)
{
	out[0] = DotProduct(v, vright);
	out[1] = DotProduct(v, vup);
	out[2] = DotProduct(v, vpn);
}

void R_RotateForEntity(const entity_t* e)
{
	// gl1: glTranslatef(origin) + glRotatef(yaw, Z) + glRotatef(-pitch, Y) + glRotatef(-roll, X),
	// angles in radians scaled by H2's RAD_TO_ANGLE for glRotatef's degrees.
	// gl3: build the same T * Rz * Ry * Rx matrix directly in radians
	// (YQ2 rotAroundAxisZYX() math) and multiply it onto uni3D transModelMat4.
	const float alpha = e->angles[1];	// Around Z (yaw).
	const float beta = -e->angles[0];	// Around Y (pitch).
	const float gamma = -e->angles[2];	// Around X (roll).

	const float sinA = sinf(alpha);
	const float cosA = cosf(alpha);
	const float sinB = sinf(beta);
	const float cosB = cosf(beta);
	const float sinG = sinf(gamma);
	const float cosG = cosf(gamma);

	const hmm_mat4 transMat = {{
		{ cosA * cosB,						  sinA * cosB,						   -sinB,		0.0f }, // First *column*.
		{ cosA * sinB * sinG - sinA * cosG,	  sinA * sinB * sinG + cosA * cosG,		cosB * sinG, 0.0f },
		{ cosA * sinB * cosG + sinA * sinG,	  sinA * sinB * cosG - cosA * sinG,		cosB * cosG, 0.0f },
		{ e->origin[0],						  e->origin[1],							e->origin[2], 1.0f } // glTranslatef(e->origin).
	}};

	gl3state.uni3DData.transModelMat4 = HMM_MultiplyMat4(gl3state.uni3DData.transModelMat4, transMat);
	GL3_UpdateUBO3D();
}

//mxd. Map object coordinates to window coordinates (slightly modified version of glhProjectf() from https://wikis.khronos.org/opengl/GluProject_and_gluUnProject_code).
qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos)
{
	// gl1 captured these via glGetFloatv(); in gl3 they're plain copies of
	// gl3state.viewMat3D / projMat3D made in R_SetupGL3D() (gl3_Main.c).

	// Transformation vectors.
	float tmp[8];

	// Modelview transform.
	tmp[0] = r_world_matrix[0] * pos[0] + r_world_matrix[4] * pos[1] + r_world_matrix[8] *  pos[2] + r_world_matrix[12]; // w is always 1.
	tmp[1] = r_world_matrix[1] * pos[0] + r_world_matrix[5] * pos[1] + r_world_matrix[9] *  pos[2] + r_world_matrix[13];
	tmp[2] = r_world_matrix[2] * pos[0] + r_world_matrix[6] * pos[1] + r_world_matrix[10] * pos[2] + r_world_matrix[14];
	tmp[3] = r_world_matrix[3] * pos[0] + r_world_matrix[7] * pos[1] + r_world_matrix[11] * pos[2] + r_world_matrix[15];

	// Projection transform, the final row of projection matrix is always [0 0 -1 0], so we optimize for that.
	tmp[4] = r_projection_matrix[0] * tmp[0] + r_projection_matrix[4] * tmp[1] + r_projection_matrix[8] *  tmp[2] + r_projection_matrix[12] * tmp[3];
	tmp[5] = r_projection_matrix[1] * tmp[0] + r_projection_matrix[5] * tmp[1] + r_projection_matrix[9] *  tmp[2] + r_projection_matrix[13] * tmp[3];
	tmp[6] = r_projection_matrix[2] * tmp[0] + r_projection_matrix[6] * tmp[1] + r_projection_matrix[10] * tmp[2] + r_projection_matrix[14] * tmp[3];

	// The result normalizes between -1 and 1.
	if (tmp[2] == 0.0f) // The w value.
		return false;

	tmp[7] = 1.0f / -tmp[2];

	// Perspective division.
	tmp[4] *= tmp[7];
	tmp[5] *= tmp[7];
	tmp[6] *= tmp[7];

	// Window coordinates. Map x, y to range 0 - 1.
	screen_pos[0] = (tmp[4] * 0.5f + 0.5f) * (float)r_newrefdef.width +  (float)r_newrefdef.x;
	screen_pos[1] = (tmp[5] * 0.5f + 0.5f) * (float)r_newrefdef.height + (float)r_newrefdef.y;
	screen_pos[2] = (1.0f + tmp[6]) * 0.5f; // This is only correct when glDepthRange(0.0, 1.0).

	//mxd. y-coord needs flipping...
	screen_pos[1] = (float)r_newrefdef.height - screen_pos[1];

	return true;
}

paletteRGBA_t R_ModulateRGBA(const paletteRGBA_t a, const paletteRGBA_t b) //mxd
{
	const paletteRGBA_t c = { .r = a.r * b.r / 255, .g = a.g * b.g / 255, .b = a.b * b.b / 255, .a = a.a * b.a / 255 };
	return c;
}

paletteRGBA_t R_GetSpriteShadelight(const vec3_t origin, const byte alpha) //mxd
{
	static const vec3_t light_add = { 0.1f, 0.1f, 0.1f };

	vec3_t c;
	R_LightPoint(origin, c, false);
	Vec3AddAssign(light_add, c); // Make it slightly brighter than lightmap color.
	Vec3ScaleAssign(255.0f, c);

	// Make sure light color is valid...
	const float max = max(c[0], max(c[1], c[2]));
	if (max > 255.0f)
		Vec3ScaleAssign(255.0f / max, c);

	const paletteRGBA_t color = { .r = (byte)c[0], .g = (byte)c[1], .b = (byte)c[2], alpha };

	return color;
}

void R_HandleTransparency(const entity_t* e) // H2: HandleTrans().
{
	if (e->flags & RF_TRANS_ADD)
	{
		if (e->flags & RF_ALPHA_TEXTURE)
		{
			gl3state.uni3DData.alphaTestRef = 0.0f; // glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.0f).
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			GL3_SetCurrentColorRGBA(e->color); // glColor4ub(e->color.r, e->color.g, e->color.b, e->color.a).
		}
		else
		{
			if ((int)r_fog->value || (int)cl_camera_under_surface->value) //mxd. Skipped gl_fog_broken check.
				gl3state.uni3DData.fogSkipAdditive = 1; // glDisable(GL_FOG).

			gl3state.uni3DData.alphaTestRef = -1.0f; // glDisable(GL_ALPHA_TEST).
			glBlendFunc(GL_ONE, GL_ONE);

			if (e->flags & RF_TRANS_ADD_ALPHA)
			{
				const float scaler = (float)e->color.a / 255.0f / 255.0f; //TODO: why is it divided twice?..
				GL3_SetCurrentColor((float)e->color.r * scaler, (float)e->color.g * scaler, (float)e->color.b * scaler, 1.0f); //mxd. qglColor4f -> qglColor3f
			}
			else
			{
				GL3_SetCurrentColor((float)e->color.r / 255.0f, (float)e->color.g / 255.0f, (float)e->color.b / 255.0f, 1.0f); //mxd. qglColor4ub -> qglColor3ub
			}
		}
	}
	else
	{
		gl3state.uni3DData.alphaTestRef = 0.05f; // glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.05f).
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// H2_1.07: qglBlendFunc(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR) when RF_TRANS_GHOST flag is set.
		if (!(e->flags & RF_TRANS_GHOST))
		{
			if (e->flags & RF_LM_COLOR) //mxd
			{
				const paletteRGBA_t c = R_ModulateRGBA(e->color, R_GetSpriteShadelight(e->origin, e->color.a));
				GL3_SetCurrentColor((float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f, (float)e->color.a / 255.0f); // glColor4ub(c.r, c.g, c.b, e->color.a).
			}
			else
			{
				GL3_SetCurrentColorRGBA(e->color); // glColor4ub(e->color.r, e->color.g, e->color.b, e->color.a).
			}
		}
	}

	glEnable(GL_BLEND);

	GL3_UpdateUBO3D(); // alphaTestRef / fogSkipAdditive live in uni3D.
}

void R_CleanupTransparency(const entity_t* e) // H2: CleanupTrans().
{
	glDisable(GL_BLEND);

	if (e->flags & (RF_TRANS_GHOST | RF_TRANS_ADD))
	{
		if ((int)r_fog->value || (int)cl_camera_under_surface->value) //mxd. Removed gl_fog_broken cvar check.
			gl3state.uni3DData.fogSkipAdditive = 0; // glEnable(GL_FOG).

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR.
	}
	else
	{
		// gl1: glDisable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.666f) - the test ends up
		// DISABLED; 0.666 is only the latched func value for whoever enables the test next
		// (in gl3 every module sets its own alphaTestRef before drawing).
		gl3state.uni3DData.alphaTestRef = -1.0f;
	}

	GL3_UpdateUBO3D();
}
