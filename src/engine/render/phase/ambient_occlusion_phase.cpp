#include "ambient_occlusion_phase.hpp"


#include "console/cvars.hpp"
#include "core/string_conversion.hpp"
#include "render/noise_texture.hpp"
#include "render/render_scene.hpp"
#include "render/scene_view.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/render_backend.hpp"

namespace render {
    static AutoCVar_Enum cvar_ao_technique{
        "r.AO", "What kind of AO to use", AoTechnique::RTAO
    };

    static AutoCVar_Int cvar_rtao_samples{
        "r.AO.RTAO.SamplesPerPixel", "Number of RTAO samples per pixel", 1
    };

    static AutoCVar_Float cvar_ao_radius{
        "r.AO.MaxRayDistance", "Maximum ray distance for RTAO", 1.2f
    };


    AmbientOcclusionPhase::AmbientOcclusionPhase() {
        uint32_t val = 0;
        const auto count = static_cast<uint8_t>(std::popcount(val));

    }

    AmbientOcclusionPhase::~AmbientOcclusionPhase() {

    }

    void AmbientOcclusionPhase::generate_ao(
        RenderGraph& graph, const SceneView& view, const RenderScene& scene, const NoiseTexture& noise,
        const TextureHandle gbuffer_normals, const TextureHandle gbuffer_depth, const TextureHandle ao_out
    ) {
        ZoneScoped;

        frame_index++;

        switch (cvar_ao_technique.get()) {
        case AoTechnique::Off:
            graph.add_render_pass(
                {
                    .name = "Clear AO",
                    .color_attachments = {
                        RenderingAttachmentInfo{
                            .image = ao_out,
                            .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                            .clear_value = {.color = {.float32 = {1, 1, 1, 1}}}
                        }
                    },
                    .execute = [](CommandBuffer&) {}
                });
            break;

        case AoTechnique::RTAO:
            evaluate_rtao(graph, view, scene, noise, gbuffer_depth, gbuffer_normals, ao_out);
            break;
        }
    }

    void AmbientOcclusionPhase::evaluate_rtao(
        RenderGraph& graph, const SceneView& view, const RenderScene& scene, const NoiseTexture& noise,
        const TextureHandle gbuffer_depth, const TextureHandle gbuffer_normals, const TextureHandle ao_out
    ) {
        auto& backend = RenderBackend::get();
        if (rtao_pipeline == nullptr) {
            rtao_pipeline = backend.get_pipeline_cache().create_pipeline("shaders/ao/rtao.comp.spv");
        }

        const auto set = backend.get_transient_descriptor_allocator().build_set(rtao_pipeline, 0)
            .bind(view.get_buffer())
            .bind(scene.get_raytracing_scene().get_acceleration_structure())
            .bind(gbuffer_depth)
            .bind(gbuffer_normals)
            .bind(noise.layers[frame_index % noise.num_layers])
            .bind(ao_out)
            .build();

        struct Constants {
            uint samples_per_pixel;
            float max_ray_distance;
            glm::u16vec2 output_resolution;
            glm::u16vec2 noise_tex_resolution;
        };

        const auto resolution = glm::uvec2{ ao_out->create_info.extent.width, ao_out->create_info.extent.height };

        graph.add_compute_dispatch(
            ComputeDispatch<Constants>{
            .name = "Ray traced ambient occlusion",
                .descriptor_sets = { set },
                .push_constants = Constants{
                    .samples_per_pixel = static_cast<uint32_t>(cvar_rtao_samples.get()),
                    .max_ray_distance = static_cast<float>(cvar_ao_radius.get()),
                    .output_resolution = {resolution},
                    .noise_tex_resolution = {noise.resolution}
            },
                .num_workgroups = glm::uvec3{ (resolution + glm::uvec2{7}) / glm::uvec2{8}, 1 },
                .compute_shader = rtao_pipeline
        });
    }
}
