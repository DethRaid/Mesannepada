#include "pipeline_builder.hpp"

#include <algorithm>

#include <imgui.h>
#include <spirv_reflect.h>
#include <EASTL/span.h>
#include <RmlUi/Core/Vertex.h>
#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include "pipeline_cache.hpp"
#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "resources/resource_path.hpp"
#include "shared/vertex_data.hpp"

namespace render {
    static std::shared_ptr<spdlog::logger> logger;

    static eastl::string POSITION_VERTEX_ATTRIBUTE_NAME = "position_in";
    static eastl::string TEXCOORD_VERTEX_ATTRIBUTE_NAME = "texcoord_in";
    static eastl::string NORMAL_VERTEX_ATTRIBUTE_NAME = "normal_in";
    static eastl::string TANGENT_VERTEX_ATTRIBUTE_NAME = "tangent_in";
    static eastl::string COLOR_VERTEX_ATTRIBUTE_NAME = "color_in";
    static eastl::string PRIMITIVE_ID_VERTEX_ATTRIBUTE_NAME = "primitive_id_in";

    static auto standard_vertex_layout = VertexLayout{
        .input_bindings = {
            {
                .binding = 0,
                .stride = sizeof(uint32_t),
                .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE

            }
        },
        .attributes = {
            {
                PRIMITIVE_ID_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R32_UINT,
                    .offset = 0,
                }
            },
        }
    };

    static auto rmlui_vertex_layout = VertexLayout{
        .input_bindings = {
            {
                .binding = 0,
                .stride = sizeof(Rml::Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            }
        },
        .attributes = {
            {
                POSITION_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(Rml::Vertex, position)
                }
            },
            {
                COLOR_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .offset = offsetof(Rml::Vertex, colour)
                }
            },
            {
                TEXCOORD_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(Rml::Vertex, tex_coord)
                }
            },
        },
    };

    static auto imgui_vertex_layout = VertexLayout{
        .input_bindings = {
            {
                .binding = 0,
                .stride = sizeof(ImDrawVert),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            }
        },
        .attributes = {
            {
                POSITION_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(ImDrawVert, pos)
                }
            },
            {
                TEXCOORD_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(ImDrawVert, uv)
                }
            },
            {
                COLOR_VERTEX_ATTRIBUTE_NAME, {
                    .binding = 0,
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .offset = offsetof(ImDrawVert, col)
                }
            },
        },
    };

    static VkDescriptorType to_vk_type(SpvReflectDescriptorType type);

    /**
     * Collects the descriptor sets from the provided list of descriptor sets. Performs basic validation that the sets
     * match the sets that have already been collected
     *
     * @param sets The descriptor sets to collect
     * @param shader_stage The shader stage these descriptor sets came from
     * @param descriptor_sets Information about the descriptor sets that the shader uses. We make an effort to add bindings
     * to existing sets when possible
     * @return True if there was an error, false if everything's fine
     */
    static bool collect_descriptor_sets(
        const eastl::vector<SpvReflectDescriptorSet*>& sets,
        VkShaderStageFlags shader_stage,
        eastl::fixed_vector<DescriptorSetInfo, 8>& descriptor_sets
        );

    static bool collect_push_constants(
        std::string_view shader_name,
        const eastl::vector<SpvReflectBlockVariable*>& spv_push_constants,
        VkShaderStageFlags shader_stage,
        eastl::fixed_vector<VkPushConstantRange, 4>& push_constants
        );

    static void collect_vertex_attributes(
        const VertexLayout& vertex_layout,
        const eastl::vector<SpvReflectInterfaceVariable*>& inputs,
        eastl::fixed_vector<VkVertexInputAttributeDescription, 8>& vertex_attributes,
        bool& needs_position_buffer,
        bool& needs_data_buffer,
        bool& needs_primitive_id_buffer
        );

    static void init_logger() {
        logger = SystemInterface::get().get_logger("PipelineBuilder");
        logger->set_level(spdlog::level::debug);
    }

    GraphicsPipelineBuilder::GraphicsPipelineBuilder(PipelineCache& cache_in) :
        cache{cache_in} {
        if(logger == nullptr) {
            init_logger();
        }

        use_standard_vertex_layout();
        set_depth_state({});
        set_raster_state({});
        set_blend_state(
            0,
            {
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                  VK_COLOR_COMPONENT_A_BIT
            }
            );
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_name(const std::string_view name_in) {
        name = name_in;

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_vertex_layout(VertexLayout& layout) {
        vertex_layout = &layout;

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::use_standard_vertex_layout() {
        return set_vertex_layout(standard_vertex_layout);
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::use_rmlui_vertex_layout() {
        return set_vertex_layout(rmlui_vertex_layout);
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::use_imgui_vertex_layout() {
        return set_vertex_layout(imgui_vertex_layout);
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_topology(const VkPrimitiveTopology topology_in) {
        topology = topology_in;

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_vertex_shader(const ResourcePath& vertex_path) {
        if(vertex_shader) {
            throw std::runtime_error{"Vertex shader already loaded set"};
        }
        const auto vertex_shader_maybe = SystemInterface::get().load_file(vertex_path);

        if(!vertex_shader_maybe) {
            throw std::runtime_error{"Could not load vertex shader"};
        }

        vertex_shader = *vertex_shader_maybe;
        vertex_shader_name = vertex_path.to_string().c_str();

        const auto shader_module = spv_reflect::ShaderModule{
            vertex_shader->size(),
            vertex_shader->data(),
            SPV_REFLECT_MODULE_FLAG_NO_COPY
        };
        if(shader_module.GetResult() != SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error{"Could not perform reflection on vertex shader"};
        }

        logger->trace("Beginning reflection on vertex shader {}", vertex_shader_name);

        collect_bindings(
            *vertex_shader_maybe,
            vertex_shader_name,
            VK_SHADER_STAGE_VERTEX_BIT,
            descriptor_sets,
            push_constants);

        // Collect inputs
        uint32_t input_count;
        auto result = shader_module.EnumerateInputVariables(&input_count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);
        auto spv_vertex_inputs = eastl::vector<SpvReflectInterfaceVariable*>{input_count};
        result = shader_module.EnumerateInputVariables(&input_count, spv_vertex_inputs.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        collect_vertex_attributes(
            *vertex_layout,
            spv_vertex_inputs,
            vertex_attributes,
            need_position_buffer,
            need_data_buffer,
            need_primitive_id_buffer
            );

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_geometry_shader(const ResourcePath& geometry_path) {
        if(geometry_shader) {
            throw std::runtime_error{"Geometry shader already set!"};
        }

        const auto geometry_shader_maybe = SystemInterface::get().load_file(geometry_path);
        if(!geometry_shader_maybe) {
            throw std::runtime_error{"Could not load geometry shader"};
        }

        geometry_shader = *geometry_shader_maybe;
        geometry_shader_name = geometry_path.to_string().c_str();

        const auto shader_module = spv_reflect::ShaderModule{
            geometry_shader->size(),
            geometry_shader->data(),
            SPV_REFLECT_MODULE_FLAG_NO_COPY
        };
        if(shader_module.GetResult() != SpvReflectResult::SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error{"Could not perform reflection on geometry shader"};
        }

        logger->trace("Beginning reflection on geometry shader {}", geometry_shader_name);

        collect_bindings(
            *geometry_shader,
            geometry_shader_name,
            VK_SHADER_STAGE_GEOMETRY_BIT,
            descriptor_sets,
            push_constants);

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_fragment_shader(const ResourcePath& fragment_path) {
        if(fragment_shader) {
            throw std::runtime_error{"Fragment shader already set"};
        }

        const auto fragment_shader_maybe = SystemInterface::get().load_file(fragment_path);

        if(!fragment_shader_maybe) {
            throw std::runtime_error{"Could not load fragment shader"};
        }

        fragment_shader = *fragment_shader_maybe;
        fragment_shader_name = fragment_path.to_string().c_str();

        logger->trace("Beginning reflection on fragment shader {}", fragment_shader_name);

        collect_bindings(
            *fragment_shader,
            fragment_shader_name,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            descriptor_sets,
            push_constants
            );

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_depth_state(const DepthStencilState& depth_stencil) {
        depth_stencil_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            // TODO: We might need flags
            .depthTestEnable = depth_stencil.enable_depth_test ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = depth_stencil.enable_depth_write ? VK_TRUE : VK_FALSE,
            .depthCompareOp = depth_stencil.compare_op,
            .depthBoundsTestEnable = depth_stencil.enable_depth_bounds_test ? VK_TRUE : VK_FALSE,
            .stencilTestEnable = depth_stencil.enable_stencil_test ? VK_TRUE : VK_FALSE,
            .front = depth_stencil.front_face_stencil_state,
            .back = depth_stencil.back_face_stencil_state,
            .minDepthBounds = depth_stencil.min_depth_bounds,
            .maxDepthBounds = depth_stencil.max_depth_bounds,
        };

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_raster_state(const RasterState& raster_state_in) {
        raster_state = VkPipelineRasterizationStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = raster_state_in.depth_clamp_enable,
            .polygonMode = raster_state_in.polygon_mode,
            // Cull mode and front face are dynamic
            .lineWidth = raster_state_in.line_width,
        };

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_blend_flag(VkPipelineColorBlendStateCreateFlagBits flag) {
        blend_flags |= flag;

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_blend_mode(const BlendMode mode) {
        for(auto& blend_state : blends) {
            switch(mode) {
            case BlendMode::NoBlending:
                blend_state = {
                    .blendEnable = VK_FALSE,
                    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT,
                };
                break;

            case BlendMode::Additive:
                blend_state = {
                    .blendEnable = VK_TRUE,
                    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT,
                };
                break;

            case BlendMode::Mix:
                blend_state = {
                    .blendEnable = VK_TRUE,
                    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT,
                };
                break;

            defualt:
                throw std::runtime_error{"Unsupported blend mode!"};

            }
        }

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_blend_state(
        const uint32_t color_target_index, const VkPipelineColorBlendAttachmentState& blend
        ) {
        if(blends.size() <= color_target_index) {
            blends.resize(color_target_index + 1);
        }

        blends[color_target_index] = blend;

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_dynamic_state(const VkDynamicState state) {
        dynamic_states.emplace_back(state);

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_num_attachments(const uint32_t num_attachments) {
        for(auto attachment = 0u; attachment < num_attachments; attachment++) {
            set_blend_state(
                attachment,
                {
                    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT
                }
                );
        }

        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::enable_dgc() {
        should_enable_dgc = true;

        return *this;
    }

    GraphicsPipelineHandle GraphicsPipelineBuilder::build() {
        vertex_inputs.clear();
        vertex_inputs.reserve(2);

        if(vertex_layout == nullptr) {
            throw std::runtime_error{"Vertex layout is required!"};
        }

        // If we have one vertex input, all attributes pull from it
        vertex_inputs.insert(vertex_inputs.begin(),
                             vertex_layout->input_bindings.begin(),
                             vertex_layout->input_bindings.end());

        return cache.create_pipeline(*this);
    }

    bool collect_descriptor_sets(
        const eastl::vector<SpvReflectDescriptorSet*>& sets,
        const VkShaderStageFlags shader_stage,
        eastl::fixed_vector<DescriptorSetInfo, 8>& descriptor_sets
        ) {
        if(logger == nullptr) {
            init_logger();
        }

        const auto texture_array_size = static_cast<uint32_t>(*CVarSystem::Get()->
            GetIntCVar("r.RHI.SampledImageCount"));

        bool has_error = false;
        for(const auto* set : sets) {
            auto num_bindings = 0u;
            if(descriptor_sets.size() <= set->set) {
                descriptor_sets.resize(set->set + 1);
            }
            auto& set_info = descriptor_sets[set->set];

            for(auto* binding : eastl::span{set->bindings, set->bindings + set->binding_count}) {
                logger->trace(
                    "Adding new descriptor {}.{} of type {} with count {} for shader stage {}",
                    set->set,
                    binding->binding,
                    string_VkDescriptorType(to_vk_type(binding->descriptor_type)),
                    binding->count,
                    string_VkShaderStageFlags(shader_stage)
                    );
                if(set_info.bindings.size() <= binding->binding) {
                    set_info.bindings.resize(binding->binding + 1);
                }
                const auto old_stage_flags = set_info.bindings[binding->binding].stageFlags;
                set_info.bindings[binding->binding] =
                    DescriptorInfo{
                        {
                            .binding = binding->binding,
                            .descriptorType = to_vk_type(binding->descriptor_type),
                            .descriptorCount = binding->count > 0 ? binding->count : texture_array_size,
                            .stageFlags = static_cast<VkShaderStageFlags>(shader_stage) | old_stage_flags,
                            .pImmutableSamplers = nullptr
                        },
                        (binding->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0
                    };
                num_bindings = std::max(num_bindings, binding->binding + 1);

                if(binding->count == 0) {
                    set_info.has_variable_count_binding = true;
                }
            }

            auto binding_index = 0;
            for(auto& binding : set_info.bindings) {
                binding.binding = binding_index;
                binding_index++;
            }
        }

        return has_error;
    }

    bool collect_push_constants(
        const std::string_view shader_name,
        const eastl::vector<SpvReflectBlockVariable*>& spv_push_constants,
        const VkShaderStageFlags shader_stage,
        eastl::fixed_vector<VkPushConstantRange, 4>& push_constants
        ) {
        bool has_error = false;

        for(const auto& constant_range : spv_push_constants) {
            auto existing_constant = std::find_if(
                push_constants.begin(),
                push_constants.end(),
                [&](const VkPushConstantRange& existing_range) {
                    return existing_range.offset == constant_range->offset;
                }
                );
            if(existing_constant != push_constants.end()) {
                if(existing_constant->size != constant_range->size) {
                    logger->error(
                        "Push constant range at offset {} has size {} in shader {}, but it had size {} earlier",
                        constant_range->offset,
                        constant_range->size,
                        std::string_view{shader_name.data(), shader_name.size()},
                        existing_constant->size
                        );
                    has_error = true;

                    // Expand the size - is this correct?
                    existing_constant->size = std::max(existing_constant->size, constant_range->size);
                }

                // Make the range visible to this stage
                existing_constant->stageFlags |= shader_stage;
            } else {
                // New range!
                push_constants.emplace_back(
                    VkPushConstantRange{
                        .stageFlags = static_cast<VkShaderStageFlags>(shader_stage),
                        .offset = constant_range->offset,
                        .size = constant_range->size
                    }
                    );
            }
        }

        return has_error;
    }

    bool collect_bindings(
        eastl::span<const std::byte> shader_instructions, const std::string_view shader_name,
        const VkShaderStageFlags shader_stage,
        eastl::fixed_vector<DescriptorSetInfo, 8>& descriptor_sets,
        eastl::fixed_vector<VkPushConstantRange, 4>& push_constants
        ) {
        if(logger == nullptr) {
            init_logger();
        }

        const auto shader_module = spv_reflect::ShaderModule{
            shader_instructions.size(),
            shader_instructions.data(),
            SPV_REFLECT_MODULE_FLAG_NO_COPY
        };
        if(shader_module.GetResult() != SpvReflectResult::SPV_REFLECT_RESULT_SUCCESS) {
            throw std::runtime_error{"Could not perform reflection on shader"};
        }

        bool has_error = false;

        // Collect descriptor set info
        uint32_t set_count;
        auto result = shader_module.EnumerateDescriptorSets(&set_count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);
        auto sets = eastl::vector<SpvReflectDescriptorSet*>{set_count};
        result = shader_module.EnumerateDescriptorSets(&set_count, sets.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        has_error |= collect_descriptor_sets(sets, shader_stage, descriptor_sets);

        // Collect push constant info
        uint32_t constant_count;
        result = shader_module.EnumeratePushConstantBlocks(&constant_count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);
        auto spv_push_constants = eastl::vector<SpvReflectBlockVariable*>{constant_count};
        result = shader_module.EnumeratePushConstantBlocks(&constant_count, spv_push_constants.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        has_error |= collect_push_constants(
            shader_name,
            spv_push_constants,
            shader_stage,
            push_constants
            );

        return has_error;
    }

    void collect_vertex_attributes(
        const VertexLayout& vertex_layout,
        const eastl::vector<SpvReflectInterfaceVariable*>& inputs,
        eastl::fixed_vector<VkVertexInputAttributeDescription, 8>& vertex_attributes,
        bool& needs_position_buffer,
        bool& needs_data_buffer,
        bool& needs_primitive_id_buffer
        ) {
        needs_position_buffer = false;
        needs_data_buffer = false;
        for(const auto* input : inputs) {
            if(input->name == nullptr) {
                continue; // UGH
            }
            if(auto itr = vertex_layout.attributes.find(input->name); itr != vertex_layout.attributes.end()) {
                auto& attribute = vertex_attributes.emplace_back(itr->second);
                attribute.location = input->location;
            }

            const auto string_name = std::string{input->name};

            if(string_name.find(POSITION_VERTEX_ATTRIBUTE_NAME.c_str()) != std::string::npos) {
                needs_position_buffer = true;
            } else if(string_name.find(NORMAL_VERTEX_ATTRIBUTE_NAME.c_str()) != std::string::npos) {
                needs_data_buffer = true;
            } else if(string_name.find(TANGENT_VERTEX_ATTRIBUTE_NAME.c_str()) != std::string::npos) {
                needs_data_buffer = true;
            } else if(string_name.find(TEXCOORD_VERTEX_ATTRIBUTE_NAME.c_str()) != std::string::npos) {
                needs_data_buffer = true;
            } else if(string_name.find(COLOR_VERTEX_ATTRIBUTE_NAME.c_str()) != std::string::npos) {
                needs_data_buffer = true;
            } else if(string_name.find(PRIMITIVE_ID_VERTEX_ATTRIBUTE_NAME.c_str()) != std::string::npos) {
                needs_primitive_id_buffer = true;
            } else if(input->location != 0xFFFFFFFF) {
                // -1 is used for some builtin things i guess
                // I can't
                logger->error("Vertex input {} unrecognized", input->location);
            }
        }
    }

    VkDescriptorType to_vk_type(SpvReflectDescriptorType type) {
        switch(type) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_SAMPLER;

        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

        case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        default:
            spdlog::error("Unknown descriptor type {}", static_cast<uint32_t>(type));
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }
}
