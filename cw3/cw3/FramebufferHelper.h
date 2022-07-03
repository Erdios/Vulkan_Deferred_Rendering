#pragma once
#include "../labutils/vkutil.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp"
#include "../labutils/vulkan_window.hpp"
#include "../labutils/error.hpp"
#include "../labutils/to_string.hpp"

namespace lut = labutils;

struct Attachment
{
	// vkimage
	lut::Image lutImage;
	lut::ImageView imageView;
	// image format
	VkFormat format;


	Attachment(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,
		VkFormat inFormat, VkImageUsageFlags usage);
	void create_image_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,
		VkFormat inFormat, VkImageUsageFlags usage);
	
};


class FramebufferPack
{

public:
	//	---	Parameters ---  //
	unsigned int colorAttachmentCount;
	Attachment* colorAttachments;
	Attachment* depthAttachment;
	lut::Framebuffer framebuffer;
	lut::RenderPass renderPass;


	//	---	Constructors ---  //
	FramebufferPack(lut::VulkanWindow const& aWindow, Attachment* inColorAttachments, unsigned int inColorAttachmentCount, VkImageLayout colorAttachdstLayout, Attachment* inDepthAttachment,
		VkSubpassDependency* spDeps = nullptr, std::uint32_t spDepCount = 0);


	//	---	Functions ---  //
	void create_render_pass(lut::VulkanWindow const& aWindow, VkImageLayout colorAttachdstLayout, VkSubpassDependency* spDeps = nullptr, std::uint32_t spDependCount = 0);
	
	void create_framebuffer(lut::VulkanWindow const& aWindow);
	
};

class SwapChainFramebufferPack
{

public:
	//	---	Parameters ---  //
	std::vector<lut::Framebuffer> framebuffers;
	lut::RenderPass renderPass;

	//	---	Constructors ---  //
	SwapChainFramebufferPack(lut::VulkanWindow const& aWindow, VkSubpassDependency* spDeps = nullptr, std::uint32_t spDepCount = 0);


	//	---	Functions ---  //
	void create_render_pass(lut::VulkanWindow const& aWindow, VkSubpassDependency* spDeps = nullptr, std::uint32_t spDependCount = 0);

	void create_framebuffer(lut::VulkanWindow const& aWindow);
};
