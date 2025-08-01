#include "procedural_sky.hpp"

#include <tracy/Tracy.hpp>

#include "backend/pipeline_cache.hpp"
#include "backend/render_backend.hpp"
#include "core/system_interface.hpp"
#include "render/backend/render_graph.hpp"

namespace render {
    ProceduralSky::ProceduralSky() {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        transmittance_lut = allocator.create_texture(
            "Transmittance LUT",
            {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                glm::uvec2{256, 64},
                1,
                TextureUsage::StorageImage
            }
        );

        multiscattering_lut = allocator.create_texture(
            "Multiscattering LUT",
            {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                glm::uvec2{32, 32},
                1,
                TextureUsage::StorageImage
            }
        );

        sky_view_lut = allocator.create_texture(
            "Sky view LUT",
            {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                glm::uvec2{200, 200},
                1,
                TextureUsage::StorageImage
            }
        );

        auto& pipelines = backend.get_pipeline_cache();

        transmittance_lut_pso = pipelines.create_pipeline("shader://sky/transmittance_lut.comp.spv"_res);

        multiscattering_lut_pso = pipelines.create_pipeline("shader://sky/multiscattering_lut.comp.spv"_res);

        sky_view_lut_pso = pipelines.create_pipeline("shader://sky/sky_view_lut.comp.spv"_res);

        sky_application_pso = backend.begin_building_pipeline("Hillaire Sky")
            .set_vertex_shader("shader://common/fullscreen.vert.spv"_res)
            .set_fragment_shader("shader://sky/sky_unified.frag.spv"_res)
            .set_depth_state(
                {
                    .enable_depth_write = false,
                    .compare_op = VK_COMPARE_OP_EQUAL
                })
            .build();

        linear_sampler = allocator.get_sampler(
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .maxLod = VK_LOD_CLAMP_NONE,
            });

        const auto occlusion_miss_shader = *SystemInterface::get().load_file("shader://sky/sky_unified_occlusion.miss.spv"_res);
        const auto gi_miss_shader = *SystemInterface::get().load_file("shader://sky/sky_unified_gi.miss.spv"_res);
        backend.get_pipeline_cache().add_miss_shaders(occlusion_miss_shader, gi_miss_shader);
    }

    void ProceduralSky::update_sky_luts(RenderGraph& graph, const glm::vec3& light_vector) const {
        auto& backend = RenderBackend::get();
        auto& descriptors = backend.get_transient_descriptor_allocator();

        graph.begin_label("Update sky LUTs");

        {
            const auto set = descriptors.build_set(transmittance_lut_pso, 0)
                .bind(transmittance_lut)
                .build();

            graph.add_compute_dispatch(
                ComputeDispatch<uint32_t>{
                .name = "Generate transmittance LUT",
                    .descriptor_sets = { set },
                    .num_workgroups = glm::uvec3{ 256 / 8, 64 / 8, 1 },
                    .compute_shader = transmittance_lut_pso
            });
        }

        {
            const auto set = descriptors.build_set(multiscattering_lut_pso, 0)
                .bind(transmittance_lut, linear_sampler)
                .bind(multiscattering_lut)
                .build();

            graph.add_compute_dispatch(
                ComputeDispatch<uint32_t>{
                .name = "Generate multiscattering LUT",
                    .descriptor_sets = { set },
                    .num_workgroups = glm::uvec3{ 32 / 8, 32 / 8, 1 },
                    .compute_shader = multiscattering_lut_pso
            });
        }

        {
            const auto set = descriptors.build_set(sky_view_lut_pso, 0)
                .bind(transmittance_lut, linear_sampler)
                .bind(multiscattering_lut, linear_sampler)
                .bind(sky_view_lut)
                .build();

            graph.add_compute_dispatch(
                ComputeDispatch<glm::vec3>{
                .name = "Compute sky view LUT",
                    .descriptor_sets = { set },
                    .push_constants = light_vector,
                    .num_workgroups = glm::uvec3{ (200 / 8) + 1, (200 / 8) + 1, 1 },
                    .compute_shader = sky_view_lut_pso
            });
        }

        graph.add_transition_pass(
            {
                .textures = {
                    {
                        .texture = transmittance_lut,
                        .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .access = VK_ACCESS_2_SHADER_READ_BIT,
                        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    },
                    {
                        .texture = multiscattering_lut,
                        .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .access = VK_ACCESS_2_SHADER_READ_BIT,
                        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    },
                    {
                        .texture = sky_view_lut,
                        .stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .access = VK_ACCESS_2_SHADER_READ_BIT,
                        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    },
                }
            });

        graph.end_label();
    }

    void ProceduralSky::render_sky(
        CommandBuffer& commands, const BufferHandle view_buffer, const BufferHandle sun_buffer,
        const TextureHandle gbuffer_depth
    ) const {
        auto& backend = RenderBackend::get();

        const auto set = backend.get_transient_descriptor_allocator().build_set(sky_application_pso, 0)
            .bind(transmittance_lut, linear_sampler)
            .bind(sky_view_lut, linear_sampler)
            .bind(view_buffer)
            .bind(sun_buffer)
            .bind(gbuffer_depth)
            .build();

        commands.bind_pipeline(sky_application_pso);

        commands.bind_descriptor_set(0, set);

        commands.draw_triangle();

        commands.clear_descriptor_set(0);
    }

    TextureHandle ProceduralSky::get_sky_view_lut() const {
        return sky_view_lut;
    }

    TextureHandle ProceduralSky::get_transmittance_lut() const {
        return transmittance_lut;
    }

    VkSampler ProceduralSky::get_sampler() const {
        return linear_sampler;
    }
}
