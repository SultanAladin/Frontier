#!/usr/bin/env python3
"""Run compiled Slang plume compute entries through Vulkan for comparison with the WGSL reference.

Test-only validation. Requires the `vulkan` Python package and a Vulkan driver. The fixture is a
small 3-D lattice; output is JSON containing every pressure, curl, and velocity reading.
"""
import json
import struct
import sys
from pathlib import Path

import vulkan as vk

fixture = json.loads(Path(sys.argv[1]).read_text())
shader_directory = Path(sys.argv[2])
output = Path(sys.argv[3])
width, height, depth = fixture["extent"]
cells = width * height * depth

instance = vk.vkCreateInstance(vk.VkInstanceCreateInfo(
    pApplicationInfo=vk.VkApplicationInfo(pApplicationName="Plume parity", apiVersion=vk.VK_API_VERSION_1_0)), None)
physical = vk.vkEnumeratePhysicalDevices(instance)[0]
families = vk.vkGetPhysicalDeviceQueueFamilyProperties(physical)
family = next(index for index, properties in enumerate(families) if properties.queueFlags & vk.VK_QUEUE_COMPUTE_BIT)
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


def make_buffer(data, usage):
    buffer = vk.vkCreateBuffer(device, vk.VkBufferCreateInfo(
        size=len(data), usage=usage, sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE), None)
    requirements = vk.vkGetBufferMemoryRequirements(device, buffer)
    memory = allocate(requirements, vk.VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | vk.VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    vk.vkBindBufferMemory(device, buffer, memory, 0)
    mapped = vk.vkMapMemory(device, memory, 0, len(data), 0)
    mapped[:] = data
    vk.vkUnmapMemory(device, memory)
    return {"buffer": buffer, "memory": memory, "size": len(data)}


def make_image(usage):
    image = vk.vkCreateImage(device, vk.VkImageCreateInfo(
        imageType=vk.VK_IMAGE_TYPE_3D, format=vk.VK_FORMAT_R16G16B16A16_SFLOAT,
        extent=vk.VkExtent3D(width=width, height=height, depth=depth), mipLevels=1, arrayLayers=1,
        samples=vk.VK_SAMPLE_COUNT_1_BIT, tiling=vk.VK_IMAGE_TILING_OPTIMAL,
        usage=usage, sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE, initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED), None)
    memory = allocate(vk.vkGetImageMemoryRequirements(device, image), 0)
    vk.vkBindImageMemory(device, image, memory, 0)
    subresource = vk.VkImageSubresourceRange(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                              baseMipLevel=0, levelCount=1, baseArrayLayer=0, layerCount=1)
    view = vk.vkCreateImageView(device, vk.VkImageViewCreateInfo(
        image=image, viewType=vk.VK_IMAGE_VIEW_TYPE_3D, format=vk.VK_FORMAT_R16G16B16A16_SFLOAT,
        subresourceRange=subresource), None)
    return {"image": image, "memory": memory, "view": view, "range": subresource}


uniform = bytearray(256)
struct.pack_into("<4i", uniform, 0, *fixture["origin"], 0)
struct.pack_into("<4i", uniform, 16, *fixture["prior"], 0)
struct.pack_into("<4I", uniform, 32, width, height, depth, 0)
struct.pack_into("<4I", uniform, 48, width * 2, height * 2, depth * 2, 0)
struct.pack_into("<4f", uniform, 64, fixture["edge"] / 2, fixture["edge"], fixture["floor"], 1 / 60)
struct.pack_into("<f", uniform, 88, fixture["confinement"])
velocity = struct.pack(f"<{len(fixture['velocity'])}e", *fixture["velocity"])
float_bytes = lambda values: struct.pack(f"<{len(values)}f", *values)
uniform_buffer = make_buffer(uniform, vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
staging = make_buffer(velocity, vk.VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
readback = make_buffer(bytes(len(velocity)), vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT)
divergence = make_buffer(bytes(cells * 4), vk.VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
pressure_a = make_buffer(float_bytes(fixture["pressureA"]), vk.VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
pressure_b = make_buffer(float_bytes(fixture["pressureB"]), vk.VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
source_image = make_image(vk.VK_IMAGE_USAGE_TRANSFER_DST_BIT | vk.VK_IMAGE_USAGE_SAMPLED_BIT)
target_image = make_image(vk.VK_IMAGE_USAGE_STORAGE_BIT | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT)

bindings = [vk.VkDescriptorSetLayoutBinding(binding=index, descriptorType=kind, descriptorCount=1,
                                            stageFlags=vk.VK_SHADER_STAGE_COMPUTE_BIT)
            for index, kind in enumerate([vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                          vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                          vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vk.VK_DESCRIPTOR_TYPE_STORAGE_IMAGE])]
set_layout = vk.vkCreateDescriptorSetLayout(device, vk.VkDescriptorSetLayoutCreateInfo(
    bindingCount=len(bindings), pBindings=bindings), None)
pipeline_layout = vk.vkCreatePipelineLayout(device, vk.VkPipelineLayoutCreateInfo(
    setLayoutCount=1, pSetLayouts=[set_layout]), None)
pool_sizes = [vk.VkDescriptorPoolSize(type=kind, descriptorCount=count) for kind, count in [
    (vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2), (vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2),
    (vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6), (vk.VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2)]]
pool = vk.vkCreateDescriptorPool(device, vk.VkDescriptorPoolCreateInfo(
    maxSets=2, poolSizeCount=len(pool_sizes), pPoolSizes=pool_sizes), None)
sets = vk.vkAllocateDescriptorSets(device, vk.VkDescriptorSetAllocateInfo(
    descriptorPool=pool, descriptorSetCount=2, pSetLayouts=[set_layout, set_layout]))
for descriptor_set, source_pressure, target_pressure in [(sets[0], pressure_a, pressure_b),
                                                           (sets[1], pressure_b, pressure_a)]:
    for index, kind, resource in [(0, vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform_buffer),
                                  (2, vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, divergence),
                                  (3, vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, source_pressure),
                                  (4, vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, target_pressure)]:
        info = vk.VkDescriptorBufferInfo(buffer=resource["buffer"], offset=0, range=resource["size"])
        write = vk.VkWriteDescriptorSet(dstSet=descriptor_set, dstBinding=index, descriptorCount=1,
                                        descriptorType=kind, pBufferInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
    for index, kind, image in [(1, vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, source_image),
                               (5, vk.VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, target_image)]:
        info = vk.VkDescriptorImageInfo(imageView=image["view"], imageLayout=vk.VK_IMAGE_LAYOUT_GENERAL)
        write = vk.VkWriteDescriptorSet(dstSet=descriptor_set, dstBinding=index, descriptorCount=1,
                                        descriptorType=kind, pImageInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)

pipelines = []
modules = []
for name in ["ResolveDivergence", "RelaxPressure", "ProjectVelocity"]:
    code = (shader_directory / f"{name}.spv").read_bytes()
    module = vk.vkCreateShaderModule(device, vk.VkShaderModuleCreateInfo(codeSize=len(code), pCode=code), None)
    modules.append(module)
    stage = vk.VkPipelineShaderStageCreateInfo(stage=vk.VK_SHADER_STAGE_COMPUTE_BIT, module=module, pName="main")
    info = vk.VkComputePipelineCreateInfo(stage=stage, layout=pipeline_layout, basePipelineIndex=-1)
    pipelines.append(vk.vkCreateComputePipelines(device, vk.VK_NULL_HANDLE, 1, [info], None)[0])

command_pool = vk.vkCreateCommandPool(device, vk.VkCommandPoolCreateInfo(queueFamilyIndex=family), None)
command = vk.vkAllocateCommandBuffers(device, vk.VkCommandBufferAllocateInfo(
    commandPool=command_pool, level=vk.VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandBufferCount=1))[0]
vk.vkBeginCommandBuffer(command, vk.VkCommandBufferBeginInfo(flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
image_barriers = [vk.VkImageMemoryBarrier(srcAccessMask=0, dstAccessMask=access,
                    oldLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, newLayout=vk.VK_IMAGE_LAYOUT_GENERAL,
                    srcQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED,
                    image=image["image"], subresourceRange=image["range"])
                  for image, access in [(source_image, vk.VK_ACCESS_TRANSFER_WRITE_BIT),
                                        (target_image, vk.VK_ACCESS_SHADER_WRITE_BIT)]]
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        vk.VK_PIPELINE_STAGE_TRANSFER_BIT | vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 0, None, 0, None, len(image_barriers), image_barriers)
region = vk.VkBufferImageCopy(bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
    imageSubresource=vk.VkImageSubresourceLayers(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                                 mipLevel=0, baseArrayLayer=0, layerCount=1),
    imageOffset=vk.VkOffset3D(x=0, y=0, z=0), imageExtent=vk.VkExtent3D(width=width, height=height, depth=depth))
vk.vkCmdCopyBufferToImage(command, staging["buffer"], source_image["image"], vk.VK_IMAGE_LAYOUT_GENERAL, 1, [region])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
for index, pipeline in enumerate(pipelines):
    vk.vkCmdBindPipeline(command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipeline)
    vk.vkCmdBindDescriptorSets(command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                               [sets[0 if index < 2 else 1]], 0, None)
    vk.vkCmdDispatch(command, (width + 3) // 4, (height + 3) // 4, (depth + 3) // 4)
    barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_SHADER_WRITE_BIT,
                                 dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT | vk.VK_ACCESS_SHADER_WRITE_BIT |
                                               vk.VK_ACCESS_TRANSFER_READ_BIT | vk.VK_ACCESS_HOST_READ_BIT)
    vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | vk.VK_PIPELINE_STAGE_TRANSFER_BIT | vk.VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, [barrier], 0, None, 0, None)
vk.vkCmdCopyImageToBuffer(command, target_image["image"], vk.VK_IMAGE_LAYOUT_GENERAL, readback["buffer"], 1, [region])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_HOST_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkEndCommandBuffer(command)
vk.vkQueueSubmit(queue, 1, [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[command])], vk.VK_NULL_HANDLE)
vk.vkQueueWaitIdle(queue)


def read_buffer(resource, kind, count):
    mapped = vk.vkMapMemory(device, resource["memory"], 0, resource["size"], 0)
    values = struct.unpack(f"<{count}{kind}", bytes(mapped))
    vk.vkUnmapMemory(device, resource["memory"])
    return values

result = {"device": vk.vkGetPhysicalDeviceProperties(physical).deviceName,
          "divergence": read_buffer(divergence, "f", cells),
          "pressureA": read_buffer(pressure_a, "f", cells),
          "pressureB": read_buffer(pressure_b, "f", cells),
          "velocity": read_buffer(readback, "e", cells * 4)}

# Run curl and confinement on the same input lattice, but with the original four-resource layout.
curl = make_buffer(bytes(cells * 16), vk.VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
confined_readback = make_buffer(bytes(len(velocity)), vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT)
vorticity_types = [vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                   vk.VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, vk.VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]
vorticity_bindings = [vk.VkDescriptorSetLayoutBinding(binding=index, descriptorType=kind, descriptorCount=1,
                        stageFlags=vk.VK_SHADER_STAGE_COMPUTE_BIT) for index, kind in enumerate(vorticity_types)]
vorticity_set_layout = vk.vkCreateDescriptorSetLayout(device, vk.VkDescriptorSetLayoutCreateInfo(
    bindingCount=len(vorticity_bindings), pBindings=vorticity_bindings), None)
vorticity_layout = vk.vkCreatePipelineLayout(device, vk.VkPipelineLayoutCreateInfo(
    setLayoutCount=1, pSetLayouts=[vorticity_set_layout]), None)
vorticity_pool_sizes = [vk.VkDescriptorPoolSize(type=kind, descriptorCount=1) for kind in vorticity_types]
vorticity_pool = vk.vkCreateDescriptorPool(device, vk.VkDescriptorPoolCreateInfo(
    maxSets=1, poolSizeCount=len(vorticity_pool_sizes), pPoolSizes=vorticity_pool_sizes), None)
vorticity_set = vk.vkAllocateDescriptorSets(device, vk.VkDescriptorSetAllocateInfo(
    descriptorPool=vorticity_pool, descriptorSetCount=1, pSetLayouts=[vorticity_set_layout]))[0]
for index, kind, resource in [(0, vorticity_types[0], uniform_buffer), (2, vorticity_types[2], curl)]:
    info = vk.VkDescriptorBufferInfo(buffer=resource["buffer"], offset=0, range=resource["size"])
    write = vk.VkWriteDescriptorSet(dstSet=vorticity_set, dstBinding=index, descriptorCount=1,
                                    descriptorType=kind, pBufferInfo=[info])
    vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
for index, kind, image in [(1, vorticity_types[1], source_image), (3, vorticity_types[3], target_image)]:
    info = vk.VkDescriptorImageInfo(imageView=image["view"], imageLayout=vk.VK_IMAGE_LAYOUT_GENERAL)
    write = vk.VkWriteDescriptorSet(dstSet=vorticity_set, dstBinding=index, descriptorCount=1,
                                    descriptorType=kind, pImageInfo=[info])
    vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
vorticity_pipelines = []
for name in ["ResolveCurl", "ConfineVorticity"]:
    code = (shader_directory / f"{name}.spv").read_bytes()
    module = vk.vkCreateShaderModule(device, vk.VkShaderModuleCreateInfo(codeSize=len(code), pCode=code), None)
    modules.append(module)
    stage = vk.VkPipelineShaderStageCreateInfo(stage=vk.VK_SHADER_STAGE_COMPUTE_BIT, module=module, pName="main")
    info = vk.VkComputePipelineCreateInfo(stage=stage, layout=vorticity_layout, basePipelineIndex=-1)
    pipeline = vk.vkCreateComputePipelines(device, vk.VK_NULL_HANDLE, 1, [info], None)[0]
    pipelines.append(pipeline)
    vorticity_pipelines.append(pipeline)
vorticity_command = vk.vkAllocateCommandBuffers(device, vk.VkCommandBufferAllocateInfo(
    commandPool=command_pool, level=vk.VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandBufferCount=1))[0]
vk.vkBeginCommandBuffer(vorticity_command, vk.VkCommandBufferBeginInfo(flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_READ_BIT, dstAccessMask=vk.VK_ACCESS_SHADER_WRITE_BIT)
vk.vkCmdPipelineBarrier(vorticity_command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
for pipeline in vorticity_pipelines:
    vk.vkCmdBindPipeline(vorticity_command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipeline)
    vk.vkCmdBindDescriptorSets(vorticity_command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, vorticity_layout, 0, 1,
                               [vorticity_set], 0, None)
    vk.vkCmdDispatch(vorticity_command, (width + 3) // 4, (height + 3) // 4, (depth + 3) // 4)
    barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_SHADER_WRITE_BIT,
        dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT | vk.VK_ACCESS_TRANSFER_READ_BIT | vk.VK_ACCESS_HOST_READ_BIT)
    vk.vkCmdPipelineBarrier(vorticity_command, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | vk.VK_PIPELINE_STAGE_TRANSFER_BIT | vk.VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, [barrier], 0, None, 0, None)
vk.vkCmdCopyImageToBuffer(vorticity_command, target_image["image"], vk.VK_IMAGE_LAYOUT_GENERAL,
                          confined_readback["buffer"], 1, [region])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_HOST_READ_BIT)
vk.vkCmdPipelineBarrier(vorticity_command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkEndCommandBuffer(vorticity_command)
vk.vkQueueSubmit(queue, 1, [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[vorticity_command])], vk.VK_NULL_HANDLE)
vk.vkQueueWaitIdle(queue)
result["curl"] = read_buffer(curl, "f", cells * 4)
result["confined"] = read_buffer(confined_readback, "e", cells * 4)
output.write_text(json.dumps(result, allow_nan=False))
vk.vkDeviceWaitIdle(device)
vk.vkDestroyCommandPool(device, command_pool, None)
for pipeline in pipelines:
    vk.vkDestroyPipeline(device, pipeline, None)
for module in modules:
    vk.vkDestroyShaderModule(device, module, None)
vk.vkDestroyDescriptorPool(device, pool, None)
vk.vkDestroyDescriptorPool(device, vorticity_pool, None)
vk.vkDestroyPipelineLayout(device, pipeline_layout, None)
vk.vkDestroyPipelineLayout(device, vorticity_layout, None)
vk.vkDestroyDescriptorSetLayout(device, set_layout, None)
vk.vkDestroyDescriptorSetLayout(device, vorticity_set_layout, None)
for image in [source_image, target_image]:
    vk.vkDestroyImageView(device, image["view"], None)
    vk.vkDestroyImage(device, image["image"], None)
    vk.vkFreeMemory(device, image["memory"], None)
for resource in [uniform_buffer, staging, readback, divergence, pressure_a, pressure_b, curl, confined_readback]:
    vk.vkDestroyBuffer(device, resource["buffer"], None)
    vk.vkFreeMemory(device, resource["memory"], None)
vk.vkDestroyDevice(device, None)
vk.vkDestroyInstance(instance, None)
