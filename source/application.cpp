#include "application.hpp"

#include <cstddef>
#include <array>
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
        //  Структура одной вершины
        // ============================================================
        struct Vertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        // ============================================================
        //  Данные икосаэдра
        // ============================================================
        constexpr float t = 1.6180339887498948482f;  // золотое сечение

        std::vector<Vertex> icosahedron_vertices = {
            {{-1,  t,  0}, {0.0f, 1.0f, 0.5f}},
            {{ 1,  t,  0}, {1.0f, 1.0f, 0.5f}},
            {{-1, -t,  0}, {0.0f, 0.0f, 0.5f}},
            {{ 1, -t,  0}, {1.0f, 0.0f, 0.5f}},

            {{ 0, -1,  t}, {0.5f, 0.0f, 1.0f}},
            {{ 0,  1,  t}, {0.5f, 1.0f, 1.0f}},
            {{ 0, -1, -t}, {0.5f, 0.0f, 0.0f}},
            {{ 0,  1, -t}, {0.5f, 1.0f, 0.0f}},

            {{ t,  0, -1}, {1.0f, 0.5f, 0.0f}},
            {{ t,  0,  1}, {1.0f, 0.5f, 1.0f}},
            {{-t,  0, -1}, {0.0f, 0.5f, 0.0f}},
            {{-t,  0,  1}, {0.0f, 0.5f, 1.0f}},
        };

        std::vector<uint32_t> icosahedron_indices = {
             0, 11,  5,   0,  5,  1,   0,  1,  7,   0,  7, 10,   0, 10, 11,
             1,  5,  9,   5, 11,  4,  11, 10,  2,  10,  7,  6,   7,  1,  8,
             3,  9,  4,   3,  4,  2,   3,  2,  6,   3,  6,  8,   3,  8,  9,
             4,  9,  5,   2,  4, 11,   6,  2, 10,   8,  6,  7,   9,  8,  1,
        };

        // ============================================================
        //  Uniform buffer
        // ============================================================
        struct UniformBufferObject {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
            glm::vec4 color_multiplier;
        };

        // ============================================================
        //  Глобальные Vulkan-объекты
        // ============================================================
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
        //  Параметры, управляемые через ImGUI
        // ============================================================
        struct Controls {
            // Поворот
            float rotation_x = 0.0f;
            float rotation_y = 0.0f;
            float rotation_z = 0.0f;
            float scale = 1.0f;

            // Позиция (задание 2)
            float position[3] = { 0.0f, 0.0f, 0.0f };

            // Цвет (задание 4 и 5)
            float color[3] = { 1.0f, 1.0f, 1.0f };

            // Проекция (задание 1)
            bool  use_perspective = true;
            float fov_degrees = 60.0f;

            // Анимация (задание 3)
            bool  playing = true;
            float animation_speed = 30.0f;
            float trajectory_radius = 0.0f;
        };

        Controls controls;

        // ============================================================
        //  Создание буфера через VMA
        // ============================================================
        bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VmaMemoryUsage memory_usage,
            VkBuffer& out_buffer, VmaAllocation& out_allocation) {
            const VkBufferCreateInfo buffer_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
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

        // ============================================================
        //  Vertex buffer
        // ============================================================
        bool createVertexBuffer() {
            const VkDeviceSize buffer_size = sizeof(Vertex) * icosahedron_vertices.size();

            if (!createBuffer(buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO, vertex_buffer, vertex_buffer_allocation)) {
                return false;
            }

            void* data = nullptr;
            vmaMapMemory(graphics::internal::context.allocator, vertex_buffer_allocation, &data);
            std::memcpy(data, icosahedron_vertices.data(), static_cast<size_t>(buffer_size));
            vmaUnmapMemory(graphics::internal::context.allocator, vertex_buffer_allocation);

            return true;
        }

        // ============================================================
        //  Index buffer
        // ============================================================
        bool createIndexBuffer() {
            const VkDeviceSize buffer_size = sizeof(uint32_t) * icosahedron_indices.size();

            if (!createBuffer(buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO, index_buffer, index_buffer_allocation)) {
                return false;
            }

            void* data = nullptr;
            vmaMapMemory(graphics::internal::context.allocator, index_buffer_allocation, &data);
            std::memcpy(data, icosahedron_indices.data(), static_cast<size_t>(buffer_size));
            vmaUnmapMemory(graphics::internal::context.allocator, index_buffer_allocation);

            return true;
        }

        // ============================================================
        //  Uniform buffer
        // ============================================================
        bool createUniformBuffer() {
            const VkDeviceSize buffer_size = sizeof(UniformBufferObject);

            if (!createBuffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO,
                uniform_buffer, uniform_buffer_allocation)) {
                return false;
            }

            if (vmaMapMemory(graphics::internal::context.allocator, uniform_buffer_allocation,
                &uniform_buffer_mapped) != VK_SUCCESS) {
                std::cerr << "Failed to map uniform buffer\n";
                return false;
            }

            return true;
        }

        // ============================================================
        //  Descriptor set layout
        // ============================================================
        bool createDescriptorSetLayout() {
            const VkDescriptorSetLayoutBinding ubo_binding = {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            };

            const VkDescriptorSetLayoutCreateInfo layout_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings = &ubo_binding,
            };

            if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layout_info,
                nullptr, &descriptor_set_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create descriptor set layout\n";
                return false;
            }

            return true;
        }

        // ============================================================
        //  Descriptor pool + set
        // ============================================================
        bool createDescriptorSet() {
            const VkDescriptorPoolSize pool_size = {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
            };

            const VkDescriptorPoolCreateInfo pool_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .maxSets = 1,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size,
            };

            if (vkCreateDescriptorPool(graphics::internal::context.device, &pool_info,
                nullptr, &descriptor_pool) != VK_SUCCESS) {
                std::cerr << "Failed to create descriptor pool\n";
                return false;
            }

            const VkDescriptorSetAllocateInfo alloc_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &descriptor_set_layout,
            };

            if (vkAllocateDescriptorSets(graphics::internal::context.device, &alloc_info,
                &descriptor_set) != VK_SUCCESS) {
                std::cerr << "Failed to allocate descriptor set\n";
                return false;
            }

            const VkDescriptorBufferInfo buffer_info = {
                .buffer = uniform_buffer,
                .offset = 0,
                .range = sizeof(UniformBufferObject),
            };

            const VkWriteDescriptorSet descriptor_write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptor_set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &buffer_info,
            };

            vkUpdateDescriptorSets(graphics::internal::context.device, 1, &descriptor_write, 0, nullptr);

            return true;
        }

        // ============================================================
        //  Чтение SPIR-V шейдера
        // ============================================================
        std::vector<char> readShaderFile(const std::string& filename) {
            FILE* file = nullptr;
            fopen_s(&file, filename.c_str(), "rb");
            if (!file) {
                std::cerr << "Failed to open shader file: " << filename << "\n";
                return {};
            }

            fseek(file, 0, SEEK_END);
            const long size = ftell(file);
            fseek(file, 0, SEEK_SET);

            std::vector<char> buffer(static_cast<size_t>(size));
            fread(buffer.data(), 1, static_cast<size_t>(size), file);
            fclose(file);

            return buffer;
        }

        // ============================================================
        //  Shader module
        // ============================================================
        VkShaderModule createShaderModule(const std::vector<char>& code) {
            if (code.empty()) {
                return VK_NULL_HANDLE;
            }

            const VkShaderModuleCreateInfo module_info = {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = code.size(),
                .pCode = reinterpret_cast<const uint32_t*>(code.data()),
            };

            VkShaderModule module = VK_NULL_HANDLE;
            if (vkCreateShaderModule(graphics::internal::context.device, &module_info,
                nullptr, &module) != VK_SUCCESS) {
                std::cerr << "Failed to create shader module\n";
                return VK_NULL_HANDLE;
            }

            return module;
        }

        // ============================================================
        //  Graphics pipeline
        // ============================================================
        bool createGraphicsPipeline() {
            const auto vert_code = readShaderFile("shaders/icosahedron.vert.spv");
            const auto frag_code = readShaderFile("shaders/icosahedron.frag.spv");

            if (vert_code.empty() || frag_code.empty()) {
                std::cerr << "Shader files are empty or missing\n";
                return false;
            }

            const VkShaderModule vert_module = createShaderModule(vert_code);
            const VkShaderModule frag_module = createShaderModule(frag_code);
            if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
                return false;
            }

            const VkPipelineShaderStageCreateInfo shader_stages[] = {
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vert_module,
                    .pName = "main",
                },
                {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = frag_module,
                    .pName = "main",
                },
            };

            const VkVertexInputBindingDescription binding_desc = {
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            };

            const VkVertexInputAttributeDescription attribute_descs[] = {
                {
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, position),
                },
                {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex, color),
                },
            };

            const VkPipelineVertexInputStateCreateInfo vertex_input = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &binding_desc,
                .vertexAttributeDescriptionCount = 2,
                .pVertexAttributeDescriptions = attribute_descs,
            };

            const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .primitiveRestartEnable = VK_FALSE,
            };

            const VkPipelineViewportStateCreateInfo viewport_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount = 1,
            };

            const VkPipelineRasterizationStateCreateInfo rasterizer = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable = VK_FALSE,
                .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .depthBiasEnable = VK_FALSE,
                .lineWidth = 1.0f,
            };

            const VkPipelineMultisampleStateCreateInfo multisampling = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                .sampleShadingEnable = VK_FALSE,
            };

            const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                .depthTestEnable = VK_TRUE,
                .depthWriteEnable = VK_TRUE,
                .depthCompareOp = VK_COMPARE_OP_LESS,
                .depthBoundsTestEnable = VK_FALSE,
                .stencilTestEnable = VK_FALSE,
            };

            const VkPipelineColorBlendAttachmentState color_blend_attachment = {
                .blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };

            const VkPipelineColorBlendStateCreateInfo color_blending = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .logicOpEnable = VK_FALSE,
                .attachmentCount = 1,
                .pAttachments = &color_blend_attachment,
            };

            const VkDynamicState dynamic_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR,
            };

            const VkPipelineDynamicStateCreateInfo dynamic_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates = dynamic_states,
            };

            const VkPipelineLayoutCreateInfo layout_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &descriptor_set_layout,
            };

            if (vkCreatePipelineLayout(graphics::internal::context.device, &layout_info,
                nullptr, &pipeline_layout) != VK_SUCCESS) {
                std::cerr << "Failed to create pipeline layout\n";
                vkDestroyShaderModule(graphics::internal::context.device, vert_module, nullptr);
                vkDestroyShaderModule(graphics::internal::context.device, frag_module, nullptr);
                return false;
            }

            const VkGraphicsPipelineCreateInfo pipeline_info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 2,
                .pStages = shader_stages,
                .pVertexInputState = &vertex_input,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depth_stencil,
                .pColorBlendState = &color_blending,
                .pDynamicState = &dynamic_state,
                .layout = pipeline_layout,
                .renderPass = graphics::internal::context.render_pass,
                .subpass = 0,
            };

            if (vkCreateGraphicsPipelines(graphics::internal::context.device, VK_NULL_HANDLE,
                1, &pipeline_info, nullptr, &graphics_pipeline) != VK_SUCCESS) {
                std::cerr << "Failed to create graphics pipeline\n";
                vkDestroyShaderModule(graphics::internal::context.device, vert_module, nullptr);
                vkDestroyShaderModule(graphics::internal::context.device, frag_module, nullptr);
                return false;
            }

            vkDestroyShaderModule(graphics::internal::context.device, vert_module, nullptr);
            vkDestroyShaderModule(graphics::internal::context.device, frag_module, nullptr);

            return true;
        }

        // ============================================================
        //  Обновление uniform buffer
        // ============================================================
        void updateUniformBuffer() {
            auto& ctx = graphics::internal::context;

            const float aspect = static_cast<float>(ctx.swapchain_extent.width) /
                static_cast<float>(ctx.swapchain_extent.height);

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(controls.position[0],
                controls.position[1],
                controls.position[2]));
            model = glm::scale(model, glm::vec3(controls.scale));
            model = glm::rotate(model, glm::radians(controls.rotation_x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(controls.rotation_y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(controls.rotation_z), glm::vec3(0.0f, 0.0f, 1.0f));

            const glm::mat4 view = glm::lookAt(
                glm::vec3(0.0f, 0.0f, 3.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            glm::mat4 proj;
            if (controls.use_perspective) {
                proj = glm::perspective(glm::radians(controls.fov_degrees), aspect, 0.1f, 100.0f);
            }
            else {
                // Ортографическая проекция: область [-3, 3] по X и Y с учётом aspect
                const float ortho_size = 3.0f;
                proj = glm::ortho(-ortho_size * aspect, ortho_size * aspect,
                    -ortho_size, ortho_size,
                    -100.0f, 100.0f);
            }

            proj[1][1] *= -1.0f;

            UniformBufferObject ubo = {};
            ubo.model = model;
            ubo.view = view;
            ubo.proj = proj;
            ubo.color_multiplier = glm::vec4(controls.color[0], controls.color[1], controls.color[2], 1.0f);

            std::memcpy(uniform_buffer_mapped, &ubo, sizeof(ubo));
        }

    } // namespace

    // ============================================================
    //  Публичные функции
    // ============================================================

    bool initialize() {
        if (!createVertexBuffer()) {
            std::cerr << "Failed to create vertex buffer\n";
            return false;
        }

        if (!createIndexBuffer()) {
            std::cerr << "Failed to create index buffer\n";
            return false;
        }

        if (!createUniformBuffer()) {
            std::cerr << "Failed to create uniform buffer\n";
            return false;
        }

        if (!createDescriptorSetLayout()) {
            std::cerr << "Failed to create descriptor set layout\n";
            return false;
        }

        if (!createDescriptorSet()) {
            std::cerr << "Failed to create descriptor set\n";
            return false;
        }

        if (!createGraphicsPipeline()) {
            std::cerr << "Failed to create graphics pipeline\n";
            return false;
        }

        return true;
    }

    void shutdown() {
        auto& ctx = graphics::internal::context;

        vkQueueWaitIdle(ctx.graphics_queue);

        if (graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx.device, graphics_pipeline, nullptr);
        }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
        }

        if (descriptor_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(ctx.device, descriptor_set_layout, nullptr);
        }
        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(ctx.device, descriptor_pool, nullptr);
        }

        if (uniform_buffer_mapped != nullptr) {
            vmaUnmapMemory(ctx.allocator, uniform_buffer_allocation);
            uniform_buffer_mapped = nullptr;
        }

        if (uniform_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(ctx.allocator, uniform_buffer, uniform_buffer_allocation);
        }
        if (index_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(ctx.allocator, index_buffer, index_buffer_allocation);
        }
        if (vertex_buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(ctx.allocator, vertex_buffer, vertex_buffer_allocation);
        }
    }

    void update(double time) {
        static double last_time = time;
        const float delta = static_cast<float>(time - last_time);
        last_time = time;

        static float trajectory_angle = 0.0f;

        if (controls.playing) {
            controls.rotation_y += controls.animation_speed * delta;
            if (controls.rotation_y > 180.0f)  controls.rotation_y -= 360.0f;
            if (controls.rotation_y < -180.0f) controls.rotation_y += 360.0f;

            if (controls.trajectory_radius > 0.0f) {
                trajectory_angle += controls.animation_speed * delta * 0.5f;
                controls.position[0] = controls.trajectory_radius * std::cos(glm::radians(trajectory_angle));
                controls.position[2] = controls.trajectory_radius * std::sin(glm::radians(trajectory_angle));
            }
        }

        ImGui::Begin("Icosahedron Controls");

        ImGui::Text("Animation");
        ImGui::Checkbox("Playing", &controls.playing);
        ImGui::SliderFloat("Speed (deg/sec)", &controls.animation_speed, 0.0f, 180.0f);
        ImGui::SliderFloat("Trajectory radius", &controls.trajectory_radius, 0.0f, 3.0f);

        ImGui::Separator();

        ImGui::Text("Position");
        ImGui::SliderFloat3("Position XYZ", controls.position, -3.0f, 3.0f);

        ImGui::Separator();

        ImGui::Text("Rotation (degrees)");
        ImGui::SliderFloat("Rotation X", &controls.rotation_x, -180.0f, 180.0f);
        ImGui::SliderFloat("Rotation Y", &controls.rotation_y, -180.0f, 180.0f);
        ImGui::SliderFloat("Rotation Z", &controls.rotation_z, -180.0f, 180.0f);

        ImGui::Separator();

        ImGui::Text("Scale");
        ImGui::SliderFloat("Scale", &controls.scale, 0.1f, 3.0f);

        ImGui::Separator();

        ImGui::Text("Color");
        ImGui::ColorEdit3("Color multiplier", controls.color);

        ImGui::Separator();

        ImGui::Text("Projection");
        ImGui::Checkbox("Perspective (uncheck = Orthographic)", &controls.use_perspective);

        if (controls.use_perspective) {
            ImGui::SliderFloat("FOV", &controls.fov_degrees, 30.0f, 120.0f);
        }

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
            {.color = {.float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
            {.depthStencil = {.depth = 1.0f, .stencil = 0 } },
        };

        const VkRenderPassBeginInfo render_pass_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = ctx.render_pass,
            .framebuffer = fd.framebuffer,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = ctx.swapchain_extent,
            },
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };

        vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

        const VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(ctx.swapchain_extent.width),
            .height = static_cast<float>(ctx.swapchain_extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

        const VkRect2D scissor = {
            .offset = { 0, 0 },
            .extent = ctx.swapchain_extent,
        };
        vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

        const VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, offsets);

        vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

        vkCmdDrawIndexed(fd.command_buffer,
            static_cast<uint32_t>(icosahedron_indices.size()),
            1, 0, 0, 0);

        vkCmdEndRenderPass(fd.command_buffer);
        vkEndCommandBuffer(fd.command_buffer);
    }

} // namespace application