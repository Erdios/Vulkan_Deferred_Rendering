# HPG Coursework 03
Coursework from HPG Programme in University of Leeds.

# Option Deferred Shading
## Features
Use PBR shader for lighting only.

5 lights

4 intermediate textures:
- 3 color attachments (albedo, normal, material)
    - All of them are `VK_FORMAT_R16G16B16A16_SFLOAT`.
    - Why use this for 
        - albedo: Albedo is normaly decimal number, and normally we should use SRGB or UNORM data type instead. However, in my albedo attachment, there is also shininess (a quite large number) stored inside, so I chose SFLOAT format (maybe it is better to store shininess inside normal attachment to use less space, but it is so weird). 
        - normal: since the decimal number for normal is not sure to be inside [0, 1], I chose to use SFLOAT. With an extra A16 is because `VK_FORMAT_R16G16B16_SFLOAT` causes error in my laptop.
        - material: in material, emissive and metalness are stored. I use SFLOAT to make sure that they can be passed correctly. But since emissive and metalness are all between [0,1], maybe there can be other smaller data type for them (like UNORM) ?
- 1 depth attachments (using barrier to convert depth stencil attachment to be shader visible)Formats for
    - `VK_FORMAT_D32_SFLOAT`
        - Use this because of the choice of near and far panels mentioned in the code template (I just keep using the default setting from the template).



# Fence and Barrier
I use fence for every command buffer to make sure all the commands are finished after they are submitted.

An extra barrier to convert the the depth attachments into shader visible in the second pipeline.
```c++
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
```
# Code changed
In FramebufferHelper.h/.cpp

Add FramebufferPack class to make it easier to create framebuffer with multiple attachments.

Add SwapChainFramebufferPack to manage multiple framebuffers for a swapchain.

In DescriptorSetHelper.h/.cpp

Write small functions to help creating descriptor set.
