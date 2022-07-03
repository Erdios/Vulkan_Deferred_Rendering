#pragma once
#include "../labutils/vkutil.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp"
#include "../labutils/vulkan_window.hpp"
#include "FramebufferHelper.h"

namespace lut = labutils;

namespace desc
{
	struct Buffer {
		lut::Buffer buffer;
		std::uint32_t bufferSize;
		void* data;

		void update_ubo_data(VkCommandBuffer aCmdBuff);
	};

	struct BufferInfo {
		Buffer* buffer;
		VkDescriptorBufferInfo bufferInfo;
		std::uint32_t binding;
	};

	struct ImageInfo {
		lut::Image* image;
		VkDescriptorImageInfo imageInfo;
		std::uint32_t binding;
	};

	class DescriptorSetPack
	{

	public:
		VkDescriptorSetLayout layout;
		std::vector<lut::Buffer> buffer;
		VkDescriptorSet descriptorSet;
		std::vector<std::uint32_t> uniformBlockSize;
		std::vector<void*> data;

		DescriptorSetPack();
		DescriptorSetPack(VkDescriptorSetLayout inLayout,
							std::vector<lut::Buffer> inBuffer,
							VkDescriptorSet inDescriptorSet,
							std::vector<std::uint32_t> inUniformBlockSize,
							std::vector<void*> inData);

		void update_ubo_data(VkCommandBuffer aCmdBuff);
	
	};

	DescriptorSetPack create_descriptor_set_for_uniform_buffer(lut::VulkanWindow const& inWindow, lut::Allocator const& inAllocator, VkDescriptorPool inDpool,
		std::vector<labutils::DescriptorSetLayout>& layouts, VkShaderStageFlags stageFlag, std::vector<std::uint32_t> typeSize, std::uint32_t bufferCount);

	DescriptorSetPack  create_descriptor_set_for_uniform_buffer(lut::VulkanWindow const& inWindow, lut::Allocator const& inAllocator,
		VkDescriptorPool inDpool, VkDescriptorSetLayout layout, std::vector<std::uint32_t> typeSize, std::uint32_t bufferCount);

	lut::DescriptorSetLayout create_descriptor_layout(lut::VulkanWindow const& aWindow, VkDescriptorType descriptorType, VkShaderStageFlags shaderStageFlag);
	
	void update_descriptor_set(lut::VulkanWindow const& window, std::vector<lut::Buffer> const& descriptorBuffers, VkDescriptorSet descritporSet, VkDescriptorType descriptorType);
	
	// functions to make the set creation process clearer
	VkDescriptorSet create_descriptor_set(lut::VulkanWindow const& inWindow, VkDescriptorPool inDpool, VkDescriptorSetLayout layout, desc::BufferInfo* bufferInfos, std::uint32_t bufferCount, desc::ImageInfo* imageInfos, std::uint32_t imageCount);
	VkDescriptorSetLayoutBinding create_descriptor_layout_binding(std::uint32_t bindingID, VkDescriptorType descriptorType, VkShaderStageFlags shaderStageFlag, std::uint32_t descriptorCount = 1);
	lut::DescriptorSetLayout create_descriptor_layout(lut::VulkanWindow const& aWindow, VkDescriptorSetLayoutBinding* bindings, std::uint32_t bindingCount);
	VkDescriptorBufferInfo create_desc_buffer_info(VkBuffer buffer, VkDeviceSize range = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
	VkDescriptorImageInfo create_desc_image_info(VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	VkWriteDescriptorSet create_write_desc_set(VkDescriptorSet descritporSet, std::uint32_t layoutBinding, VkDescriptorBufferInfo* descBufferInfos, std::uint32_t descriptorCount = 1);
	VkWriteDescriptorSet create_write_desc_set(VkDescriptorSet descritporSet, std::uint32_t layoutBinding, VkDescriptorImageInfo* descImageInfos, std::uint32_t descriptorCount = 1);
}