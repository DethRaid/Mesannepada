#include "render_scene.hpp"

#include <glm/gtx/matrix_decompose.hpp>

#include "components/light_component.hpp"
#include "components/skeletal_mesh_component.hpp"
#include "console/cvars.hpp"
#include "core/box.hpp"
#include "render/indirect_drawing_utils.hpp"
#include "render/material_storage.hpp"
#include "render/mesh_storage.hpp"
#include "render/raytracing_scene.hpp"
#include "render/scene_view.hpp"
#include "render/backend/pipeline_cache.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_allocator.hpp"
#include "render/components/static_mesh_component.hpp"
#include "scene/camera_component.hpp"
#include "scene/transform_component.hpp"
#include "scene/world.hpp"

namespace render {
    constexpr uint32_t MAX_NUM_PRIMITIVES = 65536;
    constexpr uint32_t MAX_NUM_POINT_LIGHTS = 8192;

    RenderWorld::RenderWorld(MeshStorage& meshes_in, MaterialStorage& materials_in) :
        meshes{meshes_in}, materials{materials_in} {
        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();
        primitive_data_buffer = allocator.create_buffer(
            "Primitive data",
            MAX_NUM_PRIMITIVES * sizeof(PrimitiveDataGPU),
            BufferUsage::StorageBuffer
            );
        skeletal_data_buffer = allocator.create_buffer(
            "Skeletal primitive data",
            MAX_NUM_PRIMITIVES * sizeof(SkeletalPrimitiveDataGPU),
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
            raytracing_world.emplace(RaytracingScene{*this});
        }

        auto& pipeline_cache = backend.get_pipeline_cache();
        emissive_point_cloud_shader = pipeline_cache.create_pipeline("shader://util/emissive_point_cloud.comp.spv"_res);

        vertex_deformer = pipeline_cache.create_pipeline("shader://animation/vertex_skinning.comp.spv"_res);
    }

    void RenderWorld::setup_observers(World& world) {
        auto& registry = world.get_registry();

        registry.on_construct<StaticMeshComponent>().connect<&RenderWorld::on_construct_static_mesh>(this);
        registry.on_destroy<StaticMeshComponent>().connect<&RenderWorld::on_destroy_static_mesh>(this);

        registry.on_construct<SkeletalMeshComponent>().connect<&RenderWorld::on_construct_skeletal_mesh>(this);
        registry.on_destroy<SkeletalMeshComponent>().connect<&RenderWorld::on_destroy_skeletal_mesh>(this);

        registry.on_construct<PointLightComponent>().connect<&RenderWorld::on_construct_light>(this);
        registry.on_destroy<PointLightComponent>().connect<&RenderWorld::on_destroy_light>(this);

        registry.on_construct<SpotLightComponent>().connect<&RenderWorld::on_construct_light>(this);
        registry.on_destroy<SpotLightComponent>().connect<&RenderWorld::on_destroy_light>(this);

        registry.on_construct<DirectionalLightComponent>().connect<&RenderWorld::on_construct_light>(this);
        registry.on_destroy<DirectionalLightComponent>().connect<&RenderWorld::on_destroy_light>(this);

        registry.on_update<TransformComponent>().connect<&RenderWorld::on_transform_update>(this);
    }

    void RenderWorld::tick(World& world) {
        ZoneScopedN("RenderWorld::tick");
        auto& registry = world.get_registry();

        // Set the camera's location to the location of the camera entity (if any)
        registry.view<TransformComponent, CameraComponent>().each(
            [this](const TransformComponent& transform) {
                const auto model_matrix = transform.get_local_to_world();
                player_view.set_view_matrix(glm::inverse(model_matrix));
            });

        auto& upload_queue = RenderBackend::get().get_upload_queue();
        // Pull bone matrices from skeletal animators
        registry.view<SkeletalMeshComponent>().each([&](const SkeletalMeshComponent& skinny) {
            upload_queue.upload_to_buffer(skinny.bone_matrices_buffer, eastl::span{skinny.worldspace_bone_matrices});
            upload_queue.upload_to_buffer(skinny.previous_bone_matrices_buffer,
                                          eastl::span{skinny.previous_worldspace_bone_matrices});
        });
    }

    void RenderWorld::update_mesh_proxy(const MeshPrimitiveProxyHandle handle) {
        if(primitive_upload_buffer.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            primitive_upload_buffer.flush_to_buffer(graph, primitive_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }

        handle->calculate_worldspace_bounds();
        handle->data.inverse_model = glm::inverse(handle->data.model);
        primitive_upload_buffer.add_data(handle.index, handle->data);
    }

    void RenderWorld::update_mesh_proxy(const SkeletalMeshPrimitiveProxyHandle handle) {
        update_mesh_proxy(handle->mesh_proxy);

        if(skeletal_data_upload_buffer.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            skeletal_data_upload_buffer.flush_to_buffer(graph, skeletal_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }

        handle->skeletal_data.bone_transforms = handle->bone_transforms->address;

        skeletal_data_upload_buffer.add_data(handle.index, handle->skeletal_data);
    }

    MeshPrimitiveProxyHandle RenderWorld::create_mesh_proxy(
        const float4x4& transform, const MeshHandle mesh, const PooledObject<BasicPbrMaterialProxy> material,
        const bool visible_to_ray_tracing
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

        const auto material_buffer_address = materials.get_material_instance_buffer()->address;
        primitive.data.material = material_buffer_address + primitive.material.index * sizeof(BasicPbrMaterialGpu);
        primitive.data.mesh_id = primitive.mesh.index;
        const auto transparency_mode_bit = static_cast<uint32_t>(primitive.material->first.transparency_mode);
        primitive.data.type_flags = 1 << transparency_mode_bit;
        primitive.data.runtime_flags = PRIMITIVE_RUNTIME_FLAG_ENABLED;

        const auto index_buffer_address = meshes.get_index_buffer()->address;
        primitive.data.indices = index_buffer_address + primitive.mesh->first_index * sizeof(uint32_t);

        const auto positions_buffer_address = meshes.get_vertex_position_buffer()->address;
        primitive.data.vertex_positions = positions_buffer_address + primitive.mesh->first_vertex * sizeof(
                                              StandardVertexPosition);

        const auto data_buffer_address = meshes.get_vertex_data_buffer()->address;
        primitive.data.vertex_data = data_buffer_address + primitive.mesh->first_vertex * sizeof(StandardVertexData);

        const auto handle = static_mesh_primitives.emplace(std::move(primitive));

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

        if(raytracing_world && handle->visible_to_ray_tracing) {
            raytracing_world->add_primitive(handle);
        }

        update_mesh_proxy(handle);

        return handle;
    }

    SkeletalMeshPrimitiveProxyHandle RenderWorld::create_skeletal_mesh_proxy(
        const float4x4& transform, const SkeletalMeshPrimitive& primitive, const BufferHandle bone_matrices_buffer,
        const BufferHandle previous_bone_matrices_buffer
        ) {
        auto proxy = SkeletalMeshPrimitiveProxy{
            .mesh_proxy = create_mesh_proxy(transform,
                                            primitive.mesh,
                                            primitive.material,
                                            primitive.visible_to_ray_tracing),
            .bone_transforms = bone_matrices_buffer,
            .previous_bone_transforms = previous_bone_matrices_buffer,
        };

        proxy.mesh_proxy->data.type_flags |= PRIMITIVE_TYPE_SKINNED;

        // Allocate per-instance buffers for transformed vertex data. Set those as the data's vertex buffers to maintain
        // a consistent interface, and set the original buffers in the skeletal data

        proxy.skeletal_data.original_positions = proxy.mesh_proxy->data.vertex_positions;
        proxy.skeletal_data.original_data = proxy.mesh_proxy->data.vertex_data;

        const auto bone_id_buffer_address = meshes.get_bone_ids_buffer()->address;
        proxy.skeletal_data.bone_ids = bone_id_buffer_address + primitive.mesh->weights_offset * sizeof(u16vec4);
        const auto weights_buffer_address = meshes.get_weights_buffer()->address;
        proxy.skeletal_data.weights = weights_buffer_address + primitive.mesh->weights_offset * sizeof(float4);

        const auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        proxy.transformed_vertices = allocator.create_buffer(
            "transformed_vertex_positions_a",
            primitive.mesh->num_vertices * sizeof(StandardVertexPosition),
            BufferUsage::VertexBuffer);
        proxy.mesh_proxy->data.vertex_positions = proxy.transformed_vertices->address;

        proxy.transformed_data = allocator.create_buffer(
            "transformed_vertex_data",
            primitive.mesh->num_vertices * sizeof(StandardVertexData),
            BufferUsage::VertexBuffer);
        proxy.mesh_proxy->data.vertex_data = proxy.transformed_data->address;

        proxy.skeletal_data.primitive_id = proxy.mesh_proxy.index;
        proxy.previous_skinned_vertices = allocator.create_buffer(
            "transformed_vertex_positions_b",
            primitive.mesh->num_vertices * sizeof(StandardVertexPosition),
            BufferUsage::VertexBuffer
            );
        proxy.skeletal_data.last_frame_skinned_positions = proxy.previous_skinned_vertices->address;

        const auto handle = skeletal_mesh_primitives.emplace(eastl::move(proxy));

        update_mesh_proxy(handle);

        return handle;
    }

    void RenderWorld::mark_proxy_inactive(const MeshPrimitiveProxyHandle primitive) {
        primitive->data.runtime_flags &= ~PRIMITIVE_RUNTIME_FLAG_ENABLED;
        update_mesh_proxy(primitive);
    }

    void RenderWorld::destroy_primitive(const MeshPrimitiveProxyHandle primitive) {
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

        if(raytracing_world) {
            raytracing_world->remove_primitive(primitive);
        }

        mark_proxy_inactive(primitive);

        static_mesh_primitives.free_object(primitive);
    }

    void RenderWorld::destroy_primitive(const SkeletalMeshPrimitiveProxyHandle proxy) {
        destroy_primitive(proxy->mesh_proxy);

        auto& allocator = RenderBackend::get().get_global_allocator();
        allocator.destroy_buffer(proxy->transformed_vertices);
        allocator.destroy_buffer(proxy->transformed_data);

        active_skeletal_meshes.erase_first_unsorted(proxy);

        skeletal_mesh_primitives.free_object(proxy);
    }

    PointLightProxyHandle RenderWorld::create_light_proxy(const PointLightGPU& light) {
        const auto handle = point_lights.emplace(PointLightProxy{.gpu_data = light});
        update_light_proxy(handle);

        return handle;
    }

    SpotLightProxyHandle RenderWorld::create_light_proxy(const SpotLightGPU& light) {
        const auto handle = spot_lights.emplace({.gpu_data = light});
        update_light_proxy(handle);

        return handle;
    }

    void RenderWorld::update_light_proxy(const PointLightProxyHandle handle) {
        if(point_light_uploads.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            point_light_uploads.flush_to_buffer(graph, point_light_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }
        point_light_uploads.add_data(handle.index, handle->gpu_data);
    }

    void RenderWorld::update_light_proxy(const SpotLightProxyHandle handle) {
        if(spot_light_uploads.is_full()) {
            auto graph = RenderGraph{RenderBackend::get()};

            spot_light_uploads.flush_to_buffer(graph, spot_light_data_buffer);

            graph.finish();
            RenderBackend::get().execute_graph(graph);
        }
        spot_light_uploads.add_data(handle.index, handle->gpu_data);
    }

    void RenderWorld::destroy_light(const PointLightProxyHandle proxy) {
        point_lights.free_object(proxy);
    }

    void RenderWorld::destroy_light(const SpotLightProxyHandle proxy) {
        spot_lights.free_object(proxy);
    }

    void RenderWorld::begin_frame(RenderGraph& graph) {
        graph.begin_label("RenderScene::begin_frame");

        primitive_upload_buffer.flush_to_buffer(graph, primitive_data_buffer);

        skeletal_data_upload_buffer.flush_to_buffer(graph, skeletal_data_buffer);

        point_light_uploads.flush_to_buffer(graph, point_light_data_buffer);

        spot_light_uploads.flush_to_buffer(graph, spot_light_data_buffer);

        if(raytracing_world) {
            raytracing_world->finalize(graph);
        }

        graph.end_label();

        sky.update_sky_luts(graph, sun.get_direction());
    }

    void RenderWorld::deform_skinned_meshes(RenderGraph& graph) {
        auto barriers = BufferUsageList{};
        barriers.reserve(active_skeletal_meshes.size() * 3);
        for(const auto& mesh : active_skeletal_meshes) {
            barriers.emplace_back(mesh->bone_transforms,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_READ_BIT);
            barriers.emplace_back(mesh->transformed_vertices,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_WRITE_BIT);
            barriers.emplace_back(mesh->transformed_data,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_WRITE_BIT);
        }

        const auto set = RenderBackend::get().get_transient_descriptor_allocator()
                                             .build_set(vertex_deformer, 0)
                                             .bind(primitive_data_buffer)
                                             .bind(skeletal_data_buffer)
                                             .build();

        graph.add_pass({
            .name = "deform_skinned_meshes",
            .buffers = barriers,
            .descriptor_sets = {set},
            .execute = [&](CommandBuffer& commands) {
                commands.bind_pipeline(vertex_deformer);

                commands.bind_descriptor_set(0, set);

                // For each _active_ skeletal mesh proxy, we bind the proxy's bone transforms buffer, and set push
                // constants for the skeletal data (original vertices) and mesh data (transformed vertices). Dispatch
                // enough workgroups that we have the number of vertices in the x

                for(const auto& mesh : active_skeletal_meshes) {
                    eastl::swap(mesh->mesh_proxy->data.vertex_data, mesh->skeletal_data.last_frame_skinned_positions);
                    update_mesh_proxy(mesh);

                    commands.set_push_constant(0, mesh.index);
                    commands.set_push_constant(1, mesh->mesh_proxy.index);
                    commands.set_push_constant(2, mesh->mesh_proxy->mesh->num_vertices);

                    commands.dispatch((mesh->mesh_proxy->mesh->num_vertices + 63) / 64, 1, 1);
                }
            }
        });
    }

    const eastl::vector<PooledObject<MeshPrimitiveProxy> >& RenderWorld::get_solid_primitives() const {
        return solid_primitives;
    }

    const eastl::vector<MeshPrimitiveProxyHandle>& RenderWorld::get_masked_primitives() const {
        return masked_primitives;
    }

    const eastl::vector<MeshPrimitiveProxyHandle>& RenderWorld::get_transparent_primitives() const {
        return translucent_primitives;
    }

    BufferHandle RenderWorld::get_primitive_buffer() const {
        return primitive_data_buffer;
    }

    uint32_t RenderWorld::get_total_num_primitives() const {
        return static_mesh_primitives.size();
    }

    DirectionalLight& RenderWorld::get_sun_light() {
        return sun;
    }

    const DirectionalLight& RenderWorld::get_sun_light() const {
        return sun;
    }

    const ProceduralSky& RenderWorld::get_sky() const {
        return sky;
    }

    BufferHandle RenderWorld::get_point_lights_buffer() const {
        return point_light_data_buffer;
    }

    uint32_t RenderWorld::get_num_point_lights() const {
        return point_lights.size();
    }

    SceneView& RenderWorld::get_player_view() {
        return player_view;
    }

    const SceneView& RenderWorld::get_player_view() const {
        return player_view;
    }

    eastl::vector<PooledObject<MeshPrimitiveProxy> > RenderWorld::get_primitives_in_bounds(
        const glm::vec3& min_bounds, const glm::vec3& max_bounds
        ) const {
        auto output = eastl::vector<PooledObject<MeshPrimitiveProxy> >{};
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

    void RenderWorld::draw_opaque(CommandBuffer& commands, const GraphicsPipelineHandle pso) const {
        draw_primitives(commands, pso, solid_primitives);
    }

    void RenderWorld::draw_masked(CommandBuffer& commands, const GraphicsPipelineHandle pso) const {
        draw_primitives(commands, pso, masked_primitives);
    }

    void RenderWorld::draw_opaque(
        CommandBuffer& commands, const IndirectDrawingBuffers& drawcalls, const GraphicsPipelineHandle solid_pso
        ) const {
        commands.bind_index_buffer(meshes.get_index_buffer());

        if(solid_pso->descriptor_sets.size() > 1) {
            commands.bind_descriptor_set(1, commands.get_backend().get_texture_descriptor_pool().get_descriptor_set());
        }

        commands.bind_pipeline(solid_pso);

        commands.set_cull_mode(VK_CULL_MODE_BACK_BIT);
        commands.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);

        commands.draw_indexed_indirect(
            drawcalls.commands,
            static_cast<uint32_t>(solid_primitives.size()));

        if(solid_pso->descriptor_sets.size() > 1) {
            commands.clear_descriptor_set(1);
        }
    }

    void RenderWorld::draw_masked(
        CommandBuffer& commands, const IndirectDrawingBuffers& draw_buffers, const GraphicsPipelineHandle masked_pso
        ) const {
        commands.bind_index_buffer(meshes.get_index_buffer());

        if(masked_pso->descriptor_sets.size() > 1) {
            commands.bind_descriptor_set(1, commands.get_backend().get_texture_descriptor_pool().get_descriptor_set());
        }

        commands.bind_pipeline(masked_pso);

        commands.set_cull_mode(VK_CULL_MODE_NONE);
        commands.set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE);

        commands.draw_indexed_indirect(
            draw_buffers.commands,
            static_cast<uint32_t>(masked_primitives.size()));

        if(masked_pso->descriptor_sets.size() > 1) {
            commands.clear_descriptor_set(1);
        }
    }

    void RenderWorld::draw_transparent(CommandBuffer& commands, const GraphicsPipelineHandle pso) const {
        draw_primitives(commands, pso, translucent_primitives);
    }

    const MeshStorage& RenderWorld::get_meshes() const {
        return meshes;
    }

    RaytracingScene& RenderWorld::get_raytracing_world() {
        return *raytracing_world;
    }

    const RaytracingScene& RenderWorld::get_raytracing_world() const {
        return *raytracing_world;
    }

    MaterialStorage& RenderWorld::get_material_storage() const {
        return materials;
    }

    MeshStorage& RenderWorld::get_mesh_storage() const {
        return meshes;
    }

    float RenderWorld::get_fog_strength() const {
        return fog_strength;
    }

    void RenderWorld::draw_primitives(
        CommandBuffer& commands, const GraphicsPipelineHandle pso,
        const eastl::span<const MeshPrimitiveProxyHandle> primitives
        ) const {
        commands.bind_index_buffer(meshes.get_index_buffer());

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

            commands.draw_indexed(
                mesh->num_indices,
                1,
                static_cast<uint32_t>(mesh->first_index),
                0,
                primitive.index);
        }

        if(pso->descriptor_sets.size() > 1) {
            commands.clear_descriptor_set(1);
        }
    }

    void RenderWorld::on_construct_static_mesh(entt::registry& registry, const entt::entity entity) {
        auto [transform, mesh] = registry.get<TransformComponent, StaticMeshComponent>(entity);
        for(auto& primitive : mesh.primitives) {
            primitive.proxy = create_mesh_proxy(
                transform.get_local_to_world(),
                primitive.mesh,
                primitive.material,
                primitive.visible_to_ray_tracing);
        }
    }

    void RenderWorld::on_destroy_static_mesh(entt::registry& registry, const entt::entity entity) {
        const auto& mesh = registry.get<StaticMeshComponent>(entity);
        for(const auto& primitive : mesh.primitives) {
            destroy_primitive(primitive.proxy);
        }
    }

    void RenderWorld::on_construct_skeletal_mesh(entt::registry& registry, const entt::entity entity) {
        auto& mesh = registry.get<SkeletalMeshComponent>(entity);
        mesh.bone_matrices_buffer = RenderBackend::get().get_global_allocator().create_buffer(
            "Bone matrices",
            mesh.bones.size() * sizeof(float4x4),
            BufferUsage::StorageBuffer);

        mesh.previous_bone_matrices_buffer = RenderBackend::get().get_global_allocator().create_buffer(
            "Previous bone matrices",
            mesh.bones.size() * sizeof(float4x4),
            BufferUsage::StorageBuffer);

        // TODO: We need to make per-primitive BLASes for skeletal meshes, since they can all be deformed individually
        const auto& transform = registry.get<TransformComponent>(entity);
        for(auto& primitive : mesh.primitives) {
            primitive.proxy = create_skeletal_mesh_proxy(
                transform.get_local_to_world(),
                primitive,
                mesh.bone_matrices_buffer,
                mesh.previous_bone_matrices_buffer
                );
            active_skeletal_meshes.emplace_back(primitive.proxy);
        }
    }

    void RenderWorld::on_destroy_skeletal_mesh(entt::registry& registry, const entt::entity entity) {
        const auto& mesh = registry.get<SkeletalMeshComponent>(entity);

        RenderBackend::get().get_global_allocator().destroy_buffer(mesh.bone_matrices_buffer);
        RenderBackend::get().get_global_allocator().destroy_buffer(mesh.previous_bone_matrices_buffer);

        for(auto& primitive : mesh.primitives) {
            destroy_primitive(primitive.proxy);
        }
    }

    void RenderWorld::on_construct_light(entt::registry& registry, const entt::entity entity) {
        const auto& transform = registry.get<TransformComponent>(entity);
        const auto location = transform.location;

        // Create a proxy based on what kind of light we have

        if(auto* point_light_component = registry.try_get<PointLightComponent>(entity)) {
            point_light_component->proxy = create_light_proxy(
                PointLightGPU{
                    .location = location,
                    .size = point_light_component->size,
                    .color = point_light_component->color,
                    .range = point_light_component->range
                });
        }

        if(auto* spot_light_component = registry.try_get<SpotLightComponent>(entity)) {
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

    void RenderWorld::on_destroy_light(entt::registry& registry, const entt::entity entity) {
        if(const auto* point_light_component = registry.try_get<PointLightComponent>(entity)) {
            destroy_light(point_light_component->proxy);
        }

        if(const auto* spot_light_component = registry.try_get<SpotLightComponent>(entity)) {
            destroy_light(spot_light_component->proxy);
        }
    }

    void RenderWorld::on_transform_update(entt::registry& registry, const entt::entity entity) {
        if(!registry.any_of<SkeletalMeshComponent, StaticMeshComponent, PointLightComponent, SpotLightComponent,
                            DirectionalLightComponent>(entity)) {
            return;
        }

        const auto& transform = registry.get<TransformComponent>(entity);
        const auto matrix = transform.get_local_to_world();

        if(const auto* mesh = registry.try_get<StaticMeshComponent>(entity)) {
            ZoneScopedN("update_static_mesh_location");

            for(const auto& primitive : mesh->primitives) {
                primitive.proxy->data.model = matrix;
                update_mesh_proxy(primitive.proxy);

                if(raytracing_world && primitive.visible_to_ray_tracing) {
                    raytracing_world->update_primitive(primitive.proxy);
                }
            }
        }

        if(const auto* mesh = registry.try_get<SkeletalMeshComponent>(entity)) {
            ZoneScopedN("update_skeletal_mesh_location");

            for(const auto& primitive : mesh->primitives) {
                primitive.proxy->mesh_proxy->data.model = matrix;
                update_mesh_proxy(primitive.proxy);

                if(raytracing_world && primitive.visible_to_ray_tracing) {
                    raytracing_world->update_primitive(primitive.proxy->mesh_proxy);
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
