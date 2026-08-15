#include "compat.h"
//
// vk_Image.c -- image loading and texture management for the Vulkan renderer.
//
// H2 semantics ported from gl3_Image.c (the validated H2 port of gl1_Image.c:
// palette handling, .m8/.m32 loading, name hash, image types/filtering,
// registration-sequence GC); Vulkan texture technique from yq2remaster
// vk_image.c (staging upload, layout transitions, per-texture descriptor set,
// sampler attachment, deferred-release GC).
//
// Deviations from gl1/gl3 (per CONTRACT.md):
// - NO texture-baked gamma: images upload raw, H2 gamma/brightness/contrast run
//   in the fragment shaders (vk_gradePush), nothing ever needs re-uploading.
// - GL texture names/bind caching replaced by qvktexture_t objects: VkImage +
//   VkImageView + per-texture VkDescriptorSet (combined image sampler).
// - .m8/.m32 files ship complete mip chains - ALL file mip levels are uploaded
//   into the VkImage (one vkCmdCopyBufferToImage region per level); there is NO
//   runtime mip generation (yq2's generateMipmaps() path is intentionally absent).
// - gl_texturemode's GL filter pairs map onto the backend's fixed sampler set
//   (vk_common.c CreateSamplers()); R_TextureMode() re-points the per-image
//   descriptor sets at the requested sampler (yq2 Vk_TextureMode technique).
//
// Copyright 1998 Raven Software
//

#include "vk_Image_internal.h"
#include "qfiles.h"

// NOTE: vktextures[] / numvktextures / the permanent image pointers (r_notexture,
// r_font1, ...) are defined in vk_Stubs.c ("shared module globals") - this module
// only fills them (vk_Local.h externs).

#define NUM_HASHED_VKTEXTURES	256
static image_t* vktextures_hashed[NUM_HASHED_VKTEXTURES]; // H2

extern image_t* draw_chars; // Defined in vk_Draw.c (gl1: gl1_Draw.c).

extern void R_InitMinlight(void); // YQ2. Implemented in vk_Light.c (gl1: gl1_Light.c).

//mxd
static paletteRGBA_t* upload_buffer = NULL;
static uint upload_buffer_size = 0;

// gl1/gl3 kept a (min, mag) GL filter pair (gl_filter_min/gl_filter_max); the vk
// backend has a fixed sampler inventory instead, so each gl_texturemode name maps
// to the closest qvksampler_t (mag filter dominates: the GL_LINEAR_MIPMAP_* modes
// use the linear sampler). Applies to mipmapped textures only - it_pic is always
// S_NEAREST, it_sky always S_LINEAR (gl1 R_SetFilter() semantics).
typedef struct
{
	char* name;
	qvksampler_t sampler;
} vkglmode_t;

static const vkglmode_t vk_gl_modes[] =
{
	{ "GL_NEAREST", S_NEAREST },
	{ "GL_LINEAR", S_LINEAR },
	{ "GL_NEAREST_MIPMAP_NEAREST", S_MIPMAP_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST", S_MIPMAP_LINEAR },
	{ "GL_NEAREST_MIPMAP_LINEAR", S_MIPMAP_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR", S_MIPMAP_LINEAR }
};

#define NUM_VK_GL_MODES ((int)(sizeof(vk_gl_modes) / sizeof(vk_gl_modes[0]))) //mxd. Added int cast.

// Current sampler for mipmapped textures (set by R_TextureMode from gl_texturemode).
static qvksampler_t vk_current_sampler = S_MIPMAP_LINEAR;

// NOTE: gl1's R_InitGammaTable() is intentionally gone: the exact same math now runs
// per-fragment in the shaders (H2ColorGrade() - see shaders/basic.frag).

//mxd. Part of GL_LoadPic logic in Q2
image_t* R_GetFreeImage(void) // H2: GL_GetFreeImage().
{
	int index;
	image_t* image;

	// Find a free image_t
	for (index = 0, image = &vktextures[0]; index < numvktextures; index++, image++)
		if (image->registration_sequence == 0)
			break;

	if (index == numvktextures)
	{
		if (numvktextures == MAX_VKTEXTURES)
			ri.Sys_Error(ERR_DROP, "R_GetFreeImage: no free image_t slots!\n"); //mxd. Sys_Error() -> ri.Sys_Error().

		numvktextures++;
	}

	memset(image, 0, sizeof(image_t));
	QVVKTEXTURE_CLEAR(image->vk_texture); // Restore valid defaults (format, sampleCount = 1, mipLevels = 1) after the memset.

	return image;
}

#pragma region ========================== VULKAN TEXTURE UPLOAD CORE (YQ2 vk_image.c) ==========================

/* internal helper (yq2) */
static void transitionImageLayout(const VkCommandBuffer* cmdBuffer, const VkQueue* queue, const qvktexture_t* texture, const VkImageLayout oldLayout, const VkImageLayout newLayout)
{
	VkPipelineStageFlags srcStage = 0;
	VkPipelineStageFlags dstStage = 0;

	VkImageMemoryBarrier imgBarrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = NULL,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = texture->resource.image,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel = 0, // All mip levels transition together (they all come from the file).
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = texture->mipLevels
	};

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		imgBarrier.srcAccessMask = 0;
		imgBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	// Transition that may occur when updating an existing image (cinematic streaming).
	else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		imgBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imgBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		if (vk_device.transferQueue == vk_device.gfxQueue)
		{
			imgBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else
		{
			if (vk_device.transferQueue == *queue)
			{
				// If the image is exclusively shared, start queue ownership transfer process (release) - only for VK_SHARING_MODE_EXCLUSIVE.
				imgBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imgBarrier.dstAccessMask = 0;
				imgBarrier.srcQueueFamilyIndex = vk_device.transferFamilyIndex;
				imgBarrier.dstQueueFamilyIndex = vk_device.gfxFamilyIndex;
				srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			}
			else
			{
				// Continuing queue transfer (acquisition) - this will only happen for VK_SHARING_MODE_EXCLUSIVE images.
				if (texture->sharingMode == VK_SHARING_MODE_EXCLUSIVE)
				{
					imgBarrier.srcAccessMask = 0;
					imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					imgBarrier.srcQueueFamilyIndex = vk_device.transferFamilyIndex;
					imgBarrier.dstQueueFamilyIndex = vk_device.gfxFamilyIndex;
					srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
					dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}
				else
				{
					imgBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
					dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				}
			}
		}
	}
	else
	{
		assert(0 && !"Invalid image stage!");
	}

	vkCmdPipelineBarrier(*cmdBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &imgBarrier);
}

// Creates a VkImage with 'num_mips' mip levels and uploads pre-staged pixel data
// into every level (one VkBufferImageCopy region per level). The pixel data for
// level i must already sit in 'staging_buffer' at 'staging_offset + mip_offsets[i]'
// (tightly packed RGBA8). This replaces yq2's createTextureImage() +
// generateMipmaps() pair: H2 .m8/.m32 files ship complete mip chains, so all
// levels come from the file and no blit-based mip generation is needed (CONTRACT.md).
static void QVk_CreateTextureMips(qvktexture_t* texture, const VkCommandBuffer command_buffer, const VkBuffer staging_buffer, const uint32_t staging_offset,
	const int num_mips, const uint32_t* mip_widths, const uint32_t* mip_heights, const VkDeviceSize* mip_offsets)
{
	VkBufferImageCopy regions[MIPLEVELS];
	const int unifiedTransferAndGfx = (vk_device.transferQueue == vk_device.gfxQueue ? 1 : 0);

	texture->format = VK_FORMAT_R8G8B8A8_UNORM;
	texture->sampleCount = VK_SAMPLE_COUNT_1_BIT;
	texture->mipLevels = num_mips;

	// No VK_IMAGE_USAGE_TRANSFER_SRC_BIT: yq2 needed it for mip-to-mip blits; all our mips upload from the staging buffer.
	VK_VERIFY(QVk_CreateImage(mip_widths[0], mip_heights[0], texture->format,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture));

	transitionImageLayout(&command_buffer, &vk_device.transferQueue, texture, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Copy staging buffer to image - all mip levels in one call.
	for (int mip = 0; mip < num_mips; mip++)
	{
		VkBufferImageCopy* region = &regions[mip];

		memset(region, 0, sizeof(VkBufferImageCopy));
		region->bufferOffset = staging_offset + mip_offsets[mip];
		region->bufferRowLength = 0;
		region->bufferImageHeight = 0;
		region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region->imageSubresource.mipLevel = mip;
		region->imageSubresource.baseArrayLayer = 0;
		region->imageSubresource.layerCount = 1;
		region->imageExtent.width = mip_widths[mip];
		region->imageExtent.height = mip_heights[mip];
		region->imageExtent.depth = 1;
	}

	vkCmdCopyBufferToImage(command_buffer, staging_buffer, texture->resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_mips, regions);

	// yq2 createTextureImage() epilogue: for non-unified transfer and graphics,
	// this step begins queue ownership transfer to the graphics queue (for exclusive sharing only).
	if (unifiedTransferAndGfx || texture->sharingMode == VK_SHARING_MODE_EXCLUSIVE)
		transitionImageLayout(&command_buffer, &vk_device.transferQueue, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	if (!unifiedTransferAndGfx)
		transitionImageLayout(&command_buffer, &vk_device.gfxQueue, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// Creates the image view and the per-texture descriptor set (combined image
// sampler, vk_samplerDescSetLayout) and attaches the requested sampler -
// second half of yq2's QVk_CreateTexture().
static void QVk_CreateTextureDescriptorSet(qvktexture_t* texture, const qvksampler_t samplerType, const qboolean clampToEdge)
{
	VK_VERIFY(QVk_CreateImageView(&texture->resource.image, VK_IMAGE_ASPECT_COLOR_BIT, &texture->imageView, texture->format, texture->mipLevels));

	// Create descriptor set for the texture.
	VkDescriptorSetAllocateInfo dsAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = NULL,
		.descriptorPool = vk_descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_samplerDescSetLayout
	};

	VK_VERIFY(vkAllocateDescriptorSets(vk_device.logical, &dsAllocInfo, &texture->descriptorSet));

	// Attach sampler.
	QVk_UpdateTextureSampler(texture, samplerType, clampToEdge);
}

// yq2 QVk_CreateTexture(): single-level RGBA texture (cinematic frames, fallback
// texture; also used by the lightmap module for the lightmap atlases).
void QVk_CreateTexture(qvktexture_t* texture, const byte* data, const uint32_t width, const uint32_t height, const qvksampler_t samplerType, const qboolean clampToEdge)
{
	VkBuffer staging_buffer;
	VkCommandBuffer command_buffer;
	uint32_t staging_offset;
	const uint32_t mip_width = width;
	const uint32_t mip_height = height;
	const VkDeviceSize mip_offset = 0;

	if (width == 0 || height == 0 || width > UINT32_MAX / height / 4)
		ri.Sys_Error(ERR_FATAL, "%s: invalid dimensions: %u x %u", __func__, width, height); //mxd. Sys_Error() -> ri.Sys_Error().

	// Assuming 32bit images.
	const uint32_t image_size = width * height * 4;

	byte* staged = QVk_GetStagingBuffer(image_size, 4, &command_buffer, &staging_buffer, &staging_offset);
	if (staged == NULL)
	{
		ri.Sys_Error(ERR_FATAL, "%s: staging buffer is smaller than image: %u", __func__, image_size);
		return;
	}

	memcpy(staged, data, image_size);

	QVk_CreateTextureMips(texture, command_buffer, staging_buffer, staging_offset, 1, &mip_width, &mip_height, &mip_offset);
	QVk_CreateTextureDescriptorSet(texture, samplerType, clampToEdge);
}

// yq2 QVk_UpdateTextureData(): streams new RGBA data into (a region of) an
// existing single-level texture - the cinematic per-frame upload path.
void QVk_UpdateTextureData(qvktexture_t* texture, const byte* data, const uint32_t offset_x, const uint32_t offset_y, const uint32_t width, const uint32_t height)
{
	VkBuffer staging_buffer;
	VkCommandBuffer command_buffer;
	uint32_t staging_offset;
	const int unifiedTransferAndGfx = (vk_device.transferQueue == vk_device.gfxQueue ? 1 : 0);

	// Assuming 32bit images.
	const uint32_t image_size = width * height * 4;

	byte* staged = QVk_GetStagingBuffer(image_size, 4, &command_buffer, &staging_buffer, &staging_offset);
	if (staged == NULL)
	{
		ri.Sys_Error(ERR_FATAL, "%s: staging buffer is smaller than image: %u", __func__, image_size);
		return;
	}

	memcpy(staged, data, image_size);

	transitionImageLayout(&command_buffer, &vk_device.transferQueue, texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Copy buffer to image.
	VkBufferImageCopy region = {
		.bufferOffset = staging_offset,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount = 1,
		.imageOffset = { (int32_t)offset_x, (int32_t)offset_y, 0 },
		.imageExtent = { width, height, 1 }
	};

	vkCmdCopyBufferToImage(command_buffer, staging_buffer, texture->resource.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	// (Streamed textures are always single-level - no mip regeneration; yq2 parity.)
	if (unifiedTransferAndGfx || texture->sharingMode == VK_SHARING_MODE_EXCLUSIVE)
		transitionImageLayout(&command_buffer, &vk_device.transferQueue, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	if (!unifiedTransferAndGfx)
		transitionImageLayout(&command_buffer, &vk_device.gfxQueue, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// yq2 QVk_ReleaseTexture(): frees image + view + descriptor set. 'tosync' flushes
// pending staging uploads and waits for the frame fences (device idle) first, so
// resources still referenced by in-flight frames are never destroyed early.
void QVk_ReleaseTexture(qvktexture_t* texture, const qboolean tosync)
{
	if (tosync)
	{
		QVk_SubmitStagingBuffers();
		if (vk_device.logical != VK_NULL_HANDLE)
			vkDeviceWaitIdle(vk_device.logical);
	}

	if (texture->imageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(vk_device.logical, texture->imageView, NULL);
		texture->imageView = VK_NULL_HANDLE;
	}

	if (texture->resource.image != VK_NULL_HANDLE)
		image_destroy(&texture->resource);

	if (texture->descriptorSet != VK_NULL_HANDLE)
		vkFreeDescriptorSets(vk_device.logical, vk_descriptorPool, 1, &texture->descriptorSet);

	texture->descriptorSet = VK_NULL_HANDLE;
	QVVKTEXTURE_CLEAR(*texture); // Restore valid defaults for potential reuse.
}

// yq2 QVk_ReadPixels(): copies a region of the current swapchain image into
// dstBuffer (RGBA/BGRA in swapchain format order) - screenshot readback path.
void QVk_ReadPixels(uint8_t* dstBuffer, const VkOffset2D* offset, const VkExtent2D* extent)
{
	BufferResource_t buff;
	const uint8_t* pMappedData;
	VkCommandBuffer cmdBuffer;

	VkBufferCreateInfo bcInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = extent->width * extent->height * 4,
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
	VkImageMemoryBarrier imgBarrier = {
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

	VkBufferImageCopy region = {
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

	// Copy the swapchain image.
	vkCmdCopyImageToBuffer(cmdBuffer, vk_swapchain.images[vk_imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buff.buffer, 1, &region);
	QVk_SubmitCommand(&cmdBuffer, &vk_device.gfxQueue);

	// Store image in destination buffer.
	pMappedData = buffer_map(&buff);
	memcpy(dstBuffer, pMappedData, extent->width * extent->height * 4);
	buffer_unmap(&buff);

	buffer_destroy(&buff);
}

#pragma endregion

#pragma region ========================== TEXTURE FILTERING ==========================

// gl1 R_SetFilter() semantics on the vk sampler inventory: it_pic is always
// nearest (H2_1.07: GL_LINEAR), it_sky is always linear non-mipmapped
// (gl1: gl_filter_max for both min and mag), everything else follows
// gl_texturemode (mipmapped).
static qvksampler_t GetImageSampler(const image_t* image)
{
	switch (image->type)
	{
		case it_pic:
			return S_NEAREST;

		case it_sky:
			return S_LINEAR;

		default:
			return vk_current_sampler;
	}
}

// Re-points the per-image descriptor set at the sampler appropriate for the image
// type. Callers must guarantee the descriptor set is not referenced by an
// in-flight frame (creation time / after vkDeviceWaitIdle).
void R_SetFilter(image_t* image)
{
	if (image->vk_texture.descriptorSet != VK_NULL_HANDLE)
		QVk_UpdateTextureSampler(&image->vk_texture, GetImageSampler(image), image->vk_texture.clampToEdge);
}

void R_TextureMode(const char* string) // Q2: GL_TextureMode()
{
	int cur_mode;

	for (cur_mode = 0; cur_mode < NUM_VK_GL_MODES; cur_mode++)
		if (!Q_stricmp(vk_gl_modes[cur_mode].name, string))
			break;

	if (cur_mode == NUM_VK_GL_MODES)
	{
		ri.Con_Printf(PRINT_ALL, "Bad texture filter name\n"); // H2: text change.
		return;
	}

	vk_current_sampler = vk_gl_modes[cur_mode].sampler;

	if (!vk_initialized)
		return;

	// yq2 Vk_TextureMode(): the descriptor sets about to be rewritten may still be
	// referenced by in-flight frames - wait for the device to go idle first.
	vkDeviceWaitIdle(vk_device.logical);

	// Change all the existing mipmap texture objects.
	image_t* image = &vktextures[0];
	for (int i = 0; i < numvktextures; i++, image++)
	{
		if (image->registration_sequence == 0) // gl3: skip free slots.
			continue;

		if (image->vk_texture.resource.image == VK_NULL_HANDLE)
			continue;

		if (image->type != it_pic && image->type != it_sky) // Mipmapped texture.
			R_SetFilter(image);
	}

	// NOTE: r_anisotropic is baked into the backend sampler objects at QVk_Init()
	// time (vk_common.c CreateSamplers(), yq2 parity) - runtime r_anisotropic
	// changes take effect after vid_restart.
}

#pragma endregion

void R_ImageList_f(void) // Q2: GL_ImageList_f()
{
	int tex_count = 0;
	int tex_texels = 0;
	int sky_count = 0;
	int sky_texels = 0;
	int skin_count = 0;
	int skin_texels = 0;
	int sprite_count = 0;
	int sprite_texels = 0;
	int pic_count = 0;
	int pic_texels = 0;

	const char* palstrings[] = { "RGB", "PAL" };

	ri.Con_Printf(PRINT_ALL, "---------------------------\n"); //mxd. Com_Printf() -> ri.Con_Printf() (here and below).

	image_t* image = &vktextures[0];
	for (int i = 0; i < numvktextures; i++, image++)
	{
		switch (image->type)
		{
			case it_skin:
				ri.Con_Printf(PRINT_ALL, "M");
				skin_count++;
				skin_texels += image->width * image->height;
				break;

			case it_sprite:
				ri.Con_Printf(PRINT_ALL, "S");
				sprite_count++;
				sprite_texels += image->width * image->height;
				break;

			//mxd. Original code also handles types 3 and 7 here. These aren't used anywhere else in the code.
			case it_wall:
				ri.Con_Printf(PRINT_ALL, "W");
				tex_count++;
				tex_texels += (image->width * image->height * 4) / 3;
				break;

			case it_pic:
				ri.Con_Printf(PRINT_ALL, "P");
				pic_count++;
				pic_texels += image->width * image->height;
				break;

			case it_sky:
				ri.Con_Printf(PRINT_ALL, "K"); //mxd. Was also "P" in original logic.
				sky_count++;
				sky_texels += image->width * image->height;
				break;

			default: //mxd. Added to silence compiler warning.
				ri.Con_Printf(PRINT_ALL, "U%i", image->type);
				break;
		}

		ri.Con_Printf(PRINT_ALL, " %3i %3i %s %s\n", image->width, image->height, palstrings[image->palette != NULL], image->name);
	}

	ri.Con_Printf(PRINT_ALL, "-------------------------------\n");
	ri.Con_Printf(PRINT_ALL, "Total skin   : %i (%i texels)\n", skin_count, skin_texels);
	ri.Con_Printf(PRINT_ALL, "Total world  : %i (%i texels)\n", tex_count, tex_texels);
	ri.Con_Printf(PRINT_ALL, "Total sky    : %i (%i texels)\n", sky_count, sky_texels);
	ri.Con_Printf(PRINT_ALL, "Total sprite : %i (%i texels)\n", sprite_count, sprite_texels);
	ri.Con_Printf(PRINT_ALL, "Total pic    : %i (%i texels)\n", pic_count, pic_texels);
	ri.Con_Printf(PRINT_ALL, "-------------------------------\n");
}

#pragma region ========================== CINEMATIC STREAMING UPLOAD ==========================

// Creates the vk texture for a streamed (cinematic) image on first use, updates
// its contents afterwards. Sampler is attached at creation (it_pic = S_NEAREST).
static void R_StreamTexture(image_t* image, const byte* rgba, const int width, const int height)
{
	if (image->vk_texture.resource.image == VK_NULL_HANDLE)
		QVk_CreateTexture(&image->vk_texture, rgba, width, height, GetImageSampler(image), false);
	else
		QVk_UpdateTextureData(&image->vk_texture, rgba, 0, 0, width, height);
}

//mxd. Somewhat similar to Q2's GL_Upload8(). gl3: R_UploadPaletted(level, ...) operating
// on the bound texture; vk: the target image is passed explicitly (create-or-stream).
void R_UploadPaletted(image_t* image, const byte* data, const paletteRGB_t* palette, const int width, const int height) // H2: GL_UploadPaletted().
{
	const uint src_size = width * height;
	const uint dst_size = src_size * sizeof(paletteRGBA_t);

	//mxd. Use dynamically allocated buffer (original logic uses fixed-size 65536 (256 * 256) buffer instead).
	if (dst_size > upload_buffer_size)
	{
		upload_buffer = realloc(upload_buffer, dst_size);

		if (upload_buffer == NULL)
			ri.Sys_Error(ERR_DROP, "R_UploadPaletted: failed to allocate upload buffer for %i x %i image!\n", width, height);

		upload_buffer_size = dst_size;
	}

	for (uint i = 0; i < src_size; i++)
	{
		const paletteRGB_t* src_p = &palette[data[i]];
		paletteRGBA_t* dst_p = &upload_buffer[i];

		// Copy rgb components.
		dst_p->r = src_p->r;
		dst_p->g = src_p->g;
		dst_p->b = src_p->b;
		dst_p->a = 255;
	}

	R_StreamTexture(image, (const byte*)upload_buffer, width, height);
}

// Upload a tightly-packed 32-bit RGBA frame (used by the MPEG cinematic path). --morb
void R_UploadRGBA(image_t* image, const byte* rgba, const int width, const int height)
{
	R_StreamTexture(image, rgba, width, height);
}

#pragma endregion

#pragma region ========================== .M8 LOADING ==========================

static void GrabPalette(const paletteRGB_t* src, paletteRGB_t* dst) // H2
{
	// gl1 pushed every palette entry through gammatable[] here; vk applies the H2
	// gamma/brightness/contrast grade in the fragment shaders instead (CONTRACT.md),
	// so the palette is kept raw (gl3 parity).
	memcpy(dst, src, sizeof(paletteRGB_t) * PAL_SIZE);
}

//mxd. Gross hack to fix lensflare sprites. //TODO: fix image files instead?
static void FixPalette(const image_t* image)
{
	if (image->type != it_sprite || strstr(image->name, "Sprites/lens/flare") == NULL)
		return;

	if (strstr(image->name, "flare1_0.m8") != NULL) // Remap BG color from [7 7 5] to [0 0 0].
	{
		memset(&image->palette[192], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare2_0.m8") != NULL) // Remap BG colors from [5 5 3] and [20 20 12] to [0 0 0].
	{
		memset(&image->palette[187], 0, sizeof(paletteRGB_t));
		memset(&image->palette[188], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare3_0.m8") != NULL) // Remap BG color from [6 6 4] to [0 0 0].
	{
		memset(&image->palette[208], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare4_0.m8") != NULL) // Remap BG color from [7 7 5] to [0 0 0].
	{
		memset(&image->palette[198], 0, sizeof(paletteRGB_t));
	}
	else if (strstr(image->name, "flare5_0.m8") != NULL) // Remap BG colors from [20 20 16] and [22 22 18] to [0 0 0].
	{
		memset(&image->palette[184], 0, sizeof(paletteRGB_t));
		memset(&image->palette[185], 0, sizeof(paletteRGB_t));
	}
}

// Uploads the .m8 file's complete mip chain: each paletted level is expanded to
// RGBA directly into the staging buffer (gl3 R_UploadPaletted() per level), then
// all levels are copied into the VkImage. Returns false when no valid mip levels
// were found (caller falls back to r_notexture).
static qboolean R_UploadM8(const miptex_t* mt, const int filesize, image_t* image) // H2: GL_Upload8M().
{
	uint32_t mip_widths[MIPLEVELS];
	uint32_t mip_heights[MIPLEVELS];
	VkDeviceSize mip_offsets[MIPLEVELS];
	VkBuffer staging_buffer;
	VkCommandBuffer command_buffer;
	uint32_t staging_offset;
	VkDeviceSize total_size = 0;

	int num_mips = 0;
	for (int mip = 0; mip < MIPLEVELS && mt->width[mip] > 0 && mt->height[mip] > 0; mip++)
	{
		const uint mip_size = mt->width[mip] * mt->height[mip];
		if ((int)(mt->offsets[mip] + mip_size) > filesize) // Bounds check --morb
		{
			ri.Con_Printf(PRINT_ALL, "R_UploadM8: mip %i offset %u out of bounds (filesize %i) for '%s'\n", mip, mt->offsets[mip], filesize, image->name);
			break;
		}

		// vk (unlike GL) requires exact half-size mip extents - stop the chain on malformed levels.
		if (mt->width[mip] != max(mt->width[0] >> mip, 1u) || mt->height[mip] != max(mt->height[0] >> mip, 1u))
		{
			ri.Con_Printf(PRINT_ALL, "R_UploadM8: mip %i has non-halving size (%u x %u) for '%s'\n", mip, mt->width[mip], mt->height[mip], image->name);
			break;
		}

		mip_widths[num_mips] = mt->width[mip];
		mip_heights[num_mips] = mt->height[mip];
		mip_offsets[num_mips] = total_size;
		total_size += (VkDeviceSize)mip_size * sizeof(paletteRGBA_t);
		num_mips++;
	}

	if (num_mips == 0)
		return false;

	// Stage the whole mip chain in one staging allocation.
	byte* staged = QVk_GetStagingBuffer(total_size, 4, &command_buffer, &staging_buffer, &staging_offset);

	for (int mip = 0; mip < num_mips; mip++)
	{
		const byte* src = (const byte*)mt + mt->offsets[mip];
		paletteRGBA_t* dst = (paletteRGBA_t*)(staged + mip_offsets[mip]);
		const uint mip_size = mip_widths[mip] * mip_heights[mip];

		for (uint i = 0; i < mip_size; i++)
		{
			const paletteRGB_t* src_p = &image->palette[src[i]];

			// Copy rgb components.
			dst[i].r = src_p->r;
			dst[i].g = src_p->g;
			dst[i].b = src_p->b;
			dst[i].a = 255;
		}
	}

	QVk_CreateTextureMips(&image->vk_texture, command_buffer, staging_buffer, staging_offset, num_mips, mip_widths, mip_heights, mip_offsets);
	QVk_CreateTextureDescriptorSet(&image->vk_texture, GetImageSampler(image), false); // gl3: R_SetFilter(image).

	return true;
}

// Loads .M8 image.
static image_t* R_LoadM8(const char* name, const imagetype_t type) // H2: GL_LoadWal().
{
	miptex_t* mt;
	const int filesize = ri.FS_LoadFile(name, (void**)&mt);

	if (mt == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return NULL;
	}

	if (mt->version != MIP_VERSION)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s': invalid version (%i)\n", name, mt->version); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	if (strlen(name) >= MAX_QPATH)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM8: can't load '%s': filename too long\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	paletteRGB_t* palette = malloc(sizeof(paletteRGB_t) * 256);
	GrabPalette(mt->palette, palette);

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = (int)mt->width[0];
	image->height = (int)mt->height[0];
	image->type = type;
	image->palette = palette;
	image->has_alpha = false;
	image->num_frames = (byte)mt->value;

	FixPalette(image); //mxd

	// gl1/gl3: glGenTextures() + GL_Bind() + R_UploadM8(); vk: staging upload of the whole mip chain.
	if (!R_UploadM8(mt, filesize, image))
	{
		// No valid mip levels - release the slot, R_FindImage() falls back to r_notexture.
		free(image->palette);
		memset(image, 0, sizeof(image_t));
		ri.FS_FreeFile(mt);

		return NULL;
	}

	QVk_DebugSetObjectName((uint64_t)image->vk_texture.resource.image, VK_OBJECT_TYPE_IMAGE, va("Image: %s", name));
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.imageView, VK_OBJECT_TYPE_IMAGE_VIEW, va("Image View: %s", name));
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, va("Descriptor Set: %s", name));

	ri.FS_FreeFile(mt);

	return image;
}

#pragma endregion

#pragma region ========================== .M32 LOADING ==========================

// NOTE: gl1's R_ApplyGamma32() is intentionally gone (shader-side color grading).

// Uploads the .m32 file's complete mip chain (already RGBA - straight memcpy into
// the staging buffer per level). Returns false when no valid mip levels were found.
static qboolean R_UploadM32(const miptex32_t* mt, const int filesize, image_t* image) // H2: GL_Upload32M().
{
	uint32_t mip_widths[MIPLEVELS];
	uint32_t mip_heights[MIPLEVELS];
	VkDeviceSize mip_offsets[MIPLEVELS];
	VkBuffer staging_buffer;
	VkCommandBuffer command_buffer;
	uint32_t staging_offset;
	VkDeviceSize total_size = 0;

	int num_mips = 0;
	for (int mip = 0; mip < MIPLEVELS && mt->width[mip] > 0 && mt->height[mip] > 0; mip++)
	{
		const uint mip_size = mt->width[mip] * mt->height[mip];
		if ((int)(mt->offsets[mip] + mip_size * sizeof(paletteRGBA_t)) > filesize) // Bounds check --morb
		{
			ri.Con_Printf(PRINT_ALL, "R_UploadM32: mip %i offset %u out of bounds (filesize %i) for '%s'\n", mip, mt->offsets[mip], filesize, image->name);
			break;
		}

		// vk (unlike GL) requires exact half-size mip extents - stop the chain on malformed levels.
		if (mt->width[mip] != max(mt->width[0] >> mip, 1u) || mt->height[mip] != max(mt->height[0] >> mip, 1u))
		{
			ri.Con_Printf(PRINT_ALL, "R_UploadM32: mip %i has non-halving size (%u x %u) for '%s'\n", mip, mt->width[mip], mt->height[mip], image->name);
			break;
		}

		mip_widths[num_mips] = mt->width[mip];
		mip_heights[num_mips] = mt->height[mip];
		mip_offsets[num_mips] = total_size;
		total_size += (VkDeviceSize)mip_size * sizeof(paletteRGBA_t);
		num_mips++;
	}

	if (num_mips == 0)
		return false;

	// Stage the whole mip chain in one staging allocation.
	byte* staged = QVk_GetStagingBuffer(total_size, 4, &command_buffer, &staging_buffer, &staging_offset);

	for (int mip = 0; mip < num_mips; mip++)
		memcpy(staged + mip_offsets[mip], (const byte*)mt + mt->offsets[mip], (size_t)mip_widths[mip] * mip_heights[mip] * sizeof(paletteRGBA_t));

	QVk_CreateTextureMips(&image->vk_texture, command_buffer, staging_buffer, staging_offset, num_mips, mip_widths, mip_heights, mip_offsets);
	QVk_CreateTextureDescriptorSet(&image->vk_texture, GetImageSampler(image), false); // gl3: R_SetFilter(image).

	return true;
}

// Loads .M32 image.
static image_t* R_LoadM32(const char* name, const imagetype_t type) // H2: GL_LoadWal32()
{
	miptex32_t* mt;

	const int filesize = ri.FS_LoadFile(name, (void**)&mt);
	if (mt == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return NULL;
	}

	if (mt->version != MIP32_VERSION)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s': invalid version (%i)\n", name, mt->version); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	if (strlen(name) >= MAX_QPATH)
	{
		ri.Con_Printf(PRINT_ALL, "R_LoadM32: can't load '%s': filename too long\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		ri.FS_FreeFile(mt); //mxd

		return NULL;
	}

	// gl1: R_ApplyGamma32(mt, filesize) - removed, pixels upload raw (shader-side grading).

	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = (int)mt->width[0];
	image->height = (int)mt->height[0];
	image->type = type;
	image->palette = NULL;
	image->has_alpha = 1;
	image->num_frames = (byte)mt->num_frames;

	if (!R_UploadM32(mt, filesize, image))
	{
		// No valid mip levels - release the slot, R_FindImage() falls back to r_notexture.
		memset(image, 0, sizeof(image_t));
		ri.FS_FreeFile(mt);

		return NULL;
	}

	QVk_DebugSetObjectName((uint64_t)image->vk_texture.resource.image, VK_OBJECT_TYPE_IMAGE, va("Image: %s", name));
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.imageView, VK_OBJECT_TYPE_IMAGE_VIEW, va("Image View: %s", name));
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, va("Descriptor Set: %s", name));

	ri.FS_FreeFile(mt);

	return image;
}

#pragma endregion

// Creates a fallback texture programmatically (e.g., checkerboard pattern)
image_t* R_CreateFallbackTexture(const char* name, const imagetype_t type)
{
	image_t* image = R_GetFreeImage();
	strcpy_s(image->name, sizeof(image->name), name);
	image->registration_sequence = registration_sequence;
	image->width = 64;
	image->height = 64;
	image->type = type;
	image->palette = NULL;
	image->has_alpha = false;
	image->num_frames = 1;

	// Generate a simple 64x64 checkerboard pattern (magenta/black)
	byte pixels[64 * 64 * 4];
	for (int y = 0; y < 64; y++)
	{
		for (int x = 0; x < 64; x++)
		{
			const int idx = (y * 64 + x) * 4;
			const qboolean white = ((x / 8) + (y / 8)) & 1;
			pixels[idx + 0] = (white ? 255 : 0);	// R
			pixels[idx + 1] = 0;					// G
			pixels[idx + 2] = (white ? 255 : 128);	// B
			pixels[idx + 3] = 255;					// A
		}
	}

	// gl3: single mip level + GL_NEAREST filters.
	QVk_CreateTexture(&image->vk_texture, pixels, 64, 64, S_NEAREST, false);
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.resource.image, VK_OBJECT_TYPE_IMAGE, va("Image: %s", name));
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.imageView, VK_OBJECT_TYPE_IMAGE_VIEW, va("Image View: %s", name));
	QVk_DebugSetObjectName((uint64_t)image->vk_texture.descriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, va("Descriptor Set: %s", name));

	// Add to hash
	const uint len = strlen(image->name);
	const byte hash = image->name[len - 7] + image->name[len - 5] * image->name[len - 6];
	image->next = vktextures_hashed[hash];
	vktextures_hashed[hash] = image;

	return image;
}

// Now with name hashing. When no texture found, returns r_notexture instead of NULL.
image_t* R_FindImage(const char* name, const imagetype_t type) // H2: GL_FindImage()
{
	if (name == NULL)
	{
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Invalid null name\n"); //mxd. Com_Printf() -> ri.Con_Printf().
		return r_notexture;
	}

	const uint len = strlen(name);

	if (len < 8)
	{
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Name too short (%s)\n", name); //mxd. Com_Printf() -> ri.Con_Printf().
		return r_notexture;
	}

	// Check for hashed image first.
	const byte hash = name[len - 7] + name[len - 5] * name[len - 6];
	image_t* image = vktextures_hashed[hash];

	while (image != NULL)
	{
		if (strcmp(name, image->name) == 0)
		{
			image->registration_sequence = registration_sequence;
			return image;
		}

		image = image->next;
	}

	// Not hashed. Load image from disk.
	if (strcmp(name + len - 3, ".m8") == 0)
		image = R_LoadM8(name, type);
	else if (strcmp(name + len - 4, ".m32") == 0)
		image = R_LoadM32(name, type);
	else
		ri.Con_Printf(PRINT_ALL, "R_FindImage: Extension not recognized in '%s'\n", name); //mxd. Com_Printf() -> ri.Con_Printf().

	if (image == NULL)
		return r_notexture;

	// Add image to hash.
	image->next = vktextures_hashed[hash];
	vktextures_hashed[hash] = image;

	return image;
}

// H2: new 'retval' arg.
struct image_s* RI_RegisterSkin(const char* name, qboolean* retval)
{
	image_t* img = R_FindImage(name, it_skin);
	if (retval != NULL)
		*retval = (img != r_notexture);

	return img;
}

#pragma region ========================== IMAGE FREEING / GC ==========================

static void R_FreeImage(image_t* image) // H2: GL_FreeImage()
{
	// Delete the Vulkan texture (image + view + descriptor set). No per-texture
	// sync: callers (R_FreeUnusedImages()/R_ShutdownImages()) have already flushed
	// pending uploads and waited for the frame fences (yq2 Vk_FreeUnusedImages technique).
	QVk_ReleaseTexture(&image->vk_texture, false);

	if (image->palette != NULL)
	{
		free(image->palette);
		image->palette = NULL;
	}

	// Remove from hash.
	const uint len = strlen(image->name);
	const byte hash = image->name[len - 7] + image->name[len - 5] * image->name[len - 6];

	image_t** tgt = &vktextures_hashed[hash];
	for (image_t* img = vktextures_hashed[hash]; img != image; img = img->next)
		tgt = &img->next;

	*tgt = image->next;
	image->registration_sequence = 0;

	// gl3 reset its GL bind cache here - no bind caches in vk (per-image descriptor sets).
}

void R_FreeImageNoHash(image_t* image) // H2: GL_FreeImageNoHash()
{
	// Called for self-managed images (cinematic frames) outside the GC paths -
	// sync before releasing (the texture may be referenced by an in-flight frame).
	QVk_ReleaseTexture(&image->vk_texture, true);

	if (image->palette != NULL)
	{
		free(image->palette);
		image->palette = NULL;
	}

	image->registration_sequence = 0;
}

void R_FreeUnusedImages(void)
{
	// Never free r_notexture or particle texture. H2: extra never-to-free textures.
	// NULL checks added - these are set by Draw_InitLocal() (vk_Draw.c module
	// port), which may not have run/landed yet when a map load triggers GC (gl3 parity).
	image_t* permanent[] = { r_notexture, r_particletexture, r_aparticletexture, r_reflecttexture, draw_chars, r_font1, r_font2 };
	for (uint i = 0; i < sizeof(permanent) / sizeof(permanent[0]); i++)
		if (permanent[i] != NULL)
			permanent[i]->registration_sequence = registration_sequence;

	// yq2 Vk_FreeUnusedImages(): the textures about to be freed may still be
	// referenced by in-flight frames - flush pending staging uploads and wait
	// for the device (frame fences) ONCE, then release without per-texture sync.
	QVk_SubmitStagingBuffers();
	if (vk_device.logical != VK_NULL_HANDLE)
		vkDeviceWaitIdle(vk_device.logical);

	image_t* image = &vktextures[0];
	for (int i = 0; i < numvktextures; i++, image++)
	{
		// Used in this sequence.
		if (image->registration_sequence == registration_sequence)
			continue;

		// Free image_t slot.
		if (image->registration_sequence == 0)
			continue;

		// Missing: it_pic check

		// fix for nameless self-managed images --morb
		if (!image->name[0])
			continue;

		// Free it.
		R_FreeImage(image);
	}

	// yq2: release now-empty device memory blocks.
	vulkan_memory_free_unused();
}

#pragma endregion

void R_InitImages(void) // Q2: GL_InitImages()
{
	registration_sequence = 1;
	// gl1: gl_state.inverse_intensity = 1.0f - intensity is fixed at 1.0 in the vk shaders (gl3 parity).

	for (int i = 0; i < numvktextures; i++)
	{
		if (vktextures[i].palette != NULL)
		{
			free(vktextures[i].palette);
			vktextures[i].palette = NULL;
		}
	}
	memset(vktextures, 0, sizeof(image_t) * MAX_VKTEXTURES);
	memset(vktextures_hashed, 0, sizeof(vktextures_hashed));
	numvktextures = 0;

	// gl1/gl3 invalidated their texture bind caches here - no bind caches in vk
	// (textures bind through per-image descriptor sets at draw time).

	// gl1 also reset its R_BlendFunc()/R_AlphaFunc() caches here - no such caches in vk
	// (blend state is baked into the pipelines, alpha test is shader-side).

	R_InitMinlight(); // YQ2
}

void R_ShutdownImages(void) // Q2: GL_ShutdownImages()
{
	// Flush pending uploads and wait for the frame fences once, then free
	// everything without per-texture sync (see R_FreeUnusedImages()).
	QVk_SubmitStagingBuffers();
	if (vk_device.logical != VK_NULL_HANDLE)
		vkDeviceWaitIdle(vk_device.logical);

	image_t* image = &vktextures[0];
	for (int i = 0; i < numvktextures; i++, image++)
		// same crash -- nameless cin_frame segfault --morb
		//if (image->registration_sequence != 0)
		if (image->registration_sequence != 0 && image->name[0])
			R_FreeImage(image);

	//mxd. Free upload_buffer.
	if (upload_buffer != NULL)
	{
		free(upload_buffer);
		upload_buffer = NULL;
		upload_buffer_size = 0;
	}
}

// NOTE: gl1's R_RefreshImage() and R_GammaAffect() are intentionally gone: with
// shader-side color grading nothing ever needs to be re-uploaded on gamma change
// (vk_Main.c consumes/resets vid_textures_refresh_required as a no-op).

void R_DisplayHashTable(void)
{
	int total_count = 0;
	int hashed_count = 0;

	image_t** vkt = vktextures_hashed;
	for (int i = 0; i < NUM_HASHED_VKTEXTURES; i++, vkt++)
	{
		const image_t* image = *vkt;
		if (image != NULL)
		{
			while (image != NULL)
			{
				image = image->next;
				total_count++;
			}

			hashed_count++;
		}
	}

	ri.Con_Printf(PRINT_ALL, "Hash entries: %d, Total images: %d\n", hashed_count, total_count); //mxd. Com_Printf() -> ri.Con_Printf().
}
