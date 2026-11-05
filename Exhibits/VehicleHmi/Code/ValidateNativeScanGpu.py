"""Rasterize the actual native Slang sparkle vertex/fragment shaders on Vulkan.

Requires the compiled SPIR-V and the Python vulkan bindings. Checks point-size scaling,
normal blending and depth occlusion against the same fixture used by the WGSL test.
"""
import math
import pathlib
import struct
import sys

import vulkan as vk

shader_directory = pathlib.Path(sys.argv[1])
size = 64
instance = vk.vkCreateInstance(vk.VkInstanceCreateInfo(pApplicationInfo=vk.VkApplicationInfo(
    pApplicationName="NativeScanValidation", apiVersion=vk.VK_MAKE_VERSION(1, 1, 0))), None)
physical = vk.vkEnumeratePhysicalDevices(instance)[0]
family = next(index for index, props in enumerate(vk.vkGetPhysicalDeviceQueueFamilyProperties(physical))
              if props.queueFlags & vk.VK_QUEUE_GRAPHICS_BIT)
device = vk.vkCreateDevice(physical, vk.VkDeviceCreateInfo(
    queueCreateInfoCount=1, pQueueCreateInfos=[vk.VkDeviceQueueCreateInfo(
        queueFamilyIndex=family, queueCount=1, pQueuePriorities=[1.0])]), None)
queue = vk.vkGetDeviceQueue(device, family, 0)
memory_properties = vk.vkGetPhysicalDeviceMemoryProperties(physical)


def allocate(requirements, properties):
    for index in range(memory_properties.memoryTypeCount):
        if (requirements.memoryTypeBits & (1 << index) and
                memory_properties.memoryTypes[index].propertyFlags & properties == properties):
            return vk.vkAllocateMemory(device, vk.VkMemoryAllocateInfo(
                allocationSize=requirements.size, memoryTypeIndex=index), None)
    raise RuntimeError(f"No memory type for flags {properties}")


buffers, images = [], []


def make_buffer(content, usage):
    handle = vk.vkCreateBuffer(device, vk.VkBufferCreateInfo(size=len(content), usage=usage,
                                                            sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE), None)
    memory = allocate(vk.vkGetBufferMemoryRequirements(device, handle),
                      vk.VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | vk.VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    vk.vkBindBufferMemory(device, handle, memory, 0)
    mapped = vk.vkMapMemory(device, memory, 0, len(content), 0)
    mapped[:] = content
    vk.vkUnmapMemory(device, memory)
    resource = {"handle": handle, "memory": memory, "size": len(content)}
    buffers.append(resource)
    return resource


def make_image(fmt, usage, aspect):
    handle = vk.vkCreateImage(device, vk.VkImageCreateInfo(
        imageType=vk.VK_IMAGE_TYPE_2D, format=fmt, extent=vk.VkExtent3D(width=size, height=size, depth=1),
        mipLevels=1, arrayLayers=1, samples=vk.VK_SAMPLE_COUNT_1_BIT, tiling=vk.VK_IMAGE_TILING_OPTIMAL,
        usage=usage, sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE, initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED), None)
    memory = allocate(vk.vkGetImageMemoryRequirements(device, handle), 0)
    vk.vkBindImageMemory(device, handle, memory, 0)
    subresource = vk.VkImageSubresourceRange(aspectMask=aspect, baseMipLevel=0, levelCount=1,
                                              baseArrayLayer=0, layerCount=1)
    view = vk.vkCreateImageView(device, vk.VkImageViewCreateInfo(image=handle, viewType=vk.VK_IMAGE_VIEW_TYPE_2D,
                                                               format=fmt, subresourceRange=subresource), None)
    resource = {"handle": handle, "memory": memory, "view": view}
    images.append(resource)
    return resource


particle = make_buffer(struct.pack("<8f", 0, 0, -5, 2.2, 1, 1, 1, 0.35), vk.VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
# Slang's default host matrix layout is row-major (the SPIR-V presents the transposed matrix).
projection = [0.9765625, 0, 0, 0, 0, 0.9765625, 0, 0, 0, 0, 0, 0.1, 0, 0, -1, 0]


def frame_data(ratio):
    values = projection + [0, 0, 0, 0] + [0, 0, 0, 0] + [0, 0, 0, 3] + [0, 0, 0.55, 0] + identity + [ratio, 1 / size, 1 / size, 0]
    assert len(values) == 52
    return struct.pack("<52f", *values)


frames = [make_buffer(frame_data(ratio), vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) for ratio in [1, 2, 1]]
colors = [make_image(vk.VK_FORMAT_R16G16B16A16_SFLOAT,
                     vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     vk.VK_IMAGE_ASPECT_COLOR_BIT) for _ in frames]
depths = [make_image(vk.VK_FORMAT_D32_SFLOAT, vk.VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     vk.VK_IMAGE_ASPECT_DEPTH_BIT) for _ in frames]
readbacks = [make_buffer(bytes(size * size * 8), vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT) for _ in frames]

bindings = [vk.VkDescriptorSetLayoutBinding(binding=0, descriptorType=vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    descriptorCount=1, stageFlags=vk.VK_SHADER_STAGE_VERTEX_BIT | vk.VK_SHADER_STAGE_FRAGMENT_BIT),
    vk.VkDescriptorSetLayoutBinding(binding=1, descriptorType=vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    descriptorCount=1, stageFlags=vk.VK_SHADER_STAGE_VERTEX_BIT)]
set_layout = vk.vkCreateDescriptorSetLayout(device, vk.VkDescriptorSetLayoutCreateInfo(bindingCount=2, pBindings=bindings), None)
pipeline_layout = vk.vkCreatePipelineLayout(device, vk.VkPipelineLayoutCreateInfo(setLayoutCount=1, pSetLayouts=[set_layout]), None)
pool_sizes = [vk.VkDescriptorPoolSize(type=kind, descriptorCount=3) for kind in
              [vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER]]
pool = vk.vkCreateDescriptorPool(device, vk.VkDescriptorPoolCreateInfo(maxSets=3, poolSizeCount=2, pPoolSizes=pool_sizes), None)
sets = vk.vkAllocateDescriptorSets(device, vk.VkDescriptorSetAllocateInfo(
    descriptorPool=pool, descriptorSetCount=3, pSetLayouts=[set_layout] * 3))
for index, frame in enumerate(frames):
    for binding, category, resource in [(0, vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame),
                                         (1, vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, particle)]:
        info = vk.VkDescriptorBufferInfo(buffer=resource["handle"], offset=0, range=resource["size"])
        write = vk.VkWriteDescriptorSet(dstSet=sets[index], dstBinding=binding, descriptorCount=1,
                                        descriptorType=category, pBufferInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)

attachments = [
    vk.VkAttachmentDescription(format=vk.VK_FORMAT_R16G16B16A16_SFLOAT, samples=vk.VK_SAMPLE_COUNT_1_BIT,
        loadOp=vk.VK_ATTACHMENT_LOAD_OP_CLEAR, storeOp=vk.VK_ATTACHMENT_STORE_OP_STORE,
        stencilLoadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE, stencilStoreOp=vk.VK_ATTACHMENT_STORE_OP_DONT_CARE,
        initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, finalLayout=vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
    vk.VkAttachmentDescription(format=vk.VK_FORMAT_D32_SFLOAT, samples=vk.VK_SAMPLE_COUNT_1_BIT,
        loadOp=vk.VK_ATTACHMENT_LOAD_OP_CLEAR, storeOp=vk.VK_ATTACHMENT_STORE_OP_DONT_CARE,
        stencilLoadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE, stencilStoreOp=vk.VK_ATTACHMENT_STORE_OP_DONT_CARE,
        initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, finalLayout=vk.VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)]
color_reference = vk.VkAttachmentReference(attachment=0, layout=vk.VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
depth_reference = vk.VkAttachmentReference(attachment=1, layout=vk.VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
subpass = vk.VkSubpassDescription(pipelineBindPoint=vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
    colorAttachmentCount=1, pColorAttachments=[color_reference], pDepthStencilAttachment=depth_reference)
render_pass = vk.vkCreateRenderPass(device, vk.VkRenderPassCreateInfo(attachmentCount=2, pAttachments=attachments,
                                                                      subpassCount=1, pSubpasses=[subpass]), None)
framebuffers = [vk.vkCreateFramebuffer(device, vk.VkFramebufferCreateInfo(renderPass=render_pass, attachmentCount=2,
    pAttachments=[color["view"], depth["view"]], width=size, height=size, layers=1), None)
    for color, depth in zip(colors, depths)]
modules = []
stages = []
for name, flag in [("SparkleVertex", vk.VK_SHADER_STAGE_VERTEX_BIT), ("SparkleFragment", vk.VK_SHADER_STAGE_FRAGMENT_BIT)]:
    code = (shader_directory / f"{name}.spv").read_bytes()
    module = vk.vkCreateShaderModule(device, vk.VkShaderModuleCreateInfo(codeSize=len(code), pCode=code), None)
    modules.append(module)
    stages.append(vk.VkPipelineShaderStageCreateInfo(stage=flag, module=module, pName="main"))
viewport = vk.VkViewport(x=0, y=size, width=size, height=-size, minDepth=0, maxDepth=1)
scissor = vk.VkRect2D(offset=vk.VkOffset2D(x=0, y=0), extent=vk.VkExtent2D(width=size, height=size))
blend = vk.VkPipelineColorBlendAttachmentState(blendEnable=vk.VK_TRUE,
    srcColorBlendFactor=vk.VK_BLEND_FACTOR_SRC_ALPHA, dstColorBlendFactor=vk.VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    colorBlendOp=vk.VK_BLEND_OP_ADD, srcAlphaBlendFactor=vk.VK_BLEND_FACTOR_ONE,
    dstAlphaBlendFactor=vk.VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, alphaBlendOp=vk.VK_BLEND_OP_ADD,
    colorWriteMask=vk.VK_COLOR_COMPONENT_R_BIT | vk.VK_COLOR_COMPONENT_G_BIT | vk.VK_COLOR_COMPONENT_B_BIT | vk.VK_COLOR_COMPONENT_A_BIT)
pipeline_info = vk.VkGraphicsPipelineCreateInfo(stageCount=2, pStages=stages,
    pVertexInputState=vk.VkPipelineVertexInputStateCreateInfo(),
    pInputAssemblyState=vk.VkPipelineInputAssemblyStateCreateInfo(topology=vk.VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
    pViewportState=vk.VkPipelineViewportStateCreateInfo(viewportCount=1, pViewports=[viewport], scissorCount=1, pScissors=[scissor]),
    pRasterizationState=vk.VkPipelineRasterizationStateCreateInfo(polygonMode=vk.VK_POLYGON_MODE_FILL,
        cullMode=vk.VK_CULL_MODE_NONE, frontFace=vk.VK_FRONT_FACE_COUNTER_CLOCKWISE, lineWidth=1),
    pMultisampleState=vk.VkPipelineMultisampleStateCreateInfo(rasterizationSamples=vk.VK_SAMPLE_COUNT_1_BIT),
    pDepthStencilState=vk.VkPipelineDepthStencilStateCreateInfo(depthTestEnable=vk.VK_TRUE,
        depthWriteEnable=vk.VK_FALSE, depthCompareOp=vk.VK_COMPARE_OP_GREATER_OR_EQUAL,
        minDepthBounds=0, maxDepthBounds=1),
    pColorBlendState=vk.VkPipelineColorBlendStateCreateInfo(attachmentCount=1, pAttachments=[blend]),
    layout=pipeline_layout, renderPass=render_pass, subpass=0, basePipelineIndex=-1)
pipeline = vk.vkCreateGraphicsPipelines(device, vk.VK_NULL_HANDLE, 1, [pipeline_info], None)[0]
command_pool = vk.vkCreateCommandPool(device, vk.VkCommandPoolCreateInfo(queueFamilyIndex=family), None)
command = vk.vkAllocateCommandBuffers(device, vk.VkCommandBufferAllocateInfo(
    commandPool=command_pool, level=vk.VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandBufferCount=1))[0]
vk.vkBeginCommandBuffer(command, vk.VkCommandBufferBeginInfo(flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
region = vk.VkBufferImageCopy(bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
    imageSubresource=vk.VkImageSubresourceLayers(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                                 mipLevel=0, baseArrayLayer=0, layerCount=1),
    imageOffset=vk.VkOffset3D(x=0, y=0, z=0), imageExtent=vk.VkExtent3D(width=size, height=size, depth=1))
for index in range(3):
    clears = [vk.VkClearValue(color=vk.VkClearColorValue(float32=[0.2, 0.3, 0.4, 1.0])),
              vk.VkClearValue(depthStencil=vk.VkClearDepthStencilValue(depth=0.05 if index == 2 else 0, stencil=0))]
    info = vk.VkRenderPassBeginInfo(renderPass=render_pass, framebuffer=framebuffers[index], renderArea=scissor,
                                    clearValueCount=2, pClearValues=clears)
    vk.vkCmdBeginRenderPass(command, info, vk.VK_SUBPASS_CONTENTS_INLINE)
    vk.vkCmdBindPipeline(command, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline)
    vk.vkCmdBindDescriptorSets(command, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, [sets[index]], 0, None)
    vk.vkCmdDraw(command, 6, 1, 0, 0)
    vk.vkCmdEndRenderPass(command)
    barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_TRANSFER_READ_BIT)
    vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 1, [barrier], 0, None, 0, None)
    vk.vkCmdCopyImageToBuffer(command, colors[index]["handle"], vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              readbacks[index]["handle"], 1, [region])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_HOST_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkEndCommandBuffer(command)
vk.vkQueueSubmit(queue, 1, [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[command])], vk.VK_NULL_HANDLE)
vk.vkQueueWaitIdle(queue)

pixels = []
for resource in readbacks:
    mapped = vk.vkMapMemory(device, resource["memory"], 0, resource["size"], 0)
    pixels.append(struct.unpack(f"<{size * size * 4}e", bytes(mapped)))
    vk.vkUnmapMemory(device, resource["memory"])
background = [0.2, 0.3, 0.4]
tint = [0.274677, 0.686685, 0.887923]
if "--debug" in sys.argv:
    changed = [(x, y, pixels[0][(y * size + x) * 4:(y * size + x) * 4 + 3])
               for y in range(size) for x in range(size) if abs(pixels[0][(y * size + x) * 4] - background[0]) > 0.002]
    print("changed pixels", len(changed), changed[:20])
checks = 0
for index, ratio, visible, samples in [(0, 1, True, [(33, 33), (35, 33), (37, 33), (38, 33), (39, 33), (33, 38)]),
                                      (1, 2, True, [(33, 33), (37, 33), (40, 33), (43, 33), (45, 33)]),
                                      (2, 1, False, [(33, 33)])]:
    diameter = 11 * ratio
    for x, y in samples:
        distance = math.hypot(x + 0.5 - 33.25, y + 0.5 - 33.25) / diameter
        inside = abs(x + 0.5 - 33.25) <= diameter / 2 and abs(y + 0.5 - 33.25) <= diameter / 2
        alpha = max(0, min(1, (0.05 / max(distance, 1e-6) - 0.1) * 0.55)) if visible and inside else 0
        for channel in range(3):
            actual = pixels[index][(y * size + x) * 4 + channel]
            expected = tint[channel] * alpha + background[channel] * (1 - alpha)
            assert abs(actual - expected) < 0.002, f"sample {index}/{x},{y}/{channel}: {actual} != {expected}"
            checks += 1
print(f"Native Slang particle vertex/fragment: {checks} component checks over 12 pixels, size scaling and depth occlusion passed on {vk.vkGetPhysicalDeviceProperties(physical).deviceName}.")

vk.vkDeviceWaitIdle(device)
vk.vkDestroyCommandPool(device, command_pool, None)
vk.vkDestroyPipeline(device, pipeline, None)
for module in modules:
    vk.vkDestroyShaderModule(device, module, None)
for framebuffer in framebuffers:
    vk.vkDestroyFramebuffer(device, framebuffer, None)
vk.vkDestroyRenderPass(device, render_pass, None)
vk.vkDestroyDescriptorPool(device, pool, None)
vk.vkDestroyPipelineLayout(device, pipeline_layout, None)
vk.vkDestroyDescriptorSetLayout(device, set_layout, None)
for image in images:
    vk.vkDestroyImageView(device, image["view"], None)
    vk.vkDestroyImage(device, image["handle"], None)
    vk.vkFreeMemory(device, image["memory"], None)
for resource in buffers:
    vk.vkDestroyBuffer(device, resource["handle"], None)
    vk.vkFreeMemory(device, resource["memory"], None)
vk.vkDestroyDevice(device, None)
vk.vkDestroyInstance(instance, None)
