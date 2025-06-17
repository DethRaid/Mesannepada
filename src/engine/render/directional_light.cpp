#include "directional_light.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include "gbuffer.hpp"
#include "material_pipelines.hpp"
#include "material_storage.hpp"
#include "mesh_storage.hpp"
#include "noise_texture.hpp"
#include "render_scene.hpp"
#include "backend/pipeline_cache.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_allocator.hpp"
#include "render/backend/command_buffer.hpp"
#include "render/scene_view.hpp"
#include "console/cvars.hpp"
#include "core/system_interface.hpp"

namespace render {
    static std::shared_ptr<spdlog::logger> logger;

    static auto cvar_sun_shadow_mode = AutoCVar_Enum{
        "r.Shadow.SunShadowMode",
        "How to calculate shadows for the sun.\n\t0 = off\n\t1 = Cascade Shadow Maps\n\t2 = Hardware ray pipelines",
        SunShadowMode::CascadedShadowMaps
    };

    static auto cvar_num_shadow_cascades = AutoCVar_Int{"r.Shadow.NumCascades", "Number of shadow cascades", 4};

    static auto cvar_shadow_cascade_resolution = AutoCVar_Int{
        "r.Shadow.CSM.CascadeResolution",
        "Resolution of one cascade in the shadowmap", 2048
    };

    static auto cvar_max_shadow_distance = AutoCVar_Float{"r.Shadow.Distance", "Maximum distance of shadows", 8192};

    static auto cvar_shadow_cascade_split_lambda = AutoCVar_Float{
        "r.Shadow.CSM.CascadeSplitLambda",
        "Factor to use when calculating shadow cascade splits", 0.996
    };

    static auto cvar_shadow_samples = AutoCVar_Int{
        "r.Shadow.RT.NumSamples", "Number of rays to send for ray traced shadows", 1
    };

    DirectionalLight::DirectionalLight() {
        logger = SystemInterface::get().get_logger("SunLight");

        auto& allocator = RenderBackend::get().get_global_allocator();
        sun_buffer = allocator.create_buffer(
            "Sun Constant Buffer",
            sizeof(SunLightConstants),
            BufferUsage::UniformBuffer
            );
        world_to_ndc_matrices_buffer = allocator.create_buffer(
            "sun_world_to_ndc_matrices",
            sizeof(glm::mat4) * cvar_num_shadow_cascades.Get(),
            BufferUsage::UniformBuffer);

        auto& backend = RenderBackend::get();
        pipeline = backend.begin_building_pipeline("Sun Light")
                          .set_vertex_shader("shaders/common/fullscreen.vert.spv")
                          .set_fragment_shader("shaders/lighting/directional_light.frag.spv")
                          .set_depth_state(
                              DepthStencilState{
                                  .enable_depth_write = false,
                                  .compare_op = VK_COMPARE_OP_LESS
                              }
                              )
                          .set_blend_state(
                              0,
                              {
                                  .blendEnable = VK_TRUE,
                                  .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR,
                                  .dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR,
                                  .colorBlendOp = VK_BLEND_OP_ADD,
                                  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                              }
                              )
                          .build();

        // Hardware PCF sampler
        shadowmap_sampler = allocator.get_sampler(
            VkSamplerCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                .compareEnable = VK_TRUE,
                .compareOp = VK_COMPARE_OP_LESS,
                .minLod = 0,
                .maxLod = VK_LOD_CLAMP_NONE,
            }
            );

        for(auto i = 0; i < cvar_num_shadow_cascades.Get(); i++) {
            constants.cascade_matrices[i] = float4x4{1};
            constants.cascade_inverse_matrices[i] = glm::inverse(constants.cascade_matrices[i]);
        }
    }

    void DirectionalLight::update_shadow_cascades(const SceneView& view) {
        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        if(cvar_sun_shadow_mode.get() != SunShadowMode::CascadedShadowMaps) {
            if(shadowmap_handle != nullptr) {
                allocator.destroy_texture(shadowmap_handle);
                shadowmap_handle = nullptr;
            }
            return;
        }

        const auto num_cascades = static_cast<uint32_t>(cvar_num_shadow_cascades.Get());
        const auto max_shadow_distance = static_cast<float>(cvar_max_shadow_distance.Get());
        const auto cascade_split_lambda = static_cast<float>(cvar_shadow_cascade_split_lambda.Get());

        // Shadow frustum fitting code based on
        // https://github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmappingcascade/shadowmappingcascade.cpp#L637,
        // adapted for infinite projection

        /*
         * Algorithm:
         * - Transform frustum corners from NDC to worldspace
         *  May need to use a z of 0.5 for the far points, because infinite projection
         * - Get the direction vectors in the viewspace z for each frustum corner
         * - Multiply by each cascade's begin and end distance to get the eight points of the frustum that the cascade must cover
         * - Transform points into lightspace, calculate min and max x y and max z
         * - Fit shadow frustum to those bounds, adjusting frustum's view matrix to keep the frustum centered
         */

        const auto z_near = view.get_near();
        const auto clip_range = z_near + max_shadow_distance;
        const auto ratio = clip_range / z_near;

        auto cascade_splits = eastl::fixed_vector<float, 4>{};
        cascade_splits.resize(num_cascades);

        const auto texel_scale = 2.f / static_cast<float>(cvar_shadow_cascade_resolution.Get());
        const auto inverse_texel_scale = 1.f / texel_scale;

        // Calculate split depths based on view camera frustum
        // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
        // Also based on Sasha Willem's Vulkan examples
        for(uint32_t i = 0; i < num_cascades; i++) {
            float p = (static_cast<int32_t>(i) + 1) / static_cast<float>(num_cascades);
            float log = z_near * std::pow(ratio, p);
            float uniform = z_near + max_shadow_distance * p;
            float d = cascade_split_lambda * (log - uniform) + uniform;
            cascade_splits[i] = (d - z_near) / clip_range;
        }

        auto last_split_distance = z_near;
        for(auto i = 0u; i < num_cascades; i++) {
            const auto split_distance = cascade_splits[i];

            auto frustum_corners = eastl::array{
                glm::vec3{-1.0f, 1.0f, -1.0f},
                glm::vec3{1.0f, 1.0f, -1.0f},
                glm::vec3{1.0f, -1.0f, -1.0f},
                glm::vec3{-1.0f, -1.0f, -1.0f},
                glm::vec3{-1.0f, 1.0f, 1.0f},
                glm::vec3{1.0f, 1.0f, 1.0f},
                glm::vec3{1.0f, -1.0f, 1.0f},
                glm::vec3{-1.0f, -1.0f, 1.0f},
            };

            // Construct a new projection matrix for the region of the frustum this cascade
            // zNear and zFar intentionally reversed because reverse-z. Probably doesn't mathematically matter, but it
            // keeps the contentions consistent
            const auto projection_matrix = glm::perspectiveFov(
                view.get_fov(),
                view.get_aspect_ratio(),
                1.f,
                last_split_distance * max_shadow_distance,
                split_distance * max_shadow_distance
                );

            const auto inverse_camera = glm::inverse(projection_matrix * view.get_gpu_data().view);

            for(auto& corner : frustum_corners) {
                const auto transformed_corner = inverse_camera * glm::vec4{corner, 1.f};
                corner = glm::vec3{transformed_corner} / transformed_corner.w;
            }

            // Get frustum center
            auto frustum_center = glm::vec3{0};
            for(const auto& corner : frustum_corners) {
                frustum_center += corner;
            }
            frustum_center /= frustum_corners.size();

            // Snap to texel
            const auto projected_center = constants.cascade_matrices[i] * float4{frustum_center, 1};
            const auto snapped_xy = round(
                                        inverse_texel_scale * float2{projected_center} / projected_center.w) *
                                    texel_scale;
            const auto z = projected_center.z / projected_center.w;
            auto corrected_center = constants.cascade_inverse_matrices[i] * float4{snapped_xy, z, 1};
            corrected_center /= corrected_center.w;
            frustum_center = float3{corrected_center};

            // Fit a sphere to the frustum
            float radius = 0.f;
            for(const auto& corner : frustum_corners) {
                const auto distance = glm::length(corner - frustum_center);
                radius = glm::max(radius, distance);
            }

            radius *= 2;

            // Shadow cascade frustum
            const auto light_dir = glm::normalize(glm::vec3{constants.direction_and_tan_size});

            const auto light_view_matrix = glm::lookAt(
                frustum_center - light_dir * radius,
                frustum_center,
                glm::vec3{0.f, 1.f, 0.f}
                );
            const auto light_projection_matrix = glm::ortho(-radius, radius, -radius, radius, 0.f, radius + radius);

            // Store split distance and matrix in cascade
            constants.data[i] = glm::vec4{};
            constants.data[i].x = split_distance * clip_range * -1;
            constants.cascade_matrices[i] = light_projection_matrix * light_view_matrix;
            constants.cascade_inverse_matrices[i] = glm::inverse(constants.cascade_matrices[i]);

            last_split_distance = cascade_splits[i];
        }

        const auto csm_resolution = static_cast<uint32_t>(cvar_shadow_cascade_resolution.Get());
        constants.csm_resolution.x = csm_resolution;
        constants.csm_resolution.y = csm_resolution;

        world_to_ndc_matrices.clear();
        world_to_ndc_matrices.reserve(num_cascades);
        for(const auto& matrix : constants.cascade_matrices) {
            world_to_ndc_matrices.emplace_back(matrix);
        }

        sun_buffer_dirty = true;
    }

    void DirectionalLight::set_direction(const glm::vec3& direction) {
        constants.direction_and_tan_size = glm::vec4{glm::normalize(direction), tan(glm::radians(angular_size))};
        sun_buffer_dirty = true;
    }

    void DirectionalLight::set_color(const glm::vec4 color) {
        constants.color = color;
        sun_buffer_dirty = true;
    }

    void DirectionalLight::update_buffer(ResourceUploadQueue& queue) {
        // Write the data to the buffer
        // This is NOT safe. We'll probably write data while the GPU is reading data
        // A better solution might use virtual resources in the frontend and assign real resources
        // just-in-time. That'd solve sync without making the frontend care about frames. We could also
        // just have the frontend care about frames...

        if(constants.shadow_mode != static_cast<uint32_t>(cvar_sun_shadow_mode.get())) {
            constants.shadow_mode = static_cast<uint32_t>(cvar_sun_shadow_mode.get());
            sun_buffer_dirty = true;
        }

        const auto num_samples = static_cast<float>(cvar_shadow_samples.Get());
        if(constants.num_shadow_samples != num_samples) {
            constants.num_shadow_samples = num_samples;
            sun_buffer_dirty = true;
        }

        if(sun_buffer_dirty) {
            queue.upload_to_buffer(sun_buffer, constants);
            if(!world_to_ndc_matrices.empty()) {
                queue.upload_to_buffer(world_to_ndc_matrices_buffer, eastl::span{world_to_ndc_matrices});
            }

            sun_buffer_dirty = false;
        }
    }

    BufferHandle DirectionalLight::get_constant_buffer() const {
        return sun_buffer;
    }

    GraphicsPipelineHandle DirectionalLight::get_pipeline() const {
        return pipeline;
    }

    glm::vec3 DirectionalLight::get_direction() const {
        return glm::normalize(glm::vec3{constants.direction_and_tan_size});
    }

    void DirectionalLight::render_shadows(
        RenderGraph& graph, const GBuffer& gbuffer, const RenderScene& scene, const TextureHandle noise
        ) {
        if(shadow_mask_texture == nullptr) {
            shadow_mask_texture = RenderBackend::get().get_global_allocator().create_texture(
                "sun_shadow_mask",
                {
                    .format = VK_FORMAT_R8_UNORM,
                    .resolution = gbuffer.depth->get_resolution(),
                    .usage = TextureUsage::RenderTarget,
                    .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                }
                );
        }

        if(cvar_sun_shadow_mode.get() == SunShadowMode::RayTracing && !RenderBackend::get().supports_ray_tracing()) {
            cvar_sun_shadow_mode.set(SunShadowMode::CascadedShadowMaps);
        }

        switch(cvar_sun_shadow_mode.get()) {
        case SunShadowMode::CascadedShadowMaps:
            render_shadowmaps(graph, gbuffer, scene);
            break;

        case SunShadowMode::RayTracing:
            ray_trace_shadows(graph, gbuffer, scene, noise);
            break;

        default:
            // This page intentionally left blank
            break;
        }
    }

    void DirectionalLight::get_resource_usages(
        BufferUsageList& buffers, TextureUsageList& textures
        ) const {
        textures.emplace_back(
            TextureUsageToken{
                .texture = shadow_mask_texture,
                .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
    }

    SunShadowMode DirectionalLight::get_shadow_mode() {
        return cvar_sun_shadow_mode.get();
    }

    void DirectionalLight::render_shadowmaps(
        RenderGraph& graph, const GBuffer& gbuffer, const RenderScene& scene
        ) {

        auto& backend = RenderBackend::get();

        auto& allocator = backend.get_global_allocator();
        if(shadowmap_handle == nullptr) {
            shadowmap_handle = allocator.create_texture(
                "Sun shadowmap",
                {VK_FORMAT_D16_UNORM,
                 glm::uvec2{cvar_shadow_cascade_resolution.Get(), cvar_shadow_cascade_resolution.Get()},
                 1,
                 TextureUsage::RenderTarget,
                 static_cast<uint32_t>(cvar_num_shadow_cascades.Get())});
        }

        const auto& pipelines = scene.get_material_storage().get_pipelines();
        const auto shadow_pso = pipelines.get_shadow_pso();
        const auto shadow_masked_pso = pipelines.get_shadow_masked_pso();

        const auto solid_set = backend.get_transient_descriptor_allocator()
                                      .build_set(shadow_pso, 0)
                                      .bind(scene.get_primitive_buffer())
                                      .bind(world_to_ndc_matrices_buffer)
                                      .build();

        const auto masked_set = backend.get_transient_descriptor_allocator()
                                       .build_set(shadow_masked_pso, 0)
                                       .bind(scene.get_primitive_buffer())
                                       .bind(world_to_ndc_matrices_buffer)
                                       .build();

        graph.add_render_pass({
            .name = "Sun shadow",
            .descriptor_sets = {masked_set},
            .depth_attachment = RenderingAttachmentInfo{
                .image = shadowmap_handle,
                .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .clear_value = {.depthStencil = {.depth = 1.f}}
            },
            .view_mask = 0x000F,
            .execute = [&](CommandBuffer& commands) {
                commands.bind_descriptor_set(0, solid_set);

                scene.draw_opaque(commands, shadow_pso);

                commands.bind_descriptor_set(0, masked_set);
                scene.draw_masked(commands, shadow_masked_pso);

                commands.clear_descriptor_set(0);
            }
        });

        if(shadow_mask_shadowmap_pso == nullptr) {
            shadow_mask_shadowmap_pso = backend.begin_building_pipeline("shadow_mask_shadowmap")
                                               .set_vertex_shader("shaders/common/fullscreen.vert.spv")
                                               .set_fragment_shader(
                                                   "shaders/lighting/directional_light_shadow.frag.spv")
                                               .set_depth_state(
                                                   DepthStencilState{
                                                       .enable_depth_test = false,
                                                       .enable_depth_write = false,
                                                   }
                                                   )
                                               .build();
        }

        const auto shadowmask_set = backend.get_transient_descriptor_allocator()
                                           .build_set(shadow_mask_shadowmap_pso, 0)
                                           .bind(gbuffer.normals)
                                           .bind(gbuffer.depth)
                                           .bind(shadowmap_handle, shadowmap_sampler)
                                           .bind(sun_buffer)
                                           .bind(scene.get_player_view().get_buffer())
                                           .build();

        graph.add_render_pass({
            .name = "sun_shadow_sampling",
            .descriptor_sets = {shadowmask_set},
            .color_attachments = {{.image = shadow_mask_texture}},
            .execute = [&](CommandBuffer& commands) {
                commands.bind_pipeline(shadow_mask_shadowmap_pso);
                commands.bind_descriptor_set(0, shadowmask_set);

                commands.draw_triangle();

                commands.clear_descriptor_set(0);
            }
        });
    }

    void DirectionalLight::ray_trace_shadows(
        RenderGraph& graph, const GBuffer& gbuffer, const RenderScene& scene, const TextureHandle noise
        ) {
        auto& backend = RenderBackend::get();

        if(rt_shadow_pipeline == nullptr) {
            rt_shadow_pipeline = backend.get_pipeline_cache()
                                        .create_ray_tracing_pipeline(
                                            "shaders/lighting/directional_light_shadow.rt.spv",
                                            true);
        }

        graph.add_pass({
            .name = "clear_sun_shadow_uav",
            .textures = {
                {
                    .texture = shadow_mask_texture,
                    .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                }
            },
            .execute = [&](const CommandBuffer& commands) {
                commands.clear_texture(shadow_mask_texture);
            }
        });

        auto set = backend.get_transient_descriptor_allocator()
                          .build_set(rt_shadow_pipeline, 0)
                          .bind(scene.get_primitive_buffer())
                          .bind(sun_buffer)
                          .bind(scene.get_player_view().get_buffer())
                          .bind(scene.get_raytracing_scene().get_acceleration_structure())
                          .bind(shadow_mask_texture)
                          .bind(gbuffer.depth)
                          .bind(noise)
                          .build();

        graph.add_pass({
            .name = "ray_traced_sun_shadow",
            .buffers = {
                {
                    .buffer = rt_shadow_pipeline->shader_tables_buffer,
                    .stage = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                    .access = VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR,
                }
            },
            .descriptor_sets = {set},
            .execute = [&](CommandBuffer& commands) {
                commands.bind_pipeline(rt_shadow_pipeline);

                commands.bind_descriptor_set(0, set);
                commands.bind_descriptor_set(1, backend.get_texture_descriptor_pool().get_descriptor_set());

                commands.dispatch_rays({shadow_mask_texture->get_resolution()});

                commands.clear_descriptor_set(0);
                commands.clear_descriptor_set(1);
            }
        });
    }


    void DirectionalLight::render(CommandBuffer& commands, const SceneView& view) const {
        ZoneScoped;

        commands.begin_label("DirectionalLight::render");

        auto& backend = RenderBackend::get();

        commands.bind_pipeline(pipeline);

        const auto sun_descriptor_set = backend.get_transient_descriptor_allocator()
                                               .build_set(pipeline, 1)
                                               .bind(shadow_mask_texture)
                                               .bind(sun_buffer)
                                               .bind(view.get_buffer())
                                               .build();

        commands.bind_descriptor_set(1, sun_descriptor_set);

        commands.draw_triangle();

        commands.clear_descriptor_set(1);

        commands.end_label();
    }

    TextureHandle DirectionalLight::get_shadowmap_handle() const {
        return shadowmap_handle;
    }
}
