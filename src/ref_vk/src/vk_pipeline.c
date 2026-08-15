#include "compat.h"
//
// vk_pipeline.c -- Vulkan shader module and graphics pipeline creation.
//
// Mechanical port of yquake2remaster vk_pipeline.c (Copyright (C) 2018-2019
// Krzysztof Kondrak, GPLv2) - yq2 refimport calls replaced with H2R's ri.*;
// yq2's vk_sampleshading cvar dropped (sample shading disabled - CONTRACT.md
// cvar set has no equivalent).
//

#include "vk_Local.h"

qvkshader_t QVk_CreateShader(const uint32_t* shaderSrc, size_t shaderCodeSize, VkShaderStageFlagBits shaderStage)
{
	qvkshader_t shader;
	VkShaderModuleCreateInfo smCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.codeSize = shaderCodeSize,
		.pCode = shaderSrc
	};

	VK_VERIFY(vkCreateShaderModule(vk_device.logical, &smCreateInfo, NULL, &shader.module));

	VkPipelineShaderStageCreateInfo vssCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.stage = shaderStage,
		.module = shader.module,
		.pName = "main",
		.pSpecializationInfo = NULL
	};

	shader.createInfo = vssCreateInfo;

	return shader;
}

void QVk_CreatePipeline(const VkDescriptorSetLayout* descriptorLayout,
	const uint32_t descLayoutCount, const VkPipelineVertexInputStateCreateInfo* vertexInputInfo,
	qvkpipeline_t* pipeline, const qvkrenderpass_t* renderpass,
	const qvkshader_t* shaders, uint32_t shaderCount)
{
	VkPipelineShaderStageCreateInfo* ssCreateInfos;
	size_t i;

	ssCreateInfos = (VkPipelineShaderStageCreateInfo*)malloc(shaderCount * sizeof(VkPipelineShaderStageCreateInfo));
	VK_CHECK_OOM(ssCreateInfos, "malloc() VkPipelineShaderStageCreateInfo")

	for (i = 0; i < shaderCount; i++)
	{
		ssCreateInfos[i] = shaders[i].createInfo;
	}

	VkPipelineInputAssemblyStateCreateInfo iaCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.topology = pipeline->topology,
		.primitiveRestartEnable = VK_FALSE
	};

	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)viddef.width,
		.height = (float)viddef.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = {
		.offset.x = 0,
		.offset.y = 0,
		.extent = vk_swapchain.extent
	};

	VkPipelineViewportStateCreateInfo vpCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor
	};

	VkPipelineRasterizationStateCreateInfo rCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = pipeline->cullMode,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = 0.0f,
		.lineWidth = 1.0f
	};

	VkPipelineMultisampleStateCreateInfo msCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.rasterizationSamples = renderpass->sampleCount,
		.sampleShadingEnable = VK_FALSE, // yq2 vk_sampleshading dropped (not in the H2 cvar set).
		.minSampleShading = 0.0f,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE
	};

	VkPipelineDepthStencilStateCreateInfo dCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.depthTestEnable = pipeline->depthTestEnable,
		// There should be NO depth writes if depthTestEnable is false but Intel seems to not follow the specs fully...
		.depthWriteEnable = (pipeline->depthTestEnable == VK_TRUE ? pipeline->depthWriteEnable : VK_FALSE),
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
		.front = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, 0, 0, 0 },
		.back = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, 0, 0, 0 },
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f
	};

	VkPipelineColorBlendStateCreateInfo cbsCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &pipeline->blendOpts,
		.blendConstants[0] = 0.0f,
		.blendConstants[1] = 0.0f,
		.blendConstants[2] = 0.0f,
		.blendConstants[3] = 0.0f
	};

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dsCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates
	};

	// Push constant sizes accomodate for maximum number of uploaded elements
	// (see the shared push constant layout description in vk_Local.h).
	VkPushConstantRange pushConstantRange[] = {
		{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			.offset = 0,
			.size = PUSH_CONSTANT_VERTEX_SIZE * sizeof(float)
		},
		{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = PUSH_CONSTANT_VERTEX_SIZE * sizeof(float),
			.size = PUSH_CONSTANT_FRAGMENT_SIZE * sizeof(float)
		}
	};

	VkPipelineLayoutCreateInfo plCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.setLayoutCount = descLayoutCount,
		.pSetLayouts = descriptorLayout,
		// for simplicity assume only two push constant ranges are passed,
		// so it's not the most flexible approach
		.pushConstantRangeCount = 2,
		.pPushConstantRanges = pushConstantRange
	};

	VK_VERIFY(vkCreatePipelineLayout(vk_device.logical, &plCreateInfo, NULL, &pipeline->layout));

	// create THE pipeline
	VkGraphicsPipelineCreateInfo pCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = NULL,
		.flags = pipeline->flags,
		.stageCount = shaderCount,
		.pStages = ssCreateInfos,
		.pVertexInputState = vertexInputInfo,
		.pInputAssemblyState = &iaCreateInfo,
		.pTessellationState = NULL,
		.pViewportState = &vpCreateInfo,
		.pRasterizationState = &rCreateInfo,
		.pMultisampleState = &msCreateInfo,
		.pDepthStencilState = &dCreateInfo,
		.pColorBlendState = &cbsCreateInfo,
		.pDynamicState = &dsCreateInfo,
		.layout = pipeline->layout,
		.renderPass = renderpass->rp,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1
	};

	VK_VERIFY(vkCreateGraphicsPipelines(vk_device.logical, VK_NULL_HANDLE, 1, &pCreateInfo, NULL, &pipeline->pl));
	free(ssCreateInfos);
}

void QVk_DestroyPipeline(qvkpipeline_t* pipeline)
{
	if (pipeline->layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(vk_device.logical, pipeline->layout, NULL);
	if (pipeline->pl != VK_NULL_HANDLE)
		vkDestroyPipeline(vk_device.logical, pipeline->pl, NULL);

	pipeline->layout = VK_NULL_HANDLE;
	pipeline->pl = VK_NULL_HANDLE;
}
