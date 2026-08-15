#include "compat.h"
//
// vk_Misc.c -- gl1_Misc.c port for the Vulkan renderer (CPU logic from the
// validated gl3_Misc.c port; draw submission through the QVk streaming
// buffers + pipeline inventory).
//
// NOTE: R_Strings_f()'s vk analog (Vk_Strings_f) already lives in the (locked)
// vk_Main.c foundation - it is NOT duplicated here. R_ScreenShot_f() IS
// implemented here (vkCmdCopyImageToBuffer swapchain readback, yq2remaster
// QVk_ReadPixels technique) - the vk_Main.c placeholder must be removed (see
// the module report).
//
// Fixed-function state from gl1 maps to the vk backend like this:
//  - glColor*             -> vk_currentDrawColor[] (baked into per-vertex colors by the emitters).
//  - glBlendFunc/GL_BLEND -> vk_entityBlendMode -> pipeline variant (QVk_SelectEntityPipeline()).
//  - GL_ALPHA_TEST        -> vk_alphaTestRef fragment push constant (< 0.0 = test disabled; shaders discard when texel.a <= ref).
//  - GL_FOG en/disable    -> vk_fogSkipAdditive, written into the per-draw UBO fog block (fog params themselves come from vk_fogblock, set by R_Fog()/R_WaterFog() in the frame module port).
//  - GL_DEPTH_TEST (RF_NODEPTHTEST) -> no-depth-test pipeline row (vk_drawModelBlendPipelines[1][*]).
//  - glDepthRange (RF_DEPTHHACK)    -> vkCmdSetViewport() minDepth/maxDepth (dynamic state; done in vk_FlexModel.c).
//  - glPushMatrix/glPopMatrix around R_RotateForEntity() -> vk_modelMatrix multiply / QVk_RestoreModelIdentity() (per-draw UBO model matrix).
//  - GL_MODULATE/GL_REPLACE + glShadeModel -> inherent in the vertex-color shaders (no-ops here).
//
// Copyright 1998 Raven Software
//

#include "vk_Entity_internal.h"
#include "Angles.h"
#include "Vector.h"

#pragma region ========================== SHARED / TENTATIVE GLOBALS ==========================

// gl1 glColor* state mirror (see vk_Entity_internal.h).
float vk_currentDrawColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

// gl1 R_BindImage() state mirror.
const image_t* vk_currentTexture;

// gl1 GL_BLEND / GL_ALPHA_TEST / GL_FOG state mirrors. gl1 R_SetDefaultState()
// enables the alpha test at 0.666 as the ambient default.
vk_entityblend_t vk_entityBlendMode = ENTITY_BLEND_NONE;
float vk_alphaTestRef = 0.666f;
int vk_fogSkipAdditive = 0;

// gl1 modelview matrix stack mirror (identity between entities).
matrix4_t vk_modelMatrix = { .m16 = { 1.0f, 0.0f, 0.0f, 0.0f,
									  0.0f, 1.0f, 0.0f, 0.0f,
									  0.0f, 0.0f, 1.0f, 0.0f,
									  0.0f, 0.0f, 0.0f, 1.0f } };

// Tentative definitions (-fcommon): merged with the real definitions once the
// owning module ports (frame module / vk_Light.c) land - the same trick
// gl3_Misc.c used for minlight[] (see vk_Entity_internal.h for ownership).
qboolean r_minlight_set;
vkfogblock_t vk_fogblock;

#pragma endregion

#pragma region ========================== NEW VK BACKEND HELPERS ==========================

// glEnable(GL_BLEND) with the currently latched blend func (gl1's ambient
// blend func is GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA - R_SetDefaultState()).
void QVk_EnableBlend(void)
{
	if (vk_entityBlendMode == ENTITY_BLEND_NONE)
		vk_entityBlendMode = ENTITY_BLEND_STANDARD;
}

// The R_HandleTransparency() state matrix -> pipeline mapping (see the pipeline
// matrix description in vk_Entity_internal.h).
qvkpipeline_t* QVk_SelectEntityPipeline(const qboolean no_depth_test)
{
	if (vk_entityBlendMode == ENTITY_BLEND_NONE)
		return &vk_drawModelPipelineFan[RP_WORLD]; // No blend, depth test + write on (RF_NODEPTHTEST implies a blended sprite, so no unblended no-depth-test variant is needed).

	return &vk_drawModelBlendPipelines[no_depth_test ? 1 : 0][vk_entityBlendMode - 1];
}

// Per-draw UBO for the model.vert/model.frag pipelines (set 1, binding 0).
// Entities fog identically to the world around them. r_world_fog (vk_Surface.c)
// is the single maintained fog block; the previous vk_fogblock was an orphaned
// zero-init global (fogMode 0 = black linear fog), which turned every model into
// a black silhouette. See vk_World_internal.h.
extern vkfogblock_t r_world_fog;

void QVk_GetEntityUbo(const qboolean textured, uint32_t* uboOffset, VkDescriptorSet* uboDescSet)
{
	vkmodelubo_t ubo;

	memcpy(ubo.model, vk_modelMatrix.m16, sizeof(ubo.model));
	ubo.fog = r_world_fog;
	ubo.fog.fogSkipAdditive = vk_fogSkipAdditive; // gl1 glDisable(GL_FOG) around additive entity draws.
	ubo.textured = (textured ? 1 : 0);

	uint8_t* uboData = QVk_GetUniformBuffer(sizeof(ubo), uboOffset, uboDescSet);
	memcpy(uboData, &ubo, sizeof(ubo));
}

// Shared entity push constants: vertex = view-projection matrix (+ sprite
// alpha, unread by model.vert), fragment = the H2ColorGrade trio (RP_WORLD
// grades per-fragment now) + the current gl1-style alpha test ref.
void QVk_PushEntityConstants(const qvkpipeline_t* pipeline)
{
	float vertPush[PUSH_CONSTANT_VERTEX_SIZE];
	memcpy(vertPush, r_viewproj_matrix, sizeof(r_viewproj_matrix));
	vertPush[16] = 1.0f; // sprite.vert alpha slot (unused by model.vert/nullmodel.vert).

	const float fragPush[4] = { vk_gradePush[0], vk_gradePush[1], vk_gradePush[2], vk_alphaTestRef };

	vkCmdPushConstants(vk_activeCmdbuffer, pipeline->layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vertPush), vertPush);
	vkCmdPushConstants(vk_activeCmdbuffer, pipeline->layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, PUSH_CONSTANT_VERTEX_SIZE * sizeof(float), sizeof(fragPush), fragPush);
}

// Streams alias-layout vertices as a triangle fan and draws them through the
// current entity pipeline state (glBegin(GL_QUADS/GL_POLYGON) equivalent -
// convex polygons only, same as GL rasterization). Used by the sprite module.
void QVk_DrawEntityFan(const vk_alias_vtx_t* verts, const int numVerts, const qboolean no_depth_test)
{
	if (!vk_frameStarted || numVerts < 3)
		return;

	if (vk_currentTexture == NULL || vk_currentTexture->vk_texture.descriptorSet == VK_NULL_HANDLE)
		return; // No texture uploaded (yet) - nothing sane to draw.

	qvkpipeline_t* pipeline = QVk_SelectEntityPipeline(no_depth_test);

	// Stream the vertices.
	VkBuffer vbo;
	VkDeviceSize vboOffset;
	const VkDeviceSize vboSize = (VkDeviceSize)numVerts * sizeof(vk_alias_vtx_t);
	uint8_t* vertData = QVk_GetVertexBuffer(vboSize, &vbo, &vboOffset);
	memcpy(vertData, verts, vboSize);

	// glEnd(): translate the fan to plain triangle indices (the model pipelines
	// use VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST).
	const int numIndices = (numVerts - 2) * 3;
	uint16_t indices[numIndices];

	for (int v = 1; v < numVerts - 1; v++)
	{
		indices[(v - 1) * 3 + 0] = 0;
		indices[(v - 1) * 3 + 1] = (uint16_t)v;
		indices[(v - 1) * 3 + 2] = (uint16_t)(v + 1);
	}

	VkDeviceSize iboOffset;
	const VkBuffer* ibo = UpdateIndexBuffer(indices, numIndices * sizeof(uint16_t), &iboOffset);

	// Per-draw UBO (model matrix + fog block).
	uint32_t uboOffset;
	VkDescriptorSet uboDescSet;
	QVk_GetEntityUbo(true, &uboOffset, &uboDescSet);

	const VkDescriptorSet descriptorSets[2] = { vk_currentTexture->vk_texture.descriptorSet, uboDescSet };

	QVk_BindPipeline(pipeline);
	QVk_PushEntityConstants(pipeline);
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, 2, descriptorSets, 1, &uboOffset);
	vkCmdBindVertexBuffers(vk_activeCmdbuffer, 0, 1, &vbo, &vboOffset);
	vkCmdBindIndexBuffer(vk_activeCmdbuffer, *ibo, iboOffset, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(vk_activeCmdbuffer, (uint32_t)numIndices, 1, 0, 0, 0);
}

// glPopMatrix() equivalent for R_RotateForEntity().
void QVk_RestoreModelIdentity(void)
{
	R_MatrixIdentity(&vk_modelMatrix);
}

#pragma endregion

#pragma region ========================== gl1_Misc.c PORTS ==========================

void R_SetDefaultState(void) // Q2: GL_SetDefaultState()
{
	// glClearColor(1.0f, 0.0f, 0.5f, 0.5f): render pass clear color (frame module R_Clear() / gl_clear).
	// glCullFace(GL_FRONT): baked per pipeline - VK_CULL_MODE_BACK_BIT with
	//   VK_FRONT_FACE_CLOCKWISE culls the same (CCW) triangles gl1's GL_FRONT cull did.
	// glEnable(GL_TEXTURE_2D): texturing is always on in the shaders.

	// glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.666f): H2's ambient
	// alpha-test state becomes the latched push-constant default (world
	// alpha-tested surfaces rely on it).
	vk_alphaTestRef = 0.666f;

	// glDisable(GL_DEPTH_TEST) / glDisable(GL_CULL_FACE): baked per pipeline.

	// glDisable(GL_BLEND) + R_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
	// (H2_1.07: GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR): the blend state matrix
	// lives in the pipeline inventory; latch the gl1 defaults.
	vk_entityBlendMode = ENTITY_BLEND_NONE;
	vk_fogSkipAdditive = 0;

	// GL_MULTISAMPLE: RP_WORLD sample count, from r_msaa_samples at swapchain/renderpass creation (vk_common.c).

	QVk_SetCurrentColor(1.0f, 1.0f, 1.0f, 1.0f); // glColor4f(1, 1, 1, 1).
	vk_currentTexture = NULL;

	// glPolygonMode(GL_FRONT_AND_BACK, GL_FILL): baked per pipeline; glShadeModel doesn't exist.

	R_TextureMode(gl_texturemode->string);

	// gl1's global GL_TEXTURE_MIN/MAG_FILTER + GL_TEXTURE_WRAP defaults are
	// per-texture sampler state in vk - R_TextureMode()/texture upload
	// (vk_Image.c module port) handle them.

	// R_TexEnv(GL_REPLACE): no-op (shader-determined).

	QVk_RestoreModelIdentity();

	// Fog defaults to off until the frame module port's R_Fog()/R_WaterFog()
	// fill vk_fogblock per frame (avoids a zero-initialized GL_LINEAR fog with
	// fogStart == fogEnd == 0 in the per-draw UBOs).
	vk_fogblock.fogMode = -1;
	vk_fogblock.fogSkipAdditive = 0;
}

// Q2 counterpart
void R_DrawNullModel(const entity_t* e) //mxd. Original logic uses 'currententity' global var.
{
	// nullmodel.vert vertex layout (RGB_RGB): pos + RGB color.
	typedef struct
	{
		float pos[3];
		float color[3];
	} nullvtx_t;

	if (!vk_frameStarted)
		return;

	vec3_t shadelight;

	if (e->flags & RF_FULLBRIGHT)
		VectorSet(shadelight, 1.0f, 1.0f, 1.0f);
	else
		R_LightPoint(e->origin, shadelight, false);

	// glPushMatrix() + R_RotateForEntity(e).
	R_RotateForEntity(e);

	// glDisable(GL_TEXTURE_2D) + glColor3fv(shadelight): the nullmodel pipeline
	// is untextured (nullmodel.vert + basic_color_quad.frag) - per-vertex color only.
	nullvtx_t verts[12];

	for (int i = 0; i < 12; i++)
		VectorCopy(shadelight, verts[i].color);

	// Bottom fan.
	VectorSet(verts[0].pos, 0.0f, 0.0f, -16.0f);
	for (int i = 0; i < 5; i++)
		VectorSet(verts[i + 1].pos, 16.0f * cosf((float)i * ANGLE_90), 16.0f * sinf((float)i * ANGLE_90), 0.0f); //mxd. M_PI/2 -> ANGLE_90

	// Top fan.
	VectorSet(verts[6].pos, 0.0f, 0.0f, 16.0f);
	for (int i = 4; i > -1; i--)
		VectorSet(verts[6 + 5 - i].pos, 16.0f * cosf((float)i * ANGLE_90), 16.0f * sinf((float)i * ANGLE_90), 0.0f); //mxd. M_PI/2 -> ANGLE_90

	// glBegin(GL_TRIANGLE_FAN) x2 -> one indexed triangle list (both 6-vertex fans).
	uint16_t indices[24];
	int n = 0;

	for (int fan = 0; fan < 2; fan++)
	{
		const uint16_t first = (uint16_t)(fan * 6);
		for (uint16_t v = 1; v < 5; v++)
		{
			indices[n++] = first;
			indices[n++] = first + v;
			indices[n++] = first + v + 1;
		}
	}

	// Stream and draw through the nullmodel pipeline (UBO = model matrix only).
	VkBuffer vbo;
	VkDeviceSize vboOffset;
	uint8_t* vertData = QVk_GetVertexBuffer(sizeof(verts), &vbo, &vboOffset);
	memcpy(vertData, verts, sizeof(verts));

	VkDeviceSize iboOffset;
	const VkBuffer* ibo = UpdateIndexBuffer(indices, sizeof(indices), &iboOffset);

	uint32_t uboOffset;
	VkDescriptorSet uboDescSet;
	uint8_t* uboData = QVk_GetUniformBuffer(sizeof(vk_modelMatrix.m16), &uboOffset, &uboDescSet);
	memcpy(uboData, vk_modelMatrix.m16, sizeof(vk_modelMatrix.m16));

	QVk_BindPipeline(&vk_drawNullModelPipeline);
	QVk_PushEntityConstants(&vk_drawNullModelPipeline); // Fragment grade trio stays neutral (world path).
	vkCmdBindDescriptorSets(vk_activeCmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_drawNullModelPipeline.layout, 0, 1, &uboDescSet, 1, &uboOffset);
	vkCmdBindVertexBuffers(vk_activeCmdbuffer, 0, 1, &vbo, &vboOffset);
	vkCmdBindIndexBuffer(vk_activeCmdbuffer, *ibo, iboOffset, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(vk_activeCmdbuffer, 24, 1, 0, 0, 0);

	QVk_SetCurrentColor(1.0f, 1.0f, 1.0f, 1.0f); // glColor3f(1, 1, 1).
	QVk_RestoreModelIdentity(); // glPopMatrix().
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
	// vk: build the same T * Rz * Ry * Rx matrix directly in radians (gl3/YQ2
	// rotAroundAxisZYX() math) and multiply it onto the model matrix mirror
	// (goes into the per-draw UBO instead of gl3's uni3D transModelMat4).
	const float alpha = e->angles[1];	// Around Z (yaw).
	const float beta = -e->angles[0];	// Around Y (pitch).
	const float gamma = -e->angles[2];	// Around X (roll).

	const float sinA = sinf(alpha);
	const float cosA = cosf(alpha);
	const float sinB = sinf(beta);
	const float cosB = cosf(beta);
	const float sinG = sinf(gamma);
	const float cosG = cosf(gamma);

	const matrix4_t transMat = { .m4x4 = {
		{ cosA * cosB,						  sinA * cosB,						   -sinB,		 0.0f }, // First *column*.
		{ cosA * sinB * sinG - sinA * cosG,	  sinA * sinB * sinG + cosA * cosG,		cosB * sinG, 0.0f },
		{ cosA * sinB * cosG + sinA * sinG,	  sinA * sinB * cosG - cosA * sinG,		cosB * cosG, 0.0f },
		{ e->origin[0],						  e->origin[1],							e->origin[2], 1.0f } // glTranslatef(e->origin).
	} };

	R_MatrixMultiply(&vk_modelMatrix, &vk_modelMatrix, &transMat);
}

//mxd. Map object coordinates to window coordinates (slightly modified version of glhProjectf() from https://wikis.khronos.org/opengl/GluProject_and_gluUnProject_code).
qboolean R_PointToScreen(const vec3_t pos, vec3_t screen_pos)
{
	// gl1 captured these via glGetFloatv(); the frame module port keeps
	// GL-convention copies in r_world_matrix / r_projection_matrix so this
	// math stays verbatim (the Vulkan clip-space correction lives only in
	// r_viewproj_matrix).

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
			vk_alphaTestRef = 0.0f; // glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.0f).
			vk_entityBlendMode = ENTITY_BLEND_ADD_ALPHA; // glBlendFunc(GL_SRC_ALPHA, GL_ONE).
			QVk_SetCurrentColorRGBA(e->color); // glColor4ub(e->color.r, e->color.g, e->color.b, e->color.a).
		}
		else
		{
			if ((int)r_fog->value || (int)cl_camera_under_surface->value) //mxd. Skipped gl_fog_broken check.
				vk_fogSkipAdditive = 1; // glDisable(GL_FOG).

			vk_alphaTestRef = -1.0f; // glDisable(GL_ALPHA_TEST).
			vk_entityBlendMode = ENTITY_BLEND_ADD; // glBlendFunc(GL_ONE, GL_ONE).

			if (e->flags & RF_TRANS_ADD_ALPHA)
			{
				const float scaler = (float)e->color.a / 255.0f / 255.0f; //TODO: why is it divided twice?..
				QVk_SetCurrentColor((float)e->color.r * scaler, (float)e->color.g * scaler, (float)e->color.b * scaler, 1.0f); //mxd. qglColor4f -> qglColor3f
			}
			else
			{
				QVk_SetCurrentColor((float)e->color.r / 255.0f, (float)e->color.g / 255.0f, (float)e->color.b / 255.0f, 1.0f); //mxd. qglColor4ub -> qglColor3ub
			}
		}
	}
	else
	{
		vk_alphaTestRef = 0.05f; // glEnable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.05f).
		vk_entityBlendMode = ENTITY_BLEND_STANDARD; // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).

		// H2_1.07: qglBlendFunc(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR) when RF_TRANS_GHOST flag is set.
		if (!(e->flags & RF_TRANS_GHOST))
		{
			if (e->flags & RF_LM_COLOR) //mxd
			{
				const paletteRGBA_t c = R_ModulateRGBA(e->color, R_GetSpriteShadelight(e->origin, e->color.a));
				QVk_SetCurrentColor((float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f, (float)e->color.a / 255.0f); // glColor4ub(c.r, c.g, c.b, e->color.a).
			}
			else
			{
				QVk_SetCurrentColorRGBA(e->color); // glColor4ub(e->color.r, e->color.g, e->color.b, e->color.a).
			}
		}
	}

	// glEnable(GL_BLEND): implied by vk_entityBlendMode != ENTITY_BLEND_NONE
	// (blend enable is baked into the selected pipeline).
}

void R_CleanupTransparency(const entity_t* e) // H2: CleanupTrans().
{
	vk_entityBlendMode = ENTITY_BLEND_NONE; // glDisable(GL_BLEND).

	if (e->flags & (RF_TRANS_GHOST | RF_TRANS_ADD))
	{
		if ((int)r_fog->value || (int)cl_camera_under_surface->value) //mxd. Removed gl_fog_broken cvar check.
			vk_fogSkipAdditive = 0; // glEnable(GL_FOG).

		// R_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA): latched blend func
		// restore - implicit (ENTITY_BLEND_NONE re-latches the standard func).
	}
	else
	{
		// gl1: glDisable(GL_ALPHA_TEST) + R_AlphaFunc(GL_GREATER, 0.666f) - the test ends up
		// DISABLED; 0.666 is only the latched func value for whoever enables the test next
		// (in vk every module pushes its own alphaTestRef before drawing).
		vk_alphaTestRef = -1.0f;
	}
}

#pragma endregion

#pragma region ========================== SCREENSHOTS ==========================

// Reads back a rectangle of the last-presented swapchain image (BGRA/RGBA8,
// top-down rows). Port of yq2remaster QVk_ReadPixels() (vk_image.c).
static void QVk_ReadPixels(uint8_t* dstBuffer, const VkOffset2D* offset, const VkExtent2D* extent)
{
	BufferResource_t buff;
	VkCommandBuffer cmdBuffer;

	VkBufferCreateInfo bcInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = (VkDeviceSize)extent->width * extent->height * 4,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL,
	};

	VK_VERIFY(buffer_create(&buff, bcInfo,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		0));

	cmdBuffer = QVk_CreateCommandBuffer(&vk_commandPool[vk_activeBufferIdx], VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	VK_VERIFY(QVk_BeginCommand(&cmdBuffer));

	// Transition the current swapchain image to be a source of data transfer to our buffer.
	const VkImageMemoryBarrier imgBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = vk_swapchain.images[vk_imageIndex],
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1
	};

	vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &imgBarrier);

	const VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = extent->width,
		.bufferImageHeight = extent->height,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = { offset->x, offset->y, 0 },
		.imageExtent = { extent->width, extent->height, 1 }
	};

	// Copy the swapchain image. (No transition back: the RP_UI swapchain
	// attachment is initialLayout UNDEFINED, so the next frame doesn't care.)
	vkCmdCopyImageToBuffer(cmdBuffer, vk_swapchain.images[vk_imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buff.buffer, 1, &region);
	QVk_SubmitCommand(&cmdBuffer, &vk_device.gfxQueue);
	vkFreeCommandBuffers(vk_device.logical, vk_commandPool[vk_activeBufferIdx], 1, &cmdBuffer);

	// Store image in destination buffer.
	const uint8_t* pMappedData = buffer_map(&buff);
	memcpy(dstBuffer, pMappedData, (size_t)extent->width * extent->height * 4);
	buffer_unmap(&buff);

	buffer_destroy(&buff);
}

void R_ScreenShot_f(void) // Based on YQ2 logic (yq2remaster Vk_ScreenShot_f); gl1: glReadPixels() -> ri.Vid_WriteScreenshot().
{
#define SCREENSHOT_COMP	3

	if (!vk_initialized || vk_swapchain.images == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_ScreenShot_f: Vulkan not initialized!\n");
		return;
	}

	if (!vk_device.screenshotSupported)
	{
		ri.Con_Printf(PRINT_ALL, "R_ScreenShot_f: screenshots are not supported by this GPU.\n");
		return;
	}

	const uint32_t w = min((uint32_t)viddef.width, vk_swapchain.extent.width);
	const uint32_t h = min((uint32_t)viddef.height, vk_swapchain.extent.height);

	byte* readback = malloc((size_t)w * h * 4);
	byte* buffer = malloc((size_t)w * h * SCREENSHOT_COMP);

	if (readback == NULL || buffer == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_ScreenShot_f: couldn't malloc %i bytes!\n", (int)(w * h * (4 + SCREENSHOT_COMP)));
		free(readback);
		free(buffer);

		return;
	}

	const VkExtent2D extent = { .width = w, .height = h };
	const VkOffset2D offset = {
		.x = (int32_t)((vk_swapchain.extent.width - extent.width) / 2u),
		.y = (int32_t)((vk_swapchain.extent.height - extent.height) / 2u),
	};

	QVk_ReadPixels(readback, &offset, &extent);

	// Vulkan readback rows are top-down; ri.Vid_WriteScreenshot() expects the
	// GL bottom-up order gl1's glReadPixels() produced (it flips on write).
	// Also swizzle BGRA -> RGB unless the swapchain is RGBA already.
	const qboolean is_rgba = (vk_swapchain.format == VK_FORMAT_R8G8B8A8_UNORM || vk_swapchain.format == VK_FORMAT_R8G8B8A8_SRGB);

	for (uint32_t y = 0; y < h; y++)
	{
		const byte* src = readback + (size_t)(h - 1 - y) * w * 4;
		byte* dst = buffer + (size_t)y * w * SCREENSHOT_COMP;

		for (uint32_t x = 0; x < w; x++, src += 4, dst += SCREENSHOT_COMP)
		{
			if (is_rgba)
			{
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
			}
			else // BGRA (the common swapchain format).
			{
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
			}
		}
	}

	ri.Vid_WriteScreenshot((int)w, (int)h, SCREENSHOT_COMP, buffer);

	free(readback);
	free(buffer);
}

#pragma endregion
