"""Rasterize the standalone Slang raymarch stages on Vulkan into a half-float image."""
import json
import pathlib
import struct
import sys

import vulkan as vk

fixture_path, shader_directory, result_path = map(pathlib.Path, sys.argv[1:4])
fixture = json.loads(fixture_path.read_text())
width, height = fixture["width"], fixture["height"]
instance = vk.vkCreateInstance(vk.VkInstanceCreateInfo(pApplicationInfo=vk.VkApplicationInfo(
    pApplicationName="FlowRaymarchValidation", apiVersion=vk.VK_MAKE_VERSION(1, 1, 0))), None)
physical = vk.vkEnumeratePhysicalDevices(instance)[0]
family = next(index for index, props in enumerate(vk.vkGetPhysicalDeviceQueueFamilyProperties(physical))
              if props.queueFlags & vk.VK_QUEUE_GRAPHICS_BIT)
device = vk.vkCreateDevice(physical, vk.VkDeviceCreateInfo(queueCreateInfoCount=1,
    pQueueCreateInfos=[vk.VkDeviceQueueCreateInfo(queueFamilyIndex=family, queueCount=1, pQueuePriorities=[1.0])]), None)
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


def make_image(extent, dimension, usage):
    image_type = vk.VK_IMAGE_TYPE_3D if dimension == 3 else vk.VK_IMAGE_TYPE_2D
    view_type = vk.VK_IMAGE_VIEW_TYPE_3D if dimension == 3 else vk.VK_IMAGE_VIEW_TYPE_2D
    handle = vk.vkCreateImage(device, vk.VkImageCreateInfo(imageType=image_type, format=vk.VK_FORMAT_R16G16B16A16_SFLOAT,
        extent=vk.VkExtent3D(width=extent[0], height=extent[1], depth=extent[2]), mipLevels=1, arrayLayers=1,
        samples=vk.VK_SAMPLE_COUNT_1_BIT, tiling=vk.VK_IMAGE_TILING_OPTIMAL, usage=usage,
        sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE, initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED), None)
    memory = allocate(vk.vkGetImageMemoryRequirements(device, handle), 0)
    vk.vkBindImageMemory(device, handle, memory, 0)
    subresource = vk.VkImageSubresourceRange(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                              baseMipLevel=0, levelCount=1, baseArrayLayer=0, layerCount=1)
    view = vk.vkCreateImageView(device, vk.VkImageViewCreateInfo(image=handle, viewType=view_type,
        format=vk.VK_FORMAT_R16G16B16A16_SFLOAT, subresourceRange=subresource), None)
    resource = {"handle": handle, "memory": memory, "view": view, "range": subresource, "extent": extent}
    images.append(resource)
    return resource


def region(extent):
    return vk.VkBufferImageCopy(bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
        imageSubresource=vk.VkImageSubresourceLayers(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                                     mipLevel=0, baseArrayLayer=0, layerCount=1),
        imageOffset=vk.VkOffset3D(x=0, y=0, z=0),
        imageExtent=vk.VkExtent3D(width=extent[0], height=extent[1], depth=extent[2]))


assert len(fixture["uniform"]) == 52
frame = make_buffer(struct.pack("<52f", *fixture["uniform"]), vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
textures, staging = {}, {}
for name, source in fixture["textures"].items():
    extent, data = source["extent"], source["data"]
    assert len(data) == extent[0] * extent[1] * extent[2] * 4
    textures[name] = make_image(extent, 2 if name == "palette" else 3,
        vk.VK_IMAGE_USAGE_TRANSFER_DST_BIT | vk.VK_IMAGE_USAGE_SAMPLED_BIT)
    staging[name] = make_buffer(struct.pack(f"<{len(data)}e", *data), vk.VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
color = make_image([width, height, 1], 2, vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
readback = make_buffer(bytes(width * height * 8), vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT)
sampler = vk.vkCreateSampler(device, vk.VkSamplerCreateInfo(magFilter=vk.VK_FILTER_LINEAR, minFilter=vk.VK_FILTER_LINEAR,
    mipmapMode=vk.VK_SAMPLER_MIPMAP_MODE_NEAREST, addressModeU=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    addressModeV=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, addressModeW=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    minLod=0, maxLod=0, maxAnisotropy=1), None)
uniform, sampled, sampling = (vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                vk.VK_DESCRIPTOR_TYPE_SAMPLER)
types = [uniform, sampled, sampled, sampling, sampled, sampled]
bindings = [vk.VkDescriptorSetLayoutBinding(binding=index, descriptorType=kind, descriptorCount=1,
    stageFlags=vk.VK_SHADER_STAGE_VERTEX_BIT | vk.VK_SHADER_STAGE_FRAGMENT_BIT) for index, kind in enumerate(types)]
set_layout = vk.vkCreateDescriptorSetLayout(device, vk.VkDescriptorSetLayoutCreateInfo(bindingCount=len(bindings),
                                                                                       pBindings=bindings), None)
pipeline_layout = vk.vkCreatePipelineLayout(device, vk.VkPipelineLayoutCreateInfo(setLayoutCount=1,
                                                                                  pSetLayouts=[set_layout]), None)
pool_sizes = [vk.VkDescriptorPoolSize(type=kind, descriptorCount=types.count(kind)) for kind in [uniform, sampled, sampling]]
pool = vk.vkCreateDescriptorPool(device, vk.VkDescriptorPoolCreateInfo(maxSets=1, poolSizeCount=len(pool_sizes),
                                                                       pPoolSizes=pool_sizes), None)
sets = vk.vkAllocateDescriptorSets(device, vk.VkDescriptorSetAllocateInfo(descriptorPool=pool,
    descriptorSetCount=1, pSetLayouts=[set_layout]))
info = vk.VkDescriptorBufferInfo(buffer=frame["handle"], offset=0, range=frame["size"])
write = vk.VkWriteDescriptorSet(dstSet=sets[0], dstBinding=0, descriptorCount=1, descriptorType=uniform, pBufferInfo=[info])
vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
info = vk.VkDescriptorImageInfo(sampler=sampler)
write = vk.VkWriteDescriptorSet(dstSet=sets[0], dstBinding=3, descriptorCount=1, descriptorType=sampling, pImageInfo=[info])
vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
for binding, name in [(1, "scalar"), (2, "velocity"), (4, "palette"), (5, "irradiance")]:
    info = vk.VkDescriptorImageInfo(imageView=textures[name]["view"], imageLayout=vk.VK_IMAGE_LAYOUT_GENERAL)
    write = vk.VkWriteDescriptorSet(dstSet=sets[0], dstBinding=binding, descriptorCount=1,
                                    descriptorType=sampled, pImageInfo=[info])
    vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
attachment = vk.VkAttachmentDescription(format=vk.VK_FORMAT_R16G16B16A16_SFLOAT, samples=vk.VK_SAMPLE_COUNT_1_BIT,
    loadOp=vk.VK_ATTACHMENT_LOAD_OP_CLEAR, storeOp=vk.VK_ATTACHMENT_STORE_OP_STORE,
    stencilLoadOp=vk.VK_ATTACHMENT_LOAD_OP_DONT_CARE, stencilStoreOp=vk.VK_ATTACHMENT_STORE_OP_DONT_CARE,
    initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, finalLayout=vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
reference = vk.VkAttachmentReference(attachment=0, layout=vk.VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
subpass = vk.VkSubpassDescription(pipelineBindPoint=vk.VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 colorAttachmentCount=1, pColorAttachments=[reference])
render_pass = vk.vkCreateRenderPass(device, vk.VkRenderPassCreateInfo(attachmentCount=1, pAttachments=[attachment],
                                                                      subpassCount=1, pSubpasses=[subpass]), None)
framebuffer = vk.vkCreateFramebuffer(device, vk.VkFramebufferCreateInfo(renderPass=render_pass, attachmentCount=1,
    pAttachments=[color["view"]], width=width, height=height, layers=1), None)
modules, stages = [], []
for name, flag in [("ProjectFullscreenCorner", vk.VK_SHADER_STAGE_VERTEX_BIT),
                   ("IntegrateVolumeRadiance", vk.VK_SHADER_STAGE_FRAGMENT_BIT)]:
    code = (shader_directory / f"{name}.spv").read_bytes()
    module = vk.vkCreateShaderModule(device, vk.VkShaderModuleCreateInfo(codeSize=len(code), pCode=code), None)
    modules.append(module)
    stages.append(vk.VkPipelineShaderStageCreateInfo(stage=flag, module=module, pName="main"))
viewport = vk.VkViewport(x=0, y=height, width=width, height=-height, minDepth=0, maxDepth=1)
scissor = vk.VkRect2D(offset=vk.VkOffset2D(x=0, y=0), extent=vk.VkExtent2D(width=width, height=height))
blend = vk.VkPipelineColorBlendAttachmentState(blendEnable=vk.VK_FALSE,
    colorWriteMask=vk.VK_COLOR_COMPONENT_R_BIT | vk.VK_COLOR_COMPONENT_G_BIT | vk.VK_COLOR_COMPONENT_B_BIT | vk.VK_COLOR_COMPONENT_A_BIT)
pipeline_info = vk.VkGraphicsPipelineCreateInfo(stageCount=2, pStages=stages,
    pVertexInputState=vk.VkPipelineVertexInputStateCreateInfo(),
    pInputAssemblyState=vk.VkPipelineInputAssemblyStateCreateInfo(topology=vk.VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP),
    pViewportState=vk.VkPipelineViewportStateCreateInfo(viewportCount=1, pViewports=[viewport], scissorCount=1, pScissors=[scissor]),
    pRasterizationState=vk.VkPipelineRasterizationStateCreateInfo(polygonMode=vk.VK_POLYGON_MODE_FILL,
        cullMode=vk.VK_CULL_MODE_NONE, frontFace=vk.VK_FRONT_FACE_COUNTER_CLOCKWISE, lineWidth=1),
    pMultisampleState=vk.VkPipelineMultisampleStateCreateInfo(rasterizationSamples=vk.VK_SAMPLE_COUNT_1_BIT),
    pColorBlendState=vk.VkPipelineColorBlendStateCreateInfo(attachmentCount=1, pAttachments=[blend]),
    layout=pipeline_layout, renderPass=render_pass, subpass=0, basePipelineIndex=-1)
pipeline = vk.vkCreateGraphicsPipelines(device, vk.VK_NULL_HANDLE, 1, [pipeline_info], None)[0]
command_pool = vk.vkCreateCommandPool(device, vk.VkCommandPoolCreateInfo(queueFamilyIndex=family), None)
command = vk.vkAllocateCommandBuffers(device, vk.VkCommandBufferAllocateInfo(commandPool=command_pool,
    level=vk.VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandBufferCount=1))[0]
vk.vkBeginCommandBuffer(command, vk.VkCommandBufferBeginInfo(flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
barriers = [vk.VkImageMemoryBarrier(srcAccessMask=0, dstAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT,
    oldLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, newLayout=vk.VK_IMAGE_LAYOUT_GENERAL,
    srcQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED,
    image=image["handle"], subresourceRange=image["range"]) for image in textures.values()]
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, None, 0, None, len(barriers), barriers)
for name, image in textures.items():
    vk.vkCmdCopyBufferToImage(command, staging[name]["handle"], image["handle"], vk.VK_IMAGE_LAYOUT_GENERAL,
                              1, [region(image["extent"])])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
clear = vk.VkClearValue(color=vk.VkClearColorValue(float32=[-2, -2, -2, -2]))
info = vk.VkRenderPassBeginInfo(renderPass=render_pass, framebuffer=framebuffer, renderArea=scissor,
                                clearValueCount=1, pClearValues=[clear])
vk.vkCmdBeginRenderPass(command, info, vk.VK_SUBPASS_CONTENTS_INLINE)
vk.vkCmdBindPipeline(command, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline)
vk.vkCmdBindDescriptorSets(command, vk.VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, [sets[0]], 0, None)
vk.vkCmdDraw(command, 4, 1, 0, 0)
vk.vkCmdEndRenderPass(command)
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_TRANSFER_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkCmdCopyImageToBuffer(command, color["handle"], vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          readback["handle"], 1, [region([width, height, 1])])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_HOST_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkEndCommandBuffer(command)
vk.vkQueueSubmit(queue, 1, [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[command])], vk.VK_NULL_HANDLE)
vk.vkQueueWaitIdle(queue)
mapped = vk.vkMapMemory(device, readback["memory"], 0, readback["size"], 0)
pixels = struct.unpack(f"<{width * height * 4}e", bytes(mapped))
vk.vkUnmapMemory(device, readback["memory"])
result_path.write_text(json.dumps({"device": vk.vkGetPhysicalDeviceProperties(physical).deviceName, "pixels": pixels}, allow_nan=False))
vk.vkDeviceWaitIdle(device)
vk.vkDestroyCommandPool(device, command_pool, None)
vk.vkDestroyPipeline(device, pipeline, None)
for module in modules:
    vk.vkDestroyShaderModule(device, module, None)
vk.vkDestroyFramebuffer(device, framebuffer, None)
vk.vkDestroyRenderPass(device, render_pass, None)
vk.vkDestroyDescriptorPool(device, pool, None)
vk.vkDestroyPipelineLayout(device, pipeline_layout, None)
vk.vkDestroyDescriptorSetLayout(device, set_layout, None)
vk.vkDestroySampler(device, sampler, None)
for image in images:
    vk.vkDestroyImageView(device, image["view"], None)
    vk.vkDestroyImage(device, image["handle"], None)
    vk.vkFreeMemory(device, image["memory"], None)
for resource in buffers:
    vk.vkDestroyBuffer(device, resource["handle"], None)
    vk.vkFreeMemory(device, resource["memory"], None)
vk.vkDestroyDevice(device, None)
vk.vkDestroyInstance(instance, None)
