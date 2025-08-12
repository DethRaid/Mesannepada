#include <spdlog/logger.h>
#include <spdlog/sinks/android_sink.h>

#include "sarah_renderer.hpp"

#include "backend/pipeline_cache.hpp"
#include "core/string_utils.hpp"
#include "core/system_interface.hpp"
#include "render/antialiasing_type.hpp"
#include "render/indirect_drawing_utils.hpp"
#include "render/render_scene.hpp"
#include "render/backend/blas_build_queue.hpp"
#include "render/backend/render_graph.hpp"
#include "render/gi/rtgi.hpp"
#include "render/upscaling/dlss.hpp"
#include "render/upscaling/fsr3.hpp"
#include "render/upscaling/xess.hpp"

namespace render {
    static std::shared_ptr<spdlog::logger> logger;

    // ReSharper disable CppDeclaratorNeverUsed
    static auto cvar_gi_mode = AutoCVar_Enum{
        "r.GI.Mode", "How to calculate GI", GIMode::RT
    };

    static auto cvar_raytrace_mesh_lights = AutoCVar_Int{
        "r.MeshLight.Raytrace", "Whether or not to raytrace mesh lights", 0
    };

    static auto cvar_anti_aliasing = AutoCVar_Enum{
        "r.AntiAliasing", "What kind of antialiasing to use", AntiAliasingType::None
    };

    static auto cvar_enable_fog = AutoCVar_Int{"r.Fog.Enable", "Whether to enable fog effects", 0};

    /*
     * Quick guide to anti-aliasing quality modes:
     * Name               | DLSS   | XeSS | FSR
     * ------------------------------------------
     * Anti-Aliasing      | 1.0x   | 1.0x | 1.0x
     * Ultra Quality Plus | N/A    | 1.3x | N/A
     * Ultra Quality      | N/A    | 1.5x | N/A
     * Quality            | 1.5x   | 1.7x | 1.5x
     * Balanced           | 1.724x | 2.0x | 1.7x
     * Performance        | 2.0x   | 2.3x | 2.0x
     * Ultra Performance  | 3.0x   | 3.0x | 3.0x
     *
     * Alternately:
     * Scaling factor | DLSS              | XeSS              | FSR
     * ----------------------------------------------------------------------------
     * 1.0x           | DLAA              | Native AA         | Anti-Aliasing
     * 1.5x           | Quality           | Ultra Quality     | Quality
     * 1.7x           | Balanced          | Quality           | Balanced
     * 2.0x           | Performance       | Balanced          | Performance
     * 3.0x           | Ultra Performance | Ultra Performance | Ultra Performance
     *
     */

    // ReSharper restore CppDeclaratorNeverUsed

    SarahRenderer::SarahRenderer() :
        rmlui_renderer{texture_loader} {
        ZoneScoped;

        logger = SystemInterface::get().get_logger("SceneRenderer");

        const auto screen_resolution = SystemInterface::get().get_resolution();

        set_output_resolution(screen_resolution);

        auto& backend = RenderBackend::get();

        logger->debug("Initialized render backend");

        if(!backend.supports_shading_rate_image && cvar_anti_aliasing.get() == AntiAliasingType::VRSAA) {
            logger->info("Backend does not support VRSAA, turning AA off");
            cvar_anti_aliasing.set(AntiAliasingType::None);
        }

        auto& allocator = backend.get_global_allocator();

        copy_scene_pso = backend.begin_building_pipeline("Copy Scene")
                                .set_vertex_shader("shader://common/fullscreen.vert.spv"_res)
                                .set_fragment_shader("shader://util/copy_with_sampler.frag.spv"_res)
                                .set_depth_state({.enable_depth_test = false, .enable_depth_write = false})
                                .build();
        linear_sampler = allocator.get_sampler(
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
        });

        stbn_3d_unitvec = NoiseTexture::create("stbn/stbn_unitvec3_2Dx1D_128x128x64", 64, texture_loader);

        stbn_2d_scalar = NoiseTexture::create("stbn/stbn_vec2_2Dx1D_128x128x64", 64, texture_loader);

        luminance_histogram = allocator.create_buffer(
            "luminance_histogram",
            256 * sizeof(uint32_t),
            BufferUsage::StorageBuffer);

        average_exposure = allocator.create_texture(
            "average_exposure",
            {
                .format = VK_FORMAT_R16G16_SFLOAT,
                .resolution = {1, 1},
                .usage = TextureUsage::StorageImage,
            });
        exposure_factor = allocator.create_texture(
            "exposure_factor",
            {
                .format = VK_FORMAT_R16G16_SFLOAT,
                .resolution = {1, 1},
                .usage = TextureUsage::StorageImage,
            });

        //backend.get_upload_queue().enqueue(
        //    TextureUploadJob{
        //        .destination = average_exposure,
        //        .data = {0}
        //    });
        //backend.get_upload_queue().enqueue(
        //    TextureUploadJob{
        //        .destination = exposure_factor,
        //        .data = {0}
        //    });

        exposure_histogram_pipeline = backend.get_pipeline_cache()
                                             .create_pipeline("shader://autoexposure/histogram.comp.spv"_res);
        average_exposure_pipeline = backend.get_pipeline_cache()
                                           .create_pipeline("shader://autoexposure/average_exposure.comp.spv"_res);

#ifdef JPH_DEBUG_RENDERER
        jolt_debug = eastl::make_unique<JoltDebugRenderer>(meshes);
#endif

        logger->info("Initialized SceneRenderer");
    }

    void SarahRenderer::set_output_resolution(const glm::uvec2& new_output_resolution) {
        output_resolution = new_output_resolution;
    }

    void SarahRenderer::set_world(RenderWorld& world_in) {
        world = &world_in;
    }

    void SarahRenderer::set_render_resolution(const glm::uvec2 new_render_resolution) {
        if(new_render_resolution == render_resolution) {
            return;
        }

        render_resolution = new_render_resolution;

        logger->info(
            "Setting resolution to {} by {}",
            render_resolution.x,
            render_resolution.y);

        auto& player_view = world->get_player_view();
        player_view.set_render_resolution(render_resolution);

        player_view.set_perspective_projection(
            75.f,
            static_cast<float>(render_resolution.x) /
            static_cast<float>(render_resolution.y),
            0.05f
            );

        create_scene_render_targets();
    }

    void SarahRenderer::render() {
        ZoneScoped;

        auto& backend = RenderBackend::get();

        backend.advance_frame();

        auto& player_view = world->get_player_view();
        player_view.increment_frame_count();

        logger->trace("Beginning frame");

        auto needs_motion_vectors = true;

        switch(cvar_anti_aliasing.get()) {
        case AntiAliasingType::VRSAA:
            if(vrsaa == nullptr) {
                vrsaa = eastl::make_unique<VRSAA>();
            }

            upscaler = nullptr;

            set_render_resolution(output_resolution * 2u);

            vrsaa->init(render_resolution);
            player_view.set_mip_bias(0);

            break;

#if SAH_USE_STREAMLINE
        case AntiAliasingType::DLSS:
            if (cached_aa != AntiAliasingType::DLSS) {
                upscaler = eastl::make_unique<DLSSAdapter>();
            }
            break;
#endif
#if SAH_USE_FFX
        case AntiAliasingType::FSR3:
            if (cached_aa != AntiAliasingType::FSR3) {
                upscaler = eastl::make_unique<FidelityFSSuperResolution3>();
            }
            break;
#endif
#if SAH_USE_XESS
        case AntiAliasingType::XeSS:
            if (cached_aa != AntiAliasingType::XeSS) {
                upscaler = eastl::make_unique<XeSSAdapter>();
            }
            break;
#endif

        case AntiAliasingType::None:
            [[fallthrough]];
        default:
            vrsaa = nullptr;
            upscaler = nullptr;
            set_render_resolution(output_resolution);
            player_view.set_mip_bias(0);
            break;
        }

        cached_aa = cvar_anti_aliasing.get();

        if(upscaler) {
            vrsaa = nullptr;

            upscaler->initialize(output_resolution, frame_count);

            const auto optimal_render_resolution = upscaler->get_optimal_render_resolution();
            set_render_resolution(optimal_render_resolution);

            const auto d_output_resolution = glm::vec2{output_resolution};
            const auto d_render_resolution = glm::vec2{optimal_render_resolution};
            player_view.set_mip_bias(log2(d_render_resolution.x / d_output_resolution.x) - 1.0f);

            needs_motion_vectors = true;
        }

        if(cvar_gi_mode.get() == GIMode::RT && !backend.supports_ray_tracing()) {
            cvar_gi_mode.set(GIMode::LPV);
        }

        switch(cvar_gi_mode.get()) {
        case GIMode::Off:
            gi = nullptr;
            break;
        case GIMode::LPV:
            if(cached_gi_mode != GIMode::LPV) {
                gi = eastl::make_unique<LightPropagationVolume>();
            }
            break;
        case GIMode::RT:
            if(cached_gi_mode != GIMode::RT) {
                gi = eastl::make_unique<RayTracedGlobalIllumination>();
                needs_motion_vectors = true;
            }
            break;
        }
        cached_gi_mode = cvar_gi_mode.get();

        ui_phase.add_data_upload_passes(backend.get_upload_queue());

        gbuffer.depth = depth_culling_phase.get_depth_buffer();

        backend.get_texture_descriptor_pool().commit_descriptors();

        update_jitter();
        player_view.update_buffer(backend.get_upload_queue());

        if(upscaler) {
            upscaler->set_constants(player_view, render_resolution);
        }

        auto render_graph = RenderGraph{backend};

        render_graph.add_pass(
            {
                .name = "Tracy Collect",
                .execute = [&](const CommandBuffer& commands) {
                    backend.collect_tracy_data(commands);
                }
            }
            ); {
            ZoneScopedN("Begin Frame");
            auto& sun = world->get_sun_light();
            if(DirectionalLight::get_shadow_mode() == SunShadowMode::CascadedShadowMaps) {
                sun.update_shadow_cascades(player_view);
            }

            sun.update_buffer(backend.get_upload_queue());
        }

        backend.get_blas_build_queue().flush_pending_builds(render_graph);

        material_storage.flush_material_instance_buffer(render_graph);

        meshes.flush_mesh_draw_arg_uploads(render_graph);

        render_graph.add_transition_pass(
            {
                .buffers = {
                    {
                        .buffer = world->get_primitive_buffer(),
                        .stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        .access = VK_ACCESS_SHADER_READ_BIT
                    },
                    {
                        .buffer = meshes.get_index_buffer(),
                        .stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                        .access = VK_ACCESS_2_INDEX_READ_BIT
                    },
                    {
                        .buffer = meshes.get_vertex_position_buffer(),
                        .stage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                        .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
                    },
                    {
                        .buffer = meshes.get_vertex_data_buffer(),
                        .stage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                        .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT
                    }
                }
            }
            );

        world->begin_frame(render_graph);

        world->deform_skinned_meshes(render_graph);

        // Depth and stuff

        depth_culling_phase.render(
            render_graph,
            *world,
            material_storage,
            player_view.get_buffer());

        const auto visible_objects_list = depth_culling_phase.get_visible_objects_buffer();
        const auto visible_solids_buffers = translate_visibility_list_to_draw_commands(
            render_graph,
            visible_objects_list,
            world->get_primitive_buffer(),
            world->get_total_num_primitives(),
            world->get_meshes().get_draw_args_buffer(),
            PRIMITIVE_FLAG_SOLID);
        const auto visible_masked_buffers = translate_visibility_list_to_draw_commands(
            render_graph,
            visible_objects_list,
            world->get_primitive_buffer(),
            world->get_total_num_primitives(),
            world->get_meshes().get_draw_args_buffer(),
            PRIMITIVE_FLAG_CUTOUT);

        if(needs_motion_vectors) {
            motion_vectors_phase.render(
                render_graph,
                *world,
                player_view.get_buffer(),
                depth_culling_phase.get_depth_buffer(),
                visible_solids_buffers,
                visible_masked_buffers);
        }

        if(gi) {
            gi->pre_render(render_graph, player_view, *world, stbn_3d_unitvec.get_layer(frame_count));
        }

        // Generate shading rate image

        /*
         * Use the contrast image from last frame and the depth buffer from last frame to initialize a shading rate image.
         * We'll take more samples in areas of higher contrast and in areas of depth increasing
         *
         * How to handle disocclusions:
         * If the depth increases, something went in front of the current pixel, and we should use the old pixel's contrast.
         * If the depth decreases, the pixel is recently disoccluded, and we should use the maximum sample rate
         *
         * Once we ask for samples, we need to scale them by the available number of samples. Reduce the number of samples,
         * decreasing larger numbers the most, so that we don't take more samples than the hardware supports
         *
         * uint num_samples_4x = num_samples_if_we_take_a_max_of_4_samples_per_pixel()
         * uint num_samples_2x = you get the drill;
         * uint num_samples_1x = yada yada;
         *
         * if(sample_budget <= num_samples_4x) {
         *  write_shading_rate_image(4);
         * } else if(sample_budget <= num_samples_2x_msaa) {
         *  write_shading_rate_image(2);
         * } else {
         *  write_shading_rate_image(1);
         * }
         *
         * Writing the shading rate image allows any sample value below or equal to the max
         */

        eastl::optional<TextureHandle> vrsaa_shading_rate_image = eastl::nullopt;
        if(vrsaa) {
            vrsaa->generate_shading_rate_image(render_graph);
            vrsaa_shading_rate_image = vrsaa->get_shading_rate_image();
        }

        // Gbuffers, lighting, and translucency

        gbuffer_phase.render(
            render_graph,
            *world,
            visible_solids_buffers,
            visible_masked_buffers,
            gbuffer,
            vrsaa_shading_rate_image,
            player_view);

        // Shadows
        auto& sun = world->get_sun_light();
        sun.render_shadows(render_graph, gbuffer, *world, stbn_3d_unitvec.get_layer(frame_count));

        if(gi) {
            gi->post_render(
                render_graph,
                player_view,
                *world,
                gbuffer,
                motion_vectors_phase.get_motion_vectors(),
                stbn_3d_unitvec.layers[frame_count % stbn_3d_unitvec.num_layers]);
        }

        ao_phase.generate_ao(
            render_graph,
            player_view,
            *world,
            stbn_3d_unitvec,
            gbuffer.normals,
            gbuffer.depth,
            ao_handle);

        lighting_pass.render(
            render_graph,
            *world,
            gbuffer,
            lit_scene_handle,
            ao_handle,
            gi.get(),
            vrsaa_shading_rate_image,
            stbn_3d_unitvec,
            stbn_2d_scalar.get_layer(frame_count));

        // scene->add_atmospheric_effects(
        //     render_graph,
        //     player_view,
        //     gbuffer,
        //     lit_scene_handle
        // );

        if(cvar_enable_fog.get() != 0) {
            if(gi) {
                gi->render_volumetrics(render_graph, player_view, *world, gbuffer, lit_scene_handle);
            }
        }

        // Debug

        if(active_visualization != RenderVisualization::None) {
            draw_debug_visualizers(render_graph);
        }

        measure_exposure(render_graph);

        // Anti-aliasing/upscaling

        evaluate_antialiasing(render_graph, gbuffer.depth);

        // Bloom

        bloomer.fill_bloom_tex(render_graph, antialiased_scene_handle);

        // Other postprocessing

        // TODO

        // UI

        const auto swapchain_index = backend.get_current_swapchain_index();
        const auto& swapchain_image = swapchain_images.at(swapchain_index);

        render_graph.add_render_pass(
        {
            .name = "UI",
            .textures = {
                {
                    .texture = antialiased_scene_handle,
                    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                },
                {
                    .texture = bloomer.get_bloom_tex(),
                    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                },
                {
                    .texture = exposure_factor,
                    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            },
            .buffers = {
                {
                    .buffer = rmlui_renderer.get_vertex_buffer(),
                    .stage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
                    .access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                },
                {
                    .buffer = rmlui_renderer.get_index_buffer(),
                    .stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                    .access = VK_ACCESS_2_INDEX_READ_BIT,
                },
            },
            .color_attachments = {RenderingAttachmentInfo{.image = swapchain_image}},
            .execute = [&](CommandBuffer& commands) {
                ui_phase.render(commands, player_view, bloomer.get_bloom_tex(), exposure_factor, rmlui_renderer);
            }
        });

        render_graph.add_finish_frame_and_present_pass(
            {
                .swapchain_image = swapchain_image
            }
            );

        render_graph.finish();

        auto& allocator = backend.get_global_allocator();
        allocator.destroy_buffer(visible_solids_buffers.commands);
        allocator.destroy_buffer(visible_solids_buffers.count);
        allocator.destroy_buffer(visible_solids_buffers.primitive_ids);
        allocator.destroy_buffer(visible_masked_buffers.commands);
        allocator.destroy_buffer(visible_masked_buffers.count);
        allocator.destroy_buffer(visible_masked_buffers.primitive_ids);

        backend.execute_graph(render_graph);

        frame_count++;
    }

    const SceneView& SarahRenderer::get_player_view() const {
        return world->get_player_view();
    }

    void SarahRenderer::measure_exposure(RenderGraph& render_graph) {
        ZoneScoped;

        auto& descriptors = RenderBackend::get().get_transient_descriptor_allocator();

        const auto resolution = lit_scene_handle->get_resolution();
        const auto& player_view = world->get_player_view();

        // Build histogram
        {
            const auto set = descriptors.build_set(exposure_histogram_pipeline, 0)
                                        .bind(lit_scene_handle)
                                        .bind(luminance_histogram)
                                        .build();

            struct Constants {
                float min_luminance;
                float max_luminance;
                uint2 dim;
            };

            const auto dispatch_size = (resolution + 15u) / 16u;

            render_graph.add_compute_dispatch<Constants>(
            {
                .name = "build_luminance_histogram",
                .descriptor_sets = {set},
                .push_constants = Constants{
                    .min_luminance = player_view.get_min_log_luminance(),
                    .max_luminance = player_view.get_max_log_luminance(),
                    .dim = resolution
                },
                .num_workgroups = {dispatch_size, 1},
                .compute_shader = exposure_histogram_pipeline
            });
        }

        // Compute average luminance
        {
            const auto set = descriptors.build_set(average_exposure_pipeline, 0)
                                        .bind(luminance_histogram)
                                        .bind(average_exposure)
                                        .bind(exposure_factor)
                                        .build();

            struct Constants {
                float minLogLum;
                float logLumRange;
                float timeCoeff;
                float numPixels;
            };

            render_graph.add_compute_dispatch<Constants>(
            {
                .name = "average_exposure",
                .descriptor_sets = {set},
                .push_constants = Constants{
                    .minLogLum = player_view.get_min_log_luminance(),
                    .logLumRange = player_view.get_max_log_luminance() - player_view.get_min_log_luminance(),
                    .timeCoeff = 0.05f,
                    .numPixels = static_cast<float>(resolution.x * resolution.y)
                },
                .num_workgroups = {1, 1, 1},
                .compute_shader = average_exposure_pipeline,
            });
        }
    }

    void SarahRenderer::evaluate_antialiasing(RenderGraph& render_graph, const TextureHandle gbuffer_depth_handle
        ) const {
        auto& backend = RenderBackend::get();
        auto& player_view = world->get_player_view();

        switch(cvar_anti_aliasing.get()) {
        case AntiAliasingType::VRSAA:
            if(vrsaa) {
                vrsaa->measure_aliasing(render_graph, gbuffer.color, gbuffer_depth_handle);
                // TODO: Perform a proper VSR resolve, and also do VRS in lighting
            }
            break;

        case AntiAliasingType::FSR3:
            [[fallthrough]];
        case AntiAliasingType::DLSS:
            [[fallthrough]];
        case AntiAliasingType::XeSS:
            if(upscaler) {
                const auto motion_vectors_handle = motion_vectors_phase.get_motion_vectors();
                const auto denoiser_data = gi->get_denoiser_data_texture();
                upscaler->evaluate(
                    render_graph,
                    player_view,
                    gbuffer,
                    lit_scene_handle,
                    antialiased_scene_handle,
                    motion_vectors_handle,
                    exposure_factor, 
                    denoiser_data);
                break;
            } else {
                [[fallthrough]];
            }

        case AntiAliasingType::None: {
            const auto set = backend.get_transient_descriptor_allocator()
                                    .build_set(copy_scene_pso, 0)
                                    .bind(lit_scene_handle, linear_sampler)
                                    .build();
            render_graph.add_render_pass(
            {
                .name = "Copy scene",
                .descriptor_sets = {set},
                .color_attachments = {
                    {
                        .image = antialiased_scene_handle,
                        .load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE
                    }
                },
                .execute = [&](CommandBuffer& commands) {
                    commands.set_push_constant(0, 1.f / static_cast<float>(output_resolution.x));
                    commands.set_push_constant(1, 1.f / static_cast<float>(output_resolution.y));
                    commands.bind_descriptor_set(0, set);
                    commands.bind_pipeline(copy_scene_pso);
                    commands.draw_triangle();

                    commands.clear_descriptor_set(0);
                }
            });
        }
        }
    }

    TextureLoader& SarahRenderer::get_texture_loader() {
        return texture_loader;
    }

    MaterialStorage& SarahRenderer::get_material_storage() {
        return material_storage;
    }

    void SarahRenderer::create_scene_render_targets() {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        if(gbuffer.color != nullptr) {
            allocator.destroy_texture(gbuffer.color);
        }

        if(gbuffer.normals != nullptr) {
            allocator.destroy_texture(gbuffer.normals);
        }

        if(gbuffer.data != nullptr) {
            allocator.destroy_texture(gbuffer.data);
        }

        if(gbuffer.emission != nullptr) {
            allocator.destroy_texture(gbuffer.emission);
        }

        if(ao_handle != nullptr) {
            allocator.destroy_texture(ao_handle);
        }

        if(lit_scene_handle != nullptr) {
            allocator.destroy_texture(lit_scene_handle);
        }

        if(antialiased_scene_handle != nullptr) {
            allocator.destroy_texture(antialiased_scene_handle);
        }

        depth_culling_phase.set_render_resolution(render_resolution);

        motion_vectors_phase.set_render_resolution(render_resolution, output_resolution);

        // gbuffer and lighting render targets
        gbuffer.color = allocator.create_texture(
            "gbuffer_color",
            {
                VK_FORMAT_R8G8B8A8_SRGB,
                render_resolution,
                1,
                TextureUsage::RenderTarget
            }
            );

        gbuffer.normals = allocator.create_texture(
            "gbuffer_normals",
            {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                render_resolution,
                1,
                TextureUsage::RenderTarget
            }
            );

        gbuffer.data = allocator.create_texture(
            "gbuffer_data",
            {
                VK_FORMAT_R8G8B8A8_UNORM,
                render_resolution,
                1,
                TextureUsage::RenderTarget
            }
            );

        gbuffer.emission = allocator.create_texture(
            "gbuffer_emission",
            {
                VK_FORMAT_R8G8B8A8_SRGB,
                render_resolution,
                1,
                TextureUsage::RenderTarget
            }
            );

        ao_handle = allocator.create_texture(
            "AO",
            TextureCreateInfo{
                .format = VK_FORMAT_R32_SFLOAT,
                .resolution = render_resolution,
                .num_mips = 1,
                .usage = TextureUsage::RenderTarget,
                .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT
            }
            );

        lit_scene_handle = allocator.create_texture(
            "lit_scene",
            {
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .resolution = render_resolution,
                .usage = TextureUsage::RenderTarget,
                .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT
            }
            );

        antialiased_scene_handle = allocator.create_texture(
            "antialiased_scene",
            {
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .resolution = output_resolution,
                .usage = TextureUsage::RenderTarget,
                .usage_flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT
            });

        auto& swapchain = backend.get_swapchain();
        const auto& images = swapchain.get_images();
        const auto& image_views = swapchain.get_image_views();
        for(auto swapchain_image_index = 0u; swapchain_image_index < swapchain.image_count;
            swapchain_image_index++) {
            const auto swapchain_image_name = fmt::format("Swapchain image {}", swapchain_image_index);
            const auto swapchain_image = allocator.emplace_texture(
                GpuTexture{
                    .name = swapchain_image_name.c_str(),
                    .create_info = {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                        .imageType = VK_IMAGE_TYPE_2D,
                        .format = swapchain.image_format,
                        .extent = VkExtent3D{swapchain.extent.width, swapchain.extent.height, 1},
                        .mipLevels = 1,
                        .arrayLayers = 1,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .tiling = VK_IMAGE_TILING_OPTIMAL,
                        .usage = swapchain.image_usage_flags,
                    },
                    .image = images->at(swapchain_image_index),
                    .image_view = image_views->at(swapchain_image_index),
                    .type = TextureAllocationType::Swapchain,
                }
                );

            swapchain_images.push_back(swapchain_image);
        }

        ui_phase.set_resources(
            antialiased_scene_handle,
            glm::uvec2{swapchain.extent.width, swapchain.extent.height});
    }

    void SarahRenderer::update_jitter() {
        previous_jitter = jitter;

        if(upscaler) {
            jitter = upscaler->get_jitter();
        }

        auto& player_view = world->get_player_view();
        player_view.set_jitter(jitter);
    }

    void SarahRenderer::draw_debug_visualizers(RenderGraph& render_graph) {
        switch(active_visualization) {
        case RenderVisualization::None:
            // Intentionally empty
            break;

        case RenderVisualization::GIDebug:
            if(gi) {
                auto& player_view = world->get_player_view();
                gi->draw_debug_overlays(render_graph, player_view, gbuffer, lit_scene_handle);
            }
            break;

#ifdef JPH_DEBUG_RENDERER
        case RenderVisualization::Physics:
            if(jolt_debug) {
                jolt_debug->draw(render_graph, *world, gbuffer, lit_scene_handle);
            }
#endif
        }
    }

    MeshStorage& SarahRenderer::get_mesh_storage() {
        return meshes;
    }

    void SarahRenderer::set_imgui_commands(ImDrawData* im_draw_data) {
        ui_phase.set_imgui_draw_data(im_draw_data);
    }

    RenderVisualization SarahRenderer::get_active_visualizer() const {
        return active_visualization;
    }

    void SarahRenderer::set_active_visualizer(const RenderVisualization visualizer) {
        active_visualization = visualizer;
    }

    Rml::RenderInterface* SarahRenderer::get_rmlui_renderer() {
        return &rmlui_renderer;
    }

#ifdef JPH_DEBUG_RENDERER
    JoltDebugRenderer* SarahRenderer::get_jolt_debug_renderer() const {
        return jolt_debug.get();
    }
#endif
}
