#include "rtgi.hpp"

#include "console/cvars.hpp"
#include "render/gbuffer.hpp"
#include "render/render_scene.hpp"
#include "render/scene_view.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/render_backend.hpp"
#include "render/gi/denoiser/denoiser_type.hpp"

namespace render {
    static auto cvar_num_bounces = AutoCVar_Int{
        "r.GI.NumBounces", "Number of times light can bounce in GI. 0 = no GI", 1};

    static auto cvar_num_reconstruction_rays = AutoCVar_Int{
        "r.GI.Reconstruction.NumSamples",
        "Number of extra rays to use in the screen-space reconstruction filter, DLSS likes 8, FSR likes 32", 0
    };

    static auto cvar_reconstruction_size = AutoCVar_Float{
        "r.GI.Reconstruction.Size", "Size in pixels of the screenspace reconstruction filter", 16
    };

    static auto cvar_denoiser = AutoCVar_Enum{"r.GI.Denoiser", "Which denoiser to use. 0 = none, 1 = ReBLUR, 2 = ReLAX",
                                              DenoiserType::None};

#if SAH_USE_IRRADIANCE_CACHE
    static AutoCVar_Int cvar_gi_cache{ "r.GI.Cache.Enabled", "Whether to enable the GI irradiance cache", false };

    static AutoCVar_Int cvar_gi_cache_debug{ "r.GI.Cache.Debug", "Enable a debug draw of the irradiance cache", true };
#endif

    bool RayTracedGlobalIllumination::should_render() {
        return cvar_num_bounces.get() > 0;
    }

    RayTracedGlobalIllumination::RayTracedGlobalIllumination() {
        const auto& backend = RenderBackend::get();
        if(overlay_pso == nullptr) {
            overlay_pso = backend.begin_building_pipeline("rtgi_application")
                                 .set_vertex_shader("shaders/common/fullscreen.vert.spv")
                                 .set_fragment_shader("shaders/gi/rtgi/overlay.frag.spv")
                                 .set_depth_state(
                                 {
                                     .enable_depth_write = false,
                                     .compare_op = VK_COMPARE_OP_LESS
                                 })
                                 .set_blend_mode(BlendMode::Additive)
                                 .build();
        }

        if(filter_pso == nullptr) {
            filter_pso = backend.begin_building_pipeline("rtgi_spatial_filter")
                                .set_vertex_shader("shaders/common/fullscreen.vert.spv")
                                .set_fragment_shader("shaders/gi/rtgi/spatial_filter.frag.spv")
                                .set_depth_state(
                                {
                                    .enable_depth_write = false,
                                    .compare_op = VK_COMPARE_OP_LESS
                                })
                                .set_blend_mode(BlendMode::Additive)
                                .build();
        }
    }

    RayTracedGlobalIllumination::~RayTracedGlobalIllumination() {
        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        allocator.destroy_texture(ray_texture);
        allocator.destroy_texture(ray_irradiance);
    }

    void RayTracedGlobalIllumination::pre_render(
        RenderGraph& graph, const SceneView& view, const RenderScene& scene, const TextureHandle noise_tex
        ) {
        ZoneScoped;

        if(cvar_denoiser.get() != DenoiserType::None && denoiser == nullptr) {
            denoiser = eastl::make_unique<NvidiaRealtimeDenoiser>();
        } else if(cvar_denoiser.get() == DenoiserType::None && denoiser) {
            RenderBackend::get().wait_for_idle();
            denoiser = nullptr;
        }

#if SAH_USE_IRRADIANCE_CACHE
        if (cvar_gi_cache.Get() == 0) {
            irradiance_cache = nullptr;
        }
        else if (irradiance_cache == nullptr) {
            irradiance_cache = eastl::make_unique<IrradianceCache>();
        }

        if (irradiance_cache) {
            irradiance_cache->update_cascades_and_probes(graph, view, scene, noise_tex);
        }
#endif
    }

    void RayTracedGlobalIllumination::post_render(
        RenderGraph& graph, const SceneView& view, const RenderScene& scene, const GBuffer& gbuffer,
        const TextureHandle motion_vectors, const TextureHandle noise_tex
        ) {
        if(!should_render()
#if SAH_USE_IRRADIANCE_CACHE
            && irradiance_cache == nullptr
#endif
        ) {
            return;
        }

        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        const auto render_resolution = gbuffer.depth->get_resolution();

        if(ray_texture == nullptr || ray_texture->get_resolution() != render_resolution) {
            allocator.destroy_texture(ray_texture);
            ray_texture = allocator.create_texture(
                "rtgi_params",
                {
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .resolution = render_resolution,
                    .usage = TextureUsage::StorageImage
                });
        }
        if(ray_irradiance == nullptr || ray_irradiance->get_resolution() != render_resolution) {
            allocator.destroy_texture(ray_irradiance);
            ray_irradiance = allocator.create_texture(
                "rtgi_irradiance",
                {
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .resolution = render_resolution,
                    .usage = TextureUsage::StorageImage,
                    .usage_flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                });
        }
        if(denoised_irradiance == nullptr || denoised_irradiance->get_resolution() != render_resolution) {
            allocator.destroy_texture(denoised_irradiance);
            denoised_irradiance = allocator.create_texture(
                "denoised_irradiance",
                {
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .resolution = render_resolution,
                    .usage = TextureUsage::StorageImage,
                    .usage_flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                });
        }
        if(denoiser_data == nullptr || denoiser_data->get_resolution() != render_resolution) {
            allocator.destroy_texture(denoiser_data);
            denoiser_data = allocator.create_texture(
                "denoiser_data",
                {
                    .format = VK_FORMAT_R16G16B16A16_SNORM,
                    .resolution = render_resolution,
                    .usage = TextureUsage::StorageImage
                });
        }
        if(rtgi_pipeline == nullptr) {
            rtgi_pipeline = backend.get_pipeline_cache()
                                   .create_ray_tracing_pipeline("shaders/gi/rtgi/rtgi.rt.spv");
        }

        if(denoiser) {
            denoiser->set_constants(view, cvar_denoiser.get(), render_resolution);
        }

        const auto sun_buffer = scene.get_sun_light().get_constant_buffer();

        auto& sky = scene.get_sky();
        const auto set = backend.get_transient_descriptor_allocator()
                                .build_set(rtgi_pipeline, 0)
                                .bind(scene.get_primitive_buffer())
                                .bind(sun_buffer)
                                .bind(view.get_buffer())
                                .bind(scene.get_raytracing_scene().get_acceleration_structure())
                                .bind(gbuffer.normals)
                                .bind(gbuffer.data)
                                .bind(gbuffer.depth)
                                .bind(noise_tex)
                                .bind(ray_texture)
                                .bind(ray_irradiance)
                                .bind(denoiser_data)
                                .bind(sky.get_sky_view_lut(), sky.get_sampler())
                                .bind(sky.get_transmittance_lut(), sky.get_sampler())
                                .build();

        graph.add_pass(
        {
            .name = "ray_traced_global_illumination",
            .descriptor_sets = {set},
            .execute = [&](CommandBuffer& commands) {
                commands.bind_pipeline(rtgi_pipeline);

                commands.bind_descriptor_set(0, set);
                commands.bind_descriptor_set(1, backend.get_texture_descriptor_pool().get_descriptor_set());

                commands.set_push_constant(0, static_cast<uint32_t>(cvar_num_bounces.get()));
                commands.set_push_constant(1, denoiser == nullptr ? 0u : 1u);

                commands.dispatch_rays(render_resolution);

                commands.clear_descriptor_set(0);
                commands.clear_descriptor_set(1);
            }
        });

        if(denoiser) {
            denoiser->do_denoising(graph,
                                   view.get_buffer(),
                                   gbuffer.depth,
                                   motion_vectors,
                                   ray_irradiance,
                                   denoiser_data,
                                   denoised_irradiance);
        } else {
            graph.add_copy_pass(ImageCopyPass{
                .name = "copy_noisy_gi",
                .dst = denoised_irradiance,
                .src = ray_irradiance});
        }
    }

    void RayTracedGlobalIllumination::get_lighting_resource_usages(
        TextureUsageList& textures, BufferUsageList& buffers
        ) const {
        if(!should_render()
#if SAH_USE_IRRADIANCE_CACHE
            && irradiance_cache == nullptr
#endif
        ) {
            return;
        }

        textures.emplace_back(
            ray_texture,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        textures.emplace_back(
            denoised_irradiance,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

#if SAH_USE_IRRADIANCE_CACHE
        if (irradiance_cache) {
            irradiance_cache->get_resource_uses(textures, buffers);
        }
#endif
    }

    void RayTracedGlobalIllumination::render_to_lit_scene(
        CommandBuffer& commands, const BufferHandle view_buffer, BufferHandle sun_buffer, TextureHandle ao_tex,
        const TextureHandle noise_texture
        ) const {
        if(!should_render()
#if SAH_USE_IRRADIANCE_CACHE
            && irradiance_cache == nullptr
#endif
        ) {
            return;
        }

#if SAH_USE_IRRADIANCE_CACHE
        if (cvar_gi_cache_debug.Get() != 0 && irradiance_cache != nullptr) {
            irradiance_cache->add_to_lit_scene(commands, view_buffer);
        }
        else
#endif
        if(denoiser == nullptr) {
            const auto set = RenderBackend::get().get_transient_descriptor_allocator()
                                                 .build_set(filter_pso, 1)
                                                 .bind(view_buffer)
                                                 .bind(noise_texture)
                                                 .bind(ray_texture)
                                                 .bind(denoised_irradiance)
                                                 .build();

            commands.set_cull_mode(VK_CULL_MODE_NONE);

            commands.bind_pipeline(filter_pso);

            commands.bind_descriptor_set(1, set);

            commands.set_push_constant(0, static_cast<uint32_t>(cvar_num_reconstruction_rays.get()));
            commands.set_push_constant(1, cvar_reconstruction_size.GetFloat());

            commands.draw_triangle();

            commands.clear_descriptor_set(1);
        } else {
            // Simple copy thing?
            const auto set = RenderBackend::get().get_transient_descriptor_allocator()
                                                 .build_set(overlay_pso, 0)
                                                 .bind(denoised_irradiance)
                                                 .build();

            commands.set_cull_mode(VK_CULL_MODE_NONE);

            commands.bind_pipeline(overlay_pso);

            commands.bind_descriptor_set(0, set);

            commands.draw_triangle();

            commands.clear_descriptor_set(0);
        }
    }

    void RayTracedGlobalIllumination::render_volumetrics(
        RenderGraph& render_graph, const SceneView& player_view, const RenderScene& scene, const GBuffer& gbuffer,
        TextureHandle lit_scene_handle
        ) {
        if(!should_render()
#if SAH_USE_IRRADIANCE_CACHE
            && irradiance_cache == nullptr
#endif
        ) {
            return;
        }

        // TODO
    }

    void RayTracedGlobalIllumination::draw_debug_overlays(
        RenderGraph& graph, const SceneView& view, const GBuffer& gbuffer, TextureHandle lit_scene_texture
        ) {
#if SAH_USE_IRRADIANCE_CACHE
        if (cvar_gi_cache_debug.Get() != 0 && irradiance_cache) {
            irradiance_cache->draw_debug_overlays(graph, view, gbuffer, lit_scene_texture);
        }
#endif

        if(denoiser) {
            denoiser->draw_validation_overlay(graph, lit_scene_texture);
        }
    }
}
