#include "lighting_phase.hpp"

#include "console/cvars.hpp"
#include "render/procedural_sky.hpp"
#include "render/render_scene.hpp"
#include "render/scene_view.hpp"
#include "render/gi/global_illuminator.hpp"

namespace render {
    enum class SkyOcclusionType {
        Off,
        DepthMap,
        RayTraced
    };

    static AutoCVar_Enum cvar_sky_occlusion_type{
        "r.Sky.OcclusionType", "How to determine sky light occlusion", SkyOcclusionType::Off
    };

    LightingPhase::LightingPhase() {
        auto& backend = RenderBackend::get();

        point_lights_pipeline = backend.begin_building_pipeline("point_lights")
                                       .set_vertex_shader("shaders/common/fullscreen.vert.spv")
                                       .set_fragment_shader("shaders/lighting/point_lights.frag.spv")
                                       .set_depth_state({.enable_depth_write = false, .compare_op = VK_COMPARE_OP_LESS})
                                       .set_blend_mode(BlendMode::Additive)
                                       .build();

        emission_pipeline = backend.begin_building_pipeline("Emissive Lighting")
                                   .set_vertex_shader("shaders/common/fullscreen.vert.spv")
                                   .set_fragment_shader("shaders/lighting/emissive.frag.spv")
                                   .set_depth_state(
                                       DepthStencilState{
                                           .enable_depth_write = false,
                                           .compare_op = VK_COMPARE_OP_LESS
                                       }
                                   )
                                   .set_blend_mode(BlendMode::Additive)
                                   .build();
    }

    void LightingPhase::render(
        RenderGraph& render_graph,
        const RenderScene& scene,
        const GBuffer& gbuffer,
        const TextureHandle lit_scene_texture,
        const TextureHandle ao_texture,
        const IGlobalIlluminator* gi,
        const std::optional<TextureHandle> vrsaa_shading_rate_image,
        const NoiseTexture& noise,
        const TextureHandle noise_2d
    ) {
        ZoneScoped;

        const auto& view = scene.get_player_view();
        auto& backend = RenderBackend::get();

        if(cvar_sky_occlusion_type.Get() == SkyOcclusionType::DepthMap) {
            rasterize_sky_shadow(render_graph, view);
        } else {
            if(sky_occlusion_map != nullptr) {
                backend.get_global_allocator().destroy_texture(sky_occlusion_map);
            }
        }

        auto& sun = scene.get_sun_light();
        const auto sun_pipeline = sun.get_pipeline();

        const auto sampler = backend.get_default_sampler();
        auto gbuffers_descriptor_set = backend.get_transient_descriptor_allocator()
                                              .build_set(sun_pipeline, 0)
                                              .bind(gbuffer.color, sampler)
                                              .bind(gbuffer.normals, sampler)
                                              .bind(gbuffer.data, sampler)
                                              .bind(gbuffer.emission, sampler)
                                              .bind(gbuffer.depth, sampler)
                                              .build();

        auto point_lights_descriptor_set = backend.get_transient_descriptor_allocator()
                                                  .build_set(point_lights_pipeline, 1)
                                                  .bind(view.get_buffer())
                                                  .bind(scene.get_point_lights_buffer())
                                                  .bind(scene.get_raytracing_scene().get_acceleration_structure())
                                                  .build();

        auto texture_usages = TextureUsageList{
            {
                .texture = ao_texture,
                .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            }
        };
        const auto sun_shadowmap_handle = sun.get_shadowmap_handle();
        if(sun_shadowmap_handle != nullptr) {
            texture_usages.emplace_back(
                TextureUsageToken{
                    .texture = sun.get_shadowmap_handle(),
                    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                });
        }

        auto buffer_usages = BufferUsageList{};

        if(gi) {
            gi->get_lighting_resource_usages(texture_usages, buffer_usages);
        }

        sun.get_resource_usages(buffer_usages, texture_usages);

        render_graph.add_render_pass(
            {
                .name = "Lighting",
                .textures = texture_usages,
                .buffers = buffer_usages,
                .descriptor_sets = {gbuffers_descriptor_set, point_lights_descriptor_set},
                .color_attachments = {
                    RenderingAttachmentInfo{.image = lit_scene_texture, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR}
                },
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_descriptor_set(0, gbuffers_descriptor_set);

                    sun.render(commands, view);

                    apply_point_lights(commands, point_lights_descriptor_set, scene.get_num_point_lights());

                    if(gi) {
                        gi->render_to_lit_scene(
                            commands,
                            view.get_buffer(),
                            sun.get_constant_buffer(),
                            ao_texture,
                            noise_2d);
                    }

                    add_emissive_lighting(commands);

                    scene.get_sky().render_sky(commands, view.get_buffer(), sun.get_constant_buffer(), gbuffer.depth);

                    // The sky uses different descriptor sets, so if we add anything after this we'll have to re-bind the gbuffer descriptor set
                }
            });
    }

    void LightingPhase::rasterize_sky_shadow(RenderGraph& render_graph, const SceneView& view) {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        if(sky_occlusion_map == nullptr) {
            sky_occlusion_map = allocator.create_texture(
                "sky_shadowmap",
                {.format = VK_FORMAT_D16_UNORM, .resolution = {1024, 1024}, .usage = TextureUsage::RenderTarget});
        }

        // const auto sky_shadow_pso = MaterialPipelines::get().get_sky_shadow_pso();
        // const auto sky_shadow_masked_pso = MaterialPipelines::get().get_sky_shadow_masked_pso();
        // 
        // render_graph.add_render_pass(
        //     {
        //         .name = "sky_shadow",
        //         .descriptor_sets = {},
        //         .depth_attachment = RenderingAttachmentInfo{
        //             .image = sky_occlusion_map,
        //             .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        //             .store_op = VK_ATTACHMENT_STORE_OP_STORE,
        //             .clear_value = {.depthStencil = {.depth = 1.f}}
        //         },
        //         .execute = [&](CommandBuffer& commands) {
        //             
        //         }
        //     });
    }

    void LightingPhase::apply_point_lights(
        CommandBuffer& commands, const DescriptorSet& set, const uint32_t num_point_lights
    ) const {
        if(num_point_lights == 0) {
            return;
        }

        // Draw a fullscreen triangle with the proper buffers bound

        commands.begin_label("point_lights");

        commands.bind_pipeline(point_lights_pipeline);

        commands.bind_descriptor_set(1, set);
        commands.set_push_constant(0, num_point_lights);

        commands.draw_triangle();

        commands.clear_descriptor_set(1);

        commands.end_label();
    }

    void LightingPhase::add_emissive_lighting(CommandBuffer& commands) const {
        ZoneScoped;

        commands.begin_label("Emissive Lighting");

        commands.bind_pipeline(emission_pipeline);

        commands.draw_triangle();

        commands.end_label();
    }
}
