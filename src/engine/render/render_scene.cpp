#include "render_scene.hpp"

#include <glm/gtx/matrix_decompose.hpp>

#include "components/light_component.hpp"
#include "console/cvars.hpp"
#include "render/material_storage.hpp"
#include "render/indirect_drawing_utils.hpp"
#include "render/mesh_storage.hpp"
#include "render/raytracing_scene.hpp"
#include "render/scene_view.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/resource_allocator.hpp"
#include "render/backend/render_backend.hpp"
#include "core/box.hpp"
#include "core/issue_breakpoint.hpp"
#include "scene/camera_component.hpp"
#include "scene/scene.hpp"
#include "render/components/static_mesh_component.hpp"
#include "scene/transform_component.hpp"

namespace render {
    constexpr uint32_t MAX_NUM_PRIMITIVES = 65536;
    constexpr uint32_t MAX_NUM_POINT_LIGHTS = 8192;

    RenderScene::RenderScene(MeshStorage& meshes_in, MaterialStorage& materials_in)
        : meshes{meshes_in}, materials{materials_in} {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        primitive_data_buffer = allocator.create_buffer(
            "Primitive data",
            MAX_NUM_PRIMITIVES * sizeof(PrimitiveDataGPU),
            BufferUsage::StorageBuffer
        );

        point_light_data_buffer = allocator.create_buffer(
            "Point lights",
            MAX_NUM_POINT_LIGHTS * sizeof(PointLightGPU),
            BufferUsage::StorageBuffer);

        // Defaults
        sun.set_direction({0.1f, -1.f, -0.3f});
        // sun.set_direction({0.1f, -1.f, -0.01f});
        sun.set_color(glm::vec4{1.f, 1.f, 1.f, 0.f} * 80000.f);

        if(backend.supports_ray_tracing()) {
            raytracing_scene.emplace(RaytracingScene{*this});
        }

        auto& pipeline_cache = backend.get_pipeline_cache();
        emissive_point_cloud_shader = pipeline_cache.create_pipeline("shaders/util/emissive_point_cloud.comp.spv");
    }

    void RenderScene::setup_observers(Scene& scene) {
        auto& registry = scene.get_registry();

        registry.on_construct<StaticMeshComponent>().connect<&RenderScene::on_construct_static_mesh>(this);
        registry.on_destroy<StaticMeshComponent>().connect<&RenderScene::on_destroy_static_mesh>(this);

        registry.on_construct<render::PointLightComponent>().connect<&RenderScene::on_construct_light>(this);
        registry.on_destroy<render::PointLightComponent>().connect<&RenderScene::on_destroy_light>(this);

        registry.on_construct<render::SpotLightComponent>().connect<&RenderScene::on_construct_light>(this);
        registry.on_destroy<render::SpotLightComponent>().connect<&RenderScene::on_destroy_light>(this);

        registry.on_construct<render::DirectionalLightComponent>().connect<&RenderScene::on_construct_light>(this);
        registry.on_destroy<render::DirectionalLightComponent>().connect<&RenderScene::on_destroy_light>(this);

        registry.on_update<TransformComponent>().connect<&RenderScene::on_transform_update>(this);
    }

    void RenderScene::tick(Scene& scene) {
        ZoneScoped;
        auto& registry = scene.get_registry();

        // Set the camera's location to the location of the camera entity (if any)
        registry.view<TransformComponent, CameraComponent>().each(
            [this](const TransformComponent& transform) {
                const auto inverse_view_matrix = transform.cached_parent_to_world * transform.local_to_parent;
                player_view.set_view_matrix(glm::inverse(inverse_view_matrix));
            });
    }

    void RenderScene::update_mesh_proxy(const MeshPrimitiveProxyHandle handle) {
        if(primitive_upload_buffer.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            primitive_upload_buffer.flush_to_buffer(graph, primitive_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }

        handle->data.inverse_model = glm::inverse(handle->data.model);
        primitive_upload_buffer.add_data(handle.index, handle->data);
    }

    MeshPrimitiveProxyHandle RenderScene::create_static_mesh_proxy(
        const float4x4& transform, const MeshHandle mesh, const PooledObject<BasicPbrMaterialProxy> material, const bool visible_to_ray_tracing
    ) {
        auto primitive = MeshPrimitiveProxy{
            .data = {
                .model = transform,
                .inverse_model = inverse(transform),
                .material = material.index,
                .mesh_id = mesh.index,
            },
            .mesh = mesh,
            .material = material,
            .visible_to_ray_tracing = visible_to_ray_tracing
        };

        const auto& bounds = mesh->bounds;
        const auto radius = glm::max(
            glm::max(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y),
            bounds.max.z - bounds.min.z
        );

        primitive.data.bounds_min_and_radius = float4{bounds.min, radius};
        primitive.data.bounds_max = float4{bounds.max, 0};

        const auto material_buffer_address = materials.get_material_instance_buffer()->address;
        primitive.data.material = material_buffer_address + primitive.material.index * sizeof(BasicPbrMaterialGpu);
        primitive.data.mesh_id = primitive.mesh.index;
        primitive.data.type = static_cast<uint32_t>(primitive.material->first.transparency_mode);

        const auto index_buffer_address = meshes.get_index_buffer()->address;
        primitive.data.indices = index_buffer_address + primitive.mesh->first_index * sizeof(uint32_t);

        const auto positions_buffer_address = meshes.get_vertex_position_buffer()->address;
        primitive.data.vertex_positions = positions_buffer_address + primitive.mesh->first_vertex * sizeof(
            StandardVertexPosition);

        const auto data_buffer_address = meshes.get_vertex_data_buffer()->address;
        primitive.data.vertex_data = data_buffer_address + primitive.mesh->first_vertex * sizeof(StandardVertexData);

        auto handle = mesh_primitives.emplace(std::move(primitive));

        switch(handle->material->first.transparency_mode) {
        case TransparencyMode::Solid:
            solid_primitives.push_back(handle);
            break;

        case TransparencyMode::Cutout:
            masked_primitives.push_back(handle);
            break;

        case TransparencyMode::Translucent:
            translucent_primitives.push_back(handle);
            break;
        }

        if(handle->material->first.emissive) {
            new_emissive_objects.push_back(handle);
        }

        if(raytracing_scene && handle->visible_to_ray_tracing) {
            raytracing_scene->add_primitive(handle);
        }

        update_mesh_proxy(handle);

        return handle;
    }

    void RenderScene::destroy_primitive(const MeshPrimitiveProxyHandle primitive) {
        switch(primitive->material->first.transparency_mode) {
        case TransparencyMode::Solid:
            solid_primitives.erase_first_unsorted(primitive);
            break;

        case TransparencyMode::Cutout:
            masked_primitives.erase_first_unsorted(primitive);
            break;

        case TransparencyMode::Translucent:
            translucent_primitives.erase_first_unsorted(primitive);
            break;
        }

        if(raytracing_scene) {
            raytracing_scene->remove_primitive(primitive);
        }

        mesh_primitives.free_object(primitive);
    }

    PointLightProxyHandle RenderScene::create_light_proxy(const PointLightGPU& light) {
        const auto handle = point_lights.emplace(PointLightProxy{.gpu_data = light});
        update_light_proxy(handle);

        return handle;
    }

    SpotLightProxyHandle RenderScene::create_light_proxy(const SpotLightGPU& light) {
        const auto handle = spot_lights.emplace({.gpu_data = light});
        update_light_proxy(handle);

        return handle;
    }

    void RenderScene::update_light_proxy(const PointLightProxyHandle handle) {
        if(point_light_uploads.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            point_light_uploads.flush_to_buffer(graph, point_light_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }
        point_light_uploads.add_data(handle.index, handle->gpu_data);
    }

    void RenderScene::update_light_proxy(const SpotLightProxyHandle handle) {
        if(spot_light_uploads.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            spot_light_uploads.flush_to_buffer(graph, spot_light_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }
        spot_light_uploads.add_data(handle.index, handle->gpu_data);
    }

    void RenderScene::destroy_light(const PointLightProxyHandle proxy) {
        point_lights.free_object(proxy);
    }

    void RenderScene::destroy_light(const SpotLightProxyHandle proxy) {
        spot_lights.free_object(proxy);
    }

    void RenderScene::begin_frame(RenderGraph& graph) {
        graph.begin_label("RenderScene::begin_frame");

        primitive_upload_buffer.flush_to_buffer(graph, primitive_data_buffer);

        point_light_uploads.flush_to_buffer(graph, point_light_data_buffer);

        spot_light_uploads.flush_to_buffer(graph, spot_light_data_buffer);

        if(raytracing_scene) {
            raytracing_scene->finalize(graph);
        }

        generate_emissive_point_clouds(graph);

        graph.end_label();

        sky.update_sky_luts(graph, sun.get_direction());
    }

    const eastl::vector<PooledObject<MeshPrimitiveProxy>>& RenderScene::get_solid_primitives() const {
        return solid_primitives;
    }

    const eastl::vector<MeshPrimitiveProxyHandle>& RenderScene::get_masked_primitives() const {
        return masked_primitives;
    }

    const eastl::vector<MeshPrimitiveProxyHandle>& RenderScene::get_transparent_primitives() const {
        return translucent_primitives;
    }

    BufferHandle RenderScene::get_primitive_buffer() const {
        return primitive_data_buffer;
    }

    uint32_t RenderScene::get_total_num_primitives() const {
        return mesh_primitives.size();
    }

    DirectionalLight& RenderScene::get_sun_light() {
        return sun;
    }

    const DirectionalLight& RenderScene::get_sun_light() const {
        return sun;
    }

    const ProceduralSky& RenderScene::get_sky() const {
        return sky;
    }

    BufferHandle RenderScene::get_point_lights_buffer() const {
        return point_light_data_buffer;
    }

    uint32_t RenderScene::get_num_point_lights() const {
        return point_lights.size();
    }

    SceneView& RenderScene::get_player_view() {
        return player_view;
    }

    const SceneView& RenderScene::get_player_view() const {
        return player_view;
    }

    eastl::vector<PooledObject<MeshPrimitiveProxy>> RenderScene::get_primitives_in_bounds(
        const glm::vec3& min_bounds, const glm::vec3& max_bounds
    ) const {
        auto output = eastl::vector<PooledObject<MeshPrimitiveProxy>>{};
        output.reserve(solid_primitives.size());

        const auto test_box = Box{.min = min_bounds, .max = max_bounds};
        for(const auto& primitive : solid_primitives) {
            const auto matrix = primitive->data.model;
            const auto mesh_bounds = primitive->mesh->bounds;

            const auto max_mesh_bounds = mesh_bounds.max;
            const auto min_mesh_bounds = mesh_bounds.min;
            const auto min_primitive_bounds = matrix * glm::vec4{min_mesh_bounds, 1.f};
            const auto max_primitive_bounds = matrix * glm::vec4{max_mesh_bounds, 1.f};

            const auto primitive_box = Box{.min = min_primitive_bounds, .max = max_primitive_bounds};

            if(test_box.overlaps(primitive_box)) {
                output.push_back(primitive);
            }
        }

        return output;
    }

    void RenderScene::generate_emissive_point_clouds(RenderGraph& render_graph) {
        if(new_emissive_objects.empty()) {
            return;
        }

        render_graph.begin_label("Generate emissive mesh VPLs");
        for(auto& primitive : new_emissive_objects) {
            primitive->emissive_points_buffer = generate_vpls_for_primitive(render_graph, primitive);
        }
        render_graph.end_label();

        new_emissive_objects.clear();
    }

    void RenderScene::draw_opaque(CommandBuffer& commands, const GraphicsPipelineHandle pso) const {
        draw_primitives(commands, pso, solid_primitives);
    }

    void RenderScene::draw_masked(CommandBuffer& commands, const GraphicsPipelineHandle pso) const {
        draw_primitives(commands, pso, masked_primitives);
    }

    void RenderScene::draw_opaque(
        CommandBuffer& commands, const IndirectDrawingBuffers& drawbuffers, const GraphicsPipelineHandle solid_pso
    ) const {
        meshes.bind_to_commands(commands);
        commands.bind_vertex_buffer(2, drawbuffers.primitive_ids);

        if(solid_pso->descriptor_sets.size() > 1) {
            commands.bind_descriptor_set(1, commands.get_backend().get_texture_descriptor_pool().get_descriptor_set());
        }

        commands.bind_pipeline(solid_pso);

        commands.set_cull_mode(VK_CULL_MODE_BACK_BIT);
        commands.set_front_face(VK_FRONT_FACE_CLOCKWISE);

        commands.draw_indexed_indirect(
            drawbuffers.commands,
            drawbuffers.count,
            static_cast<uint32_t>(solid_primitives.size()));

        if(solid_pso->descriptor_sets.size() > 1) {
            commands.clear_descriptor_set(1);
        }
    }

    void RenderScene::draw_masked(
        CommandBuffer& commands, const IndirectDrawingBuffers& draw_buffers, const GraphicsPipelineHandle masked_pso
    ) const {
        meshes.bind_to_commands(commands);
        commands.bind_vertex_buffer(2, draw_buffers.primitive_ids);

        if(masked_pso->descriptor_sets.size() > 1) {
            commands.bind_descriptor_set(1, commands.get_backend().get_texture_descriptor_pool().get_descriptor_set());
        }

        commands.bind_pipeline(masked_pso);

        commands.set_cull_mode(VK_CULL_MODE_NONE);
        commands.set_front_face(VK_FRONT_FACE_CLOCKWISE);

        commands.draw_indexed_indirect(
            draw_buffers.commands,
            draw_buffers.count,
            static_cast<uint32_t>(masked_primitives.size()));

        if(masked_pso->descriptor_sets.size() > 1) {
            commands.clear_descriptor_set(1);
        }
    }

    void RenderScene::draw_transparent(CommandBuffer& commands, GraphicsPipelineHandle pso) const {
        draw_primitives(commands, pso, translucent_primitives);
    }

    const MeshStorage& RenderScene::get_meshes() const {
        return meshes;
    }

    RaytracingScene& RenderScene::get_raytracing_scene() {
        return *raytracing_scene;
    }

    const RaytracingScene& RenderScene::get_raytracing_scene() const {
        return *raytracing_scene;
    }

    MaterialStorage& RenderScene::get_material_storage() const {
        return materials;
    }

    MeshStorage& RenderScene::get_mesh_storage() const {
        return meshes;
    }

    float RenderScene::get_fog_strength() const {
        return fog_strength;
    }

    BufferHandle RenderScene::generate_vpls_for_primitive(
        RenderGraph& graph, const PooledObject<MeshPrimitiveProxy>& primitive
    ) const {
        auto& backend = RenderBackend::get();
        const auto vpl_buffer_handle = backend.get_global_allocator().create_buffer(
            "Primitive emission buffer",
            primitive->mesh->num_points * sizeof(glm::vec4),
            BufferUsage::StorageBuffer
        );

        struct EmissivePointCloudConstants {
            DeviceAddress primitive_data;
            DeviceAddress point_cloud;
            DeviceAddress vpl_buffer;
            uint32_t primitive_index;
            uint32_t num_points;
        };

        graph.add_compute_dispatch(
            ComputeDispatch<EmissivePointCloudConstants>{
                .name = "Build emissive points",
                .descriptor_sets = {backend.get_texture_descriptor_pool().get_descriptor_set()},
                .buffers = {
                    {
                        vpl_buffer_handle,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                    },
                    {
                        primitive->mesh->point_cloud_buffer,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT
                    },
                    {
                        primitive_data_buffer,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT
                    },
                },
                .push_constants = {
                    .primitive_data = primitive_data_buffer->address,
                    .point_cloud = primitive->mesh->point_cloud_buffer->address,
                    .vpl_buffer = vpl_buffer_handle->address,
                    .primitive_index = primitive.index,
                    .num_points = primitive->mesh->num_points,
                },
                .num_workgroups = {
                    (primitive->mesh->num_points + 95) / 96, 1, 1
                },
                .compute_shader = emissive_point_cloud_shader
            });

        return vpl_buffer_handle;
    }

    void RenderScene::draw_primitives(
        CommandBuffer& commands, const GraphicsPipelineHandle pso,
        const eastl::span<const MeshPrimitiveProxyHandle> primitives
    ) const {
        meshes.bind_to_commands(commands);

        if(pso->descriptor_sets.size() > 1) {
            commands.bind_descriptor_set(1, commands.get_backend().get_texture_descriptor_pool().get_descriptor_set());
        }

        commands.bind_pipeline(pso);
        for(const auto& primitive : primitives) {
            const auto& mesh = primitive->mesh;

            if(primitive->material->first.double_sided) {
                commands.set_cull_mode(VK_CULL_MODE_NONE);
            } else {
                commands.set_cull_mode(VK_CULL_MODE_BACK_BIT);
            }

            if(primitive->material->first.front_face_ccw) {
                commands.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);
            } else {
                commands.set_front_face(VK_FRONT_FACE_CLOCKWISE);
            }

            commands.set_push_constant(0, primitive.index);
            commands.draw_indexed(
                mesh->num_indices,
                1,
                static_cast<uint32_t>(mesh->first_index),
                static_cast<uint32_t>(mesh->first_vertex),
                0);
        }

        if(pso->descriptor_sets.size() > 1) {
            commands.clear_descriptor_set(1);
        }
    }

    void RenderScene::on_construct_static_mesh(entt::registry& registry, const entt::entity entity) {
        auto [transform, mesh] = registry.get<TransformComponent, StaticMeshComponent>(entity);
        for(auto& primitive : mesh.primitives) {
            primitive.proxy = create_static_mesh_proxy(
                transform.cached_parent_to_world * transform.local_to_parent,
                primitive.mesh,
                primitive.material,
                primitive.visible_to_ray_tracing);
        }
    }

    void RenderScene::on_destroy_static_mesh(entt::registry& registry, const entt::entity entity) {
        const auto& mesh = registry.get<StaticMeshComponent>(entity);
        for(const auto& primitive : mesh.primitives) {
            destroy_primitive(primitive.proxy);
        }
    }

    void RenderScene::on_construct_light(entt::registry& registry, const entt::entity entity) {
        const auto& transform = registry.get<TransformComponent>(entity);
        const auto matrix = transform.cached_parent_to_world * transform.local_to_parent;
        const auto location = float3{matrix[3]};

        // Create a proxy based on what kind of light we have

        auto* point_light_component = registry.try_get<PointLightComponent>(entity);
        if(point_light_component) {
            point_light_component->proxy = create_light_proxy(
                PointLightGPU{
                    .location = location,
                    .size = point_light_component->size,
                    .color = point_light_component->color,
                    .range = point_light_component->range
                });
        }

        auto* spot_light_component = registry.try_get<SpotLightComponent>(entity);
        if(spot_light_component) {
            spot_light_component->proxy = create_light_proxy(
                SpotLightGPU{
                    .location = location,
                    .size = spot_light_component->size,
                    .color = spot_light_component->color,
                    .range = spot_light_component->range,
                    .inner_angle = spot_light_component->inner_cone_angle,
                    .outer_angle = spot_light_component->outer_cone_angle
                });
        }
    }

    void RenderScene::on_destroy_light(entt::registry& registry, const entt::entity entity) {
        if(const auto* point_light_component = registry.try_get<PointLightComponent>(entity)) {
            destroy_light(point_light_component->proxy);
        }

        if(const auto* spot_light_component = registry.try_get<SpotLightComponent>(entity)) {
            destroy_light(spot_light_component->proxy);
        }
    }

    void RenderScene::on_transform_update(entt::registry& registry, const entt::entity entity) {
        if(!registry.any_of<StaticMeshComponent, PointLightComponent, SpotLightComponent, DirectionalLightComponent>(
            entity)) {
            return;
        }

        const auto& transform = registry.get<TransformComponent>(entity);
        const auto matrix = transform.cached_parent_to_world * transform.local_to_parent;

        if(const auto* mesh = registry.try_get<StaticMeshComponent>(entity)) {
            ZoneScopedN("update_static_mesh_location");

            for(const auto& primitive : mesh->primitives) {
                primitive.proxy->data.model = matrix;
                update_mesh_proxy(primitive.proxy);

                if(raytracing_scene && primitive.visible_to_ray_tracing) {
                    raytracing_scene->update_primitive(primitive.proxy);
                }
            }
        }

        const auto location = float3{matrix[3]};
        if(const auto* point_light = registry.try_get<PointLightComponent>(entity)) {
            ZoneScopedN("update_point_light");

            point_light->proxy->gpu_data.location = location;
            update_light_proxy(point_light->proxy);
        }

        if(const auto* spot_light = registry.try_get<SpotLightComponent>(entity)) {
            ZoneScopedN("update_spot_light");

            spot_light->proxy->gpu_data.location = location;
            update_light_proxy(spot_light->proxy);
        }
    }
}
