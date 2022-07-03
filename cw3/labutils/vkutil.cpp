#include "vkutil.hpp"

#include <vector>

#include <cstdio>
#include <cassert>

#include "error.hpp"
#include "to_string.hpp"

namespace labutils
{
	ShaderModule load_shader_module( VulkanContext const& aContext, char const* aSpirvPath )
	{
		//DONE- (Section 1/Exercise 2) implement me!

		// check if the path for spirv file is valid
		assert(aSpirvPath);


		// open the spirv file in binary mode
		if (std::FILE* fin = std::fopen(aSpirvPath, "rb"))
		{

			// get the length of the spirv file 
			// and store the length of bytes
			std::fseek(fin, 0, SEEK_END);
			auto const bytes = std::size_t(std::ftell(fin));

			// move back to the origin
			std::fseek(fin, 0, SEEK_SET);


			// check if the bytes is a valid number
			assert(0 == bytes % 4);
			// store the length of words.
			auto const words = bytes / 4;

			// create a vector of uint32_t with the length of words
			std::vector<std::uint32_t> code(words);


			// create offset variable for counting
			std::size_t offset = 0;

			// loop through every word in SPIR-V code
			while (offset != words)
			{

				// read SPIR-V code into code vector
				auto const read = std::fread(code.data() + offset, sizeof(std::uint32_t), words - offset, fin);

				if (0 == read)
				{

					std::fclose(fin);

					throw Error("Error reading ¡¯%s¡¯: ferror = %d, feof = %d", aSpirvPath,
						std::ferror(fin), std::feof(fin));
				}


				// if create successfully, offset will be updated as the size of words
				offset += read;

			}

			std::fclose(fin);

			// create module info instance for the corresponding structure

			VkShaderModuleCreateInfo moduleInfo{};

			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

			moduleInfo.codeSize = bytes;

			moduleInfo.pCode = code.data();



			// create shader module

			VkShaderModule smod = VK_NULL_HANDLE;

			if (auto const res = vkCreateShaderModule(aContext.device, &moduleInfo, nullptr, &smod); VK_SUCCESS != res)
			{

				throw Error(Error("Unable to create shader module from %s\n"
					"vkCreateShaderModule() returned %s", aSpirvPath, to_string(res).c_str())
				);

			}

			return ShaderModule(aContext.device, smod);
		}


		throw Error("Cannont open ¡¯%s¡¯ for reading", aSpirvPath);
	}


	CommandPool create_command_pool( VulkanContext const& aContext, VkCommandPoolCreateFlags aFlags )
	{
		//DONE: implement me!
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = aContext.graphicsFamilyIndex;
		poolInfo.flags = aFlags;

		VkCommandPool cpool = VK_NULL_HANDLE;
		if (auto const res = vkCreateCommandPool(aContext.device, &poolInfo, nullptr, &cpool);
			VK_SUCCESS != res)
		{

			throw Error("Unable to create command pool\n"
				"vkCreateCommandPool() returned %s", to_string(res).c_str());

		}

		return CommandPool(aContext.device, cpool);

	}

	VkCommandBuffer alloc_command_buffer( VulkanContext const& aContext, VkCommandPool aCmdPool )
	{
		//DONE: implement me!
		VkCommandBufferAllocateInfo cbufInfo{};
		cbufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbufInfo.commandPool = aCmdPool;
		cbufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbufInfo.commandBufferCount = 1;

		VkCommandBuffer cbuff = VK_NULL_HANDLE;
		if (auto const res = vkAllocateCommandBuffers(aContext.device, &cbufInfo, &cbuff);
			VK_SUCCESS != res)
		{

			throw Error("Unable to allocate command buffer\n"
				"vkAllocateCommandBuffers() returned %s", to_string(res).c_str());

		}

		return cbuff;
	}


	Fence create_fence( VulkanContext const& aContext, VkFenceCreateFlags aFlags )
	{
		//DONE: implement me!
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = aFlags;

		VkFence fence = VK_NULL_HANDLE;
		if (auto const res = vkCreateFence(aContext.device, &fenceInfo, nullptr, &fence);
			VK_SUCCESS != res)
		{

			throw Error("Unable to create fence\n"
				"vkCreateFence() returned %s", to_string(res).c_str());

		}

		return Fence(aContext.device, fence);
	}

	Semaphore create_semaphore( VulkanContext const& aContext )
	{
		//DONE: implement me!

		VkSemaphoreCreateInfo semaphoreInfo{};

		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkSemaphore semaphore = VK_NULL_HANDLE;

		if (auto const res = vkCreateSemaphore(aContext.device, &semaphoreInfo, nullptr, &semaphore); res != VK_SUCCESS)
		{
			throw Error("Unable to create semaphore\n"
				"vkCreateSemaphore() returned %s", to_string(res).c_str()
			);

		}

		return Semaphore(aContext.device, semaphore);
	}


	void buffer_barrier(
		VkCommandBuffer aCmdBuff,
		VkBuffer aBuffer,
		VkAccessFlags aSrcAccessMask,
		VkAccessFlags aDstAccessMask,
		VkPipelineStageFlags aSrcStageMask,
		VkPipelineStageFlags aDstStageMask,
		VkDeviceSize aSize,
		VkDeviceSize aOffset,
		uint32_t aSrcQueueFamilyIndex,
		uint32_t aDstQueueFamilyIndex
	)
	{
		VkBufferMemoryBarrier bbarrier{};
		bbarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bbarrier.srcAccessMask = aSrcAccessMask;
		bbarrier.dstAccessMask = aDstAccessMask;
		bbarrier.buffer = aBuffer;
		bbarrier.size = aSize;
		bbarrier.offset = aOffset;
		bbarrier.srcQueueFamilyIndex = aSrcQueueFamilyIndex;
		bbarrier.dstQueueFamilyIndex = aDstQueueFamilyIndex;

		vkCmdPipelineBarrier(aCmdBuff, aSrcStageMask, aDstStageMask, 0, 0, nullptr, 1, &bbarrier, 0, nullptr);
	}

	DescriptorPool create_descriptor_pool(VulkanContext const& aContext, std::uint32_t aMaxDescriptors,
		std::uint32_t aMaxSets)
	{
		VkDescriptorPoolSize const pools[] = {
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, aMaxDescriptors}, // each containing a descriptor type and number of 
																  // descriptors of that type to be allocated in the pool
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, aMaxDescriptors}
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.maxSets = aMaxSets;
		poolInfo.poolSizeCount = sizeof(pools) / sizeof(pools[0]);
		poolInfo.pPoolSizes = pools;

		VkDescriptorPool pool = VK_NULL_HANDLE;
		if (auto const res = vkCreateDescriptorPool(aContext.device, &poolInfo, nullptr, &pool); VK_SUCCESS != res)
		{
			throw Error("Unable to create descriptor pool\nvkCreateDescriptorPool() returned % s", to_string(res).c_str());
		}

		return DescriptorPool(aContext.device, pool);
	}


	VkDescriptorSet alloc_desc_set(VulkanContext const& aContext, VkDescriptorPool aPool, VkDescriptorSetLayout aSetLayout)
	{
		VkDescriptorSetAllocateInfo allocInfo{};

		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = aPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &aSetLayout;

		VkDescriptorSet dset = VK_NULL_HANDLE;
		if (auto const res = vkAllocateDescriptorSets(aContext.device, &allocInfo, &dset); res != VK_SUCCESS)
		{
			throw Error("Unable to allocate descriptor set\nvkAllocateDescriptorSets() returned % s", to_string(res).c_str());
		}

		return dset;
	}

	void image_barrier(VkCommandBuffer aCmdBuff, 
		VkImage aImage, VkAccessFlags aSrcAccessMask, 
		VkAccessFlags aDstAccessMask,
		VkImageLayout aSrcLayout, VkImageLayout aDstLayout, 
		VkPipelineStageFlags aSrcStageMask,
		VkPipelineStageFlags aDstStageMask,
		VkImageSubresourceRange aRange,
		std::uint32_t aSrcQueueFamilyIndex,
		std::uint32_t aDstQueueFamilyIndex)
	{
		VkImageMemoryBarrier ibarrier{};
		ibarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		ibarrier.image = aImage;
		ibarrier.srcAccessMask = aSrcAccessMask;
		ibarrier.dstAccessMask = aDstAccessMask;
		ibarrier.srcQueueFamilyIndex = aSrcQueueFamilyIndex;
		ibarrier.dstQueueFamilyIndex = aDstQueueFamilyIndex;
		ibarrier.oldLayout = aSrcLayout;
		ibarrier.newLayout = aDstLayout;
		ibarrier.subresourceRange = aRange;

		vkCmdPipelineBarrier(aCmdBuff, aSrcStageMask, aDstStageMask, 0, 0, nullptr, 0, nullptr, 1, &ibarrier);
	}

	ImageView create_image_view_texture2d(VulkanContext const& aContext, VkImage aImage, VkFormat aFormat)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = aImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = aFormat;
		viewInfo.components = VkComponentMapping{};
		viewInfo.subresourceRange = VkImageSubresourceRange{
			VK_IMAGE_ASPECT_COLOR_BIT,
			0, VK_REMAINING_MIP_LEVELS,
			0, 1
		};

		VkImageView view = VK_NULL_HANDLE;

		if (auto const res = vkCreateImageView(aContext.device, &viewInfo, nullptr, &view); VK_SUCCESS != res)
		{
			throw Error("Unable to create image view\nvkCreateImageView() returned %s", to_string(res).c_str());
		}

		return ImageView(aContext.device, view);
	}

	Sampler create_default_sampler(VulkanContext const& aContext, VkBool32 useAnisotropy)
	{

		// properties of physical deivce (for a)
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(aContext.physicalDevice, &props);

		// create sampler info
		VkSamplerCreateInfo samplerInfo{};

		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.minLod = 0.f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.f;
		samplerInfo.anisotropyEnable = useAnisotropy;
		samplerInfo.maxAnisotropy = props.limits.maxSamplerAnisotropy;

		// create sampler
		VkSampler sampler = VK_NULL_HANDLE;

		if (auto const res = vkCreateSampler(aContext.device, &samplerInfo, nullptr, &sampler); VK_SUCCESS != res)
		{
			throw Error("Unable to create sampler\nvkCreateSampler() returned %s", to_string(res).c_str());
		}

		return Sampler(aContext.device, sampler);
	}

}
