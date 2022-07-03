#include "vkimage.hpp"

#include <vector>
#include <utility>
#include <algorithm>

#include <cstdio>
#include <cassert>
#include <cstring> // for std::memcpy()

#include <stb_image.h>

#include "error.hpp"
#include "vkutil.hpp"
#include "vkbuffer.hpp"
#include "to_string.hpp"

namespace
{
	// Unfortunately, std::countl_zero() isn't available in C++17; it was added
	// in C++20. This provides a fallback implementation. Unlike C++20, this
	// returns a std::uint32_t and not a signed int.
	//
	// See https://graphics.stanford.edu/~seander/bithacks.html for this and
	// other methods like it.
	//
	// Note: that this is unlikely to be the most efficient implementation on
	// most processors. Many instruction sets have dedicated instructions for
	// this operation. E.g., lzcnt (x86 ABM/BMI), bsr (x86).
	inline 
	std::uint32_t countl_zero_( std::uint32_t aX )
	{
		if( !aX ) return 32;

		uint32_t res = 0;

		if( !(aX & 0xffff0000) ) (res += 16), (aX <<= 16);
		if( !(aX & 0xff000000) ) (res +=  8), (aX <<=  8);
		if( !(aX & 0xf0000000) ) (res +=  4), (aX <<=  4);
		if( !(aX & 0xc0000000) ) (res +=  2), (aX <<=  2);
		if( !(aX & 0x80000000) ) (res +=  1);

		return res;
	}
}

namespace labutils
{
	Image::Image() noexcept = default;

	Image::~Image()
	{
		if( VK_NULL_HANDLE != image )
		{
			assert( VK_NULL_HANDLE != mAllocator );
			assert( VK_NULL_HANDLE != allocation );
			vmaDestroyImage( mAllocator, image, allocation );
		}
	}

	Image::Image( VmaAllocator aAllocator, VkImage aImage, VmaAllocation aAllocation ) noexcept
		: image( aImage )
		, allocation( aAllocation )
		, mAllocator( aAllocator )
	{}

	Image::Image( Image&& aOther ) noexcept
		: image( std::exchange( aOther.image, VK_NULL_HANDLE ) )
		, allocation( std::exchange( aOther.allocation, VK_NULL_HANDLE ) )
		, mAllocator( std::exchange( aOther.mAllocator, VK_NULL_HANDLE ) )
	{}
	Image& Image::operator=( Image&& aOther ) noexcept
	{
		std::swap( image, aOther.image );
		std::swap( allocation, aOther.allocation );
		std::swap( mAllocator, aOther.mAllocator );
		return *this;
	}
}

namespace labutils
{
	Image create_image_texture2d(Allocator const& aAllocator, std::uint32_t aWidth, std::uint32_t aHeight, VkFormat aFormat, VkImageUsageFlags aUsage, std::uint32_t mipLevels)
	{
		// variables that provide info
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = aFormat;
		imageInfo.extent.width = aWidth;
		imageInfo.extent.height = aHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = aUsage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		// variables that needs to be created in the function
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;

		if (auto const res = vmaCreateImage(aAllocator.allocator, &imageInfo, &allocInfo,
			&image, &allocation, nullptr); VK_SUCCESS != res)
		{
			throw Error("Unable to allocate image.\nvmaCreateImage() returned %s", to_string(res).c_str());
		}

		return Image{ aAllocator.allocator, image, allocation };
	}

	std::uint32_t compute_mip_level_count(std::uint32_t aWidth, std::uint32_t aHeight)
	{
		std::uint32_t const bits = aWidth | aHeight;
		std::uint32_t const leadingZeros = countl_zero_(bits);
		return 32 - leadingZeros;
	}

	Image load_image_texture2d_no_minmap(char const* aPattern, VulkanContext const& aContext, VkCommandPool aCmdPool, Allocator const& aAllocator)
	{
		// Get width and height of the image
		int baseWidthi, baseHeighti, baseChannelsi;
		if (1 != stbi_info(aPattern, &baseWidthi, &baseHeighti, &baseChannelsi))
		{
			throw Error("%s: unable to get image information (%s)", aPattern,
				stbi_failure_reason());
		}

		assert(baseWidthi > 0 && baseHeighti > 0);

		auto const baseWidth = std::uint32_t(baseWidthi);
		auto const baseHeight = std::uint32_t(baseHeighti);

		// Create image
		Image ret = create_image_texture2d(aAllocator, baseWidth, baseHeight, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		// Create command buffer
		VkCommandBuffer cbuff = alloc_command_buffer(aContext, aCmdPool);

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;
		beginInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(cbuff, &beginInfo); VK_SUCCESS != res)
		{
			throw Error("Beginning command buffer recording\nvkBeginCommandBuffer() returned %s",
				to_string(res).c_str());
		}

		// Image Barrier
		image_barrier(
			cbuff, ret.image, 0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			}
		);

		// update data into mip level
		std::uint32_t width = baseWidth, height = baseHeight;
		std::vector<Buffer> stagingBuffers(1);

		// Load image data
		int widthi, heighti, channelsi;
		stbi_uc* data = stbi_load(aPattern, &widthi, &heighti, &channelsi, 4 /*4 channels = RGBA*/);

		if (!data)
		{
			throw Error("%s: unable to load image (%s)", aPattern, stbi_failure_reason());
		}

		assert(widthi > 0 && std::uint32_t(widthi) == width);
		assert(heighti > 0 && std::uint32_t(heighti) == height);

		auto const sizeInBytes = width * height * 4;

		// Create staging buffer for every level!!
		auto& staging = stagingBuffers.emplace_back(create_buffer(aAllocator, sizeInBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU));

		// Memory mapping (get pointer in CPU that can point to VkDeviceMemory)
		void* sptr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &sptr); VK_SUCCESS != res)
		{
			throw Error("Mapping memory for writing\nvmaMapMemory() returned %s", to_string(res).c_str());
		}


		// Copy data into buffer
		std::memcpy(sptr, data, sizeInBytes);

		// Unmapping memory
		vmaUnmapMemory(aAllocator.allocator, staging.allocation);

		// Free image data
		stbi_image_free(data);

		// Upload data from staging buffer into image
		VkBufferImageCopy copy;
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource = VkImageSubresourceLayers{
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageOffset = VkOffset3D{ 0, 0, 0 };
		copy.imageExtent = VkExtent3D{ width, height, 1 };

		vkCmdCopyBufferToImage(cbuff, staging.buffer, ret.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);


		// Image Barrier
		image_barrier(
			cbuff, ret.image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			}
		);

		// End command
		if (auto const res = vkEndCommandBuffer(cbuff); VK_SUCCESS != res)
		{
			throw Error("Ending command buffer recording\nvkEndCommandBuffer() returned %s", to_string(res).c_str());
		}

		// Create fence to protect 
		Fence uploadComplete = create_fence(aContext);

		// Submit queue
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cbuff;

		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo,
			uploadComplete.handle); VK_SUCCESS != res)
		{
			throw Error("Submitting commands\nvkQueueSubmit() returned %s", to_string(res).c_str());
		}

		// Wait for commands to finish
		if (auto const res = vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE,
			std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw Error("Waiting for upload to complete\nvkWaitForFences() returned %s", to_string(res).c_str());
		}

		// Free commands
		vkFreeCommandBuffers(aContext.device, aCmdPool, 1, &cbuff);

		return ret;
	}

	Image load_image_texture2d_with_bliting(char const* aPattern, VulkanContext const& aContext, VkCommandPool aCmdPool, Allocator const& aAllocator)
	{
		// Get width and height of the image
		int baseWidthi, baseHeighti, baseChannelsi;
		if (1 != stbi_info(aPattern, &baseWidthi, &baseHeighti, &baseChannelsi))
		{
			throw Error("%s: unable to get image information (%s)", aPattern,
				stbi_failure_reason());
		}

		assert(baseWidthi > 0 && baseHeighti > 0);

		auto const baseWidth = std::uint32_t(baseWidthi);
		auto const baseHeight = std::uint32_t(baseHeighti);

		// Calculate miplevel
		auto const mipLevels = compute_mip_level_count(baseWidth, baseHeight);

		// Create image
		Image ret = create_image_texture2d(aAllocator, baseWidth, baseHeight, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipLevels);

		// Create command buffer
		VkCommandBuffer cbuff = alloc_command_buffer(aContext, aCmdPool);

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;
		beginInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(cbuff, &beginInfo); VK_SUCCESS != res)
		{
			throw Error("Beginning command buffer recording\nvkBeginCommandBuffer() returned %s",
				to_string(res).c_str());
		}

		// Image Barrier
		image_barrier(
			cbuff, ret.image, 0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, mipLevels,
				0, 1
			}
		);

		// update data into mip level
		std::uint32_t width = baseWidth, height = baseHeight;
		std::vector<Buffer> stagingBuffers(mipLevels);

		// Load image data
		int widthi, heighti, channelsi;
		stbi_uc* data = stbi_load(aPattern, &widthi, &heighti, &channelsi, 4 /*4 channels = RGBA*/);

		if (!data)
		{
			throw Error("%s: unable to load image (%s)", aPattern, stbi_failure_reason());
		}

		assert(widthi > 0 && std::uint32_t(widthi) == width);
		assert(heighti > 0 && std::uint32_t(heighti) == height);

		auto const sizeInBytes = width * height * 4;

		// Create staging buffer for every level!!
		auto& staging = stagingBuffers.emplace_back(create_buffer(aAllocator, sizeInBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU));

		// Memory mapping (get pointer in CPU that can point to VkDeviceMemory)
		void* sptr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &sptr); VK_SUCCESS != res)
		{
			throw Error("Mapping memory for writing\nvmaMapMemory() returned %s", to_string(res).c_str());
		}


		// Copy data into buffer
		std::memcpy(sptr, data, sizeInBytes);

		// Unmapping memory
		vmaUnmapMemory(aAllocator.allocator, staging.allocation);

		// Free image data
		stbi_image_free(data);

		// Upload data from staging buffer into image
		VkBufferImageCopy copy;
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource = VkImageSubresourceLayers{
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageOffset = VkOffset3D{ 0, 0, 0 };
		copy.imageExtent = VkExtent3D{ width, height, 1 };

		vkCmdCopyBufferToImage(cbuff, staging.buffer, ret.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		// create variables for mipmap size
		std::int32_t mipWidth = widthi;
		std::int32_t mipHeight = heighti;

		// generate mipmap texture
		for (uint32_t i = 1; i < mipLevels; i++) {
			image_barrier(
				cbuff, ret.image,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VkImageSubresourceRange{
					VK_IMAGE_ASPECT_COLOR_BIT,
					i - 1, 1,
					0, 1
				}
			);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth >> 1 : 1, mipHeight > 1 ? mipHeight >> 1 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(cbuff, ret.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ret.image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			image_barrier(
				cbuff, ret.image,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VkImageSubresourceRange{
					VK_IMAGE_ASPECT_COLOR_BIT,
					i - 1, 1,
					0, 1
				}
			);

			// to prevent the bad situation when the size of texture is not a square
			if (mipWidth > 1) mipWidth >>= 1;
			if (mipHeight > 1) mipHeight >>= 1;
		}



		// Image Barrier
		image_barrier(
			cbuff, ret.image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				mipLevels - 1, 1,
				0, 1
			}
		);


		// End command
		if (auto const res = vkEndCommandBuffer(cbuff); VK_SUCCESS != res)
		{
			throw Error("Ending command buffer recording\nvkEndCommandBuffer() returned %s", to_string(res).c_str());
		}

		// Create fence to protect 
		Fence uploadComplete = create_fence(aContext);

		// Submit queue
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cbuff;

		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo,
			uploadComplete.handle); VK_SUCCESS != res)
		{
			throw Error("Submitting commands\nvkQueueSubmit() returned %s", to_string(res).c_str());
		}

		// Wait for commands to finish
		if (auto const res = vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE,
			std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw Error("Waiting for upload to complete\nvkWaitForFences() returned %s", to_string(res).c_str());
		}

		// Free commands
		vkFreeCommandBuffers(aContext.device, aCmdPool, 1, &cbuff);





		return ret;

	}

	Image load_image_texture2d(char const* aPattern, VulkanContext const& aContext, VkCommandPool aCmdPool, Allocator const& aAllocator)
	{
		//DONE- (Section 4) implement me!

		// Concat image file name
		char baseName[4096];
		if (int iret = std::snprintf(baseName, sizeof(baseName), aPattern, 0); iret < 0 || iret >= int(sizeof(baseName)))
		{
			throw Error("Pattern ’%s’: unable to derive base image file name (%d).", aPattern, iret);
		}

		// Get width and height of the image
		int baseWidthi, baseHeighti, baseChannelsi;
		if (1 != stbi_info(baseName, &baseWidthi, &baseHeighti, &baseChannelsi))
		{
			throw Error("%s: unable to get image information (%s)", baseName,
				stbi_failure_reason());
		}

		assert(baseWidthi > 0 && baseHeighti > 0);

		auto const baseWidth = std::uint32_t(baseWidthi);
		auto const baseHeight = std::uint32_t(baseHeighti);

		// Calculate miplevel
		auto const mipLevels = compute_mip_level_count(baseWidth, baseHeight);

		// Create image
		Image ret = create_image_texture2d(aAllocator, baseWidth, baseHeight, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipLevels);

		// Create command buffer
		VkCommandBuffer cbuff = alloc_command_buffer(aContext, aCmdPool);

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;
		beginInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(cbuff, &beginInfo); VK_SUCCESS != res)
		{
			throw Error("Beginning command buffer recording\nvkBeginCommandBuffer() returned %s",
				to_string(res).c_str());
		}

		// Image Barrier
		image_barrier(
			cbuff, ret.image, 0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, mipLevels,
				0, 1
			}
		);

		// update data into mip level
		std::uint32_t width = baseWidth, height = baseHeight;
		std::vector<Buffer> stagingBuffers(mipLevels);

		for (std::uint32_t level = 0; level < mipLevels; ++level)
		{
			// Get texture name for every level
			char levelName[4096];
			if (int iret = std::snprintf(levelName, sizeof(levelName), aPattern, level);
				iret < 0 || iret >= int(sizeof(levelName)))
			{
				throw Error("Pattern ’%s’: unable to derive level %u image file name (%d).",
					aPattern, level, iret);
			}

			// Load image data
			int widthi, heighti, channelsi;
			stbi_uc* data = stbi_load(levelName, &widthi, &heighti, &channelsi, 4 /*4 channels = RGBA*/);

			if (!data)
			{
				throw Error("%s: unable to load image for level %u (%s)", levelName, level, stbi_failure_reason());
			}

			assert(widthi > 0 && std::uint32_t(widthi) == width);
			assert(heighti > 0 && std::uint32_t(heighti) == height);

			auto const sizeInBytes = width * height * 4;

			// Create staging buffer for every level!!
			auto& staging = stagingBuffers.emplace_back(create_buffer(aAllocator, sizeInBytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU));

			// Memory mapping (get pointer in CPU that can point to VkDeviceMemory)
			void* sptr = nullptr;
			if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &sptr); VK_SUCCESS != res)
			{
				throw Error("Mapping memory for writing\nvmaMapMemory() returned %s", to_string(res).c_str());
			}


			// Copy data into buffer
			std::memcpy(sptr, data, sizeInBytes);

			// Unmapping memory
			vmaUnmapMemory(aAllocator.allocator, staging.allocation);

			// Free image data
			stbi_image_free(data);

			// Upload data from staging buffer into image
			VkBufferImageCopy copy;
			copy.bufferOffset = 0;
			copy.bufferRowLength = 0;
			copy.bufferImageHeight = 0;
			copy.imageSubresource = VkImageSubresourceLayers{
				VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
			copy.imageOffset = VkOffset3D{ 0, 0, 0 };
			copy.imageExtent = VkExtent3D{ width, height, 1 };

			vkCmdCopyBufferToImage(cbuff, staging.buffer, ret.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

			width >>= 1;
			if (0 == width)
				width = 1;

			height >>= 1;
			if (0 == height)
				height = 1;
		}

		// Image Barrier
		image_barrier(
			cbuff, ret.image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, mipLevels,
				0, 1
			}
		);

		// End command
		if (auto const res = vkEndCommandBuffer(cbuff); VK_SUCCESS != res)
		{
			throw Error("Ending command buffer recording\nvkEndCommandBuffer() returned %s", to_string(res).c_str());
		}

		// Create fence to protect 
		Fence uploadComplete = create_fence(aContext);

		// Submit queue
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cbuff;

		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo,
			uploadComplete.handle); VK_SUCCESS != res)
		{
			throw Error("Submitting commands\nvkQueueSubmit() returned %s", to_string(res).c_str());
		}

		// Wait for commands to finish
		if (auto const res = vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE,
			std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw Error("Waiting for upload to complete\nvkWaitForFences() returned %s", to_string(res).c_str());
		}

		// Free commands
		vkFreeCommandBuffers(aContext.device, aCmdPool, 1, &cbuff);

		return ret;
	}

	Image create_image_texture2d_with_solid_color(char const* aPattern, VulkanContext const& aContext, VkCommandPool aCmdPool, Allocator const& aAllocator, glm::vec4 inColor)
	{

		// Create image
		Image ret = create_image_texture2d(aAllocator, 1, 1, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		// Create command buffer
		VkCommandBuffer cbuff = alloc_command_buffer(aContext, aCmdPool);

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;
		beginInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(cbuff, &beginInfo); VK_SUCCESS != res)
		{
			throw Error("Beginning command buffer recording\nvkBeginCommandBuffer() returned %s",
				to_string(res).c_str());
		}

		// Image Barrier
		image_barrier(
			cbuff, ret.image, 0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			}
		);

		// update data into mip level
		std::vector<Buffer> stagingBuffers(1);

		auto const sizeInBytes = 4;

		// Create staging buffer for every level!!
		auto& staging = stagingBuffers.emplace_back(create_buffer(aAllocator, sizeInBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU));

		// Memory mapping (get pointer in CPU that can point to VkDeviceMemory)
		void* sptr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &sptr); VK_SUCCESS != res)
		{
			throw Error("Mapping memory for writing\nvmaMapMemory() returned %s", to_string(res).c_str());
		}


		// Copy data into buffer
		const char color[4] = { inColor[0] * 255.f, inColor[1] * 255.f, inColor[2] * 255.f, inColor[3] * 255.f };

		std::memcpy(sptr, color, sizeInBytes);

		// Unmapping memory
		vmaUnmapMemory(aAllocator.allocator, staging.allocation);

		// Upload data from staging buffer into image
		VkBufferImageCopy copy;
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource = VkImageSubresourceLayers{
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copy.imageOffset = VkOffset3D{ 0, 0, 0 };
		copy.imageExtent = VkExtent3D{ 1, 1, 1 };

		vkCmdCopyBufferToImage(cbuff, staging.buffer, ret.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);


		// Image Barrier
		image_barrier(
			cbuff, ret.image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			}
		);

		// End command
		if (auto const res = vkEndCommandBuffer(cbuff); VK_SUCCESS != res)
		{
			throw Error("Ending command buffer recording\nvkEndCommandBuffer() returned %s", to_string(res).c_str());
		}

		// Create fence to protect 
		Fence uploadComplete = create_fence(aContext);

		// Submit queue
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cbuff;

		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo,
			uploadComplete.handle); VK_SUCCESS != res)
		{
			throw Error("Submitting commands\nvkQueueSubmit() returned %s", to_string(res).c_str());
		}

		// Wait for commands to finish
		if (auto const res = vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE,
			std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw Error("Waiting for upload to complete\nvkWaitForFences() returned %s", to_string(res).c_str());
		}

		// Free commands
		vkFreeCommandBuffers(aContext.device, aCmdPool, 1, &cbuff);

		return ret;
	}
}
