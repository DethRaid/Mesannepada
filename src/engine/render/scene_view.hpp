#pragma once

#include <glm/glm.hpp>

#include "backend/descriptor_set_builder.hpp"
#include "render/indirect_drawing_utils.hpp"
#include "render/backend/handles.hpp"
#include "shared/view_data.hpp"

namespace render {
    class ResourceUploadQueue;
    class ResourceAllocator;
    class RenderBackend;

    /**
     * A class that can view a scene. Contains various camera and rendering parameters
     */
    class SceneView {
    public:
        /**
         * \brief uint list of visible primitives
         *
         * 1:1 correspondence with a scene's list of primitives. 1 = visible, 0 = not visible
         */
        BufferHandle visible_objects = nullptr;

        IndirectDrawingBuffers solid_buffers = {};

        IndirectDrawingBuffers cutout_buffers = {};

        explicit SceneView();

        ~SceneView();

        void set_render_resolution(glm::uvec2 render_resolution);

        void set_perspective_projection(float fov_in, float aspect_in, float near_value_in);

        BufferHandle get_constant_buffer() const;

        void set_view_matrix(const float4x4& view_matrix);

        void update_buffer(ResourceUploadQueue& upload_queue);

        void set_aspect_ratio(float aspect_in);

        void set_mip_bias(float mip_bias);

        /**
         * Gets the near clip plane
         */
        float get_near() const;

        /**
         * Gets the field of view, in degrees
         */
        float get_fov() const;

        float get_aspect_ratio() const;

        const ViewDataGPU& get_gpu_data() const;

        glm::vec3 get_position() const;

        glm::vec3 get_forward() const;

        void set_jitter(glm::vec2 jitter_in);

        glm::vec2 get_jitter() const;

        const glm::mat4& get_projection() const;

        const glm::mat4& get_last_frame_projection() const;

        void increment_frame_count();

        /**
         * Ensures that the primitive visibility buffer has enough space
         */
        void init_visible_objects_buffer(uint32_t num_primitives);

        uint32_t get_frame_count() const;

        uint2 get_render_resolution() const;

        const float4x4& get_view() const;

        const float4x4& get_last_frame_view() const;

        float get_min_log_luminance() const;

        float get_max_log_luminance() const;

    private:
        float aperture_size = 16;

        float shutter_speed = 0.016f;

        float iso_sensitivity = 100;

        float fov = { 75.f };

        float aspect = 16.f / 9.f;

        float near_value = 0.05f;

        /**
         * The projection matrices encased within contain jitter
         */
        ViewDataGPU gpu_data = {};

        /**
         * Projection matrix with no jitter
         */
        glm::mat4 projection = {};

        /**
         * Previous projection matrix with no jitter
         */
        glm::mat4 last_frame_projection = {};

        BufferHandle constant_buffer = {};

        bool is_dirty = true;

        glm::vec2 jitter = {};

        uint32_t frame_count = 0;

        void refresh_projection_matrices();

        void refresh_exposure_settings();
    };
}
