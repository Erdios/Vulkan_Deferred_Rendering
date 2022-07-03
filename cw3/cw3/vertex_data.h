#pragma once

#include "../labutils/vulkan_window.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp" 
#include "../cw3/model.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkimage.hpp"
#include "DescriptorSetHelper.h"

//#define BLINN_PHONG_MODE
#define PBR_MODE

struct VertexInputInfo
{
	std::uint32_t bufferCount;
	std::vector<std::uint32_t> strides;
	std::vector<VkFormat> formats;
};


namespace block
{

#ifdef BLINN_PHONG_MODE
	struct MaterialUniform
	{
		// Note: must map to the std140 uniform interface in the fragment
		// shader, so need to be careful about the packing/alignment here!
		glm::vec4 emissive;
		glm::vec4 diffuse;
		glm::vec4 specular;
		float shininess;
	};
#endif


#ifdef PBR_MODE
	struct MaterialUniform
	{
		// Note: must map to the std140 uniform interface in the fragment
		// shader, so need to be careful about the packing/alignment here!
		glm::vec4 emissive;
		glm::vec4 albedo;
		float shininess;
		float metalness;
	};
#endif

}


struct Mesh
{
	// buffer
	labutils::Buffer posStaging;
	labutils::Buffer texcoordStaging;
	labutils::Buffer normalStaging;

	// data
	std::string colorTexturePath;
	glm::vec3 color;
	block::MaterialUniform materialUniform;

	// vertex count
	std::uint32_t vertexCount;
};

struct ModelVertexTexturePack
{

	// vertex input
	labutils::Buffer positions;
	labutils::Buffer texcoords;
	labutils::Buffer normals;
	
	// texture
	VkDescriptorSetLayout textureSetLayout;
	VkDescriptorSet textureDescriptorSet;
	
	labutils::Image image;
	labutils::ImageView view;
	labutils::Sampler sampler;

	// material
	desc::DescriptorSetPack materialDescSetPack;

	// vertex count
	std::uint32_t vertexCount;
};



Mesh create_mesh_data(labutils::VulkanContext const&, labutils::Allocator const&, ModelData& const modelData, unsigned int subMeshIndex);

ModelVertexTexturePack create_model_attribute_set(labutils::VulkanWindow const& window, labutils::Allocator const& allocator,
	ModelData& const modelData, VkDescriptorSetLayout textureSetLayout, VkDescriptorSetLayout materialSetLayout, VkDescriptorPool dpool, unsigned int subMeshIndex);
