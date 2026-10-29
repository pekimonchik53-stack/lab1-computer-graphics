#include "application.hpp"

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

        // ============================================================
        //  СТРУКТУРЫ ДАННЫХ
        // ============================================================

        // Вершина теперь содержит нормаль вместо цвета
        struct Vertex {
            glm::vec3 position;
            glm::vec3 normal;
        };

        // Матрицы и параметры освещения (передаются в шейдер)
        struct UniformBufferObject {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;

            glm::vec3 cameraPos;
            float padding1;     // <--- ВЫРАВНИВАНИЕ (чтобы следующий vec3 начался с 16-байтной границы)

            glm::vec3 lightDir;
            float padding2;

            glm::vec3 lightColor;
            float lightIntensity; // 4 байта (закрывает 16-байтный блок)

            glm::vec3 albedo;
            float padding3;

            glm::vec3 specularColor;
            float shininess;      // 4 байта

            glm::vec3 ambientColor;
            float ambientIntensity; // 4 байта
        };

        // Глобальные Vulkan-объекты
        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
        VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
        VkPipeline            graphics_pipeline = VK_NULL_HANDLE;

        VkBuffer       vertex_buffer = VK_NULL_HANDLE;
        VmaAllocation  vertex_buffer_allocation = VK_NULL_HANDLE;
        VkBuffer       index_buffer = VK_NULL_HANDLE;
        VmaAllocation  index_buffer_allocation = VK_NULL_HANDLE;
        VkBuffer       uniform_buffer = VK_NULL_HANDLE;
        VmaAllocation  uniform_buffer_allocation = VK_NULL_HANDLE;
        void* uniform_buffer_mapped = nullptr;

        VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet  descriptor_set = VK_NULL_HANDLE;

        // ============================================================
        //  ГЕОМЕТРИЯ ИКОСАЭДРА (с нормалями)
        // ============================================================
        constexpr float t = 1.6180339887498948482f;

        std::vector<Vertex> icosahedron_vertices = {
            {{-1,  t,  0}, glm::normalize(glm::vec3(-1,  t,  0))},
            {{ 1,  t,  0}, glm::normalize(glm::vec3(1,  t,  0))},
            {{-1, -t,  0}, glm::normalize(glm::vec3(-1, -t,  0))},
            {{ 1, -t,  0}, glm::normalize(glm::vec3(1, -t,  0))},

            {{ 0, -1,  t}, glm::normalize(glm::vec3(0, -1,  t))},
            {{ 0,  1,  t}, glm::normalize(glm::vec3(0,  1,  t))},
            {{ 0, -1, -t}, glm::normalize(glm::vec3(0, -1, -t))},
            {{ 0,  1, -t}, glm::normalize(glm::vec3(0,  1, -t))},

            {{ t,  0, -1}, glm::normalize(glm::vec3(t,  0, -1))},
            {{ t,  0,  1}, glm::normalize(glm::vec3(t,  0,  1))},
            {{-t,  0, -1}, glm::normalize(glm::vec3(-t,  0, -1))},
            {{-t,  0,  1}, glm::normalize(glm::vec3(-t,  0,  1))},
        };

        std::vector<uint32_t> icosahedron_indices = {
             0, 11,  5,   0,  5,  1,   0,  1,  7,   0,  7, 10,   0, 10, 11,
             1,  5,  9,   5, 11,  4,  11, 10,  2,  10,  7,  6,   7,  1,  8,
             3,  9,  4,   3,  4,  2,   3,  2,  6,   3,  6,  8,   3,  8,  9,
             4,  9,  5,   2,  4, 11,   6,  2, 10,   8,  6,  7,   9,  8,  1,
        };

        // ============================================================
        //  ПАРАМЕТРЫ ИНТЕРФЕЙСА
        // ============================================================
        struct Controls {
            // Камера
            float camX = 0.0f, camY = 1.0f, camZ = 3.0f;
            float yaw = 0.0f, pitch = 0.0f; // Поворот камеры
            float fov = 60.0f;

            // Свет
            float lightDirX = -0.5f, lightDirY = -1.0f, lightDirZ = -0.5f;
            float lightColor[3] = { 1.0f, 1.0f, 1.0f };
            float lightIntensity = 1.0f;

            // Материал
            float albedoColor[3] = { 0.8f, 0.2f, 0.2f }; // Красноватый
            float specularColor[3] = { 1.0f, 1.0f, 1.0f };
            float shininess = 32.0f;

            // Ambient
            float ambientColor[3] = { 1.0f, 1.0f, 1.0f };
            float ambientIntensity = 0.1f;
        };

        Controls controls;

        // ============================================================
        //  ФУНКЦИИ СОЗДАНИЯ РЕСУРСОВ (без изменений)
        // ============================================================
        bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VmaMemoryUsage memory_usage,
            VkBuffer& out_buffer, VmaAllocation& out_allocation) {
            const VkBufferCreateInfo buffer_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            };
            const VmaAllocationCreateInfo alloc_info = {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = memory_usage,
            };
            if (vmaCreateBuffer(graphics::internal::context.allocator, &buffer_info, &alloc_info,
                &out_buffer, &out_allocation, nullptr) != VK_SUCCESS) {
                std::cerr << "Failed to create Vulkan buffer\n";
                return false;
            }
            return true;
        }

        bool createVertexBuffer() {
            const VkDeviceSize buffer_size = sizeof(Vertex) * icosahedron_vertices.size();
            if (!createBuffer(buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO, vertex_buffer, vertex_buffer_allocation)) return false;
            void* data = nullptr;
            vmaMapMemory(graphics::internal::context.allocator, vertex_buffer_allocation, &data);
            std::memcpy(data, icosahedron_vertices.data(), static_cast<size_t>(buffer_size));
            vmaUnmapMemory(graphics::internal::context.allocator, vertex_buffer_allocation);
            return true;
        }

        bool createIndexBuffer() {
            const VkDeviceSize buffer_size = sizeof(uint32_t) * icosahedron_indices.size();
            if (!createBuffer(buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO, index_buffer, index_buffer_allocation)) return false;
            void* data = nullptr;
            vmaMapMemory(graphics::internal::context.allocator, index_buffer_allocation, &data);
            std::memcpy(data, icosahedron_indices.data(), static_cast<size_t>(buffer_size));
            vmaUnmapMemory(graphics::internal::context.allocator, index_buffer_allocation);
            return true;
        }

        bool createUniformBuffer() {
            const VkDeviceSize buffer_size = sizeof(UniformBufferObject);
            if (!createBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO, uniform_buffer, uniform_buffer_allocation)) return false;
            if (vmaMapMemory(graphics::internal::context.allocator, uniform_buffer_allocation,
                &uniform_buffer_mapped) != VK_SUCCESS) {
                std::cerr << "Failed to map uniform buffer\n";
                return false;
            }
            return true;
        }

        bool createDescriptorSetLayout() {
            const VkDescriptorSetLayoutBinding ubo_binding = {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            };
            const VkDescriptorSetLayoutCreateInfo layout_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1, .pBindings = &ubo_binding,
            };
            if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layout_info,
                nullptr, &descriptor_set_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create descriptor set layout\n";
                return false;
            }
            return true;
        }

        bool createDescriptorSet() {
            const VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
            const VkDescriptorPoolCreateInfo pool_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size,
            };
            if (vkCreateDescriptorPool(graphics::internal::context.device, &pool_info,
                nullptr, &descriptor_pool) != VK_SUCCESS) return false;

            const VkDescriptorSetAllocateInfo alloc_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &descriptor_set_layout,
            };
            if (vkAllocateDescriptorSets(graphics::internal::context.device, &alloc_info,
                &descriptor_set) != VK_SUCCESS) return false;

            const VkDescriptorBufferInfo buffer_info = { uniform_buffer, 0, sizeof(UniformBufferObject) };
            const VkWriteDescriptorSet descriptor_write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set, .dstBinding = 0, .dstArrayElement = 0,
                .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            };
            vkUpdateDescriptorSets(graphics::internal::context.device, 1, &descriptor_write, 0, nullptr);
            return true;
        }

        std::vector<char> readShaderFile(const std::string& filename) {
            FILE* file = nullptr;
            fopen_s(&file, filename.c_str(), "rb");
            if (!file) return {};
            fseek(file, 0, SEEK_END);
            const long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            std::vector<char> buffer(static_cast<size_t>(size));
            fread(buffer.data(), 1, static_cast<size_t>(size), file);
            fclose(file);
            return buffer;
        }

        VkShaderModule createShaderModule(const std::vector<char>& code) {
            if (code.empty()) return VK_NULL_HANDLE;
            const VkShaderModuleCreateInfo module_info = {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data()),
            };
            VkShaderModule module = VK_NULL_HANDLE;
            vkCreateShaderModule(graphics::internal::context.device, &module_info, nullptr, &module);
            return module;
        }

        bool createGraphicsPipeline() {
            const auto vert_code = readShaderFile("shaders/icosahedron.vert.spv");
            const auto frag_code = readShaderFile("shaders/icosahedron.frag.spv");
            const VkShaderModule vert_module = createShaderModule(vert_code);
            const VkShaderModule frag_module = createShaderModule(frag_code);

            const VkPipelineShaderStageCreateInfo shader_stages[] = {
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
            };

            const VkVertexInputBindingDescription binding_desc = {
                .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };
            const VkVertexInputAttributeDescription attribute_descs[] = {
                {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
                {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
            };
            const VkPipelineVertexInputStateCreateInfo vertex_input = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding_desc,
                .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attribute_descs,
            };
            const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE,
            };
            const VkPipelineViewportStateCreateInfo viewport_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1,
            };
            const VkPipelineRasterizationStateCreateInfo rasterizer = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .depthBiasEnable = VK_FALSE, .lineWidth = 1.0f,
            };
            const VkPipelineMultisampleStateCreateInfo multisampling = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, .sampleShadingEnable = VK_FALSE,
            };
            const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS,
                .depthBoundsTestEnable = VK_FALSE, .stencilTestEnable = VK_FALSE,
            };
            const VkPipelineColorBlendAttachmentState color_blend_attachment = {
                .blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };
            const VkPipelineColorBlendStateCreateInfo color_blending = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .logicOpEnable = VK_FALSE, .attachmentCount = 1, .pAttachments = &color_blend_attachment,
            };
            const VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            const VkPipelineDynamicStateCreateInfo dynamic_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
            };
            const VkPipelineLayoutCreateInfo layout_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1, .pSetLayouts = &descriptor_set_layout,
            };
            vkCreatePipelineLayout(graphics::internal::context.device, &layout_info, nullptr, &pipeline_layout);

            const VkGraphicsPipelineCreateInfo pipeline_info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2, .pStages = shader_stages,
                .pVertexInputState = &vertex_input, .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_state, .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling, .pDepthStencilState = &depth_stencil,
                .pColorBlendState = &color_blending, .pDynamicState = &dynamic_state,
                .layout = pipeline_layout, .renderPass = graphics::internal::context.render_pass, .subpass = 0,
            };
            if (vkCreateGraphicsPipelines(graphics::internal::context.device, VK_NULL_HANDLE,
                1, &pipeline_info, nullptr, &graphics_pipeline) != VK_SUCCESS) return false;

            vkDestroyShaderModule(graphics::internal::context.device, vert_module, nullptr);
            vkDestroyShaderModule(graphics::internal::context.device, frag_module, nullptr);
            return true;
        }

        // ============================================================
        //  ОБНОВЛЕНИЕ UNIFORM BUFFER
        // ============================================================
        void updateUniformBuffer() {
            auto& ctx = graphics::internal::context;
            const float aspect = static_cast<float>(ctx.swapchain_extent.width) / static_cast<float>(ctx.swapchain_extent.height);

            glm::mat4 model = glm::mat4(1.0f); // Объект в центре, без вращения

            // Камера смотрит в центр из позиции controls.camX/Y/Z
            glm::vec3 camPos(controls.camX, controls.camY, controls.camZ);
            glm::vec3 target(0.0f, 0.0f, 0.0f);
            glm::mat4 view = glm::lookAt(camPos, target, glm::vec3(0.0f, 1.0f, 0.0f));

            glm::mat4 proj = glm::perspective(glm::radians(controls.fov), aspect, 0.1f, 100.0f);
            proj[1][1] *= -1.0f; // Vulkan Y-flip

            UniformBufferObject ubo = {};
            ubo.model = model;
            ubo.view = view;
            ubo.proj = proj;

            ubo.cameraPos = camPos;

            ubo.lightDir = glm::normalize(glm::vec3(controls.lightDirX, controls.lightDirY, controls.lightDirZ));
            ubo.lightColor = glm::vec3(controls.lightColor[0], controls.lightColor[1], controls.lightColor[2]);
            ubo.lightIntensity = controls.lightIntensity;

            ubo.albedo = glm::vec3(controls.albedoColor[0], controls.albedoColor[1], controls.albedoColor[2]);
            ubo.specularColor = glm::vec3(controls.specularColor[0], controls.specularColor[1], controls.specularColor[2]);
            ubo.shininess = controls.shininess;

            ubo.ambientColor = glm::vec3(controls.ambientColor[0], controls.ambientColor[1], controls.ambientColor[2]);
            ubo.ambientIntensity = controls.ambientIntensity;

            std::memcpy(uniform_buffer_mapped, &ubo, sizeof(ubo));
        }

    } // namespace

    // ============================================================
    //  ПУБЛИЧНЫЕ ФУНКЦИИ
    // ============================================================
    bool initialize() {
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
        ImGui::Begin("Lighting & Camera Controls");

        ImGui::Text("Camera Position");
        ImGui::SliderFloat("X", &controls.camX, -5.0f, 5.0f);
        ImGui::SliderFloat("Y", &controls.camY, -5.0f, 5.0f);
        ImGui::SliderFloat("Z", &controls.camZ, -5.0f, 5.0f);
        ImGui::SliderFloat("FOV", &controls.fov, 30.0f, 120.0f);

        ImGui::Separator();

        ImGui::Text("Directional Light");
        ImGui::SliderFloat("Dir X", &controls.lightDirX, -1.0f, 1.0f);
        ImGui::SliderFloat("Dir Y", &controls.lightDirY, -1.0f, 1.0f);
        ImGui::SliderFloat("Dir Z", &controls.lightDirZ, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", controls.lightColor);
        ImGui::SliderFloat("Light Intensity", &controls.lightIntensity, 0.0f, 2.0f);

        ImGui::Separator();

        ImGui::Text("Material (Blinn-Phong)");
        ImGui::ColorEdit3("Albedo", controls.albedoColor);
        ImGui::ColorEdit3("Specular", controls.specularColor);
        ImGui::SliderFloat("Shininess", &controls.shininess, 2.0f, 256.0f, "%.0f");

        ImGui::Separator();

        ImGui::Text("Ambient Light");
        ImGui::ColorEdit3("Ambient Color", controls.ambientColor);
        ImGui::SliderFloat("Ambient Intensity", &controls.ambientIntensity, 0.0f, 1.0f);

        ImGui::End();

        updateUniformBuffer();
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;
        vkResetCommandBuffer(fd.command_buffer, 0);

        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(fd.command_buffer, &begin_info);

        const VkClearValue clear_values[] = {
            {.color = {.float32 = { 0.05f, 0.05f, 0.05f, 1.0f } } }, // Темный фон
            {.depthStencil = {.depth = 1.0f, .stencil = 0 } },
        };
        const VkRenderPassBeginInfo render_pass_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.render_pass, .framebuffer = fd.framebuffer,
            .renderArea = {.offset = { 0, 0 }, .extent = ctx.swapchain_extent },
            .clearValueCount = 2, .pClearValues = clear_values,
        };
        vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

        const VkViewport viewport = {
            .x = 0.0f, .y = 0.0f,
            .width = static_cast<float>(ctx.swapchain_extent.width),
            .height = static_cast<float>(ctx.swapchain_extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f,
        };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

        const VkRect2D scissor = { .offset = { 0, 0 }, .extent = ctx.swapchain_extent };
        vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

        const VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, offsets);
        vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

        vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(icosahedron_indices.size()), 1, 0, 0, 0);

        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }

} // namespace application
