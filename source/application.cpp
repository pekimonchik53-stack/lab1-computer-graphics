#include "application.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstddef>
#include <cmath>
#include <cstring>
#include <vector>
#include <iostream>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace application {
    namespace {

        struct Vertex {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 uv; // Текстурные координаты
        };

        struct UniformBufferObject {
            glm::mat4 model; glm::mat4 view; glm::mat4 proj;
            glm::vec3 cameraPos; float padding1;
            glm::vec3 lightDir; float padding2;
            glm::vec3 lightColor; float lightIntensity;
            glm::vec3 albedo; float padding3;
            glm::vec3 specularColor; float shininess;
            glm::vec3 ambientColor; float ambientIntensity;
        };

        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
        VkPipeline            graphics_pipeline = VK_NULL_HANDLE;
        VkCommandPool         command_pool = VK_NULL_HANDLE;

        VkBuffer vertex_buffer = VK_NULL_HANDLE; VmaAllocation vertex_buffer_allocation = VK_NULL_HANDLE;
        VkBuffer index_buffer = VK_NULL_HANDLE; VmaAllocation index_buffer_allocation = VK_NULL_HANDLE;
        VkBuffer uniform_buffer = VK_NULL_HANDLE; VmaAllocation uniform_buffer_allocation = VK_NULL_HANDLE;
        void* uniform_buffer_mapped = nullptr;

        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet  descriptor_set = VK_NULL_HANDLE;

        VkImage texture_image = VK_NULL_HANDLE; VmaAllocation texture_allocation = VK_NULL_HANDLE;
        VkImageView texture_image_view = VK_NULL_HANDLE;
        VkSampler texture_sampler = VK_NULL_HANDLE;

        constexpr float t = 1.6180339887498948482f;

        std::vector<Vertex> icosahedron_vertices = {
            {{-1,  t,  0}, glm::normalize(glm::vec3(-1,  t,  0)), {0.0f, 0.0f}},
            {{ 1,  t,  0}, glm::normalize(glm::vec3(1,  t,  0)), {1.0f, 0.0f}},
            {{-1, -t,  0}, glm::normalize(glm::vec3(-1, -t,  0)), {0.0f, 1.0f}},
            {{ 1, -t,  0}, glm::normalize(glm::vec3(1, -t,  0)), {1.0f, 1.0f}},
            {{ 0, -1,  t}, glm::normalize(glm::vec3(0, -1,  t)), {0.0f, 0.0f}},
            {{ 0,  1,  t}, glm::normalize(glm::vec3(0,  1,  t)), {1.0f, 0.0f}},
            {{ 0, -1, -t}, glm::normalize(glm::vec3(0, -1, -t)), {0.0f, 1.0f}},
            {{ 0,  1, -t}, glm::normalize(glm::vec3(0,  1, -t)), {1.0f, 1.0f}},
            {{ t,  0, -1}, glm::normalize(glm::vec3(t,  0, -1)), {0.0f, 0.0f}},
            {{ t,  0,  1}, glm::normalize(glm::vec3(t,  0,  1)), {1.0f, 0.0f}},
            {{-t,  0, -1}, glm::normalize(glm::vec3(-t,  0, -1)), {0.0f, 1.0f}},
            {{-t,  0,  1}, glm::normalize(glm::vec3(-t,  0,  1)), {1.0f, 1.0f}},
        };

        std::vector<uint32_t> icosahedron_indices = {
             0, 11,  5,   0,  5,  1,   0,  1,  7,   0,  7, 10,   0, 10, 11,
             1,  5,  9,   5, 11,  4,  11, 10,  2,  10,  7,  6,   7,  1,  8,
             3,  9,  4,   3,  4,  2,   3,  2,  6,   3,  6,  8,   3,  8,  9,
             4,  9,  5,   2,  4, 11,   6,  2, 10,   8,  6,  7,   9,  8,  1,
        };

        struct Controls {
            float camX = 0.0f, camY = 1.0f, camZ = 3.0f;
            float fov = 60.0f;
            float lightDirX = -0.5f, lightDirY = -1.0f, lightDirZ = -0.5f;
            float lightColor[3] = { 1.0f, 1.0f, 1.0f };
            float lightIntensity = 1.0f;
            float albedoColor[3] = { 1.0f, 1.0f, 1.0f };
            float specularColor[3] = { 1.0f, 1.0f, 1.0f };
            float shininess = 32.0f;
            float ambientColor[3] = { 1.0f, 1.0f, 1.0f };
            float ambientIntensity = 0.1f;
        };
        Controls controls;

        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
            auto& ctx = graphics::internal::context;
            VkCommandBufferAllocateInfo allocInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(ctx.device, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            vkBeginCommandBuffer(commandBuffer, &beginInfo);
            VkBufferCopy copyRegion = { .srcOffset = 0, .dstOffset = 0, .size = size };
            vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
            vkQueueSubmit(ctx.graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(ctx.graphics_queue);
            vkFreeCommandBuffers(ctx.device, command_pool, 1, &commandBuffer);
        }

        bool createDeviceLocalBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuffer, VmaAllocation& outAllocation) {
            VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VmaAllocationCreateInfo allocInfo = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
            if (vmaCreateBuffer(graphics::internal::context.allocator, &bufferInfo, &allocInfo, &outBuffer, &outAllocation, nullptr) != VK_SUCCESS) return false;
            return true;
        }

        bool uploadToDeviceLocal(VkBuffer dstBuffer, const void* data, VkDeviceSize size) {
            auto& ctx = graphics::internal::context;
            VkBuffer stagingBuffer; VmaAllocation stagingAllocation;
            VkBufferCreateInfo bufInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VmaAllocationCreateInfo allocInfo = { .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
            vmaCreateBuffer(ctx.allocator, &bufInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr);

            void* mapData; vmaMapMemory(ctx.allocator, stagingAllocation, &mapData);
            std::memcpy(mapData, data, (size_t)size);
            vmaUnmapMemory(ctx.allocator, stagingAllocation);

            copyBuffer(stagingBuffer, dstBuffer, size);
            vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAllocation);
            return true;
        }

        bool createVertexBuffer() {
            const VkDeviceSize size = sizeof(Vertex) * icosahedron_vertices.size();
            if (!createDeviceLocalBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertex_buffer, vertex_buffer_allocation)) return false;
            return uploadToDeviceLocal(vertex_buffer, icosahedron_vertices.data(), size);
        }

        bool createIndexBuffer() {
            const VkDeviceSize size = sizeof(uint32_t) * icosahedron_indices.size();
            if (!createDeviceLocalBuffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, index_buffer, index_buffer_allocation)) return false;
            return uploadToDeviceLocal(index_buffer, icosahedron_indices.data(), size);
        }

        bool createTexture() {
            auto& ctx = graphics::internal::context;
            int texWidth, texHeight, texChannels;
            stbi_uc* pixels = stbi_load("texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            if (!pixels) { std::cerr << "Failed to load texture.jpg\n"; return false; }

            VkDeviceSize imageSize = texWidth * texHeight * 4;

            VkImageCreateInfo imageInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = VK_FORMAT_R8G8B8A8_SRGB,
                .extent = {(uint32_t)texWidth, (uint32_t)texHeight, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };
            VmaAllocationCreateInfo allocInfo = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
            if (vmaCreateImage(ctx.allocator, &imageInfo, &allocInfo, &texture_image, &texture_allocation, nullptr) != VK_SUCCESS) { stbi_image_free(pixels); return false; }

            VkBuffer stagingBuffer; VmaAllocation stagingAlloc;
            VkBufferCreateInfo bufInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = imageSize, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VmaAllocationCreateInfo stageAlloc = { .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
            vmaCreateBuffer(ctx.allocator, &bufInfo, &stageAlloc, &stagingBuffer, &stagingAlloc, nullptr);
            void* data; vmaMapMemory(ctx.allocator, stagingAlloc, &data); std::memcpy(data, pixels, (size_t)imageSize); vmaUnmapMemory(ctx.allocator, stagingAlloc);
            stbi_image_free(pixels);

            VkCommandBufferAllocateInfo cmdAlloc = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
            VkCommandBuffer cmd; vkAllocateCommandBuffers(ctx.device, &cmdAlloc, &cmd);
            VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
            vkBeginCommandBuffer(cmd, &beginInfo);

            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = texture_image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 }, .imageOffset = {0,0,0}, .imageExtent = {(uint32_t)texWidth, (uint32_t)texHeight, 1} };
            vkCmdCopyBufferToImage(cmd, stagingBuffer, texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier barrier2 = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = texture_image,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier2);

            vkEndCommandBuffer(cmd);
            VkSubmitInfo submit = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
            vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(ctx.graphics_queue);
            vkFreeCommandBuffers(ctx.device, command_pool, 1, &cmd);
            vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAlloc);

            VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = texture_image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_SRGB, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 } };
            vkCreateImageView(ctx.device, &viewInfo, nullptr, &texture_image_view);

            VkSamplerCreateInfo samplerInfo = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                .unnormalizedCoordinates = VK_FALSE
            };
            vkCreateSampler(ctx.device, &samplerInfo, nullptr, &texture_sampler);

            return true;
        }

        bool createUniformBuffer() {
            const VkDeviceSize size = sizeof(UniformBufferObject);
            VkBufferCreateInfo bufInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
            VmaAllocationCreateInfo allocInfo = { .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
            if (vmaCreateBuffer(graphics::internal::context.allocator, &bufInfo, &allocInfo, &uniform_buffer, &uniform_buffer_allocation, nullptr) != VK_SUCCESS) return false;
            if (vmaMapMemory(graphics::internal::context.allocator, uniform_buffer_allocation, &uniform_buffer_mapped) != VK_SUCCESS) return false;
            return true;
        }

        bool createDescriptorSetLayout() {
            VkDescriptorSetLayoutBinding bindings[2] = {};
            bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; bindings[0].descriptorCount = 1; bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings };
            if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layoutInfo, nullptr, &descriptor_set_layout) != VK_SUCCESS) return false;
            return true;
        }

        bool createDescriptorSet() {
            VkDescriptorPoolSize poolSizes[2] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
            VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = poolSizes };
            if (vkCreateDescriptorPool(graphics::internal::context.device, &poolInfo, nullptr, &descriptor_pool) != VK_SUCCESS) return false;

            VkDescriptorSetAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &descriptor_set_layout };
            if (vkAllocateDescriptorSets(graphics::internal::context.device, &allocInfo, &descriptor_set) != VK_SUCCESS) return false;

            VkDescriptorBufferInfo bufferInfo = { uniform_buffer, 0, sizeof(UniformBufferObject) };
            VkDescriptorImageInfo imageInfo = { texture_sampler, texture_image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

            VkWriteDescriptorSet writes[2] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = descriptor_set; writes[0].dstBinding = 0; writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[0].pBufferInfo = &bufferInfo;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet = descriptor_set; writes[1].dstBinding = 1; writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(graphics::internal::context.device, 2, writes, 0, nullptr);
            return true;
        }

        std::vector<char> readShaderFile(const std::string& filename) {
            FILE* file = nullptr; fopen_s(&file, filename.c_str(), "rb");
            if (!file) return {};
            fseek(file, 0, SEEK_END); long size = ftell(file); fseek(file, 0, SEEK_SET);
            std::vector<char> buffer(size); fread(buffer.data(), 1, size, file); fclose(file); return buffer;
        }

        VkShaderModule createShaderModule(const std::vector<char>& code) {
            if (code.empty()) return VK_NULL_HANDLE;
            VkShaderModuleCreateInfo info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size(), .pCode = (const uint32_t*)code.data() };
            VkShaderModule mod; vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &mod); return mod;
        }

        bool createGraphicsPipeline() {
            auto vert_code = readShaderFile("shaders/icosahedron.vert.spv");
            auto frag_code = readShaderFile("shaders/icosahedron.frag.spv");
            VkShaderModule vert = createShaderModule(vert_code); VkShaderModule frag = createShaderModule(frag_code);

            VkPipelineShaderStageCreateInfo stages[] = {
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
            };

            VkVertexInputBindingDescription bindDesc = { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
            VkVertexInputAttributeDescription attrDesc[] = {
                {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
                {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
                {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
            };
            VkPipelineVertexInputStateCreateInfo vertexInput = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindDesc, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attrDesc };

            VkPipelineInputAssemblyStateCreateInfo inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
            VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
            VkPipelineRasterizationStateCreateInfo raster = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
            VkPipelineMultisampleStateCreateInfo multisample = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
            VkPipelineDepthStencilStateCreateInfo depth = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS };
            VkPipelineColorBlendAttachmentState blendAttach = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
            VkPipelineColorBlendStateCreateInfo blend = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttach };
            VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynStates };
            VkPipelineLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &descriptor_set_layout };
            vkCreatePipelineLayout(graphics::internal::context.device, &layoutInfo, nullptr, &pipeline_layout);

            VkGraphicsPipelineCreateInfo pipeInfo = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vertexInput, .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState, .pRasterizationState = &raster, .pMultisampleState = &multisample, .pDepthStencilState = &depth, .pColorBlendState = &blend, .pDynamicState = &dynState, .layout = pipeline_layout, .renderPass = graphics::internal::context.render_pass
            };
            if (vkCreateGraphicsPipelines(graphics::internal::context.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &graphics_pipeline) != VK_SUCCESS) return false;

            vkDestroyShaderModule(graphics::internal::context.device, vert, nullptr);
            vkDestroyShaderModule(graphics::internal::context.device, frag, nullptr);
            return true;
        }

        void updateUniformBuffer() {
            auto& ctx = graphics::internal::context;
            float aspect = (float)ctx.swapchain_extent.width / (float)ctx.swapchain_extent.height;
            glm::mat4 model = glm::mat4(1.0f);
            glm::vec3 camPos(controls.camX, controls.camY, controls.camZ);
            glm::mat4 view = glm::lookAt(camPos, glm::vec3(0), glm::vec3(0, 1, 0));
            glm::mat4 proj = glm::perspective(glm::radians(controls.fov), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f;

            UniformBufferObject ubo = {};
            ubo.model = model; ubo.view = view; ubo.proj = proj; ubo.cameraPos = camPos;
            ubo.lightDir = glm::normalize(glm::vec3(controls.lightDirX, controls.lightDirY, controls.lightDirZ));
            ubo.lightColor = glm::vec3(controls.lightColor[0], controls.lightColor[1], controls.lightColor[2]); ubo.lightIntensity = controls.lightIntensity;
            ubo.albedo = glm::vec3(controls.albedoColor[0], controls.albedoColor[1], controls.albedoColor[2]);
            ubo.specularColor = glm::vec3(controls.specularColor[0], controls.specularColor[1], controls.specularColor[2]); ubo.shininess = controls.shininess;
            ubo.ambientColor = glm::vec3(controls.ambientColor[0], controls.ambientColor[1], controls.ambientColor[2]); ubo.ambientIntensity = controls.ambientIntensity;
            std::memcpy(uniform_buffer_mapped, &ubo, sizeof(ubo));
        }

    } // namespace

    bool initialize() {
        auto& ctx = graphics::internal::context;
        VkCommandPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = ctx.graphics_queue_index };
        vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &command_pool);

        if (!createTexture()) return false;
        if (!createVertexBuffer()) return false;
        if (!createIndexBuffer()) return false;
        if (!createUniformBuffer()) return false;
        if (!createDescriptorSetLayout()) return false;
        if (!createDescriptorSet()) return false;
        if (!createGraphicsPipeline()) return false;
        return true;
    }

    void shutdown() {
        auto& ctx = graphics::internal::context;
        vkQueueWaitIdle(ctx.graphics_queue);
        vkDestroySampler(ctx.device, texture_sampler, nullptr);
        vkDestroyImageView(ctx.device, texture_image_view, nullptr);
        vmaDestroyImage(ctx.allocator, texture_image, texture_allocation);
        vkDestroyCommandPool(ctx.device, command_pool, nullptr);
        if (graphics_pipeline) vkDestroyPipeline(ctx.device, graphics_pipeline, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
        if (descriptor_set_layout) vkDestroyDescriptorSetLayout(ctx.device, descriptor_set_layout, nullptr);
        if (descriptor_pool) vkDestroyDescriptorPool(ctx.device, descriptor_pool, nullptr);
        if (uniform_buffer_mapped) vmaUnmapMemory(ctx.allocator, uniform_buffer_allocation);
        if (uniform_buffer) vmaDestroyBuffer(ctx.allocator, uniform_buffer, uniform_buffer_allocation);
        if (index_buffer) vmaDestroyBuffer(ctx.allocator, index_buffer, index_buffer_allocation);
        if (vertex_buffer) vmaDestroyBuffer(ctx.allocator, vertex_buffer, vertex_buffer_allocation);
    }

    void update(double time) {
        ImGui::Begin("Texture Controls");
        ImGui::SliderFloat("Cam X", &controls.camX, -5, 5); ImGui::SliderFloat("Cam Y", &controls.camY, -5, 5); ImGui::SliderFloat("Cam Z", &controls.camZ, -5, 5); ImGui::SliderFloat("FOV", &controls.fov, 30, 120);
        ImGui::Separator();
        ImGui::ColorEdit3("Albedo (Tint)", controls.albedoColor);
        ImGui::SliderFloat("Light Intensity", &controls.lightIntensity, 0, 2);
        ImGui::ColorEdit3("Light Color", controls.lightColor);
        ImGui::SliderFloat("Ambient", &controls.ambientIntensity, 0, 1);
        ImGui::End();
        updateUniformBuffer();
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;
        vkResetCommandBuffer(fd.command_buffer, 0);
        VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(fd.command_buffer, &beginInfo);
        VkClearValue clearValues[] = { {.color = {.float32 = { 0.05f, 0.05f, 0.05f, 1.0f } } }, {.depthStencil = {.depth = 1.0f, .stencil = 0 } } };
        VkRenderPassBeginInfo renderPassInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = ctx.render_pass, .framebuffer = fd.framebuffer, .renderArea = {.offset = {0,0}, .extent = ctx.swapchain_extent}, .clearValueCount = 2, .pClearValues = clearValues };
        vkCmdBeginRenderPass(fd.command_buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
        VkViewport viewport = { .x = 0, .y = 0, .width = (float)ctx.swapchain_extent.width, .height = (float)ctx.swapchain_extent.height, .minDepth = 0, .maxDepth = 1 };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
        VkRect2D scissor = { .offset = {0,0}, .extent = ctx.swapchain_extent };
        vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, offsets);
        vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, (uint32_t)icosahedron_indices.size(), 1, 0, 0, 0);
        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }

} // namespace application