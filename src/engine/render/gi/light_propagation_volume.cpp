#include "light_propagation_volume.hpp"

#include <magic_enum.hpp>
#include <numbers>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "render/material_pipelines.hpp"
#include "render/material_storage.hpp"
#include "render/backend/pipeline_builder.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/render_graph.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_allocator.hpp"
#include "console/cvars.hpp"
#include "core/string_utils.hpp"
#include "core/system_interface.hpp"
#include "render/gbuffer.hpp"
#include "render/scene_view.hpp"
#include "render/render_scene.hpp"
#include "shared/vpl.hpp"
#include "shared/lpv.hpp"
#include "shared/view_info.hpp"

namespace render {
    static auto cvar_lpv_resolution = AutoCVar_Int{
        "r.GI.LPV.Resolution",
        "Resolution of one dimension of the light propagation volume", 32
    };

    static auto cvar_lpv_cell_size = AutoCVar_Float{
        "r.GI.LPV.CellSize",
        "Size in meters of one size of a LPV cell", 0.25
    };

    static auto cvar_lpv_num_cascades = AutoCVar_Int{
        "r.GI.LPV.NumCascades",
        "Number of cascades in the light propagation volume", 4
    };

    static auto cvar_lpv_num_propagation_steps = AutoCVar_Int{
        "r.GI.LPV.NumPropagationSteps",
        "Number of times to propagate lighting through the LPV", 16
    };

    static auto cvar_lpv_behind_camera_percent = AutoCVar_Float{
        "r.GI.LPV.PercentBehindCamera",
        "The percentage of the LPV that should be behind the camera. Not exact",
        0.1
    };

    static auto cvar_lpv_build_gv_mode = AutoCVar_Enum{
        "r.GI.LPV.GvBuildMode",
        "How to build the geometry volume.\n0 = Disable\n1 = Use the RSM depth buffer and last frame's depth buffer",
        GvBuildMode::Off
    };

    static auto cvar_lpv_rsm_resolution = AutoCVar_Int{
        "r.GI.LPV.RsmResolution",
        "Resolution for the RSM targets. Should be a multiple of 16",
        128
    };

    static auto cvar_lpv_use_compute_vpl_injection = AutoCVar_Int{
        "r.GI.LPV.ComputeVPL",
        "Whether to use a compute pipeline or a raster pipeline to inject VPLs into the LPVs",
        0
    };

    static auto cvar_lpv_vpl_visualization_size = AutoCVar_Float{
        "r.GI.LPV.VPL.VisualizationSize",
        "Size of one VPL, in pixels, when drawn in the visualization pass",
        32.f
    };

    static auto cvar_lpv_exposure = AutoCVar_Float{
        "r.GI.LPV.Exposure", "Exposure to use when applying the LPV to the scene", std::numbers::pi * 1000
    };

    static auto cvar_lpv_debug_mode = AutoCVar_Int{"r.GI.LPV.DebugMode", "0 = GV, 1 = VPLs", 1};

    static std::shared_ptr<spdlog::logger> logger;

    LightPropagationVolume::LightPropagationVolume() {
        ZoneScoped;

        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("LightPropagationVolume");
        }

        auto& backend = RenderBackend::get();

        auto& pipeline_cache = backend.get_pipeline_cache();
        clear_lpv_shader = pipeline_cache.create_pipeline("shader://gi/lpv/clear_lpv.comp.spv"_res);

        propagation_shader = pipeline_cache.create_pipeline("shader://gi/lpv/lpv_propagate.comp.spv"_res);

        linear_sampler = backend.get_global_allocator().get_sampler(
            {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                .anisotropyEnable = VK_TRUE,
                .maxAnisotropy = 16,
                .maxLod = VK_LOD_CLAMP_NONE,
                .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
            }
        );

        rsm_generate_vpls_pipeline = pipeline_cache.create_pipeline("shader://gi/lpv/rsm_generate_vpls.comp.spv"_res);

        if(cvar_lpv_use_compute_vpl_injection.get() == 0) {
            vpl_injection_pipeline = backend.begin_building_pipeline("VPL Injection")
                                            .set_topology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
                                            .set_vertex_shader("shader://gi/lpv/vpl_injection.vert.spv"_res)
                                            .set_fragment_shader("shader://gi/lpv/vpl_injection.frag.spv"_res)
                                            .set_num_attachments(3)
                                            .set_blend_mode(BlendMode::Additive)
                                            .build();
        } else {
            vpl_injection_compute_pipeline = pipeline_cache.create_pipeline("shader://gi/lpv/vpl_injection.comp.spv"_res);
        }

        inject_rsm_depth_into_gv_pipeline = backend.begin_building_pipeline("GV Injection")
                                                   .set_topology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
                                                   .set_vertex_shader("shader://gi/lpv/gv_injection.vert.spv"_res)
                                                   .set_fragment_shader("shader://gi/lpv/gv_injection.frag.spv"_res)
                                                   .set_depth_state(
                                                       {.enable_depth_test = VK_FALSE, .enable_depth_write = VK_FALSE}
                                                   )
                                                   .set_blend_state(
                                                       0,
                                                       {
                                                           .blendEnable = VK_TRUE,
                                                           .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                                           .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                                           .colorBlendOp = VK_BLEND_OP_MAX,
                                                           .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                                           .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                                           .alphaBlendOp = VK_BLEND_OP_MAX,
                                                           .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                           VK_COLOR_COMPONENT_G_BIT |
                                                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                                                       }
                                                   )
                                                   .build();
        inject_scene_depth_into_gv_pipeline = backend.begin_building_pipeline("Inject scene depth into GV")
                                                     .set_topology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
                                                     .use_imgui_vertex_layout()
                                                     .set_vertex_shader(
                                                         "shader://gi/lpv/inject_scene_depth_into_gv.vert.spv"_res)
                                                     .set_fragment_shader(
                                                         "shader://gi/lpv/inject_scene_depth_into_gv.frag.spv"_res)
                                                     .set_depth_state(
                                                         {.enable_depth_test = VK_FALSE, .enable_depth_write = VK_FALSE}
                                                     )
                                                     .set_blend_state(
                                                         0,
                                                         {
                                                             .blendEnable = VK_TRUE,
                                                             .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                                             .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                                             .colorBlendOp = VK_BLEND_OP_MAX,
                                                             .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                                             .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                                             .alphaBlendOp = VK_BLEND_OP_MAX,
                                                             .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                             VK_COLOR_COMPONENT_G_BIT |
                                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                                                         }
                                                     )
                                                     .build();

        lpv_render_shader = backend.begin_building_pipeline("LPV Rendering")
                                   .set_vertex_shader("shader://common/fullscreen.vert.spv"_res)
                                   .set_fragment_shader("shader://gi/lpv/overlay.frag.spv"_res)
                                   .set_depth_state(
                                       {
                                           .enable_depth_write = false,
                                           .compare_op = VK_COMPARE_OP_LESS
                                       })
                                   .set_blend_mode(BlendMode::Additive)
                                   .build();

        vpl_visualization_pipeline = backend.begin_building_pipeline("VPL Visualization")
                                            .set_topology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
                                            .set_vertex_shader("shader://gi/lpv/visualize_vpls.vert.spv"_res)
                                            .set_geometry_shader("shader://gi/lpv/visualize_vpls.geom.spv"_res)
                                            .set_fragment_shader("shader://gi/lpv/visualize_vpls.frag.spv"_res)
                                            .set_depth_state({.enable_depth_write = false})
                                            .build();

        init_resources(backend.get_global_allocator());
    }

    LightPropagationVolume::~LightPropagationVolume() {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        allocator.destroy_texture(rsm_flux_target);
        allocator.destroy_texture(rsm_normals_target);
        allocator.destroy_texture(rsm_depth_target);

        allocator.destroy_texture(lpv_a_red);
        allocator.destroy_texture(lpv_a_green);
        allocator.destroy_texture(lpv_a_blue);
        allocator.destroy_texture(lpv_b_red);
        allocator.destroy_texture(lpv_b_green);
        allocator.destroy_texture(lpv_b_blue);
        allocator.destroy_texture(geometry_volume_handle);

        allocator.destroy_buffer(cascade_data_buffer);
        allocator.destroy_buffer(vp_matrix_buffer);
    }

    void LightPropagationVolume::pre_render(
        RenderGraph& graph, const SceneView& view, const RenderWorld& world, TextureHandle noise_tex
    ) {
        clear_volume(graph);

        update_cascade_transforms(view, world.get_sun_light());

        // VPL cloud generation

        inject_indirect_sun_light(graph, world);
    }

    void LightPropagationVolume::post_render(
        RenderGraph& graph, const SceneView& view, const RenderWorld& world, const GBuffer& gbuffer,
         const TextureHandle motion_vectors, const TextureHandle noise_tex
    ) {
        if(cvar_lpv_build_gv_mode.get() == GvBuildMode::DepthBuffers) {
            build_geometry_volume_from_scene_view(
                graph,
                gbuffer.depth,
                gbuffer.normals,
                view.get_buffer()
            );
        }

        propagate_lighting(graph);
    }

    void LightPropagationVolume::get_lighting_resource_usages(
        TextureUsageList& textures, BufferUsageList& buffers
    ) const {
        textures.emplace_back(
            lpv_a_red,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        textures.emplace_back(
            lpv_a_green,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        textures.emplace_back(
            lpv_a_blue,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void LightPropagationVolume::render_to_lit_scene(
        CommandBuffer& commands,
        const BufferHandle view_buffer,
        const BufferHandle sun_buffer,
        const TextureHandle ao_tex,
        const TextureHandle noise_tex
    ) const {
        commands.begin_label("LightPropagationVolume::add_lighting_to_scene");

        auto& backend = RenderBackend::get();
        const auto lpv_descriptor = backend.get_transient_descriptor_allocator().build_set(lpv_render_shader, 1)
                                           .bind(lpv_a_red, linear_sampler)
                                           .bind(lpv_a_green, linear_sampler)
                                           .bind(lpv_a_blue, linear_sampler)
                                           .bind(cascade_data_buffer)
                                           .bind(view_buffer)
                                           .bind(ao_tex, linear_sampler)
                                           .bind(sun_buffer)
                                           .bind(noise_tex)
                                           .build();

        commands.bind_descriptor_set(1, lpv_descriptor);

        commands.bind_pipeline(lpv_render_shader);

        commands.set_push_constant(0, static_cast<uint32_t>(cvar_lpv_num_cascades.get()));
        commands.set_push_constant(1, static_cast<float>(cvar_lpv_exposure.get()));

        commands.draw_triangle();

        commands.clear_descriptor_set(1);

        commands.end_label();
    }

    void LightPropagationVolume::render_volumetrics(
        RenderGraph& render_graph, const SceneView& player_view, const RenderWorld& world, const GBuffer& gbuffer, const
        TextureHandle lit_scene_handle
    ) {
        auto& backend = RenderBackend::get();

        if(fog_pipeline == nullptr) {
            fog_pipeline = backend.begin_building_pipeline("fog")
                                  .set_vertex_shader("shader://common/fullscreen.vert.spv"_res)
                                  .set_fragment_shader("shader://gi/lpv/fog.frag.spv"_res)
                                  .set_blend_mode(BlendMode::Mix)
                                  .build();
        }

        const auto set = backend.get_persistent_descriptor_allocator()
                                .build_set(fog_pipeline, 0)
                                .bind(player_view.get_buffer())
                                .bind(cascade_data_buffer)
                                .bind(gbuffer.depth)
                                .bind(lpv_a_red, linear_sampler)
                                .bind(lpv_a_green, linear_sampler)
                                .bind(lpv_a_blue, linear_sampler)
                                .build();

        render_graph.add_render_pass(
            {
                .name = "lpv_atmospherics",
                .descriptor_sets = {set},
                .color_attachments = {{.image = lit_scene_handle}},
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_pipeline(fog_pipeline);
                    commands.bind_descriptor_set(0, set);
                    commands.set_push_constant(0, world.get_fog_strength());

                    commands.draw_triangle();

                    commands.clear_descriptor_set(0);
                }
            });
    }

    void LightPropagationVolume::draw_debug_overlays(
        RenderGraph& graph, const SceneView& view, const GBuffer& gbuffer, const TextureHandle lit_scene_texture
    ) {
        switch(cvar_lpv_debug_mode.get()) {
        case 0:
            visualize_geometry_volume(graph, view, lit_scene_texture, gbuffer.depth);
            break;
        case 1:
            visualize_vpls(graph, view.get_buffer(), lit_scene_texture, gbuffer.depth);
            break;
        }
    }

    void LightPropagationVolume::init_resources(ResourceAllocator& allocator) {
        ZoneScoped;

        const auto size = cvar_lpv_resolution.get();
        const auto num_cascades = static_cast<uint32_t>(cvar_lpv_num_cascades.get());

        const auto texture_resolution = glm::uvec3{size * num_cascades, size, size};

        lpv_a_red = allocator.create_volume_texture(
            "LPV Red A",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );
        lpv_a_green = allocator.create_volume_texture(
            "LPV Green A",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );
        lpv_a_blue = allocator.create_volume_texture(
            "LPV Blue A",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );
        lpv_b_red = allocator.create_volume_texture(
            "LPV Red B",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );
        lpv_b_green = allocator.create_volume_texture(
            "LPV Green B",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );
        lpv_b_blue = allocator.create_volume_texture(
            "LPV Blue B",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );

        geometry_volume_handle = allocator.create_volume_texture(
            "Geometry Volume",
            VK_FORMAT_R16G16B16A16_SFLOAT,
            texture_resolution,
            1,
            TextureUsage::StorageImage
        );

        cascade_data_buffer = allocator.create_buffer(
            "LPV Cascade Data",
            sizeof(LPVCascadeMatrices) * num_cascades,
            BufferUsage::UniformBuffer
        );

        vp_matrix_buffer = allocator.create_buffer(
            "rsm_vp_matrices",
            sizeof(glm::mat4) * num_cascades,
            BufferUsage::UniformBuffer);

        const auto num_vpls = cvar_lpv_rsm_resolution.get() * cvar_lpv_rsm_resolution.get() / 4;

        auto& backend = RenderBackend::get();
        auto& upload_queue = backend.get_upload_queue();

        cascades.resize(num_cascades);
        uint32_t cascade_index = 0;
        for(auto& cascade : cascades) {
            const auto vpl_count_name = fmt::format("Cascade {} VPL count", cascade_index);
            cascade.vpl_count_buffer = allocator.create_buffer(
                vpl_count_name.c_str(),
                sizeof(VkDrawIndirectCommand),
                BufferUsage::IndirectBuffer
            );

            upload_queue.upload_to_buffer(
                cascade.vpl_count_buffer,
                VkDrawIndirectCommand{
                    .vertexCount = 0,
                    .instanceCount = 2,
                    .firstVertex = 0,
                    .firstInstance = 0
                });

            const auto vpl_list_name = fmt::format("Cascade {} VPL List", cascade_index);
            cascade.vpl_buffer = allocator.create_buffer(
                vpl_list_name.c_str(),
                static_cast<uint64_t>(sizeof(PackedVPL) * num_vpls),
                BufferUsage::StorageBuffer
            );
            cascade_index++;
        }

        const auto resolution = glm::uvec2{static_cast<uint32_t>(cvar_lpv_rsm_resolution.get())};
        rsm_flux_target = allocator.create_texture(
            "RSM Flux",
            {
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .resolution = resolution,
                .num_mips = 1,
                .usage = TextureUsage::RenderTarget,
                .num_layers = num_cascades,
            }
        );
        rsm_normals_target = allocator.create_texture(
            "RSM Normals",
            {
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .resolution = resolution,
                .num_mips = 1,
                .usage = TextureUsage::RenderTarget,
                .num_layers = num_cascades,
            }
        );
        rsm_depth_target = allocator.create_texture(
            "RSM Depth",
            {
                .format = VK_FORMAT_D16_UNORM,
                .resolution = resolution,
                .num_mips = 1,
                .usage = TextureUsage::RenderTarget,
                .num_layers = num_cascades,
            }
        );
    }

    void LightPropagationVolume::update_cascade_transforms(const SceneView& view, const DirectionalLight& light) {
        ZoneScoped;

        const auto num_cells = cvar_lpv_resolution.get();
        const auto base_cell_size = cvar_lpv_cell_size.GetFloat();

        const auto& view_position = view.get_position();

        // Position the LPV slightly in front of the view. We want some of the LPV to be behind it for reflections and such

        const auto num_cascades = cvar_lpv_num_cascades.get();

        const auto offset_distance_scale = 0.5f - cvar_lpv_behind_camera_percent.GetFloat();

        const auto texel_scale = 4.f / static_cast<float>(cvar_lpv_rsm_resolution.get());
        const auto inverse_texel_scale = 1.f / texel_scale;

        auto make_rsm_matrix = [&](const float3 center, const float pullback_distance, const float half_size) {

            const auto rsm_view_start = center - light.get_direction() * pullback_distance;
            const auto rsm_view_matrix = glm::lookAt(rsm_view_start, center, glm::vec3{0, 1, 0});
            const auto rsm_projection_matrix = glm::ortho(
                -half_size,
                half_size,
                -half_size,
                half_size,
                0.f,
                pullback_distance * 2.f
            );

            return rsm_projection_matrix * rsm_view_matrix;
        };

        for(int32_t cascade_index = 0; cascade_index < num_cascades; cascade_index++) {
            const auto cell_size = base_cell_size * static_cast<float>(glm::pow(2.f, cascade_index));
            const auto cascade_size = static_cast<float>(num_cells) * cell_size;

            // Offset the center point of the cascade by 20% of the length of one side
            // When the camera is aligned with the X or Y axis, this will offset the cascade by 20% of its length. 30% of it
            // will be behind the camera, 70% of it will be in front. This feels reasonable
            // When the camera is 45 degrees off of the X or Y axis, the cascade will have more of itself behind the camera
            // This might be fine

            const auto offset_distance = cascade_size * offset_distance_scale;
            const auto offset = view_position + view.get_forward() * offset_distance;

            // Round to the cell size to prevent flickering
            const auto snapped_offset = glm::round(offset / glm::vec3{cell_size}) * cell_size;

            const auto scale_factor = 1.0f / cascade_size;

            constexpr auto bias_mat = glm::mat4{
                0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.5f, 0.0f,
                0.5f, 0.5f, 0.5f, 1.0f
            };

            auto& cascade = cascades[cascade_index];
            cascade.world_to_cascade = glm::mat4{1.f};
            cascade.world_to_cascade = glm::scale(cascade.world_to_cascade, glm::vec3{scale_factor});
            cascade.world_to_cascade = glm::translate(cascade.world_to_cascade, -snapped_offset);
            cascade.world_to_cascade = bias_mat * cascade.world_to_cascade;

            const auto half_cascade_size = cascade_size / 2.f;
            const auto half_rsm_size = half_cascade_size * std::numbers::sqrt3_v<float>;
            const auto rsm_pullback_distance = cascade_size * std::numbers::sqrt3_v<float>;

            // cascade.rsm_vp = make_rsm_matrix(snapped_offset, rsm_pullback_distance, half_rsm_size);

            // Project the RSM's center, snap to a texel, unproject, reconstruct the matrix
            const auto projected_center = cascade.rsm_vp * float4{snapped_offset, 1};
            const auto snapped_xy = round(
                inverse_texel_scale * float2{projected_center} / projected_center.w) * texel_scale;
            const auto z = projected_center.z / projected_center.w;
            auto corrected_center = glm::inverse(cascade.rsm_vp) * float4{snapped_xy, z, 1};
            corrected_center /= corrected_center.w;

            cascade.rsm_vp = make_rsm_matrix(float3{corrected_center}, rsm_pullback_distance, half_rsm_size);

            cascade.min_bounds = snapped_offset - glm::vec3{half_cascade_size};
            cascade.max_bounds = snapped_offset + glm::vec3{half_cascade_size};
        }

        update_buffers();
    }

    void LightPropagationVolume::update_buffers() const {
        ZoneScoped;

        auto cascade_matrices = eastl::vector<LPVCascadeMatrices>{};
        cascade_matrices.reserve(cascades.size());
        for(const auto& cascade : cascades) {
            cascade_matrices.emplace_back(
                LPVCascadeMatrices{
                    .rsm_vp = cascade.rsm_vp,
                    .inverse_rsm_vp = glm::inverse(cascade.rsm_vp),
                    .world_to_cascade = cascade.world_to_cascade,
                    .cascade_to_world = glm::inverse(cascade.world_to_cascade)
                }
            );
        }

        auto& queue = RenderBackend::get().get_upload_queue();
        queue.upload_to_buffer(cascade_data_buffer, eastl::span{cascade_matrices});

        auto matrices = eastl::vector<glm::mat4>{};
        matrices.reserve(cascades.size());
        for(const auto& cascade : cascades) {
            matrices.emplace_back(cascade.rsm_vp);
        }
        queue.upload_to_buffer(vp_matrix_buffer, eastl::span{matrices});
    }

    void LightPropagationVolume::inject_indirect_sun_light(
        RenderGraph& graph, const RenderWorld& world
    ) const {
        ZoneScoped;

        // For each LPV cascade:
        // Rasterize RSM render targets for the cascade, then render a fullscreen triangle over them. That triangle's FS
        // will select the brightest VPL in each subgroup, and write it to a buffer
        // Then, we dispatch one VS invocation for each VPL. We use a geometry shader to send the VPL to the correct part
        // of the cascade
        // Why do this? I want to keep the large, heavy RSM targets in tile memory. I have to use an FS for VPL extraction
        // because only an FS can read from tile memory. I reduce the 1024x1024 RSM to only 65k VPLs, so there's much less
        // data flushed to main memory
        // Unfortunately there's a sync point between the VPL generation FS and the VPL injection pass. Not sure if I can get
        // rid of that

        graph.begin_label("LPV indirect sun light injection");

        auto view_mask = 0u;
        for(auto cascade_index = 0u; cascade_index < cascades.size(); cascade_index++) {
            view_mask |= 1 << cascade_index;
        }

        const auto& pipelines = world.get_material_storage().get_pipelines();
        const auto rsm_pso = pipelines.get_rsm_pso();
        const auto rsm_masked_pso = pipelines.get_rsm_masked_pso();

        auto& backend = RenderBackend::get();
        const auto set = backend.get_transient_descriptor_allocator()
                                .build_set(rsm_pso, 0)
                                .bind(world.get_primitive_buffer())
                                .bind(vp_matrix_buffer)
                                .bind(world.get_sun_light().get_constant_buffer())
                                .build();

        graph.add_render_pass(
            DynamicRenderingPass{
                .name = "Render RSM",
                .descriptor_sets = {set},
                .color_attachments = {
                    {
                        .image = rsm_flux_target,
                        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .clear_value = {.color = {.float32 = {0, 0, 0, 0}}}
                    },
                    {
                        .image = rsm_normals_target,
                        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .clear_value = {.color = {.float32 = {0.5, 0.5, 1.0, 0}}}
                    }
                },
                .depth_attachment = RenderingAttachmentInfo{
                    .image = rsm_depth_target,
                    .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                    .clear_value = {.depthStencil = {.depth = 1.f}}
                },
                .view_mask = view_mask,
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_descriptor_set(0, set);

                    world.draw_opaque(commands, rsm_pso);

                    world.draw_masked(commands, rsm_masked_pso);

                    commands.clear_descriptor_set(0);
                }
            });

        for(auto cascade_index = 0u; cascade_index < cascades.size(); cascade_index++) {
            const auto& cascade = cascades[cascade_index];
            graph.add_pass(
                ComputePass{
                    .name = "Clear VPL Count",
                    .buffers = {
                        {
                            .buffer = cascade.vpl_count_buffer,
                            .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .access = VK_ACCESS_2_TRANSFER_WRITE_BIT
                        }
                    },
                    .execute = [buffer = cascade.vpl_count_buffer](const CommandBuffer& commands) {
                        commands.fill_buffer(buffer, 0, 0, sizeof(uint32_t));
                    }
                });


            auto descriptor_set = backend.get_transient_descriptor_allocator()
                                         .build_set(rsm_generate_vpls_pipeline, 0)
                                         .bind(rsm_flux_target, backend.get_default_sampler())
                                         .bind(rsm_normals_target, backend.get_default_sampler())
                                         .bind(rsm_depth_target, backend.get_default_sampler())
                                         .bind(cascade_data_buffer)
                                         .build();

            struct VplPipelineConstants {
                DeviceAddress count_buffer_address;
                DeviceAddress vpl_buffer_address;
                int32_t cascade_index;
                uint32_t rsm_resolution;
                float lpv_cell_size;
            };

            const auto resolution = glm::uvec2{static_cast<uint32_t>(cvar_lpv_rsm_resolution.get())};
            const auto dispatch_size = resolution / glm::uvec2{2};
            // Each thread selects one VPL from a 2x2 filter on the RSM

            graph.add_compute_dispatch<VplPipelineConstants>(
                ComputeDispatch<VplPipelineConstants>{
                    .name = "Extract VPLs",
                    .descriptor_sets = {descriptor_set},
                    .buffers = {
                        {
                            .buffer = cascade.vpl_count_buffer,
                            .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT

                        },
                        {
                            .buffer = cascade.vpl_buffer,
                            .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            .access = VK_ACCESS_2_SHADER_WRITE_BIT
                        }

                    },
                    .push_constants = VplPipelineConstants{
                        .count_buffer_address = cascade.vpl_count_buffer->address,
                        .vpl_buffer_address = cascade.vpl_buffer->address,
                        .cascade_index = static_cast<int32_t>(cascade_index),
                        .rsm_resolution = resolution.x,
                        .lpv_cell_size = static_cast<float>(cvar_lpv_cell_size.get())
                    },
                    .num_workgroups = {(dispatch_size + glm::uvec2{7}) / glm::uvec2{8}, 1},
                    .compute_shader = rsm_generate_vpls_pipeline
                });
        }

        for(auto cascade_index = 0u; cascade_index < cascades.size(); cascade_index++) {
            const auto& cascade = cascades[cascade_index];
            dispatch_vpl_injection_pass(graph, cascade_index, cascade, world.get_sun_light().get_constant_buffer());

        }

        if(cvar_lpv_build_gv_mode.get() == GvBuildMode::DepthBuffers) {
            for(auto cascade_index = 0u; cascade_index < cascades.size(); cascade_index++) {
                const auto& cascade = cascades[cascade_index];
                inject_rsm_depth_into_cascade_gv(graph, cascade, cascade_index);
            }
        }
        graph.end_label();
    }

    void LightPropagationVolume::dispatch_vpl_injection_pass(
        RenderGraph& graph, const uint32_t cascade_index, const CascadeData& cascade,
        const BufferHandle sun_light_buffer
    ) const {
        ZoneScoped;

        auto& backend = RenderBackend::get();
        auto descriptor_set = backend.get_transient_descriptor_allocator()
                                     .build_set(vpl_injection_pipeline, 0)
                                     .bind(cascade_data_buffer)
                                     .bind(sun_light_buffer)
                                     .bind(cascade.vpl_buffer)
                                     .build();

        const auto num_vpls = cvar_lpv_rsm_resolution.get() * cvar_lpv_rsm_resolution.get() / 2;

        if(cvar_lpv_use_compute_vpl_injection.get() == 0) {
            graph.add_render_pass(
                DynamicRenderingPass{
                    .name = "VPL Injection",
                    .buffers = {
                        {cascade_data_buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT},
                        {cascade.vpl_buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
                        {
                            .buffer = cascade.vpl_count_buffer,
                            .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                            .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT

                        }
                    },
                    .descriptor_sets = {descriptor_set},
                    .color_attachments = {
                        RenderingAttachmentInfo{
                            .image = lpv_a_red,
                            .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                            .store_op = VK_ATTACHMENT_STORE_OP_STORE
                        },
                        RenderingAttachmentInfo{
                            .image = lpv_a_green,
                            .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                            .store_op = VK_ATTACHMENT_STORE_OP_STORE
                        },
                        RenderingAttachmentInfo{
                            .image = lpv_a_blue,
                            .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
                            .store_op = VK_ATTACHMENT_STORE_OP_STORE
                        },
                    },
                    .view_mask = 0,
                    .execute = [descriptor_set, cascade, cascade_index, num_vpls, this](CommandBuffer& commands) {
                        commands.bind_descriptor_set(0, descriptor_set);

                        commands.set_push_constant(0, cascade_index);
                        commands.set_push_constant(1, static_cast<uint32_t>(cvar_lpv_num_cascades.get()));

                        commands.bind_pipeline(vpl_injection_pipeline);

                        commands.draw_indirect(cascade.vpl_count_buffer);

                        commands.clear_descriptor_set(0);
                    }
                });
        } else {
            struct VplInjectionConstants {
                DeviceAddress vpl_buffer_address;
                uint32_t cascade_index;
                uint32_t num_cascades;
            };

            graph.add_compute_dispatch<VplInjectionConstants>(
                ComputeDispatch<VplInjectionConstants>{
                    .name = "VPL Injection",
                    .descriptor_sets = {descriptor_set},
                    .buffers = {
                        {cascade_data_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT},
                        {cascade.vpl_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
                    },
                    .push_constants = VplInjectionConstants{
                        cascade.vpl_buffer->address,
                        cascade_index,
                        static_cast<uint32_t>(cvar_lpv_num_cascades.get())
                    },
                    .num_workgroups = {(num_vpls + 63) / 64, 1, 1},
                    .compute_shader = vpl_injection_compute_pipeline
                }
            );
        }
    }

    void LightPropagationVolume::clear_volume(RenderGraph& render_graph) const {
        ZoneScoped;

        auto& backend = RenderBackend::get();

        render_graph.add_pass(
            {
                .name = "LightPropagationVolume::clear_volume",
                .textures = {
                    {
                        .texture = lpv_a_red,
                        .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .layout = VK_IMAGE_LAYOUT_GENERAL
                    },
                    {
                        .texture = lpv_a_green,
                        .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, .
                        access = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .layout = VK_IMAGE_LAYOUT_GENERAL
                    },
                    {
                        .texture = lpv_a_blue,
                        .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .layout = VK_IMAGE_LAYOUT_GENERAL
                    },
                    {
                        .texture = geometry_volume_handle,
                        .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .layout = VK_IMAGE_LAYOUT_GENERAL,
                    }
                },
                .execute = [&](CommandBuffer& commands) {
                    auto descriptor_set = *vkutil::DescriptorBuilder::begin(
                                               backend,
                                               backend.get_transient_descriptor_allocator()
                                           )
                                           .bind_image(
                                               0,
                                               {
                                                   .image = lpv_a_red,
                                                   .image_layout = VK_IMAGE_LAYOUT_GENERAL
                                               },
                                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                               VK_SHADER_STAGE_COMPUTE_BIT
                                           )
                                           .bind_image(
                                               1,
                                               {
                                                   .image = lpv_a_green,
                                                   .image_layout = VK_IMAGE_LAYOUT_GENERAL
                                               },
                                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                               VK_SHADER_STAGE_COMPUTE_BIT
                                           )
                                           .bind_image(
                                               2,
                                               {
                                                   .image = lpv_a_blue,
                                                   .image_layout = VK_IMAGE_LAYOUT_GENERAL
                                               },
                                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                               VK_SHADER_STAGE_COMPUTE_BIT
                                           )
                                           .bind_image(
                                               3,
                                               {
                                                   .image = geometry_volume_handle,
                                                   .image_layout = VK_IMAGE_LAYOUT_GENERAL
                                               },
                                               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                               VK_SHADER_STAGE_COMPUTE_BIT
                                           )
                                           .build();

                    commands.bind_descriptor_set(0, descriptor_set);

                    commands.bind_pipeline(clear_lpv_shader);

                    commands.dispatch(cvar_lpv_num_cascades.get(), 32, 32);

                    commands.clear_descriptor_set(0);
                }
            }
        );
    }

    GvBuildMode LightPropagationVolume::get_build_mode() {
        return cvar_lpv_build_gv_mode.get();
    }

    void LightPropagationVolume::build_geometry_volume_from_scene_view(
        RenderGraph& graph, const TextureHandle depth_buffer, const TextureHandle normal_target,
        const BufferHandle view_uniform_buffer
    ) const {
        ZoneScoped;

        auto& backend = RenderBackend::get();
        const auto sampler = backend.get_default_sampler();
        const auto set = backend.get_transient_descriptor_allocator().build_set(inject_scene_depth_into_gv_pipeline, 0)
                                .bind(normal_target, sampler)
                                .bind(depth_buffer, sampler)
                                .bind(cascade_data_buffer)
                                .bind(view_uniform_buffer)
                                .build();

        graph.add_render_pass(
            {
                .name = "Inject scene depth into GV",
                .descriptor_sets = {set},
                .color_attachments = {{.image = geometry_volume_handle}},
                .execute = [&](CommandBuffer& commands) {
                    const auto effective_resolution = depth_buffer->get_resolution();

                    commands.bind_descriptor_set(0, set);

                    commands.set_push_constant(0, effective_resolution.x);
                    commands.set_push_constant(1, effective_resolution.y);
                    commands.set_push_constant(2, static_cast<uint32_t>(cvar_lpv_num_cascades.get()));

                    commands.bind_pipeline(inject_scene_depth_into_gv_pipeline);

                    commands.draw(effective_resolution.x * effective_resolution.y, 4);

                    commands.clear_descriptor_set(0);
                }
            });
    }

    void LightPropagationVolume::propagate_lighting(RenderGraph& render_graph) const {
        ZoneScoped;

        render_graph.begin_label("LPV Propagation");

        bool use_gv = false;

        auto& descriptor_allocator = RenderBackend::get().get_transient_descriptor_allocator();
        const auto a_to_b_set = descriptor_allocator
                                .build_set(propagation_shader, 0)
                                .bind(lpv_a_red)
                                .bind(lpv_a_green)
                                .bind(lpv_a_blue)
                                .bind(lpv_b_red)
                                .bind(lpv_b_green)
                                .bind(lpv_b_blue)
                                .bind(geometry_volume_handle, linear_sampler)
                                .build();

        const auto b_to_a_set = descriptor_allocator
                                .build_set(propagation_shader, 0)
                                .bind(lpv_b_red)
                                .bind(lpv_b_green)
                                .bind(lpv_b_blue)
                                .bind(lpv_a_red)
                                .bind(lpv_a_green)
                                .bind(lpv_a_blue)
                                .bind(geometry_volume_handle, linear_sampler)
                                .build();

        const auto num_cells = static_cast<uint32_t>(cvar_lpv_resolution.get());
        const auto num_cascades = static_cast<uint32_t>(cvar_lpv_num_cascades.get());
        const auto dispatch_size = glm::uvec3{num_cells * num_cascades, num_cells, num_cells} / glm::uvec3{4, 4, 4};

        struct PropagationConstants {
            uint32_t use_gv;
            uint32_t num_cascades;
            uint32_t num_cells;
        };

        const auto constants = PropagationConstants{
            .use_gv = use_gv ? 1u : 0u,
            .num_cascades = num_cascades,
            .num_cells = num_cells
        };

        for(auto step_index = 0; step_index < cvar_lpv_num_propagation_steps.get(); step_index += 2) {
            render_graph.add_compute_dispatch<PropagationConstants>(
                ComputeDispatch<PropagationConstants>{
                    .name = "Propagate lighting cascade",
                    .descriptor_sets = {a_to_b_set},
                    .push_constants = constants,
                    .num_workgroups = dispatch_size,
                    .compute_shader = propagation_shader
                });

            render_graph.add_compute_dispatch(
                ComputeDispatch<PropagationConstants>{
                    .name = "Propagate lighting cascade",
                    .descriptor_sets = {b_to_a_set},
                    .push_constants = constants,
                    .num_workgroups = dispatch_size,
                    .compute_shader = propagation_shader
                });
        }

        render_graph.add_transition_pass(
            {
                .textures = {
                    {
                        lpv_a_red,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    },
                    {
                        lpv_a_green,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    },
                    {
                        lpv_a_blue,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    }
                }
            }
        );

        render_graph.end_label();

    }

    void LightPropagationVolume::inject_rsm_depth_into_cascade_gv(
        RenderGraph& graph,
        const CascadeData& cascade,
        const uint32_t cascade_index
    ) const {
        ZoneScoped;

        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        auto view_matrices = allocator.create_buffer(
            "GV View Matrices Buffer",
            sizeof(ViewInfo),
            BufferUsage::UniformBuffer
        );

        // We just need this data
        const auto view = ViewInfo{
            .inverse_view = glm::inverse(cascade.rsm_vp),
            .inverse_projection = glm::mat4{1.f},
        };

        graph.add_pass(
            ComputePass{
                .name = "Update view buffer",
                .execute = [&](CommandBuffer& commands) {
                    commands.update_buffer_immediate(view_matrices, view);
                }
            }
        );

        const auto rsm_resolution = glm::uvec2{static_cast<uint32_t>(cvar_lpv_rsm_resolution.get())};

        const auto sampler = backend.get_default_sampler();
        const auto set = backend.get_transient_descriptor_allocator().build_set(inject_rsm_depth_into_gv_pipeline, 0)
                                .bind(rsm_normals_target, sampler)
                                .bind(rsm_depth_target, sampler)
                                .bind(cascade_data_buffer)
                                .bind(view_matrices)
                                .build();

        graph.add_render_pass(
            {
                .name = "Inject RSM depth into GV",
                .descriptor_sets = {set},
                .color_attachments = {RenderingAttachmentInfo{.image = geometry_volume_handle}},
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_descriptor_set(0, set);

                    commands.set_push_constant(0, cascade_index);
                    commands.set_push_constant(1, rsm_resolution.x);
                    commands.set_push_constant(2, rsm_resolution.y);

                    commands.bind_pipeline(inject_rsm_depth_into_gv_pipeline);

                    commands.draw(rsm_resolution.x * rsm_resolution.y);

                    commands.clear_descriptor_set(0);
                }
            });

        allocator.destroy_buffer(view_matrices);

    }

    void LightPropagationVolume::visualize_geometry_volume(
        RenderGraph& graph, const SceneView& view, const TextureHandle lit_scene_texture, const TextureHandle depth
    ) {
        auto& backend = RenderBackend::get();
        if(gv_visualization_pipeline == nullptr) {
            gv_visualization_pipeline = backend.begin_building_pipeline("gv_visualization")
                                               .set_vertex_shader("shader://common/fullscreen.vert.spv"_res)
                                               .set_fragment_shader("shader://gi/lpv/gv_debug.frag.spv"_res)
                                               .set_depth_state(
                                                   {.enable_depth_test = false, .enable_depth_write = false})
                                               .build();
        }

        const auto set = backend.get_transient_descriptor_allocator().build_set(gv_visualization_pipeline, 0)
                                .bind(view.get_buffer())
                                .bind(cascade_data_buffer)
                                .bind(geometry_volume_handle, linear_sampler)
                                .bind(depth)
                                .build();

        graph.add_render_pass(
            {
                .name = "gv_visualization",
                .descriptor_sets = {set},
                .color_attachments = {{.image = lit_scene_texture}},
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_pipeline(gv_visualization_pipeline);

                    commands.bind_descriptor_set(0, set);

                    commands.draw_triangle();

                    commands.clear_descriptor_set(0);
                }
            });
    }

    void LightPropagationVolume::visualize_vpls(
        RenderGraph& graph, const BufferHandle scene_view_buffer, const TextureHandle lit_scene,
        const TextureHandle depth_buffer
    ) {
        auto buffer_barriers = BufferUsageList{};
        buffer_barriers.reserve(cascades.size() * 2);

        for(const auto& cascade : cascades) {
            buffer_barriers.emplace_back(
                BufferUsageToken{
                    .buffer = cascade.vpl_count_buffer,
                    .stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                    .access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                });
            buffer_barriers.emplace_back(
                BufferUsageToken{
                    .buffer = cascade.vpl_buffer,
                    .stage = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT
                });
        }

        auto& backend = RenderBackend::get();
        const auto view_descriptor_set = backend.get_transient_descriptor_allocator()
                                                .build_set(vpl_visualization_pipeline, 0)
                                                .bind(scene_view_buffer)
                                                .build();

        graph.add_render_pass(
            {
                .name = "VPL Visualization",
                .buffers = buffer_barriers,
                .descriptor_sets = {view_descriptor_set},
                .color_attachments = {{.image = lit_scene}},
                .depth_attachment = RenderingAttachmentInfo{.image = depth_buffer},
                .view_mask = {},
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_pipeline(vpl_visualization_pipeline);
                    commands.bind_descriptor_set(0, view_descriptor_set);
                    for(const auto& cascade : cascades) {
                        commands.bind_buffer_reference(0, cascade.vpl_buffer);
                        commands.set_push_constant(2, static_cast<float>(cvar_lpv_vpl_visualization_size.get() / 2.0));
                        commands.draw_indirect(cascade.vpl_count_buffer);
                    }
                }
            });
    }
}
