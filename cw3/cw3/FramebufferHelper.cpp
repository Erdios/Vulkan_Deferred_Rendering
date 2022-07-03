#include "FramebufferHelper.h"
#include <iostream>

Attachment::Attachment(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,
	VkFormat inFormat, VkImageUsageFlags usage)
{
	create_image_buffer(aWindow, aAllocator, inFormat, usage);
}

void Attachment::create_image_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,
	VkFormat inFormat, VkImageUsageFlags usage)
{
	// store input format
	format = inFormat;

	// create image
	VkImageCreateInfo imageInfo{};

	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = aWindow.swapchainExtent.width;
	imageInfo.extent.height = aWindow.swapchainExtent.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;


	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	if (auto const res = vmaCreateImage(aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr); res != VK_SUCCESS)
	{
		throw lut::Error("Unable to allocate depth buffer image.\nvmaCreateImage() returned %s", lut::to_string(res).c_str());
	}

	lutImage = lut::Image(aAllocator.allocator, image, allocation);

	// create image view
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = lutImage.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.components = VkComponentMapping{};
	VkImageAspectFlags aspectFlags = 0;
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		aspectFlags |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		aspectFlags |= VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange = VkImageSubresourceRange{
		aspectFlags,
		0,1,
		0,1
	};

	VkImageView view = VK_NULL_HANDLE;
	if (auto const res = vkCreateImageView(aWindow.device, &viewInfo, nullptr, &view); res != VK_SUCCESS)
	{
		throw lut::Error("Unable to create image view\nvkCreateImageView() returned %s", lut::to_string(res).c_str());
	}

	imageView = lut::ImageView{ aWindow.device, view };
}



FramebufferPack::FramebufferPack(lut::VulkanWindow const& aWindow, Attachment* inColorAttachments, 
	unsigned int inColorAttachmentCount, VkImageLayout colorAttachdstLayout, Attachment* inDepthAttachment
	,VkSubpassDependency* spDeps, std::uint32_t spDepCount)
	:colorAttachments(inColorAttachments), colorAttachmentCount(inColorAttachmentCount), depthAttachment(std::move(inDepthAttachment)), framebuffer(), renderPass()
{
	
	create_render_pass(aWindow, colorAttachdstLayout, spDeps, spDepCount);
	
	create_framebuffer(aWindow);
}




void FramebufferPack::create_render_pass(lut::VulkanWindow const& aWindow, VkImageLayout colorAttachdstLayout, VkSubpassDependency* spDeps, std::uint32_t spDepCount)
{
	if (renderPass.handle != VK_NULL_HANDLE)
		throw lut::Error("Gonna overwrite the render pass...\n");


	//------------//
	// Attachment //
	//------------//
	unsigned int attachmentCount = (depthAttachment == nullptr) ? colorAttachmentCount : colorAttachmentCount + 1;

	std::vector<VkAttachmentDescription> attachmentDescs(attachmentCount);

	// for COLOR attachmenets ... >
	std::vector<VkAttachmentReference> colorAttachmentRefs(colorAttachmentCount);

	for (unsigned int i = 0; i < colorAttachmentCount; ++i)
	{

		attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT; // no multisampling 

		// load and store operations
		attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		

		// format
		attachmentDescs[i].format = colorAttachments[i].format;

		// layout
		attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescs[i].finalLayout = colorAttachdstLayout;// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		//attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		// attachment references
		colorAttachmentRefs[i].attachment = i;
		colorAttachmentRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	}

	// For DEPTH attachment... >
	VkAttachmentReference depthAttachmentRef{};

	if (depthAttachment != nullptr)
	{
		attachmentDescs[colorAttachmentCount].samples = VK_SAMPLE_COUNT_1_BIT; // no multisampling 

		// load and store operations
		attachmentDescs[colorAttachmentCount].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescs[colorAttachmentCount].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescs[colorAttachmentCount].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescs[colorAttachmentCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		// format
		attachmentDescs[colorAttachmentCount].format = depthAttachment->format;
		// layout
		attachmentDescs[colorAttachmentCount].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescs[colorAttachmentCount].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		// reference
		depthAttachmentRef.attachment = colorAttachmentCount;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}


	//------------//
	// Subpass    //
	//------------//

	// create subpass
	VkSubpassDescription subpasses[1]{};
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = colorAttachmentCount; 
	subpasses[0].pColorAttachments = colorAttachmentRefs.data();
	if (depthAttachment != nullptr) subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

	//VkSubpassDependency deps[1]{};
	//deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	//deps[0].srcSubpass = 0; // == subpasses[0] declared above 
	//deps[0].dstSubpass = VK_SUBPASS_EXTERNAL;
	//deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	//deps[0].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	//deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	//deps[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

	//-------------------------//
	// Create render pass      //
	//-------------------------//

	VkRenderPassCreateInfo passInfo{};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	passInfo.attachmentCount = attachmentCount;
	passInfo.pAttachments = attachmentDescs.data();
	passInfo.subpassCount = 1;
	passInfo.pSubpasses = subpasses;
	passInfo.dependencyCount = spDepCount;
	passInfo.pDependencies = spDeps;

	VkRenderPass rpass = VK_NULL_HANDLE;
	if (auto const res = vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
	{

		throw lut::Error("Unable to create render pass\n"
			"vkCreateRenderPass() returned %s", lut::to_string(res).c_str()
		);

	}

	renderPass = lut::RenderPass(aWindow.device, rpass);
}




void FramebufferPack::create_framebuffer(lut::VulkanWindow const& aWindow)
{
	std::vector<VkImageView> imageViews(depthAttachment== nullptr?colorAttachmentCount:(colorAttachmentCount + 1));

	for (unsigned int i = 0; i < colorAttachmentCount; ++i)
	{

		imageViews[i] = colorAttachments[i].imageView.handle;
	}

	if(depthAttachment != nullptr) imageViews[colorAttachmentCount] = depthAttachment->imageView.handle;


	VkFramebufferCreateInfo fbInfo{};

	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.flags = 0;
	fbInfo.renderPass = renderPass.handle;
	fbInfo.attachmentCount = (depthAttachment == nullptr ? colorAttachmentCount : (colorAttachmentCount + 1)); // two attachment for one image
	fbInfo.pAttachments = imageViews.data();
	fbInfo.width = aWindow.swapchainExtent.width;
	fbInfo.height = aWindow.swapchainExtent.height;
	fbInfo.layers = 1;

	VkFramebuffer fb = VK_NULL_HANDLE;

	if (auto const res = vkCreateFramebuffer(aWindow.device, &fbInfo, nullptr, &fb); res != VK_SUCCESS)
	{
		throw lut::Error(
			"Unable to create framebuffer\n"
			"vkCreateFramebuffer() returned %s", lut::to_string(res).c_str()
		);

	}

	framebuffer = lut::Framebuffer(aWindow.device, fb);
}




SwapChainFramebufferPack::SwapChainFramebufferPack(lut::VulkanWindow const& aWindow, VkSubpassDependency* spDeps, std::uint32_t spDepCount )
{
	
	create_render_pass(aWindow, spDeps, spDepCount);

	create_framebuffer(aWindow);

}





void SwapChainFramebufferPack::create_render_pass(lut::VulkanWindow const& aWindow, VkSubpassDependency* spDeps, std::uint32_t spDepCount)
{
	if (renderPass.handle != VK_NULL_HANDLE)
		throw lut::Error("Gonna overwrite the render pass...\n");


	//------------//
	// Attachment //
	//------------//
	VkAttachmentDescription attachments[1]{}; // ONLY ONE attachment

	// For attachment 0
	attachments[0].format = aWindow.swapchainFormat; // VK FORMAT R8G8B8A8 SRGB 
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT; // no multisampling 
	// load and store operations
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// layout
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// create attachment reference
	VkAttachmentReference colorAttachments[1]{};
	colorAttachments[0].attachment = 0; // the zero refers to attachments[0] declared earlier.
	//specify the layout for attachment image
	colorAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;



	//------------//
	// Subpass    //
	//------------//

	// create subpass
	VkSubpassDescription subpasses[1]{};
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1; 
	subpasses[0].pColorAttachments = colorAttachments;
	

	//-------------------------//
	// Create render pass      //
	//-------------------------//

	VkRenderPassCreateInfo passInfo{};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	passInfo.attachmentCount = 1;
	passInfo.pAttachments = attachments;
	passInfo.subpassCount = 1;
	passInfo.pSubpasses = subpasses;
	passInfo.dependencyCount = spDepCount;
	passInfo.pDependencies = spDeps;

	VkRenderPass rpass = VK_NULL_HANDLE;
	if (auto const res = vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
	{

		throw lut::Error("Unable to create render pass\n"
			"vkCreateRenderPass() returned %s", lut::to_string(res).c_str()
		);

	}

	renderPass = lut::RenderPass(aWindow.device, rpass);
}


void SwapChainFramebufferPack::create_framebuffer(lut::VulkanWindow const& aWindow)
{
	framebuffers.clear();

	for (std::uint32_t i = 0; i < aWindow.swapViews.size(); i++)
	{

		VkFramebufferCreateInfo fbInfo{};

		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.flags = 0;
		fbInfo.renderPass = renderPass.handle;
		fbInfo.attachmentCount = 1; 
		fbInfo.pAttachments = &aWindow.swapViews[i];
		fbInfo.width = aWindow.swapchainExtent.width;
		fbInfo.height = aWindow.swapchainExtent.height;
		fbInfo.layers = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;

		if (auto const res = vkCreateFramebuffer(aWindow.device, &fbInfo, nullptr, &fb); res != VK_SUCCESS)
		{
			throw lut::Error(
				"Unable to create framebuffer for swap chain image %zu\n"
				"vkCreateFramebuffer() returned %s", i, lut::to_string(res).c_str()
			);

		}


		framebuffers.emplace_back(lut::Framebuffer(aWindow.device, fb));

	}
	if (framebuffers.size() != aWindow.swapViews.size())
	{
		throw lut::Error("The number of framebuffers doesn't match with the number of swapviews.\n");
	}
}