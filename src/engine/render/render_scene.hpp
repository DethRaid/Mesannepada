#pragma once

#include <entt/entity/registry.hpp>
#include <EASTL/priority_queue.h>

#include "light_proxies.hpp"
#include "render/scene_view.hpp"
#include "render/procedural_sky.hpp"
#include "render/raytracing_scene.hpp"
#include "core/object_pool.hpp"
#include "render/backend/handles.hpp"
#include "proxies/mesh_primitive_proxy.hpp"
#include "render/backend/scatter_upload_buffer.hpp"
#include "render/directional_light.hpp"
#include "proxies/skeletal_mesh_primitive_proxy.hpp"
#include "shared/lights.hpp"

namespace render {
    struct SkeletalMeshPrimitive;
}

class Scene;

namespace render {
    struct IndirectDrawingBuffers;
    class MaterialStorage;
    class MeshStorage;
    class GltfModel;
    class RenderBackend;

    /**
     * A scene that can be rendered!
     *
     * Contains lots of wonderful things - meshes, materials, ray tracing acceleration structure, and more!
     */
    class RenderScene {
    public:
        explicit RenderScene(MeshStorage& meshes_in, MaterialStorage& materials_in);

        /**
         * Sets up various observers for a scene, e.g. mesh creation and destruction listeners
         */
        void setup_observers(Scene& scene);

        /**
         * Performs scene updates that rely on the Scene, e.g. updating the player view matrices and uploading bone
         * transforms
         */
        void tick(Scene& scene);

        /**
         * Creates a proxy for a mesh. Note that this is used for both skeletal and non-skeletal meshes.
         * create_skeletal_mesh_proxy calls this method internally, you don't need to call it on your own
         *
         * @param transform Model matrix for the mesh primitive
         * @param mesh Mesh to render
         * @param material Material to use for this mesh
         * @param visible_to_ray_tracing Whether or not this mesh should be added to the ray tracing scene
         * @return A handle to the newly-created proxy
         */
        MeshPrimitiveProxyHandle create_mesh_proxy(
            const float4x4& transform, MeshHandle mesh, PooledObject<BasicPbrMaterialProxy> material,
            bool visible_to_ray_tracing
            );

        SkeletalMeshPrimitiveProxyHandle create_skeletal_mesh_proxy(
            const float4x4& transform,
            const SkeletalMeshPrimitive& primitive,
            BufferHandle bone_matrices_buffer
            );

        void update_mesh_proxy(MeshPrimitiveProxyHandle handle);

        void update_mesh_proxy(SkeletalMeshPrimitiveProxyHandle handle);

        void destroy_primitive(MeshPrimitiveProxyHandle primitive);

        void destroy_primitive(SkeletalMeshPrimitiveProxyHandle proxy);

        PointLightProxyHandle create_light_proxy(const PointLightGPU& light);

        SpotLightProxyHandle create_light_proxy(const SpotLightGPU& light);

        void update_light_proxy(PointLightProxyHandle handle);

        void update_light_proxy(SpotLightProxyHandle handle);

        void destroy_light(PointLightProxyHandle proxy);

        void destroy_light(SpotLightProxyHandle proxy);

        void begin_frame(RenderGraph& graph);

        void deform_skinned_meshes(RenderGraph& graph);

        const eastl::vector<MeshPrimitiveProxyHandle>& get_solid_primitives() const;

        const eastl::vector<MeshPrimitiveProxyHandle>& get_masked_primitives() const;

        const eastl::vector<MeshPrimitiveProxyHandle>& get_transparent_primitives() const;

        BufferHandle get_primitive_buffer() const;

        uint32_t get_total_num_primitives() const;

        DirectionalLight& get_sun_light();

        const DirectionalLight& get_sun_light() const;

        const ProceduralSky& get_sky() const;

        BufferHandle get_point_lights_buffer() const;

        uint32_t get_num_point_lights() const;

        SceneView& get_player_view();

        const SceneView& get_player_view() const;

        /**
         * Retrieves a list of all solid primitives that lie within the given bounds
         */
        eastl::vector<MeshPrimitiveProxyHandle> get_primitives_in_bounds(
            const glm::vec3& min_bounds, const glm::vec3& max_bounds
            ) const;

        void draw_opaque(CommandBuffer& commands, GraphicsPipelineHandle pso) const;

        void draw_masked(CommandBuffer& commands, GraphicsPipelineHandle pso) const;

        /**
         * Draws the commands in the IndirectDrawingBuffers with the provided opaque PSO
         */
        void draw_opaque(
            CommandBuffer& commands, const IndirectDrawingBuffers& drawbuffers, GraphicsPipelineHandle solid_pso
            ) const;

        void draw_masked(
            CommandBuffer& commands, const IndirectDrawingBuffers& draw_buffers, GraphicsPipelineHandle masked_pso
            ) const;

        void draw_transparent(CommandBuffer& commands, GraphicsPipelineHandle pso) const;

        const MeshStorage& get_meshes() const;

        RaytracingScene& get_raytracing_scene();

        const RaytracingScene& get_raytracing_scene() const;

        MaterialStorage& get_material_storage() const;

        MeshStorage& get_mesh_storage() const;

        float get_fog_strength() const;

    private:
        MeshStorage& meshes;

        MaterialStorage& materials;

        eastl::optional<RaytracingScene> raytracing_scene = eastl::nullopt;

        SceneView player_view;

        DirectionalLight sun;

        ProceduralSky sky;

        // TODO: More fog parameters, maybe a volumetric thing
        float fog_strength = 0.01f;

        ObjectPool<MeshPrimitiveProxy> static_mesh_primitives;

        ObjectPool<SkeletalMeshPrimitiveProxy> skeletal_mesh_primitives;
        eastl::vector<SkeletalMeshPrimitiveProxyHandle> active_skeletal_meshes;

        BufferHandle primitive_data_buffer;
        ScatterUploadBuffer<PrimitiveDataGPU> primitive_upload_buffer;

        BufferHandle skeletal_data_buffer;
        ScatterUploadBuffer<SkeletalPrimitiveDataGPU> skeletal_data_upload_buffer;

        ComputePipelineHandle vertex_deformer = nullptr;

        // TODO: Group solid primitives by front face

        eastl::vector<MeshPrimitiveProxyHandle> solid_primitives;

        // TODO: Group masked primitives by front face and cull mode

        eastl::vector<MeshPrimitiveProxyHandle> masked_primitives;

        eastl::vector<MeshPrimitiveProxyHandle> translucent_primitives;

        eastl::vector<MeshPrimitiveProxyHandle> new_emissive_objects;

        ComputePipelineHandle emissive_point_cloud_shader;

        ObjectPool<PointLightProxy> point_lights;
        ScatterUploadBuffer<PointLightGPU> point_light_uploads;
        BufferHandle point_light_data_buffer;

        ObjectPool<SpotLightProxy> spot_lights;
        ScatterUploadBuffer<SpotLightGPU> spot_light_uploads;
        BufferHandle spot_light_data_buffer;

        void draw_primitives(
            CommandBuffer& commands, GraphicsPipelineHandle pso,
            eastl::span<const MeshPrimitiveProxyHandle> primitives
            ) const;

        // scene observers
        void on_construct_static_mesh(entt::registry& registry, entt::entity entity);

        void on_destroy_static_mesh(entt::registry& registry, entt::entity entity);

        void on_construct_skeletal_mesh(entt::registry& registry, entt::entity entity);

        void on_destroy_skeletal_mesh(entt::registry& registry, entt::entity entity);

        void on_construct_light(entt::registry& registry, entt::entity entity);

        void on_destroy_light(entt::registry& registry, entt::entity entity);

        void on_transform_update(entt::registry& registry, entt::entity entity);
    };
}
