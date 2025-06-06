#pragma once

#include <string>
#include <functional>
#include <EASTL/optional.h>
#include <EASTL/vector.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "rendering_attachment_info.hpp"
#include "render/backend/texture_state.hpp"
#include "render/backend/handles.hpp"
#include "render/backend/buffer_usage_token.hpp"
#include "render/backend/texture_usage_token.hpp"
#include "render/backend/descriptor_set_builder.hpp"
#include "render/backend/compute_shader.hpp"

namespace render {
    class CommandBuffer;

    struct AttachmentBinding {
        TextureHandle texture;
        TextureState state;
        eastl::optional<glm::vec4> clear_color;
    };

    struct ComputePass {
        std::string name;

        TextureUsageList textures;

        BufferUsageList buffers;

        eastl::fixed_vector<DescriptorSet, 8> descriptor_sets;

        /**
         * Executes this render pass
         *
         * If this render pass renders to render targets, they're bound before this function is called. The viewport and
         * scissor are set to the dimensions of the render targets, no need to do that manually
         */
        std::function<void(CommandBuffer&)> execute;
    };

    /**
     * \brief Describes a pass that dispatches a compute shader in a specific way
     */
    template<typename PushConstantsType = uint32_t>
    struct ComputeDispatch {
        /**
         * \brief Name of this dispatch, for debugging
         */
        std::string name;

        /**
         * \brief Descriptor sets to bind for this pass. Must contain one entry for every descriptor set that the shader needs
         */
        eastl::fixed_vector<DescriptorSet, 4> descriptor_sets;

        /**
         * \brief Buffers this pass uses that aren't in a descriptor set. Useful for buffers accessed through BDA
         */
        BufferUsageList buffers;

        /**
         * \brief Push constants for this dispatch. Feel free to reinterpret_cast push_constants.data() into your own type
         */
        PushConstantsType push_constants;

        /**
         * \brief Number of workgroups to dispatch
         */
        glm::uvec3 num_workgroups;

        /**
         * \brief Compute shader to dispatch
         */
        ComputePipelineHandle compute_shader;
    };


    /**
     * \brief Describes a pass that dispatches a compute shader from an indirect dispatch buffer
     */
    template<typename PushConstantsType = uint32_t>
    struct IndirectComputeDispatch {
        /**
         * \brief Name of this dispatch, for debugging
         */
        std::string name;

        /**
         * \brief Descriptor sets to bind for this pass. Must contain one entry for every descriptor set that the shader needs
         */
        eastl::fixed_vector<DescriptorSet, 4> descriptor_sets;

        /**
         * \brief Buffers this pass uses that aren't in a descriptor set. Useful for buffers accessed through BDA
         */
        BufferUsageList buffers;

        /**
         * \brief Push constants for this dispatch. Feel free to reinterpret_cast push_constants.data() into your own type
         */
        PushConstantsType push_constants;

        /**
         * \brief Number of workgroups to dispatch
         */
        BufferHandle dispatch;

        /**
         * \brief Compute shader to dispatch
         */
        ComputePipelineHandle compute_shader;
    };

    struct TransitionPass {
        TextureUsageList textures;

        BufferUsageList buffers;
    };

    struct BufferCopyPass {
        std::string name;

        BufferHandle dst;

        BufferHandle src;
    };


    struct ImageCopyPass {
        std::string name;

        TextureHandle dst;

        TextureHandle src;
    };

    struct Subpass {
        std::string name;

        /**
         * Indices of any input attachments. These indices refer to the render targets in the parent render pass
         */
        eastl::vector<uint32_t> input_attachments = {};

        /**
         * Indices of any output attachments. These indices refer to the render targets in the parent render pass
         */
        eastl::vector<uint32_t> color_attachments = {};

        /**
         * Index of the depth attachment. This index refers to the render targets in the parent render pass
         */
        eastl::optional<uint32_t> depth_attachment = eastl::nullopt;

        std::function<void(CommandBuffer&)> execute;
    };

    struct RenderPassBeginInfo {
        std::string name;

        TextureUsageList textures;

        BufferUsageList buffers;

        /**
         * \brief Descriptor sets that contain sync info we use
         *
         * I need a better name for this
         */
        eastl::fixed_vector<DescriptorSet, 4> descriptor_sets;

        eastl::fixed_vector<TextureHandle, 8> attachments;

        eastl::fixed_vector<VkClearValue, 8> clear_values;

        eastl::optional<uint32_t> view_mask;
    };

    struct AttachmentInfo {
        TextureHandle texture;
        VkClearValue clear_value;
    };

    struct DynamicRenderingPass {
        std::string name;

        TextureUsageList textures;

        BufferUsageList buffers;

        eastl::fixed_vector<DescriptorSet, 4> descriptor_sets;

        eastl::fixed_vector<RenderingAttachmentInfo, 8> color_attachments;

        eastl::optional<RenderingAttachmentInfo> depth_attachment;

        eastl::optional<TextureHandle> shading_rate_image;

        eastl::optional<uint32_t> view_mask;

        std::function<void(CommandBuffer&)> execute;
    };

    struct PresentPass {
        TextureHandle swapchain_image;
    };
}
