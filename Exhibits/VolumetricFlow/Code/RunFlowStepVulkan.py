"""Execute a complete C++-planned solver step using compiled Slang SPIR-V on Vulkan."""
import json
import pathlib
import struct
import sys

import vulkan as vk

fixture_path, projection_directory, transport_directory, result_path = map(pathlib.Path, sys.argv[1:5])
fixture = json.loads(fixture_path.read_text())
plan = fixture["plan"]
instance = vk.vkCreateInstance(vk.VkInstanceCreateInfo(pApplicationInfo=vk.VkApplicationInfo(
    pApplicationName="FlowStepValidation", apiVersion=vk.VK_API_VERSION_1_0)), None)
physical = vk.vkEnumeratePhysicalDevices(instance)[0]
family = next(index for index, props in enumerate(vk.vkGetPhysicalDeviceQueueFamilyProperties(physical))
              if props.queueFlags & vk.VK_QUEUE_COMPUTE_BIT)
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
    resource = {"buffer": handle, "memory": memory, "size": len(content)}
    buffers.append(resource)
    return resource


def make_image(extent, vector):
    fmt = vk.VK_FORMAT_R16G16B16A16_SFLOAT if vector else vk.VK_FORMAT_R32_SFLOAT
    handle = vk.vkCreateImage(device, vk.VkImageCreateInfo(imageType=vk.VK_IMAGE_TYPE_3D, format=fmt,
        extent=vk.VkExtent3D(width=extent, height=extent, depth=extent), mipLevels=1, arrayLayers=1,
        samples=vk.VK_SAMPLE_COUNT_1_BIT, tiling=vk.VK_IMAGE_TILING_OPTIMAL,
        usage=(vk.VK_IMAGE_USAGE_TRANSFER_DST_BIT | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               vk.VK_IMAGE_USAGE_SAMPLED_BIT | vk.VK_IMAGE_USAGE_STORAGE_BIT),
        sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE, initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED), None)
    memory = allocate(vk.vkGetImageMemoryRequirements(device, handle), 0)
    vk.vkBindImageMemory(device, handle, memory, 0)
    subresource = vk.VkImageSubresourceRange(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                              baseMipLevel=0, levelCount=1, baseArrayLayer=0, layerCount=1)
    view = vk.vkCreateImageView(device, vk.VkImageViewCreateInfo(image=handle, viewType=vk.VK_IMAGE_VIEW_TYPE_3D,
                                                               format=fmt, subresourceRange=subresource), None)
    resource = {"image": handle, "memory": memory, "view": view, "range": subresource, "extent": extent, "vector": vector}
    images.append(resource)
    return resource


def pack_floats(values):
    return struct.pack(f"<{len(values)}f", *values)


def region(extent):
    return vk.VkBufferImageCopy(bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
        imageSubresource=vk.VkImageSubresourceLayers(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                                     mipLevel=0, baseArrayLayer=0, layerCount=1),
        imageOffset=vk.VkOffset3D(x=0, y=0, z=0), imageExtent=vk.VkExtent3D(width=extent, height=extent, depth=extent))


flow_buffer = make_buffer(pack_floats(plan["uniform"]), vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
level_buffers = [make_buffer(pack_floats(level), vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) for level in plan["levels"]]
volumes, staging, readbacks = {}, {}, {}
for name, data in fixture["volumes"].items():
    vector = name.startswith(("V", "S"))
    extent, readings = data["extent"], data["data"]
    assert len(readings) == extent ** 3 * (4 if vector else 1)
    payload = struct.pack(f"<{len(readings)}e", *readings) if vector else pack_floats(readings)
    volumes[name] = make_image(extent, vector)
    staging[name] = make_buffer(payload, vk.VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
    readbacks[name] = make_buffer(bytes(len(payload)), vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT)

uniform = vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
sampled = vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
storage = vk.VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
sampling = vk.VK_DESCRIPTOR_TYPE_SAMPLER
categories = {"transport": [uniform, sampled, sampled, sampling, storage, storage],
              "projection": [uniform, sampled, sampled, sampled, storage, storage, storage, uniform, sampled]}
sampler = vk.vkCreateSampler(device, vk.VkSamplerCreateInfo(
    magFilter=vk.VK_FILTER_LINEAR, minFilter=vk.VK_FILTER_LINEAR, mipmapMode=vk.VK_SAMPLER_MIPMAP_MODE_NEAREST,
    addressModeU=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, addressModeV=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    addressModeW=vk.VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, minLod=0, maxLod=0, maxAnisotropy=1), None)
set_layouts, pipeline_layouts = {}, {}
for category, types in categories.items():
    bindings = [vk.VkDescriptorSetLayoutBinding(binding=index, descriptorType=kind, descriptorCount=1,
                                                stageFlags=vk.VK_SHADER_STAGE_COMPUTE_BIT)
                for index, kind in enumerate(types)]
    set_layouts[category] = vk.vkCreateDescriptorSetLayout(device, vk.VkDescriptorSetLayoutCreateInfo(
        bindingCount=len(bindings), pBindings=bindings), None)
    pipeline_layouts[category] = vk.vkCreatePipelineLayout(device, vk.VkPipelineLayoutCreateInfo(
        setLayoutCount=1, pSetLayouts=[set_layouts[category]]), None)
dispatches = plan["stepDispatches"]
count = len(dispatches)
pool_sizes = [vk.VkDescriptorPoolSize(type=kind, descriptorCount=sum(categories[entry[1]].count(kind) for entry in dispatches))
              for kind in [uniform, sampled, storage, sampling]]
pool = vk.vkCreateDescriptorPool(device, vk.VkDescriptorPoolCreateInfo(maxSets=count, poolSizeCount=len(pool_sizes),
                                                                       pPoolSizes=pool_sizes), None)
sets = vk.vkAllocateDescriptorSets(device, vk.VkDescriptorSetAllocateInfo(
    descriptorPool=pool, descriptorSetCount=count, pSetLayouts=[set_layouts[entry[1]] for entry in dispatches]))
for index, (_, category, level, _, names) in enumerate(dispatches):
    uniform_bindings = [(0, flow_buffer)]
    if category == "projection":
        uniform_bindings.append((7, level_buffers[level]))
    for binding, buffer in uniform_bindings:
        info = vk.VkDescriptorBufferInfo(buffer=buffer["buffer"], offset=0, range=buffer["size"])
        write = vk.VkWriteDescriptorSet(dstSet=sets[index], dstBinding=binding, descriptorCount=1,
                                        descriptorType=uniform, pBufferInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
    if category == "transport":
        info = vk.VkDescriptorImageInfo(sampler=sampler)
        write = vk.VkWriteDescriptorSet(dstSet=sets[index], dstBinding=3, descriptorCount=1,
                                        descriptorType=sampling, pImageInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
    image_bindings = [1, 2, 4, 5] if category == "transport" else [1, 2, 3, 4, 5, 6, 8]
    for binding, name in zip(image_bindings, names):
        info = vk.VkDescriptorImageInfo(imageView=volumes[name]["view"], imageLayout=vk.VK_IMAGE_LAYOUT_GENERAL)
        write = vk.VkWriteDescriptorSet(dstSet=sets[index], dstBinding=binding, descriptorCount=1,
                                        descriptorType=categories[category][binding], pImageInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)

modules, pipelines = [], {}
for name, category in sorted({(entry[0], entry[1]) for entry in dispatches}):
    shader_directory = transport_directory if category == "transport" else projection_directory
    code = (shader_directory / f"{name}.spv").read_bytes()
    module = vk.vkCreateShaderModule(device, vk.VkShaderModuleCreateInfo(codeSize=len(code), pCode=code), None)
    modules.append(module)
    stage = vk.VkPipelineShaderStageCreateInfo(stage=vk.VK_SHADER_STAGE_COMPUTE_BIT, module=module, pName="main")
    info = vk.VkComputePipelineCreateInfo(stage=stage, layout=pipeline_layouts[category], basePipelineIndex=-1)
    pipelines[name] = vk.vkCreateComputePipelines(device, vk.VK_NULL_HANDLE, 1, [info], None)[0]
command_pool = vk.vkCreateCommandPool(device, vk.VkCommandPoolCreateInfo(queueFamilyIndex=family), None)
command = vk.vkAllocateCommandBuffers(device, vk.VkCommandBufferAllocateInfo(
    commandPool=command_pool, level=vk.VK_COMMAND_BUFFER_LEVEL_PRIMARY, commandBufferCount=1))[0]
vk.vkBeginCommandBuffer(command, vk.VkCommandBufferBeginInfo(flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
barriers = [vk.VkImageMemoryBarrier(srcAccessMask=0, dstAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT,
    oldLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, newLayout=vk.VK_IMAGE_LAYOUT_GENERAL,
    srcQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED,
    image=image["image"], subresourceRange=image["range"]) for image in images]
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, None, 0, None, len(barriers), barriers)
for name, image in volumes.items():
    vk.vkCmdCopyBufferToImage(command, staging[name]["buffer"], image["image"], vk.VK_IMAGE_LAYOUT_GENERAL,
                              1, [region(image["extent"])])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT,
                             dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT | vk.VK_ACCESS_SHADER_WRITE_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
for index, (name, category, _, groups, _) in enumerate(dispatches):
    vk.vkCmdBindPipeline(command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[name])
    vk.vkCmdBindDescriptorSets(command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layouts[category], 0, 1, [sets[index]], 0, None)
    vk.vkCmdDispatch(command, *groups)
    barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_SHADER_WRITE_BIT,
                                 dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT | vk.VK_ACCESS_SHADER_WRITE_BIT)
    vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, 1, [barrier], 0, None, 0, None)
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_SHADER_WRITE_BIT | vk.VK_ACCESS_TRANSFER_WRITE_BIT,
                             dstAccessMask=vk.VK_ACCESS_TRANSFER_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                        vk.VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, [barrier], 0, None, 0, None)
for name, destination in readbacks.items():
    image = volumes[name]
    vk.vkCmdCopyImageToBuffer(command, image["image"], vk.VK_IMAGE_LAYOUT_GENERAL, destination["buffer"],
                              1, [region(image["extent"])])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_HOST_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkEndCommandBuffer(command)
vk.vkQueueSubmit(queue, 1, [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[command])], vk.VK_NULL_HANDLE)
vk.vkQueueWaitIdle(queue)
result = {"device": vk.vkGetPhysicalDeviceProperties(physical).deviceName, "volumes": {}}
for name, resource in readbacks.items():
    mapped = vk.vkMapMemory(device, resource["memory"], 0, resource["size"], 0)
    kind = "e" if volumes[name]["vector"] else "f"
    width = 2 if kind == "e" else 4
    result["volumes"][name] = struct.unpack(f"<{resource['size'] // width}{kind}", bytes(mapped))
    vk.vkUnmapMemory(device, resource["memory"])
result_path.write_text(json.dumps(result, allow_nan=False))

vk.vkDeviceWaitIdle(device)
vk.vkDestroyCommandPool(device, command_pool, None)
for pipeline in pipelines.values():
    vk.vkDestroyPipeline(device, pipeline, None)
for module in modules:
    vk.vkDestroyShaderModule(device, module, None)
vk.vkDestroyDescriptorPool(device, pool, None)
for layout in pipeline_layouts.values():
    vk.vkDestroyPipelineLayout(device, layout, None)
for layout in set_layouts.values():
    vk.vkDestroyDescriptorSetLayout(device, layout, None)
vk.vkDestroySampler(device, sampler, None)
for image in images:
    vk.vkDestroyImageView(device, image["view"], None)
    vk.vkDestroyImage(device, image["image"], None)
    vk.vkFreeMemory(device, image["memory"], None)
for resource in buffers:
    vk.vkDestroyBuffer(device, resource["buffer"], None)
    vk.vkFreeMemory(device, resource["memory"], None)
vk.vkDestroyDevice(device, None)
vk.vkDestroyInstance(instance, None)
