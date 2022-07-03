#include "DescriptorSetHelper.h"
#include "../labutils/error.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/to_string.hpp"

namespace desc
{

	void Buffer::update_ubo_data(VkCommandBuffer aCmdBuff)
	{

		lut::buffer_barrier(
			aCmdBuff,
			buffer.buffer,
			VK_ACCESS_UNIFORM_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT
		);

		// Update uniform buffer
		vkCmdUpdateBuffer(aCmdBuff, buffer.buffer, 0, bufferSize, data);

		// Barrier Two
		lut::buffer_barrier(
			aCmdBuff,
			buffer.buffer,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_UNIFORM_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
		);

	}

	DescriptorSetPack::DescriptorSetPack():layout(VK_NULL_HANDLE), descriptorSet(VK_NULL_HANDLE), buffer(), uniformBlockSize(), data()
	{
	
	}
	DescriptorSetPack::DescriptorSetPack(VkDescriptorSetLayout inLayout,
		std::vector<lut::Buffer> inBuffer,
		VkDescriptorSet inDescriptorSet,
		std::vector<std::uint32_t> inUniformBlockSize,
		std::vector<void*> inData): 
		layout(inLayout), descriptorSet(inDescriptorSet), buffer(std::move(inBuffer)), uniformBlockSize(inUniformBlockSize), data(inData)
	{}

	void DescriptorSetPack::update_ubo_data(VkCommandBuffer aCmdBuff)
	{
		//assert(outBuffer.size() == inData.size());
		std::uint32_t numOfUpdateBuffers = (buffer.size() < data.size() ? buffer.size() : data.size());

		for (std::uint32_t i = 0; i < numOfUpdateBuffers; i++)
		{
			lut::buffer_barrier(
				aCmdBuff,
				buffer[i].buffer,
				VK_ACCESS_UNIFORM_READ_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT
			);

			// Update uniform buffer
			vkCmdUpdateBuffer(aCmdBuff, buffer[i].buffer, 0, uniformBlockSize[i], data[i]);

			// Barrier Two
			lut::buffer_barrier(
				aCmdBuff,
				buffer[i].buffer,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_UNIFORM_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
			);
		}
	}

	DescriptorSetPack  create_descriptor_set_for_uniform_buffer(lut::VulkanWindow const& inWindow, lut::Allocator const& inAllocator,
		VkDescriptorPool inDpool, std::vector<labutils::DescriptorSetLayout>& layouts, VkShaderStageFlags stageFlag, std::vector<std::uint32_t> typeSize, std::uint32_t bufferCount)
	{

		// move layout into the list
		layouts.emplace_back(create_descriptor_layout(inWindow, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stageFlag));
		
		return create_descriptor_set_for_uniform_buffer(inWindow, inAllocator,
			inDpool, layouts.back().handle, typeSize, bufferCount);
	}

	DescriptorSetPack  create_descriptor_set_for_uniform_buffer(lut::VulkanWindow const& inWindow, lut::Allocator const& inAllocator,
		VkDescriptorPool inDpool, VkDescriptorSetLayout layout, std::vector<std::uint32_t> typeSize, std::uint32_t bufferCount)
	{
		
		// create uniform buffer with lut::create_buffer()
		std::vector<lut::Buffer> buffers(bufferCount);
		
		for(std::uint32_t i =0; i<bufferCount; ++i)
		{ 
			buffers[i] = lut::create_buffer(
				inAllocator,
				typeSize[i],
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY
			);
		}

		// allocate descriptor set for uniform buffer
		VkDescriptorSet outDescriptorSet = lut::alloc_desc_set(inWindow, inDpool, layout);
		update_descriptor_set(inWindow, buffers, outDescriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

		return DescriptorSetPack
		{
			std::move(layout),
			std::move(buffers),
			std::move(outDescriptorSet),
			typeSize,
			{}
		};
	}

	lut::DescriptorSetLayout create_descriptor_layout(lut::VulkanWindow const& aWindow, VkDescriptorType descriptorType, VkShaderStageFlags shaderStageFlag)
	{
		//1. Define the descriptor set layout binding
		VkDescriptorSetLayoutBinding bindings[1]{};
		//match the binding id in shaders
		bindings[0].binding = 0;
		bindings[0].descriptorType = descriptorType;
		bindings[0].descriptorCount = 1;
		//specifying which pipeline shader stages can access a resource for this binding.
		bindings[0].stageFlags = shaderStageFlag;

		//2. Create the descriptor set layout
		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
		layoutInfo.pBindings = bindings;

		VkDescriptorSetLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create descriptor set layout\n"
				"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}

		return lut::DescriptorSetLayout(aWindow.device, layout);
	}

	void update_descriptor_set(lut::VulkanWindow const& window, std::vector<lut::Buffer> const& descriptorBuffers, VkDescriptorSet descritporSet, VkDescriptorType descriptorType)
	{
		// Write descriptor set
		VkWriteDescriptorSet desc[1]{};  

		// Descriptor Buffer Info 
		std::vector<VkDescriptorBufferInfo> descBufferInfos(descriptorBuffers.size()); 
		for (std::uint32_t i = 0; i < descriptorBuffers.size();++i)
		{
			descBufferInfos[i].buffer = descriptorBuffers[i].buffer;
			descBufferInfos[i].range = VK_WHOLE_SIZE;

		}

		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = descritporSet;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = descriptorType;
		desc[0].descriptorCount = descBufferInfos.size();
		desc[0].pBufferInfo = descBufferInfos.data();
		
		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}

	// new ways to help creating descriptor sets...>
	
	// only work for layout binding with descriptor count == 1
	VkDescriptorSet create_descriptor_set(lut::VulkanWindow const& inWindow, VkDescriptorPool inDpool, VkDescriptorSetLayout layout, 
		desc::BufferInfo* bufferInfos, std::uint32_t bufferCount, desc::ImageInfo* imageInfos, std::uint32_t imageCount)
	{
		
		// allocate a new descriptor set based on a layout
		VkDescriptorSet outDescriptorSet = lut::alloc_desc_set(inWindow, inDpool, layout);

		// update the descriptor set...>

		// create write descriptor set for desc set update 
		std::vector<VkWriteDescriptorSet> writeDescSet(bufferCount + imageCount);

		for (std::uint32_t i = 0; i < bufferCount; ++i)
		{
			writeDescSet[i] = create_write_desc_set(outDescriptorSet, bufferInfos[i].binding, &bufferInfos[i].bufferInfo);
		}

		for (std::uint32_t i = 0; i < imageCount; ++i)
		{
			writeDescSet[i + bufferCount] = create_write_desc_set(outDescriptorSet, imageInfos[i].binding, &imageInfos[i].imageInfo);
		}
		
		// call update function for desc set
		vkUpdateDescriptorSets(inWindow.device, bufferCount + imageCount, writeDescSet.data(), 0, nullptr);


		// return the desc set
		return outDescriptorSet;
	}

	VkDescriptorSetLayoutBinding create_descriptor_layout_binding(std::uint32_t bindingID, VkDescriptorType descriptorType, VkShaderStageFlags shaderStageFlag, std::uint32_t descriptorCount)
	{
		// Define the descriptor set layout binding
		VkDescriptorSetLayoutBinding binding{};
		//match the binding id in shaders
		binding.binding = bindingID;
		binding.descriptorType = descriptorType;
		binding.descriptorCount = descriptorCount;
		//specifying which pipeline shader stages can access a resource for this binding.
		binding.stageFlags = shaderStageFlag;

		return binding;
	}

	lut::DescriptorSetLayout create_descriptor_layout(lut::VulkanWindow const& aWindow, VkDescriptorSetLayoutBinding* bindings, std::uint32_t bindingCount)
	{
		// Create the descriptor set layout
		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = bindingCount;
		layoutInfo.pBindings = bindings;

		VkDescriptorSetLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create descriptor set layout\n"
				"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}

		return lut::DescriptorSetLayout(aWindow.device, layout);
	}
	


	VkDescriptorBufferInfo create_desc_buffer_info(VkBuffer buffer, VkDeviceSize range, VkDeviceSize offset)
	{
		VkDescriptorBufferInfo descBufferInfo{};
		descBufferInfo.buffer = buffer;
		descBufferInfo.range = range;
		descBufferInfo.offset = offset;

		return descBufferInfo;
	}

	VkDescriptorImageInfo create_desc_image_info( VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout)
	{
		VkDescriptorImageInfo descImageInfo{};

		descImageInfo.imageLayout = imageLayout;
		descImageInfo.imageView = imageView;
		descImageInfo.sampler = sampler;

		return descImageInfo;
	}

	VkWriteDescriptorSet create_write_desc_set( VkDescriptorSet descritporSet, std::uint32_t layoutBinding, VkDescriptorBufferInfo* descBufferInfos, std::uint32_t descriptorCount)
	{
		VkWriteDescriptorSet desc{};

		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = descritporSet;
		desc.dstBinding = layoutBinding;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desc.descriptorCount = descriptorCount;
		desc.pBufferInfo = descBufferInfos;

		return desc;
	}

	VkWriteDescriptorSet create_write_desc_set( VkDescriptorSet descritporSet, std::uint32_t layoutBinding, VkDescriptorImageInfo* descImageInfos, std::uint32_t descriptorCount )
	{
		VkWriteDescriptorSet desc{};

		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = descritporSet;
		desc.dstBinding = layoutBinding;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.descriptorCount = descriptorCount;
		desc.pImageInfo = descImageInfos;

		return desc;
	}

	

}