"""Execute each native flow projection entry on an actual Vulkan device and read its output volume."""
import json
import pathlib
import struct
import sys

import vulkan as vk

fixture_path, shader_directory, result_path = map(pathlib.Path, sys.argv[1:4])
fixture = json.loads(fixture_path.read_text())
extent = fixture["extent"]
cells = extent ** 3
entries = ["EvaluateDivergence", "RelaxPressure", "EvaluateResidual", "RestrictResidual",
           "ProlongateCorrection", "ClearPressureLevel", "SubtractPressureGradient", "RestoreVorticity"]
scalar_outputs = {"EvaluateDivergence", "RelaxPressure", "EvaluateResidual", "RestrictResidual",
                  "ProlongateCorrection", "ClearPressureLevel"}
instance = vk.vkCreateInstance(vk.VkInstanceCreateInfo(pApplicationInfo=vk.VkApplicationInfo(
    pApplicationName="FlowProjectionValidation", apiVersion=vk.VK_API_VERSION_1_0)), None)
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


buffers = []
images = []


def make_buffer(content, usage):
    buffer = vk.vkCreateBuffer(device, vk.VkBufferCreateInfo(
        size=len(content), usage=usage, sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE), None)
    memory = allocate(vk.vkGetBufferMemoryRequirements(device, buffer),
                      vk.VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | vk.VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    vk.vkBindBufferMemory(device, buffer, memory, 0)
    mapped = vk.vkMapMemory(device, memory, 0, len(content), 0)
    mapped[:] = content
    vk.vkUnmapMemory(device, memory)
    resource = {"buffer": buffer, "memory": memory, "size": len(content)}
    buffers.append(resource)
    return resource


def make_image(vector):
    image = vk.vkCreateImage(device, vk.VkImageCreateInfo(
        imageType=vk.VK_IMAGE_TYPE_3D,
        format=vk.VK_FORMAT_R16G16B16A16_SFLOAT if vector else vk.VK_FORMAT_R32_SFLOAT,
        extent=vk.VkExtent3D(width=extent, height=extent, depth=extent), mipLevels=1, arrayLayers=1,
        samples=vk.VK_SAMPLE_COUNT_1_BIT, tiling=vk.VK_IMAGE_TILING_OPTIMAL,
        usage=(vk.VK_IMAGE_USAGE_TRANSFER_DST_BIT | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               vk.VK_IMAGE_USAGE_SAMPLED_BIT | vk.VK_IMAGE_USAGE_STORAGE_BIT),
        sharingMode=vk.VK_SHARING_MODE_EXCLUSIVE, initialLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED), None)
    memory = allocate(vk.vkGetImageMemoryRequirements(device, image), 0)
    vk.vkBindImageMemory(device, image, memory, 0)
    subresource = vk.VkImageSubresourceRange(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                              baseMipLevel=0, levelCount=1, baseArrayLayer=0, layerCount=1)
    view = vk.vkCreateImageView(device, vk.VkImageViewCreateInfo(
        image=image, viewType=vk.VK_IMAGE_VIEW_TYPE_3D,
        format=vk.VK_FORMAT_R16G16B16A16_SFLOAT if vector else vk.VK_FORMAT_R32_SFLOAT,
        subresourceRange=subresource), None)
    resource = {"image": image, "memory": memory, "view": view, "range": subresource, "vector": vector}
    images.append(resource)
    return resource


def pack_float(readings):
    return struct.pack(f"<{len(readings)}f", *readings)


def pack_half(readings):
    return struct.pack(f"<{len(readings)}e", *readings)


flow = bytearray(192)
struct.pack_into("<f", flow, 0, fixture["delta"])
struct.pack_into("<f", flow, 12, fixture["strength"])
struct.pack_into("<f", flow, 18 * 4, fixture["enabled"])
flow_buffer = make_buffer(flow, vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
level_buffer = make_buffer(pack_float([extent, fixture["coarse"], fixture["weight"], 0]),
                           vk.VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
inputs = [make_image(True), make_image(False), make_image(False), make_image(False)]
input_contents = [pack_half(fixture["velocity"]), pack_float(fixture["pressure"]),
                  pack_float(fixture["divergence"]), pack_float(fixture["residual"])]
staging = []
for content in input_contents:
    staging.append(make_buffer(content, vk.VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
outputs = [make_image(name not in scalar_outputs) for name in entries]
dummy_velocity = make_image(True)
dummy_pressure = make_image(False)
dummy_divergence = make_image(False)
sentinel_scalar = pack_float([-37.0] * cells)
sentinel_vector = pack_half([-37.0] * (cells * 4))
scalar_staging = make_buffer(sentinel_scalar, vk.VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
vector_staging = make_buffer(sentinel_vector, vk.VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
readbacks = [make_buffer(bytes(len(sentinel_scalar) if name in scalar_outputs else len(sentinel_vector)),
                         vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT) for name in entries]

uniform = vk.VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
sampled = vk.VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
storage = vk.VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
types = [uniform, sampled, sampled, sampled, storage, storage, storage, uniform, sampled]
bindings = [vk.VkDescriptorSetLayoutBinding(binding=index, descriptorType=kind, descriptorCount=1,
                                            stageFlags=vk.VK_SHADER_STAGE_COMPUTE_BIT)
            for index, kind in enumerate(types)]
set_layout = vk.vkCreateDescriptorSetLayout(device, vk.VkDescriptorSetLayoutCreateInfo(
    bindingCount=len(bindings), pBindings=bindings), None)
pipeline_layout = vk.vkCreatePipelineLayout(device, vk.VkPipelineLayoutCreateInfo(
    setLayoutCount=1, pSetLayouts=[set_layout]), None)
pool_sizes = [vk.VkDescriptorPoolSize(type=kind, descriptorCount=types.count(kind) * len(entries))
              for kind in [uniform, sampled, storage]]
pool = vk.vkCreateDescriptorPool(device, vk.VkDescriptorPoolCreateInfo(
    maxSets=len(entries), poolSizeCount=len(pool_sizes), pPoolSizes=pool_sizes), None)
sets = vk.vkAllocateDescriptorSets(device, vk.VkDescriptorSetAllocateInfo(
    descriptorPool=pool, descriptorSetCount=len(entries), pSetLayouts=[set_layout] * len(entries)))
for index, name in enumerate(entries):
    for binding, buffer in [(0, flow_buffer), (7, level_buffer)]:
        info = vk.VkDescriptorBufferInfo(buffer=buffer["buffer"], offset=0, range=buffer["size"])
        write = vk.VkWriteDescriptorSet(dstSet=sets[index], dstBinding=binding, descriptorCount=1,
                                        descriptorType=uniform, pBufferInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)
    velocity_target = outputs[index] if name not in scalar_outputs else dummy_velocity
    pressure_target = outputs[index] if name in {"RelaxPressure", "ProlongateCorrection", "ClearPressureLevel"} else dummy_pressure
    divergence_target = outputs[index] if name in {"EvaluateDivergence", "EvaluateResidual", "RestrictResidual"} else dummy_divergence
    descriptors = [(1, sampled, inputs[0]), (2, sampled, inputs[1]), (3, sampled, inputs[2]),
                   (4, storage, velocity_target), (5, storage, pressure_target), (6, storage, divergence_target),
                   (8, sampled, inputs[3])]
    for binding, category, image in descriptors:
        info = vk.VkDescriptorImageInfo(imageView=image["view"], imageLayout=vk.VK_IMAGE_LAYOUT_GENERAL)
        write = vk.VkWriteDescriptorSet(dstSet=sets[index], dstBinding=binding, descriptorCount=1,
                                        descriptorType=category, pImageInfo=[info])
        vk.vkUpdateDescriptorSets(device, 1, [write], 0, None)

pipelines = []
modules = []
for name in entries:
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
barriers = [vk.VkImageMemoryBarrier(srcAccessMask=0, dstAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT,
    oldLayout=vk.VK_IMAGE_LAYOUT_UNDEFINED, newLayout=vk.VK_IMAGE_LAYOUT_GENERAL,
    srcQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED, dstQueueFamilyIndex=vk.VK_QUEUE_FAMILY_IGNORED,
    image=image["image"], subresourceRange=image["range"]) for image in images]
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, None, 0, None, len(barriers), barriers)
region = vk.VkBufferImageCopy(bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
    imageSubresource=vk.VkImageSubresourceLayers(aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                                                 mipLevel=0, baseArrayLayer=0, layerCount=1),
    imageOffset=vk.VkOffset3D(x=0, y=0, z=0), imageExtent=vk.VkExtent3D(width=extent, height=extent, depth=extent))
for source, target in zip(staging, inputs):
    vk.vkCmdCopyBufferToImage(command, source["buffer"], target["image"], vk.VK_IMAGE_LAYOUT_GENERAL, 1, [region])
for target in outputs + [dummy_velocity, dummy_pressure, dummy_divergence]:
    source = vector_staging if target["vector"] else scalar_staging
    vk.vkCmdCopyBufferToImage(command, source["buffer"], target["image"], vk.VK_IMAGE_LAYOUT_GENERAL, 1, [region])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT,
                             dstAccessMask=vk.VK_ACCESS_SHADER_READ_BIT | vk.VK_ACCESS_SHADER_WRITE_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
for index, pipeline in enumerate(pipelines):
    vk.vkCmdBindPipeline(command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipeline)
    vk.vkCmdBindDescriptorSets(command, vk.VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                               [sets[index]], 0, None)
    vk.vkCmdDispatch(command, (extent + 7) // 8, (extent + 7) // 8, (extent + 3) // 4)
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_SHADER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_TRANSFER_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, vk.VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 1, [barrier], 0, None, 0, None)
for image, destination in zip(outputs, readbacks):
    vk.vkCmdCopyImageToBuffer(command, image["image"], vk.VK_IMAGE_LAYOUT_GENERAL, destination["buffer"], 1, [region])
barrier = vk.VkMemoryBarrier(srcAccessMask=vk.VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask=vk.VK_ACCESS_HOST_READ_BIT)
vk.vkCmdPipelineBarrier(command, vk.VK_PIPELINE_STAGE_TRANSFER_BIT, vk.VK_PIPELINE_STAGE_HOST_BIT,
                        0, 1, [barrier], 0, None, 0, None)
vk.vkEndCommandBuffer(command)
vk.vkQueueSubmit(queue, 1, [vk.VkSubmitInfo(commandBufferCount=1, pCommandBuffers=[command])], vk.VK_NULL_HANDLE)
vk.vkQueueWaitIdle(queue)

result = {"device": vk.vkGetPhysicalDeviceProperties(physical).deviceName}
for name, resource in zip(entries, readbacks):
    mapped = vk.vkMapMemory(device, resource["memory"], 0, resource["size"], 0)
    count, category = (cells, "f") if name in scalar_outputs else (cells * 4, "e")
    result[name] = struct.unpack(f"<{count}{category}", bytes(mapped))
    vk.vkUnmapMemory(device, resource["memory"])
result_path.write_text(json.dumps(result, allow_nan=False))

vk.vkDeviceWaitIdle(device)
vk.vkDestroyCommandPool(device, command_pool, None)
for pipeline in pipelines:
    vk.vkDestroyPipeline(device, pipeline, None)
for module in modules:
    vk.vkDestroyShaderModule(device, module, None)
vk.vkDestroyDescriptorPool(device, pool, None)
vk.vkDestroyPipelineLayout(device, pipeline_layout, None)
vk.vkDestroyDescriptorSetLayout(device, set_layout, None)
for image in images:
    vk.vkDestroyImageView(device, image["view"], None)
    vk.vkDestroyImage(device, image["image"], None)
    vk.vkFreeMemory(device, image["memory"], None)
for resource in buffers:
    vk.vkDestroyBuffer(device, resource["buffer"], None)
    vk.vkFreeMemory(device, resource["memory"], None)
vk.vkDestroyDevice(device, None)
vk.vkDestroyInstance(instance, None)