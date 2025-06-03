#pragma once

#include <filesystem>

#include <EASTL/unordered_map.h>
#include <EASTL/optional.h>
#include <EASTL/unordered_set.h>

#include <entt/entity/entity.hpp>
#include <entt/resource/cache.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <fastgltf/types.hpp>

#include "animation/animation_system.hpp"
#include "animation/animation_system.hpp"
#include "resources/gltf_animations.hpp"
#include "resources/imodel.hpp"
#include "physics/physics_scene.hpp"
#include "physics/physics_shape_loader.hpp"
#include "render/mesh_primitive_proxy.hpp"
#include "render/texture_type.hpp"
#include "render/material_storage.hpp"
#include "render/mesh_storage.hpp"

class Scene;

namespace render {
    class RenderBackend;
    class SarahRenderer;
    class TextureLoader;
}

glm::mat4 get_node_to_parent_matrix(const fastgltf::Node& node);

struct ExtrasData {
    eastl::unordered_map<std::size_t, std::filesystem::path> file_references_map;
    eastl::unordered_map<std::size_t, bool> visible_to_ray_tracing;
    size_t player_parent_node = std::numeric_limits<size_t>::max();
};

/**
 * Class for a glTF model
 *
 * This class performs a few functions: It loads the glTF model from disk, it imports its data into the render context,
 * and it provides the glTF data in a runtime-friendly way
 */
class GltfModel : public IModel {
public:
    GltfModel(
        std::filesystem::path filepath_in, fastgltf::Asset&& model, render::SarahRenderer& renderer,
        ExtrasData extras_in
    );

    ~GltfModel() override;

    glm::vec4 get_bounding_sphere() const;

    const fastgltf::Asset& get_gltf_data() const;

    /**
     * Depth-first traversal of the node hierarchy
     *
     * TraversalFunction must have the following type signature: void(const size_t node_index, const fastgltf::Node& node, const glm::mat4& local_to_world)
     */
    template <typename TraversalFunction>
    void traverse_nodes(TraversalFunction traversal_function, const float4x4& parent_to_world) const;

    entt::handle add_to_scene(Scene& scene_in, const eastl::optional<entt::entity>& parent_node) const override;

    const ExtrasData& get_extras() const;

    /**
     * Finds the index of the node with the given name, or SIZE_T_MAX if it can't be found
     */
    size_t find_node(eastl::string_view name) const;

private:
    /**
     * Filepath to the glTF file itself
     */
    std::filesystem::path filepath;

    /**
     * Filepath to the file's cached data. This is a folder with the same name as the glTF file, but with the extension removed
     */
    std::filesystem::path cached_data_path;

    fastgltf::Asset asset;

    eastl::unordered_map<size_t, render::TextureHandle> gltf_texture_to_texture_handle;

    eastl::vector<PooledObject<render::BasicPbrMaterialProxy>> gltf_material_to_material_handle;

    // Outer vector is the mesh, inner vector is the primitives within that mesh
    eastl::vector<eastl::vector<render::MeshHandle>> gltf_primitive_to_mesh_primitive;

    eastl::vector<JPH::PhysicsMaterialRefC> gltf_physics_material_to_jolt;

    /**
     * Nodes which may be animated. Static nodes have their transforms fully collapsed, dynamic nodes have a parent and a local model matrix
     */
    eastl::unordered_set<size_t> dynamic_nodes;

    glm::vec4 bounding_sphere = {};

    ExtrasData extras;

#pragma region init
    void import_resources_for_model(render::SarahRenderer& renderer);

    void import_materials(
        render::MaterialStorage& material_storage, render::TextureLoader& texture_loader, render::RenderBackend& backend
    );

    void import_meshes(render::SarahRenderer& renderer);

    void import_skins(render::MeshStorage& mesh_storage);

    /**
     * Adds all of this model's animations to the global animation system
     */
    void import_animations() const;

    void calculate_bounding_sphere_and_footprint();

#pragma endregion

    entt::handle add_nodes_to_scene(Scene& scene, const eastl::optional<entt::entity>& parent_node) const;

    void add_static_mesh_component(const entt::handle& entity, const fastgltf::Node& node, size_t node_index) const;

    void add_skeletal_mesh_component(entt::handle entity, const fastgltf::Node& node, size_t node_index) const;

    void add_collider_component(
        const entt::handle& entity, const fastgltf::Node& node, size_t node_index, const float4x4& transform
    ) const;

    static void add_light_component(const entt::handle& entity, const fastgltf::Light& light);

    JPH::Ref<JPH::Shape> get_collider_for_node(size_t node_index, const fastgltf::Collider& collider) const;

    JPH::Ref<JPH::Shape> create_jolt_shape(const fastgltf::Collider& collider) const;

    template <typename TraversalFunction>
    void visit_node(
        TraversalFunction traversal_function, size_t node_index, const fastgltf::Node& node,
        const float4x4& parent_to_world
    ) const;

    render::TextureHandle get_texture(
        size_t gltf_texture_index, render::TextureType type, render::TextureLoader& texture_storage
    );

    void import_single_texture(
        size_t gltf_texture_index, render::TextureType type, render::TextureLoader& texture_storage
    );

    static VkSampler to_vk_sampler(const fastgltf::Sampler& sampler, const render::RenderBackend& backend);

    /**
     * Reads the collider-relevant mesh data from the node with the provided index
     *
     * It isn't great to copy the mesh data once for the render mesh and once for the collision shape, but this works
     */
    eastl::pair<JPH::VertexList, JPH::IndexedTriangleList> read_collision_mesh_from_node(size_t node_idx) const;
};

template <typename TraversalFunction>
void GltfModel::traverse_nodes(TraversalFunction traversal_function, const float4x4& parent_to_world) const {
    const auto& gltf_scene = asset.scenes[*asset.defaultScene];
    for(const auto& node : gltf_scene.nodeIndices) {
        visit_node(traversal_function, node, asset.nodes[node], parent_to_world);
    }
}

template <typename TraversalFunction>
void GltfModel::visit_node(
    TraversalFunction traversal_function, const size_t node_index, const fastgltf::Node& node,
    const float4x4& parent_to_world
) const {
    traversal_function(node_index, node, parent_to_world);

    const auto local_to_parent = get_node_to_parent_matrix(node);
    const auto local_to_world = parent_to_world * local_to_parent;

    for(const auto& child_node : node.children) {
        visit_node(traversal_function, child_node, asset.nodes[child_node], local_to_world);
    }
}
