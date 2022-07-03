#include "vertex_data.h"

#include <limits>
#include <iostream>
#include <cstring> // for std::memcpy()
#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/to_string.hpp"


namespace lut = labutils;


Mesh create_mesh_data(labutils::VulkanContext const& aContext, labutils::Allocator const& aAllocator, ModelData& const modelData, unsigned int subMeshIndex)
{
	
	// Store the number of vertices for the first object
	unsigned int numberOfVertices = modelData.meshes[subMeshIndex].numberOfVertices;
	unsigned int vertexStartIndex = modelData.meshes[subMeshIndex].vertexStartIndex;
	unsigned int materialIndex = modelData.meshes[subMeshIndex].materialIndex;

	// Resize the vector for texture coordinate
	if (modelData.vertexTextureCoords.empty())
		modelData.vertexTextureCoords.resize(numberOfVertices,glm::vec2(0,0));

	// Get materials
	block::MaterialUniform materialUniform{};


	materialUniform.emissive = glm::vec4(modelData.materials[materialIndex].emissive, 1.0f);
	materialUniform.shininess = modelData.materials[materialIndex].shininess;
	
#ifdef BLINN_PHONG_MODE
	materialUniform.diffuse = glm::vec4(modelData.materials[materialIndex].diffuse, 1.0f);
	materialUniform.specular = glm::vec4(modelData.materials[materialIndex].specular, 1.0f);
#endif

#ifdef PBR_MODE
	materialUniform.metalness = modelData.materials[materialIndex].metalness;
	materialUniform.albedo = glm::vec4(modelData.materials[materialIndex].albedo, 1.f);
#endif

	
	// Vertex data
	lut::Buffer posStaging = lut::create_buffer(
		aAllocator,
		numberOfVertices * sizeof(glm::vec3),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU
	);

	lut::Buffer texcoordStaging = lut::create_buffer(
		aAllocator,
		numberOfVertices * sizeof(glm::vec2),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU
	);

	lut::Buffer normalStaging = lut::create_buffer(
		aAllocator,
		numberOfVertices * sizeof(glm::vec3),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU
	);

	// map the buffer with a pointer

	// Position
	void* posPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, posStaging.allocation, &posPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\nvmaMapMemory() returned %s", lut::to_string(res).c_str());
	}
	// copy the data into buffer pointed by the pointer
	std::memcpy(posPtr, &modelData.vertexPositions[vertexStartIndex], numberOfVertices * sizeof(glm::vec3));

	// un map buffer
	vmaUnmapMemory(aAllocator.allocator, posStaging.allocation);

	// Texture Coordinate
	void* texcoordPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, texcoordStaging.allocation, &texcoordPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\nvmaMapMemory() returned % s", lut::to_string(res).c_str());
	}

	std::memcpy(texcoordPtr, &modelData.vertexTextureCoords[vertexStartIndex], numberOfVertices * sizeof(glm::vec2));

	vmaUnmapMemory(aAllocator.allocator, texcoordStaging.allocation);

	// Normal
	void* normalPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, normalStaging.allocation, &normalPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\nvmaMapMemory() returned % s", lut::to_string(res).c_str());
	}

	std::memcpy(normalPtr, &modelData.vertexNormals[vertexStartIndex], numberOfVertices * sizeof(glm::vec3));

	vmaUnmapMemory(aAllocator.allocator, normalStaging.allocation);

	// return mesh
	return Mesh{
		std::move(posStaging),
		std::move(texcoordStaging),
		std::move(normalStaging),
		"",
		modelData.materials[materialIndex].color,
		std::move(materialUniform),
		numberOfVertices
	};
}


ModelVertexTexturePack create_model_attribute_set(labutils::VulkanWindow const& window, labutils::Allocator const& allocator,
	ModelData& const modelData, VkDescriptorSetLayout textureSetLayout, VkDescriptorSetLayout materialSetLayout, VkDescriptorPool dpool, unsigned int subMeshIndex)
{
	// get mesh data
	Mesh mesh = create_mesh_data(window, allocator, modelData, subMeshIndex);

	// load textures into image
	labutils::Image image;
	{
		// check if the device image format can support 
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(window.physicalDevice, VK_FORMAT_R8G8B8A8_SRGB, &formatProperties);
		
		// create command pool
		labutils::CommandPool loadCmdPool = labutils::create_command_pool(window,VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

		// load a texture for the model
		if (mesh.colorTexturePath != "")
		{
			if (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) 
			{
				image = labutils::load_image_texture2d_with_bliting(mesh.colorTexturePath.c_str(), window, loadCmdPool.handle, allocator);
			}
			else
			{
				image = labutils::load_image_texture2d_no_minmap(mesh.colorTexturePath.c_str(), window, loadCmdPool.handle, allocator);
			}
		}
		else
			image = create_image_texture2d_with_solid_color(mesh.colorTexturePath.c_str(), window, loadCmdPool.handle, allocator, glm::vec4(mesh.color,1.f));
	}

	// create image view for texture image
	labutils::ImageView view  = labutils::create_image_view_texture2d(window, image.image, VK_FORMAT_R8G8B8A8_SRGB);
	
	labutils::Sampler sampler = labutils::create_default_sampler(window, VK_TRUE);
	

	// allocate and initialize descriptor sets for texture
	VkDescriptorSet texDescriptors = labutils::alloc_desc_set(window, dpool, textureSetLayout);
	{
		// Write descriptor set
		VkWriteDescriptorSet desc[1]{};

		// Descriptor Buffer Info 
		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = view.handle;
		imageInfo.sampler = sampler.handle;

		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = texDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc[0].descriptorCount = 1;
		desc[0].pImageInfo = &imageInfo;

		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}

	// descriptor set pack
	desc::DescriptorSetPack materialDescSetPack = desc::create_descriptor_set_for_uniform_buffer(window, allocator, dpool,
		materialSetLayout, { sizeof(block::MaterialUniform) }, 1);
	materialDescSetPack.data.emplace_back(& mesh.materialUniform);

	// vertex gpu buffer
	lut::Buffer vertexPosGPU = lut::create_buffer(
		allocator,
		mesh.vertexCount * sizeof(glm::vec3),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY
	);

	lut::Buffer vertexTexcoordGPU = lut::create_buffer(
		allocator,
		mesh.vertexCount * sizeof(glm::vec2),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY
	);

	lut::Buffer vertexNormalGPU = lut::create_buffer(
		allocator,
		mesh.vertexCount * sizeof(glm::vec3),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY
	);



	//------ Begin command state -------//

	// create fence
	lut::Fence uploadComplete = create_fence(window);

	// create command pool and buffer
	lut::CommandPool uploadPool = create_command_pool(window);

	VkCommandBuffer uploadCmd = alloc_command_buffer(window, uploadPool.handle);

	VkCommandBufferBeginInfo beginInfo{};

	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;

	if (auto const res = vkBeginCommandBuffer(uploadCmd, &beginInfo); VK_SUCCESS != res)
	{
		throw lut::Error("Beginning command buffer recording\nvkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
	}

	// copy data in stage buffer into buffer for GPU only
	VkBufferCopy pcopy{};
	pcopy.size = mesh.vertexCount * sizeof(glm::vec3);
	vkCmdCopyBuffer(uploadCmd, mesh.posStaging.buffer, vertexPosGPU.buffer, 1, &pcopy);

	// create barrier
	lut::buffer_barrier(uploadCmd,
		vertexPosGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);

	// copy data in stage buffer into buffer for GPU only
	VkBufferCopy ccopy{};
	ccopy.size = mesh.vertexCount * sizeof(glm::vec2);
	vkCmdCopyBuffer(uploadCmd, mesh.texcoordStaging.buffer, vertexTexcoordGPU.buffer, 1, &ccopy);

	// create barrier
	lut::buffer_barrier(uploadCmd,
		vertexTexcoordGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);


	// copy data in stage buffer into buffer for GPU only
	VkBufferCopy ncopy{};
	ncopy.size = mesh.vertexCount * sizeof(glm::vec3);
	vkCmdCopyBuffer(uploadCmd, mesh.normalStaging.buffer, vertexNormalGPU.buffer, 1, &ncopy);

	// create barrier
	lut::buffer_barrier(uploadCmd,
		vertexNormalGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);

	materialDescSetPack.update_ubo_data(uploadCmd);

	if (auto const res = vkEndCommandBuffer(uploadCmd); VK_SUCCESS != res)
	{
		throw lut::Error("Ending command buffer recording\nvkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
	}

	// submit command

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &uploadCmd;

	if (auto const res = vkQueueSubmit(window.graphicsQueue, 1, &submitInfo, uploadComplete.handle)
		; VK_SUCCESS != res)
	{
		throw lut::Error("Submitting commands\nvkQueueSubmit() returned %s", lut::to_string(res).c_str());
	}

	if (auto const res = vkWaitForFences(window.device, 1, &uploadComplete.handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max())
		; VK_SUCCESS != res)
	{
		throw lut::Error("Waiting for upload to complete\nvkWaitForFences() returned %s", lut::to_string(res).c_str());
	}

	//------ End command state -------//

	return ModelVertexTexturePack{
		std::move(vertexPosGPU),
		std::move(vertexTexcoordGPU),
		std::move(vertexNormalGPU),
		std::move(materialSetLayout),
		std::move(texDescriptors),
		std::move(image),
		std::move(view),
		std::move(sampler),
		std::move(materialDescSetPack),
		mesh.vertexCount
	};
}