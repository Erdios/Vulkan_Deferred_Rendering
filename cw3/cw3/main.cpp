#include <volk/volk.h>
#include <iostream>
#include <tuple>
#include <chrono>
#include <limits>
#include <vector>
#include <stdexcept>

#include <cstdio>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../labutils/to_string.hpp"
#include "../labutils/vulkan_window.hpp"
#include "../labutils/angle.hpp"
using namespace labutils::literals;

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp" 
namespace lut = labutils;


#include "camera_control.h"
#include "vertex_data.h"
#include "DescriptorSetHelper.h"
#include "FramebufferHelper.h"
#include "model.hpp"

#define INPUT_ATTRIBUTE_NUM 3
#define LIGHT_COUNT 5


namespace
{
	namespace cfg
	{
		// Compiled shader code for the graphics pipeline(s)
		// See sources in cw3/shaders/*. 
#		define SHADERDIR_ "assets/cw3/shaders/"


		constexpr char const* kVertShaderPath = SHADERDIR_ "PBR.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "PBR.frag.spv";


		constexpr char const* mrtVertShaderPath = SHADERDIR_ "MultiRenderTarget.vert.spv";
		constexpr char const* mrtFragShaderPath = SHADERDIR_ "MultiRenderTarget.frag.spv";

#		undef SHADERDIR_




#		define SCENE_ "assets/cw3/"
		constexpr char const* materialtestObjectPath = SCENE_ "materialtest.obj";
		constexpr char const* newShipObjectPath = SCENE_ "NewShip.obj";
#		undef SCENE_



		// General rule: with a standard 24 bit or 32 bit float depth buffer,
		// you can support a 1:1000 ratio between the near and far plane with
		// minimal depth fighting. Larger ratios will introduce more depth
		// fighting problems; smaller ratios will increase the depth buffer's
		// resolution but will also limit the view distance.
		constexpr float kCameraNear  = 0.1f;
		constexpr float kCameraFar   = 100.f;
		constexpr auto kCameraFov    = 60.0_degf;


		constexpr char const* kImageOutput = "output.png";

		constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
		
		

		const VertexInputInfo vertexInputInfo{ INPUT_ATTRIBUTE_NUM,
			{sizeof(float) * 3,sizeof(float) * 2,sizeof(float) * 3},
			{VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT} };

		bool isNewShip = false;

		glm::vec4 ambient = { 0.2,0.2,0.2,1 };


	}

	// Uniform data
	namespace glsl
	{

		struct SceneUniform
		{
			alignas(16) glm::mat4 projCam;
			glm::vec3 camPos;
		};

		static_assert(sizeof(SceneUniform) <= 65536, "SceneUniform must be less than 65536 bytes for vkCmdUpdateBuffer.");
		static_assert(sizeof(SceneUniform) % 4 == 0, "SceneUniform size must be a multiple of 4 bytes.");
	}

	namespace glsl
	{


		struct Light
		{
			alignas(16) glm::vec4 lightPos;
			glm::vec4 diffuse;
			glm::vec4 specular;
			float radian;
		};

		struct LightSet
		{
			std::uint32_t lightsCount = LIGHT_COUNT;
			alignas(16) glm::vec4 ambient = cfg::ambient;
			Light light[LIGHT_COUNT];

			void updateRadian(std::uint32_t count)
			{
				for (std::uint32_t i = 0; i < count; ++i)
				{
					light[i].radian += 0.05;
				}
			}
		};

		struct LightManager
		{
			std::vector<bool> lightStates;

			LightSet lightset;

			float radianOffset;

			std::uint32_t count;

			bool isAnimationOn;

			LightManager() :lightStates(LIGHT_COUNT, true), radianOffset(0), isAnimationOn(false) {}

			void updateLightSet()
			{
				count = 0;

				for (unsigned int i = 0; i < LIGHT_COUNT; ++i)
				{
					if (lightStates[i])
					{

						lightset.light[count].lightPos = { 0.f, 9.3f, -7.f, 1.f };
						lightset.light[count].diffuse = { (i + 1) % 2, (i + 1) % 3, (i + 1) % 4, 1.f };
						lightset.light[count].specular = { 1.0f,1.0f,1.0f,1.0f };
						lightset.light[count].radian = 3.1415 / LIGHT_COUNT * i + radianOffset;

						++count;
					}

				}


				lightset.lightsCount = count;
			}

			void setLightState(std::uint32_t id)
			{
				lightStates[id] = !lightStates[id];
				updateLightSet();
			}


			void updateRadian()
			{
				if (!isAnimationOn)
					return;


				radianOffset += 0.05;

				lightset.updateRadian(count);
			}

		};

		static_assert(sizeof(Light) <= 65536, "Light struct must be less than 65536 bytes for vkCmdUpdateBuffer.");
		static_assert(sizeof(Light) % 4 == 0, "Light struct size must be a multiple of 4 bytes.");

		static_assert(sizeof(LightSet) <= 65536, "Lights struct must be less than 65536 bytes for vkCmdUpdateBuffer.");
		static_assert(sizeof(LightSet) % 4 == 0, "Lights struct size must be a multiple of 4 bytes.");
	}

	namespace glsl
	{

		ControlComponent::Camera camera;
		ControlComponent::Mouse mouse;
		glsl::LightManager lightManager{};
	}

	// Local types/structures:

	// Local functions:
	lut::RenderPass create_render_pass(lut::VulkanWindow const&);
	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const&);
	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, std::vector<labutils::DescriptorSetLayout> const& layouts);
	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout* vaLayouts, std::uint32_t setLayoutCount);
	lut::Pipeline create_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout, VertexInputInfo);
	lut::Pipeline create_pipeline_without_vertex_input(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout);

	void create_swapchain_framebuffers(lut::VulkanWindow const&, VkRenderPass, std::vector<lut::Framebuffer>&, VkImageView aDepthView);
	
	void record_commands(VkCommandBuffer, VkRenderPass, VkFramebuffer, VkPipeline, VkPipelineLayout, VkExtent2D const&,
		std::vector< std::vector<ModelVertexTexturePack>>&, std::vector<desc::DescriptorSetPack>& descriptorSetPack);

	void submit_commands(lut::VulkanContext const& aContext, VkPipelineStageFlags* waitPipelineStages, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore* aWaitSemaphore, std::uint32_t waitSemaphoreCount, VkSemaphore aSignalSemaphore);
	
	void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight);
	
	std::tuple<lut::Image, lut::ImageView> create_image_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,
		VkFormat format, VkImageUsageFlags usage);
	
	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator);
	
	void record_offscreen_commands(VkCommandBuffer aCmdBuff, std::vector<desc::DescriptorSetPack>& uniformDescSets, FramebufferPack& framebufferPack, VkPipeline aGraphicsPipe,
		VkPipelineLayout aGraphicsPipeLayout, VkExtent2D const& aImageExtent, std::vector< std::vector<ModelVertexTexturePack>>& mesh);
	
	void record_draw_commands(VkCommandBuffer aCmdBuff, VkDescriptorSet* uniformDescSets, std::uint32_t uniformDescSetCount, desc::Buffer* updateBuffer, std::uint32_t updateBufferCount,
		FramebufferPack& framebufferPack, SwapChainFramebufferPack& scframebufferPack, std::uint32_t framebufferIndex, VkPipeline aGraphicsPipe, VkPipelineLayout aGraphicsPipeLayout, VkExtent2D const& aImageExtent);

}

// Definitions of functions
namespace
{

	// window & camera control
	void glfw_callback_key_press(GLFWwindow* aWindow, int aKey, int /*aScanCode*/, int aAction, int /*aModifierFlags*/)
	{

		if (GLFW_PRESS == aAction)
		{
			// close window
			if (GLFW_KEY_ESCAPE == aKey)
				glfwSetWindowShouldClose(aWindow, GLFW_TRUE);
			// adjust speed
			else if (GLFW_KEY_LEFT_SHIFT == aKey || GLFW_KEY_RIGHT_SHIFT == aKey)
				glsl::camera.speedChangeMode = ControlComponent::Camera::SpeedUp;
			else if (GLFW_KEY_LEFT_CONTROL == aKey || GLFW_KEY_RIGHT_CONTROL == aKey)
				glsl::camera.speedChangeMode = ControlComponent::Camera::SpeedDown;
			// translation
			else if (aKey == GLFW_KEY_Q)
				glsl::camera.ifKeyQPressed = true;
			else if (aKey == GLFW_KEY_W)
				glsl::camera.ifKeyWPressed = true;
			else if (aKey == GLFW_KEY_E)
				glsl::camera.ifKeyEPressed = true;
			else if (aKey == GLFW_KEY_A)
				glsl::camera.ifKeyAPressed = true;
			else if (aKey == GLFW_KEY_S)
				glsl::camera.ifKeySPressed = true;
			else if (aKey == GLFW_KEY_D)
				glsl::camera.ifKeyDPressed = true;

			// light control
			else if (aKey == GLFW_KEY_1)
				glsl::lightManager.setLightState(0);
			else if (aKey == GLFW_KEY_2)
				glsl::lightManager.setLightState(1);
			else if (aKey == GLFW_KEY_3)
				glsl::lightManager.setLightState(2);
			else if (aKey == GLFW_KEY_4)
				glsl::lightManager.setLightState(3);
			else if (aKey == GLFW_KEY_5)
				glsl::lightManager.setLightState(4);
			else if (aKey == GLFW_KEY_SPACE)
				glsl::lightManager.isAnimationOn = !glsl::lightManager.isAnimationOn;
			else if (aKey == GLFW_KEY_TAB)
				cfg::isNewShip = !cfg::isNewShip;
		}

		if (GLFW_RELEASE == aAction)
		{
			// don't adjust speed
			if (GLFW_KEY_LEFT_CONTROL == aKey || GLFW_KEY_LEFT_SHIFT == aKey || GLFW_KEY_RIGHT_CONTROL == aKey || GLFW_KEY_RIGHT_SHIFT == aKey)
			{
				glsl::camera.speedChangeMode = ControlComponent::Camera::NoChange;
			}
			else if (aKey == GLFW_KEY_Q)
				glsl::camera.ifKeyQPressed = false;
			else if (aKey == GLFW_KEY_W)
				glsl::camera.ifKeyWPressed = false;
			else if (aKey == GLFW_KEY_E)
				glsl::camera.ifKeyEPressed = false;
			else if (aKey == GLFW_KEY_A)
				glsl::camera.ifKeyAPressed = false;
			else if (aKey == GLFW_KEY_S)
				glsl::camera.ifKeySPressed = false;
			else if (aKey == GLFW_KEY_D)
				glsl::camera.ifKeyDPressed = false;
		}
	}

	static void mouse_pos_callback(GLFWwindow* window, double xpos, double ypos)
	{
		glsl::mouse.currentPos.x = xpos;
		glsl::mouse.currentPos.y = ypos;


		if (glsl::mouse.isActivated == true)
		{
			glsl::camera.rotate_camera(glsl::mouse.currentPos - glsl::mouse.previousPos);
			glsl::mouse.previousPos = glsl::mouse.currentPos;
		}

	}

	static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
	{

		if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
		{
			glsl::mouse.isActivated = !glsl::mouse.isActivated;
			glsl::mouse.previousPos = glsl::mouse.currentPos;
		}

	}


	// rendering preparation
	lut::RenderPass create_render_pass(lut::VulkanWindow const& aWindow)
	{
		//------------//
		// Attachment //
		//------------//

		VkAttachmentDescription attachments[2]{}; // ONLY ONE attachment

		// For attachment 0
		attachments[0].format = aWindow.swapchainFormat; // VK FORMAT R8G8B8A8 SRGB 
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT; // no multisampling 
		// load and store operations
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		// layout
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// For attachment 1
		attachments[1].format = cfg::kDepthFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


		//------------//
		// Subpass    //
		//------------//

		// create attachment reference
		VkAttachmentReference colorAttachments[1]{};
		colorAttachments[0].attachment = 0; // the zero refers to attachments[0] declared earlier.
		//specify the layout for attachment image
		colorAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// create depth buffer attachment reference
		VkAttachmentReference depthAttachment{};
		depthAttachment.attachment = 1;
		depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// create subpass
		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 1; // one attachment
		subpasses[0].pColorAttachments = colorAttachments;
		subpasses[0].pDepthStencilAttachment = &depthAttachment;


		//-------------------------//
		// Create render pass      //
		//-------------------------//

		VkRenderPassCreateInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		passInfo.attachmentCount = 2;
		passInfo.pAttachments = attachments;
		passInfo.subpassCount = 1;
		passInfo.pSubpasses = subpasses;
		passInfo.dependencyCount = 0;
		passInfo.pDependencies = nullptr;

		VkRenderPass rpass = VK_NULL_HANDLE;
		if (auto const res = vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
		{

			throw lut::Error("Unable to create render pass\n"
				"vkCreateRenderPass() returned %s", lut::to_string(res).c_str()
			);

		}

		return lut::RenderPass(aWindow.device, rpass);
	}


	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext)
	{

		// create pipeline layout information

		VkPipelineLayoutCreateInfo layoutInfo{};

		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		layoutInfo.setLayoutCount = 0;

		layoutInfo.pSetLayouts = nullptr;

		layoutInfo.pushConstantRangeCount = 0;

		layoutInfo.pPushConstantRanges = nullptr;


		// create pipeline layout

		VkPipelineLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{

			throw lut::Error("Unable to create pipeline layout\n"
				"vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str()
			);
		}

		return lut::PipelineLayout(aContext.device, layout);
	}

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, std::vector<labutils::DescriptorSetLayout> const& vaLayouts)
	{

		// fill the layout list with layouts
		std::vector<VkDescriptorSetLayout> layouts;

		for (labutils::DescriptorSetLayout const& vaLayout : vaLayouts)
			layouts.emplace_back(vaLayout.handle);

		// create pipeline layout information
		VkPipelineLayoutCreateInfo layoutInfo{};

		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = layouts.size();
		layoutInfo.pSetLayouts = layouts.data();
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;


		// create pipeline layout

		VkPipelineLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{

			throw lut::Error("Unable to create pipeline layout\n"
				"vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str()
			);
		}

		return lut::PipelineLayout(aContext.device, layout);
	}

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout* vaLayouts, std::uint32_t setLayoutCount)
	{
		
		// create pipeline layout information
		VkPipelineLayoutCreateInfo layoutInfo{};

		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = setLayoutCount;
		layoutInfo.pSetLayouts = vaLayouts;
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;

		// create pipeline layout
		VkPipelineLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{

			throw lut::Error("Unable to create pipeline layout\n"
				"vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str()
			);
		}

		return lut::PipelineLayout(aContext.device, layout);
	}

	lut::Pipeline create_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout, VertexInputInfo vInfo)
	{
		// load shader modules
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::mrtVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::mrtFragShaderPath);


		// create pipeline shader stage instance
		VkPipelineShaderStageCreateInfo stages[2]{}; // for vert shader and frag shader respectively


		// stage for vertex shader
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";

		// stage for fragment shader
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";

		// vertex input
		VkVertexInputBindingDescription vertexInputs[INPUT_ATTRIBUTE_NUM]{};

		for (std::uint32_t i = 0; i < INPUT_ATTRIBUTE_NUM; ++i)
		{
			vertexInputs[i].binding = i;
			vertexInputs[i].stride = vInfo.strides[i];
			vertexInputs[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		}
		// vertex attributes
		VkVertexInputAttributeDescription vertexAttributes[INPUT_ATTRIBUTE_NUM]{};

		for (std::uint32_t i = 0; i < INPUT_ATTRIBUTE_NUM; ++i)
		{
			vertexAttributes[i].binding = i; // must match binding above
			vertexAttributes[i].location = i; // must match shader
			vertexAttributes[i].format = vInfo.formats[i];
			vertexAttributes[i].offset = 0;
		}
		// vertex input info
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.vertexBindingDescriptionCount = INPUT_ATTRIBUTE_NUM;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = INPUT_ATTRIBUTE_NUM;
		inputInfo.pVertexAttributeDescriptions = vertexAttributes;
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;


		// define primitive of input
		VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
		assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assemblyInfo.primitiveRestartEnable = VK_FALSE;

		// Define viewport and scissor regions
		VkViewport viewport{};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = float(aWindow.swapchainExtent.width);
		viewport.height = float(aWindow.swapchainExtent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		VkRect2D scissor{};
		scissor.offset = VkOffset2D{ 0, 0 };
		scissor.extent = VkExtent2D{ aWindow.swapchainExtent.width, aWindow.swapchainExtent.height };

		VkPipelineViewportStateCreateInfo viewportInfo{};
		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.viewportCount = 1;
		viewportInfo.pViewports = &viewport;
		viewportInfo.scissorCount = 1;
		viewportInfo.pScissors = &scissor;



		// depth stencil state create info
		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;
		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;


		// Define rasterization options
		VkPipelineRasterizationStateCreateInfo rasterInfo{};
		rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.depthClampEnable = VK_FALSE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.rasterizerDiscardEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f; // required. 

		// Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // only one sample per pixel

		// Define blend state
		VkPipelineColorBlendAttachmentState blendStates[3]{};
		blendStates[0].blendEnable = VK_FALSE;
		blendStates[0].colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendStates[1].blendEnable = VK_FALSE;
		blendStates[1].colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendStates[2].blendEnable = VK_FALSE;
		blendStates[2].colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		

		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 3;
		blendInfo.pAttachments = blendStates;

		// Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

		pipeInfo.stageCount = 2; // vertex + fragment stages
		pipeInfo.pStages = stages;

		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr; // no tessellation 
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = &depthInfo; // no depth or stencil buffers 
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr; // no dynamic states 

		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0; // first subpass of aRenderPass 

		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); res != VK_SUCCESS)
		{

			throw lut::Error("Unable to create graphics pipeline\n"
				"vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());

		}

		return lut::Pipeline(aWindow.device, pipe);
	}

	lut::Pipeline create_pipeline_without_vertex_input(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		// load shader modules
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);


		// create pipeline shader stage instance
		VkPipelineShaderStageCreateInfo stages[2]{}; // for vert shader and frag shader respectively


		// stage for vertex shader
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";

		// stage for fragment shader
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";



		// vertex input info
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;


		// define primitive of input
		VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
		assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assemblyInfo.primitiveRestartEnable = VK_FALSE;

		// Define viewport and scissor regions
		VkViewport viewport{};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = float(aWindow.swapchainExtent.width);
		viewport.height = float(aWindow.swapchainExtent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		VkRect2D scissor{};
		scissor.offset = VkOffset2D{ 0, 0 };
		scissor.extent = VkExtent2D{ aWindow.swapchainExtent.width, aWindow.swapchainExtent.height };

		VkPipelineViewportStateCreateInfo viewportInfo{};
		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.viewportCount = 1;
		viewportInfo.pViewports = &viewport;
		viewportInfo.scissorCount = 1;
		viewportInfo.pScissors = &scissor;

		// Define rasterization options
		VkPipelineRasterizationStateCreateInfo rasterInfo{};
		rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.depthClampEnable = VK_FALSE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.rasterizerDiscardEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f; // required. 

		// Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // only one sample per pixel

		// Define blend state
		VkPipelineColorBlendAttachmentState blendStates[1]{};
		blendStates[0].blendEnable = VK_FALSE;
		blendStates[0].colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;

		// Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

		pipeInfo.stageCount = 2; // vertex + fragment stages
		pipeInfo.pStages = stages;

		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr; // no tessellation 
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = nullptr; // no depth or stencil buffers 
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr; // no dynamic states 

		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0; // first subpass of aRenderPass 

		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); res != VK_SUCCESS)
		{

			throw lut::Error("Unable to create graphics pipeline\n"
				"vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());

		}

		return lut::Pipeline(aWindow.device, pipe);
	}

	lut::Framebuffer create_framebuffer(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<VkImageView> imageViews)
	{
		VkFramebufferCreateInfo fbInfo{};

		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.flags = 0;
		fbInfo.renderPass = aRenderPass;
		fbInfo.attachmentCount = imageViews.size(); // two attachment for one image
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

		return lut::Framebuffer(aWindow.device, fb);
	}

	void create_swapchain_framebuffers(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers, VkImageView aDepthView)
	{
		assert(aFramebuffers.empty());

		for (std::uint32_t i = 0; i < aWindow.swapViews.size(); i++)
		{

			VkImageView attachments[2] = { aWindow.swapViews[i], aDepthView};

			VkFramebufferCreateInfo fbInfo{};

			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.flags = 0;
			fbInfo.renderPass = aRenderPass;
			fbInfo.attachmentCount = 2; // two attachment for one image
			fbInfo.pAttachments = attachments;
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

			aFramebuffers.emplace_back(lut::Framebuffer(aWindow.device, fb));

		}

		assert(aWindow.swapViews.size() == aFramebuffers.size());
	}

	void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight)
	{
		//DONE- (Section 3) initilize SceneUniform members

		// aspect for framebuffer
		float const aspect = aFramebufferWidth / float(aFramebufferHeight);

		// get projection matrix
		glm::mat4 projection = glm::perspectiveRH_ZO(
			lut::Radians(cfg::kCameraFov).value(),
			aspect,
			cfg::kCameraNear,
			cfg::kCameraFar
		);

		projection[1][1] *= -1.f; // mirror Y axis

		glm::mat4 camera = glsl::camera.get_view_matrix();

		aSceneUniforms.projCam = projection * camera;

		aSceneUniforms.camPos = glsl::camera.camTranslation;

	}

	std::tuple<lut::Image, lut::ImageView> create_image_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator,
		VkFormat format, VkImageUsageFlags usage)
	{
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

		lut::Image imagePack(aAllocator.allocator, image, allocation);

		// image view
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = imagePack.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.components = VkComponentMapping{};
		VkImageAspectFlags aspectFlags = 0;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
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

		return { std::move(imagePack), lut::ImageView{aWindow.device, view} };
	}

	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator)
	{
		VkImageCreateInfo imageInfo{};

		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = cfg::kDepthFormat;
		imageInfo.extent.width = aWindow.swapchainExtent.width;
		imageInfo.extent.height = aWindow.swapchainExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

		lut::Image depthImage(aAllocator.allocator, image, allocation);

		// image view
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = depthImage.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = cfg::kDepthFormat;
		viewInfo.components = VkComponentMapping{};
		viewInfo.subresourceRange = VkImageSubresourceRange{
			VK_IMAGE_ASPECT_DEPTH_BIT,
			0,1,
			0,1
		};

		VkImageView view = VK_NULL_HANDLE;
		if (auto const res = vkCreateImageView(aWindow.device, &viewInfo, nullptr, &view); res != VK_SUCCESS)
		{
			throw lut::Error("Unable to create image view\nvkCreateImageView() returned %s", lut::to_string(res).c_str());
		}

		return { std::move(depthImage), lut::ImageView{aWindow.device, view} };
	}

	// run cmd commands
	void record_commands(VkCommandBuffer aCmdBuff, VkRenderPass aRenderPass, VkFramebuffer aFramebuffer, VkPipeline aGraphicsPipe, VkPipelineLayout aGraphicsPipeLayout,
		VkExtent2D const& aImageExtent, std::vector< std::vector<ModelVertexTexturePack>>& mesh, std::vector<desc::DescriptorSetPack>& uniformDescSets)
	{

		// Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(aCmdBuff, &begInfo); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to begin recording command buffer\n"
				"vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
		}


		// Begin render pass
		VkClearValue clearValues[2]{};
		clearValues[0].color.float32[0] = 0.1f; // Clear to a dark gray background. 
		clearValues[0].color.float32[1] = 0.1f; // If we were debugging, this would potentially 
		clearValues[0].color.float32[2] = 0.1f; // help us see whether the render pass took 
		clearValues[0].color.float32[3] = 1.f;  // place, even if nothing else was drawn.
		
		clearValues[1].depthStencil.depth = 1.f;

		// update data in matrix uniform buffer object
		for (auto& descriptorSetPack : uniformDescSets)
			descriptorSetPack.update_ubo_data(aCmdBuff);


		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = aRenderPass;
		passInfo.framebuffer = aFramebuffer;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 2;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);


		// Commands
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);

		for (unsigned int meshIndex = 0; meshIndex < mesh[cfg::isNewShip].size(); ++meshIndex)
		{

			// Binding vertex buffers
			VkBuffer buffers[INPUT_ATTRIBUTE_NUM] = { mesh[cfg::isNewShip][meshIndex].positions.buffer, mesh[cfg::isNewShip][meshIndex].texcoords.buffer, mesh[cfg::isNewShip][meshIndex].normals.buffer };
			VkDeviceSize offsets[INPUT_ATTRIBUTE_NUM]{};
			vkCmdBindVertexBuffers(aCmdBuff, 0, INPUT_ATTRIBUTE_NUM, buffers, offsets);


			// Binding descriptor sets
			for (std::uint32_t descriptorSetIndex = 0; descriptorSetIndex < uniformDescSets.size(); ++descriptorSetIndex)
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, descriptorSetIndex, 1, &uniformDescSets[descriptorSetIndex].descriptorSet, 0, nullptr);

			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, uniformDescSets.size(), 1, &mesh[cfg::isNewShip][meshIndex].textureDescriptorSet, 0, nullptr);

			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, uniformDescSets.size() + 1, 1, &mesh[cfg::isNewShip][meshIndex].materialDescSetPack.descriptorSet, 0, nullptr);

			// Draw a mesh
			vkCmdDraw(aCmdBuff, mesh[cfg::isNewShip][meshIndex].vertexCount, 1, 0, 0);
		}


		// End the render pass 
		vkCmdEndRenderPass(aCmdBuff);

		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n"
				"vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
	}

	void record_offscreen_commands(VkCommandBuffer aCmdBuff, std::vector<desc::DescriptorSetPack>& uniformDescSets, FramebufferPack& framebufferPack, VkPipeline aGraphicsPipe,
		VkPipelineLayout aGraphicsPipeLayout, VkExtent2D const& aImageExtent, std::vector< std::vector<ModelVertexTexturePack>>& mesh)
	{

		// Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(aCmdBuff, &begInfo); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to begin recording command buffer\n"
				"vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
		}

		// Begin render pass
		VkClearValue clearValues[4]{};
		clearValues[0].color.float32[0] = 0.1f; 
		clearValues[0].color.float32[1] = 0.1f; 
		clearValues[0].color.float32[2] = 0.1f; 
		clearValues[0].color.float32[3] = 1.f;  

		clearValues[1].color.float32[0] = 0.0f; 
		clearValues[1].color.float32[1] = 0.0f; 
		clearValues[1].color.float32[2] = 0.0f; 
		clearValues[1].color.float32[3] = 0.0f;  

		clearValues[2].color.float32[0] = 0.1f; 
		clearValues[2].color.float32[1] = 0.1f; 
		clearValues[2].color.float32[2] = 0.1f; 
		clearValues[2].color.float32[3] = 1.f;  

		clearValues[3].depthStencil.depth = 1.f;

		// update data in matrix uniform buffer object
		for (auto& descriptorSetPack : uniformDescSets)
			descriptorSetPack.update_ubo_data(aCmdBuff);


		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = framebufferPack.renderPass.handle;
		passInfo.framebuffer = framebufferPack.framebuffer.handle;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 4;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);


		// Commands
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);

		for (unsigned int meshIndex = 0; meshIndex < mesh[cfg::isNewShip].size(); ++meshIndex)
		{

			// Binding vertex buffers
			VkBuffer buffers[INPUT_ATTRIBUTE_NUM] = { mesh[cfg::isNewShip][meshIndex].positions.buffer, mesh[cfg::isNewShip][meshIndex].texcoords.buffer, mesh[cfg::isNewShip][meshIndex].normals.buffer };
			VkDeviceSize offsets[INPUT_ATTRIBUTE_NUM]{};
			vkCmdBindVertexBuffers(aCmdBuff, 0, INPUT_ATTRIBUTE_NUM, buffers, offsets);


			// Binding descriptor sets
			for (std::uint32_t descriptorSetIndex = 0; descriptorSetIndex < uniformDescSets.size(); ++descriptorSetIndex)
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, descriptorSetIndex, 1, &uniformDescSets[descriptorSetIndex].descriptorSet, 0, nullptr);

			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, uniformDescSets.size(), 1, &mesh[cfg::isNewShip][meshIndex].textureDescriptorSet, 0, nullptr);

			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, uniformDescSets.size() + 1, 1, &mesh[cfg::isNewShip][meshIndex].materialDescSetPack.descriptorSet, 0, nullptr);

			// Draw a mesh
			vkCmdDraw(aCmdBuff, mesh[cfg::isNewShip][meshIndex].vertexCount, 1, 0, 0);
		}


		// End the render pass 
		vkCmdEndRenderPass(aCmdBuff);

		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n"
				"vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
	}

	void record_draw_commands(VkCommandBuffer aCmdBuff, VkDescriptorSet* uniformDescSets, std::uint32_t uniformDescSetCount, desc::Buffer* updateBuffer, std::uint32_t updateBufferCount,
		FramebufferPack& framebufferPack, SwapChainFramebufferPack& scframebufferPack, std::uint32_t framebufferIndex, VkPipeline aGraphicsPipe, VkPipelineLayout aGraphicsPipeLayout, VkExtent2D const& aImageExtent)
	{
		// Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begInfo.pInheritanceInfo = nullptr;

		if (auto const res = vkBeginCommandBuffer(aCmdBuff, &begInfo); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to begin recording command buffer\n"
				"vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
		}

		// Convert color image buffer back into depth image buffer
		lut::image_barrier(
			aCmdBuff,
			framebufferPack.depthAttachment->lutImage.image,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			{ VK_IMAGE_ASPECT_DEPTH_BIT,0, 1, 0, 1 });
		
		// Begin render pass
		VkClearValue clearValues[1]{};
		clearValues[0].color.float32[0] = 0.1f; // Clear to a dark gray background. 
		clearValues[0].color.float32[1] = 0.1f; // If we were debugging, this would potentially 
		clearValues[0].color.float32[2] = 0.1f; // help us see whether the render pass took 
		clearValues[0].color.float32[3] = 1.f;  // place, even if nothing else was drawn.

		// Update uniform buffers
		for (std::uint32_t i = 0; i < updateBufferCount; ++i)
			updateBuffer[i].update_ubo_data(aCmdBuff);


		// Begin render pass
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = scframebufferPack.renderPass.handle;
		passInfo.framebuffer = scframebufferPack.framebuffers[framebufferIndex].handle;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 1;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);


		// Commands
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);

		// Binding descriptor sets
		for (std::uint32_t i = 0; i < uniformDescSetCount; ++i)
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeLayout, i, 1, &uniformDescSets[i], 0, nullptr);


		// Draw a mesh
		vkCmdDraw(aCmdBuff, 6, 1, 0, 0);


		// End the render pass 
		vkCmdEndRenderPass(aCmdBuff);

		
		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n"
				"vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
		
	}


	void submit_commands(lut::VulkanContext const& aContext, VkPipelineStageFlags* waitPipelineStages,  VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore* waitSemaphores, std::uint32_t waitSemaphoreCount, VkSemaphore aSignalSemaphore)
	{
		

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &aCmdBuff;


		submitInfo.waitSemaphoreCount = waitSemaphoreCount;
		submitInfo.pWaitSemaphores = waitSemaphores;
		
		submitInfo.pWaitDstStageMask = waitPipelineStages; // number of stage masks should match the number of semaphore

		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &aSignalSemaphore;


		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo, aFence);
			VK_SUCCESS != res)
		{
			throw lut::Error("Unable to submit command buffer to queue\n"
				"vkQueueSubmit() returned %s", lut::to_string(res).c_str());
		}

		
	}

}

int main() try
{
	glsl::lightManager.updateLightSet();


	// Create Vulkan Window
	lut::VulkanWindow window = lut::make_vulkan_window();
	// Configure the GLFW window
	glfwSetKeyCallback(window.window, &glfw_callback_key_press);
	glfwSetCursorPosCallback(window.window, &mouse_pos_callback);
	glfwSetMouseButtonCallback(window.window, &mouse_button_callback);

	// Create VMA allocator
	lut::Allocator allocator = lut::create_allocator(window);

	// create descriptor pool
	lut::DescriptorPool dpool = lut::create_descriptor_pool(window);

	// Render pass
	//lut::RenderPass renderPass = create_render_pass(window);

	//-------------//
	// pipeline 00 // 
	//-------------//

	// create vector to store all descriptor set layouts for [ pipeline 0 ]
	std::vector<lut::DescriptorSetLayout> layouts;

	// Create descriptor set for uniform block
	std::vector<desc::DescriptorSetPack> descriptorSetPacks;
	descriptorSetPacks.emplace_back(desc::create_descriptor_set_for_uniform_buffer(window, allocator, dpool.handle,
		layouts, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, { sizeof(glsl::SceneUniform) }, 1));


	// update pointer that point to the data needed by the uniform buffer in the later steps
	glsl::SceneUniform matrixUniform{};
	descriptorSetPacks[0].data.emplace_back(&matrixUniform);

	

	// Load mesh
	ModelData materialtestModel = load_obj_model(cfg::materialtestObjectPath);
	ModelData newShipModel = load_obj_model(cfg::newShipObjectPath);


	// create texture layout
	layouts.emplace_back(desc::create_descriptor_layout(window, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT));
	layouts.emplace_back(desc::create_descriptor_layout(window, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT));


	// store model attributes into buffers
	std::vector<std::vector<ModelVertexTexturePack>> modelBuffer;

	modelBuffer.emplace_back();
	modelBuffer.emplace_back();

	for (int i = 0; i < materialtestModel.meshes.size(); ++i)
		modelBuffer[0].emplace_back(create_model_attribute_set(window, allocator, materialtestModel, (layouts.end() - 2)->handle, layouts.back().handle, dpool.handle, i));
	for (int i = 0; i < newShipModel.meshes.size(); ++i)
		modelBuffer[1].emplace_back(create_model_attribute_set(window, allocator, newShipModel, (layouts.end() - 2)->handle, layouts.back().handle, dpool.handle, i));


	// New for this course work ... >
	
	// Create attachments for render pass (and framebuffer)
	Attachment colorAttachments[3] = {
		{window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
		{window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
		{window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }
	};
	
	Attachment depthAttachment{ window, allocator, cfg::kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };

	// Framebuffer pack
	FramebufferPack framebufferPack(window, colorAttachments, 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &depthAttachment);
	
	// ... end new.

	// [ Pipeline 0 ]
	lut::PipelineLayout pipeLayout = create_pipeline_layout(window, layouts);
	lut::Pipeline pipe = create_pipeline(window, framebufferPack.renderPass.handle, pipeLayout.handle, cfg::vertexInputInfo);


	//-------------//
	// pipeline 01 // 
	//-------------//
	
	// create new uniform buffer for light properties for the final render frag shader.
	desc::Buffer lightBuffer = { lut::create_buffer(allocator, sizeof(glsl::LightSet),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY), sizeof(glsl::LightSet), &glsl::lightManager.lightset };
	// set layout
	VkDescriptorSetLayoutBinding layoutBindings[5];

	layoutBindings[0] = desc::create_descriptor_layout_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	layoutBindings[1] = desc::create_descriptor_layout_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	layoutBindings[2] = desc::create_descriptor_layout_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	layoutBindings[3] = desc::create_descriptor_layout_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	layoutBindings[4] = desc::create_descriptor_layout_binding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);




	lut::DescriptorSetLayout setLayout;
	setLayout = desc::create_descriptor_layout(window, layoutBindings, 5);

	// create image / buffer info
	desc::ImageInfo imageInfos[4];
	labutils::Sampler sampler = labutils::create_default_sampler(window, VK_TRUE);
	imageInfos[0] = { &colorAttachments[0].lutImage, desc::create_desc_image_info(colorAttachments[0].imageView.handle,sampler.handle), 0 };
	imageInfos[1] = { &colorAttachments[1].lutImage, desc::create_desc_image_info(colorAttachments[1].imageView.handle,sampler.handle), 1 };
	imageInfos[2] = { &colorAttachments[2].lutImage, desc::create_desc_image_info(colorAttachments[2].imageView.handle,sampler.handle), 2 };
	imageInfos[3] = { &depthAttachment.lutImage, desc::create_desc_image_info(depthAttachment.imageView.handle,sampler.handle), 3 };

	desc::BufferInfo bufferInfos[1];
	
	bufferInfos[0] = { &lightBuffer,  desc::create_desc_buffer_info(lightBuffer.buffer.buffer),    4 };
	


	// finally create descriptor set 
	VkDescriptorSet descSet;
	descSet = desc::create_descriptor_set(window, dpool.handle, setLayout.handle, bufferInfos, 1, imageInfos, 4);


	// Framebuffer and Render pass for [ Pipeline 1 ]
	SwapChainFramebufferPack swapChainFramebufferPack(window);

	// [ Pipeline 1 ]
	VkDescriptorSetLayout setLayouts[2] = { setLayout.handle, descriptorSetPacks[0].layout};

	lut::PipelineLayout defPipeLayout = create_pipeline_layout(window, setLayouts, 2);
	
	lut::Pipeline defPipe = create_pipeline_without_vertex_input(window, swapChainFramebufferPack.renderPass.handle, defPipeLayout.handle);


	// Command
	lut::CommandPool cpool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	std::vector<VkCommandBuffer> drawCmdbuffers;
	std::vector<lut::Fence> drawCmdbfences;

	for (std::size_t i = 0; i < swapChainFramebufferPack.framebuffers.size(); ++i)
	{
		drawCmdbuffers.emplace_back(lut::alloc_command_buffer(window, cpool.handle));
		drawCmdbfences.emplace_back(lut::create_fence(window, VK_FENCE_CREATE_SIGNALED_BIT));
	}


	VkCommandBuffer offscreenCmdBuffer = lut::alloc_command_buffer(window, cpool.handle);
	
	lut::Fence offscreenfence = lut::create_fence(window, VK_FENCE_CREATE_SIGNALED_BIT);

	

	// Semaphores
	lut::Semaphore imageAvailable = lut::create_semaphore(window);
	lut::Semaphore offscreenFinished = lut::create_semaphore(window);
	lut::Semaphore renderFinished = lut::create_semaphore(window);


	// Application main loop
	bool recreateSwapchain = false;


	while (!glfwWindowShouldClose(window.window))
	{
		// window event check
		glfwPollEvents();
		
		// Recreate swap chain
		if (recreateSwapchain)
		{
			//re-create swapchain and associated resources!
			vkDeviceWaitIdle(window.device);

			auto const changes = recreate_swapchain(window);

			// re-create render pass
			if (changes.changedFormat)
			{
				swapChainFramebufferPack.create_render_pass(window);
				framebufferPack.create_render_pass(window, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}


			// re-create pipeline
			if (changes.changedSize)
			{
				pipe = create_pipeline(window, framebufferPack.renderPass.handle, pipeLayout.handle, cfg::vertexInputInfo);
				defPipe = create_pipeline_without_vertex_input(window, swapChainFramebufferPack.renderPass.handle, defPipeLayout.handle);
				
				//std::tie(depthAttachment.lutImage, depthAttachment.imageView) = create_depth_buffer(window, allocator);
				
				// resize images for offscreen rendering
				colorAttachments[0].create_image_buffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
				colorAttachments[1].create_image_buffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
				colorAttachments[2].create_image_buffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
				depthAttachment.create_image_buffer( window, allocator, cfg::kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
				

				imageInfos[0] = { &colorAttachments[0].lutImage, desc::create_desc_image_info(colorAttachments[0].imageView.handle,sampler.handle), 0 };
				imageInfos[1] = { &colorAttachments[1].lutImage, desc::create_desc_image_info(colorAttachments[1].imageView.handle,sampler.handle), 1 };
				imageInfos[2] = { &colorAttachments[2].lutImage, desc::create_desc_image_info(colorAttachments[2].imageView.handle,sampler.handle), 2 };
				imageInfos[3] = { &depthAttachment.lutImage, desc::create_desc_image_info(depthAttachment.imageView.handle,sampler.handle), 3 };
				
				descSet = desc::create_descriptor_set(window, dpool.handle, setLayout.handle, bufferInfos, 1, imageInfos, 4);


			}

			// clear framebuffers in the vector and recreate a new vector of framebuffer
			framebufferPack.create_framebuffer(window);
			swapChainFramebufferPack.create_framebuffer(window);

			// disable recreate 
			recreateSwapchain = false;
			continue;
		}

		
		
		
		
		// Prepare data for this frame
		update_scene_uniforms(matrixUniform, window.swapchainExtent.width,
			window.swapchainExtent.height);

		// wait for the commands are all not be used
		if (auto const res = vkWaitForFences(window.device, 1, &offscreenfence.handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
			VK_SUCCESS != res)
		{
			throw lut::Error("Unable to wait for offscreen command buffer fence \n");
		}

		// reset the fences to be unsignalled
		if (auto const res = vkResetFences(window.device, 1, &offscreenfence.handle)
			; VK_SUCCESS != res)
		{
			throw lut::Error("Unable to reset offscreen command buffer fence\n"
				"vkResetFences() returned %s", lut::to_string(res).c_str()
			);
		}

		// record and submit commands
		record_offscreen_commands(offscreenCmdBuffer, descriptorSetPacks, framebufferPack, pipe.handle, pipeLayout.handle, window.swapchainExtent, modelBuffer);



		submit_commands(
			window,
			nullptr,
			offscreenCmdBuffer,
			offscreenfence.handle,
			VK_NULL_HANDLE,
			0,
			offscreenFinished.handle
		);
		
		
		
		// acquire swapchain image.
		std::uint32_t imageIndex = 0;
		auto const acquireRes = vkAcquireNextImageKHR(
			window.device,
			window.swapchain,
			std::numeric_limits<std::uint64_t>::max(),
			imageAvailable.handle,
			VK_NULL_HANDLE,
			&imageIndex
		);

		// make sure commands are not used
		assert(std::size_t(imageIndex) < drawCmdbfences.size());

		// check info for the swapchain image
		if (VK_SUBOPTIMAL_KHR == acquireRes || VK_ERROR_OUT_OF_DATE_KHR == acquireRes)
		{
			recreateSwapchain = true;
			continue;
		}

		else if (VK_SUCCESS != acquireRes)
		{
			throw lut::Error("Unable to acquire enxt swapchain image\n"
				"vkAcquireNextImageKHR() returned %s", lut::to_string(acquireRes).c_str()
			);
		}
		

		// wait for the commands are all not be used
		if (auto const res = vkWaitForFences(window.device, 1, &drawCmdbfences[imageIndex].handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
			VK_SUCCESS != res)
		{
			throw lut::Error("Unable to wait for command buffer fence %u\n"
				"vkWaitForFences() returned %s", imageIndex, lut::to_string(res).c_str()
			);
		}

		// reset the fences to be unsignalled
		if (auto const res = vkResetFences(window.device, 1, &drawCmdbfences[imageIndex].handle)
			; VK_SUCCESS != res)
		{
			throw lut::Error("Unable to reset command buffer fence %u\n"
				"vkResetFences() returned %s", imageIndex, lut::to_string(res).c_str()
			);
		}

		assert(std::size_t(imageIndex) < drawCmdbuffers.size());
		assert(std::size_t(imageIndex) < swapChainFramebufferPack.framebuffers.size());

		VkDescriptorSet descSets[2] = {descSet, descriptorSetPacks[0].descriptorSet};
		record_draw_commands(drawCmdbuffers[imageIndex], descSets, 2, &lightBuffer, 1,framebufferPack,swapChainFramebufferPack, imageIndex, defPipe.handle, defPipeLayout.handle, window.swapchainExtent);

		VkSemaphore waitSemaphores[2] = { offscreenFinished.handle , imageAvailable.handle };
		VkPipelineStageFlags stageFlags[2] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT , VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };


		submit_commands(
			window,
			stageFlags,
			drawCmdbuffers[imageIndex],
			drawCmdbfences[imageIndex].handle,
			waitSemaphores,
			2,
			renderFinished.handle
		);

		//DONE: present rendered images.
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinished.handle;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &window.swapchain;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;


		auto const presentRes = vkQueuePresentKHR(window.presentQueue, &presentInfo);
		if (VK_SUBOPTIMAL_KHR == presentRes || VK_ERROR_OUT_OF_DATE_KHR == presentRes)
		{
			recreateSwapchain = true;
		}
		else if (VK_SUCCESS != presentRes)
		{
			throw lut::Error("Unable present swapchain image %u\n"
				"vkQueuePresentKHR() returned %s", imageIndex, lut::to_string(presentRes).c_str()
			);

		}

		// update rotation angle
		glsl::lightManager.updateRadian();

	}

	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle(window.device);
	return 0;

}
catch( std::exception const& eErr )
{
	std::fprintf( stderr, "\n" );
	std::fprintf( stderr, "Error: %s\n", eErr.what() );
	return 1;
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
