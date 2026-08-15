#include "compat.h"
//
// vk_Lightmap.c -- lightmap atlas building.
//
// H2 semantics from gl1_Lightmap.c via the validated gl3_Lightmap.c port
// (yq2 atlas model): 4 big BLOCK_WIDTH x BLOCK_HEIGHT atlases, each consisting
// of 4 lightstyle sub-lightmaps. The gl1 dynamic lightmap (slot 0) is gone:
// lightstyles are scaled in the fragment shader (vklmapubo_t lmScales,
// polygon_lmap.frag).
//
// vk draw submission: the 4 sub-lightmaps of an atlas live in one descriptor
// set with 4 combined image samplers (vk_samplerLightmapDescSetLayout, bound
// at set = 2 of vk_drawPolyLmapPipeline) - the analog of gl3's
// GL3_BindLightmap() tmu 1..4 binding. Atlas images are uploaded through the
// QVk staging buffer system (yq2 vk_image.c createTextureImage() technique,
// single mip, no runtime mip gen).
//
// Copyright 1998 Raven Software
//

#include "vk_World_internal.h"
#include "Hunk.h"

vklightmapstate_t gl_lms;

#pragma region ========================== ATLAS TEXTURE UPLOAD (vk backend) ==========================

// yq2 vk_image.c transitionImageLayout(), reduced to the three cases the
// lightmap atlas upload needs. All commands are recorded into the staging
// command buffer (submitted to the gfx queue before the frame command buffer -
// see QVk_SubmitStagingBuffers()); when gfx and transfer families differ,
// QVk_CreateImage() makes the image VK_SHARING_MODE_CONCURRENT, so no queue
// ownership transfer barriers are needed here.
static void LM_TransitionImageLayout(const VkCommandBuffer cmd_buffer, const qvktexture_t* texture, const VkImageLayout old_layout, const VkImageLayout new_layout)
{
	VkImageMemoryBarrier img_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = texture->resource.image,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = texture->mipLevels
	};

	VkPipelineStageFlags src_stage;
	VkPipelineStageFlags dst_stage;

	if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		// Fresh image -> copy destination.
		img_barrier.srcAccessMask = 0;
		img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		// Existing atlas re-uploaded on map change.
		img_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		img_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		// Copy done -> sampled in the lightmapped fragment shader.
		img_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		img_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else
	{
		ri.Sys_Error(ERR_DROP, "LM_TransitionImageLayout(): unsupported layout transition (%i -> %i)", old_layout, new_layout);
		return;
	}

	vkCmdPipelineBarrier(cmd_buffer, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &img_barrier);
}

// Uploads one BLOCK_WIDTH x BLOCK_HEIGHT RGBA lightstyle sub-lightmap,
// creating the VkImage/view on first use (they are fixed-size and reused
// across map loads / vid_restarts - only the texel data changes).
static void LM_UploadSubLightmap(qvktexture_t* texture, const byte* data)
{
	const uint32_t upload_size = BLOCK_WIDTH * BLOCK_HEIGHT * LIGHTMAP_BYTES;
	const qboolean first_use = (texture->resource.image == VK_NULL_HANDLE);

	// Fetch the staging slice FIRST: QVk_GetStagingBuffer() may submit the
	// current staging command buffer and hand out the next one.
	VkCommandBuffer cmd_buffer;
	VkBuffer staging_buffer;
	uint32_t staging_offset;
	uint8_t* staging_data = QVk_GetStagingBuffer(upload_size, 4, &cmd_buffer, &staging_buffer, &staging_offset);
	memcpy(staging_data, data, upload_size);

	if (first_use)
	{
		QVVKTEXTURE_CLEAR(*texture);
		texture->format = VK_FORMAT_R8G8B8A8_UNORM; // GL_LIGHTMAP_FORMAT == GL_RGBA parity.
		texture->mipLevels = 1; // gl1: GL_LINEAR, no mipmaps.

		VK_VERIFY(QVk_CreateImage(BLOCK_WIDTH, BLOCK_HEIGHT, texture->format,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture));

		LM_TransitionImageLayout(cmd_buffer, texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}
	else
	{
		LM_TransitionImageLayout(cmd_buffer, texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}

	const VkBufferImageCopy region = {
		.bufferOffset = staging_offset,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = { 0, 0, 0 },
		.imageExtent = { BLOCK_WIDTH, BLOCK_HEIGHT, 1 }
	};

	vkCmdCopyBufferToImage(cmd_buffer, staging_buffer, texture->resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	LM_TransitionImageLayout(cmd_buffer, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	if (first_use)
	{
		VK_VERIFY(QVk_CreateImageView(&texture->resource.image, VK_IMAGE_ASPECT_COLOR_BIT, &texture->imageView, texture->format, texture->mipLevels));

		QVk_DebugSetObjectName((uint64_t)texture->resource.image, VK_OBJECT_TYPE_IMAGE, "Image: Lightmap Atlas");
		QVk_DebugSetObjectName((uint64_t)texture->imageView, VK_OBJECT_TYPE_IMAGE_VIEW, "Image View: Lightmap Atlas");
	}
}

// (Re)creates the combined 4-sampler descriptor set for the given atlas
// (sampler2D sLightmap[4] at set = 2, binding 0 of polygon_lmap.frag).
static void LM_UpdateAtlasDescriptorSet(const int atlas)
{
	qvktexture_t* subs = vk_state.lightmap_textures[atlas];
	VkDescriptorSet* desc_set = &gl_lms.lightmap_descriptor_sets[atlas];

	if (*desc_set == VK_NULL_HANDLE)
	{
		VkDescriptorSetAllocateInfo ds_alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = NULL,
			.descriptorPool = vk_descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &vk_samplerLightmapDescSetLayout
		};

		VK_VERIFY(vkAllocateDescriptorSets(vk_device.logical, &ds_alloc_info, desc_set));
		QVk_DebugSetObjectName((uint64_t)*desc_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Descriptor Set: Lightmap Atlas x4");
	}

	// Fetch the shared linear/clamp sampler: QVk_UpdateTextureSampler() writes
	// array element 0 of the set (gl1 lightmaps are GL_LINEAR filtered) and
	// returns the sampler handle for the manual 4-element write below.
	for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
		subs[map].descriptorSet = *desc_set;

	const VkSampler sampler = QVk_UpdateTextureSampler(&subs[0], S_LINEAR, true);

	VkDescriptorImageInfo img_infos[MAX_LIGHTMAPS_PER_SURFACE];
	for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
	{
		img_infos[map].sampler = sampler;
		img_infos[map].imageView = subs[map].imageView;
		img_infos[map].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	const VkWriteDescriptorSet write_set = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = NULL,
		.dstSet = *desc_set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = MAX_LIGHTMAPS_PER_SURFACE,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = img_infos,
		.pBufferInfo = NULL,
		.pTexelBufferView = NULL
	};

	vkUpdateDescriptorSets(vk_device.logical, 1, &write_set, 0, NULL);
}

// vk: frees all lightmap atlas resources. Call while the device is idle
// (RI_ShutdownContext() flow / R_ShutdownImages()) - the QVk core doesn't know
// about module-owned textures.
void LM_ShutdownLightmaps(void)
{
	for (int atlas = 0; atlas < MAX_LIGHTMAPS; atlas++)
	{
		for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
		{
			qvktexture_t* texture = &vk_state.lightmap_textures[atlas][map];

			if (texture->imageView != VK_NULL_HANDLE)
			{
				vkDestroyImageView(vk_device.logical, texture->imageView, NULL);
				texture->imageView = VK_NULL_HANDLE;
			}

			if (texture->resource.image != VK_NULL_HANDLE)
				image_destroy(&texture->resource);

			QVVKTEXTURE_CLEAR(*texture);
			texture->descriptorSet = VK_NULL_HANDLE;
		}

		if (gl_lms.lightmap_descriptor_sets[atlas] != VK_NULL_HANDLE)
		{
			vkFreeDescriptorSets(vk_device.logical, vk_descriptorPool, 1, &gl_lms.lightmap_descriptor_sets[atlas]);
			gl_lms.lightmap_descriptor_sets[atlas] = VK_NULL_HANDLE;
		}
	}
}

#pragma endregion

#pragma region ========================== LIGHTMAP ALLOCATION ==========================

// Q2 counterpart
void LM_InitBlock(void)
{
	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));
}

// Q2 counterpart (gl1 had a 'dynamic' arg; the gl3/vk lightmap model has no dynamic block -
// all lightmaps are uploaded at level load and never change afterwards).
void LM_UploadBlock(void)
{
	const int atlas = gl_lms.current_lightmap_texture;

	// Upload all 4 lightstyle sub-lightmaps. // YQ2
	for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++)
		LM_UploadSubLightmap(&vk_state.lightmap_textures[atlas][map], gl_lms.lightmap_buffers[map]);

	// gl3: GL3_BindLightmap() binds the 4 sub-lightmaps to tmu 1..4 at draw
	// time; vk: one combined descriptor set per atlas, created here.
	LM_UpdateAtlasDescriptorSet(atlas);

	gl_lms.current_lightmap_texture++;

	if (gl_lms.current_lightmap_texture == MAX_LIGHTMAPS)
		ri.Sys_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
}

// Q2 counterpart. Returns a texture number and the position inside it.
qboolean LM_AllocBlock(const int w, const int h, int* x, int* y)
{
	int j;
	int best = BLOCK_HEIGHT;

	for (int i = 0; i < BLOCK_WIDTH - w; i++)
	{
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (gl_lms.allocated[i + j] >= best)
				break;

			if (gl_lms.allocated[i + j] > best2)
				best2 = gl_lms.allocated[i + j];
		}

		if (j == w)
		{
			// This is a valid spot.
			*x = i;
			*y = best2;
			best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
		return false;

	for (int i = 0; i < w; i++)
		gl_lms.allocated[*x + i] = best + h;

	return true;
}

#pragma endregion

#pragma region ========================== LIGHTMAP BUILDING ==========================

// Q2 counterpart
void LM_BuildPolygonFromSurface(const model_t* mdl, msurface_t* fa) //mxd. Original logic uses 'currentmodel' global var here.
{
	// Reconstruct the polygon.
	const medge_t* pedges = mdl->edges;
	const int lnumverts = fa->numedges;

	// Draw texture.
	glpoly_t* poly = Hunk_Alloc((int)sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (int i = 0; i < lnumverts; i++)
	{
		const int lindex = mdl->surfedges[fa->firstedge + i];

		float* vec;
		if (lindex > 0)
		{
			const medge_t* r_pedge = &pedges[lindex];
			vec = mdl->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			const medge_t* r_pedge = &pedges[-lindex];
			vec = mdl->vertexes[r_pedge->v[1]].position;
		}

		float s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= (float)fa->texinfo->image->width;

		float t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= (float)fa->texinfo->image->height;

		VectorCopy(vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// Lightmap texture coordinates.
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= (float)fa->texturemins[0];
		s += (float)fa->light_s * 16;
		s += 8;
		s /= BLOCK_WIDTH * 16;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= (float)fa->texturemins[1];
		t += (float)fa->light_t * 16;
		t += 8;
		t /= BLOCK_HEIGHT * 16;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}
}

// Q2 counterpart
void LM_CreateSurfaceLightmap(msurface_t* surf)
{
	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
		return;

	const int smax = ((int)surf->extents[0] >> 4) + 1;
	const int tmax = ((int)surf->extents[1] >> 4) + 1;

	if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
	{
		LM_UploadBlock();
		LM_InitBlock();

		if (!LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
			ri.Sys_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n", smax, tmax);
	}

	surf->lightmaptexturenum = gl_lms.current_lightmap_texture;

	R_SetCacheState(surf);
	R_BuildLightMap(surf, (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES, BLOCK_WIDTH * LIGHTMAP_BYTES);
}

// Q2 counterpart
void LM_BeginBuildingLightmaps(void) //mxd. Removed unused model_t* arg.
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];

	memset(gl_lms.allocated, 0, sizeof(gl_lms.allocated));

	r_framecount = 1; // No dlightcache.

	// Setup the base lightstyles so the lightmaps won't have to be regenerated the first time they're seen.
	for (int i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1.0f;
		lightstyles[i].rgb[1] = 1.0f;
		lightstyles[i].rgb[2] = 1.0f;
		lightstyles[i].white = 3.0f;
	}

	r_newrefdef.lightstyles = lightstyles;

	// gl3/vk: no dynamic lightmap (gl1 slot 0) - static atlases start at 0.
	// The VkImages are created lazily by the first LM_UploadBlock() and reused
	// (fixed BLOCK_WIDTH x BLOCK_HEIGHT size) across map loads.
	gl_lms.current_lightmap_texture = 0;
}

// Q2 counterpart
void LM_EndBuildingLightmaps(void)
{
	LM_UploadBlock();
}

#pragma endregion
