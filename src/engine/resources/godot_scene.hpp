#pragma once

#include <filesystem>

#include <EASTL/unordered_map.h>
#include <EASTL/string.h>
#include <EASTL/optional.h>

#include "EASTL/fixed_vector.h"
#include "resources/imodel.hpp"
#include "shared/prelude.h"

namespace godot {
    struct Header {
        uint32_t load_steps;
        uint32_t format;
        eastl::string uid;
    };

    enum class ResourceType : uint8_t {
        PackedScene,
    };

    struct ExternalResource {
        ResourceType type;
        eastl::string uid;
        eastl::string path;
        eastl::string id;
    };

    struct Node {
        eastl::string name;
        eastl::optional<size_t> parent;
        eastl::optional<eastl::string> instance;
        float4x4 transform = float4x4{1.f};
        eastl::fixed_vector<size_t, 8> children;
        eastl::unordered_map<eastl::string, eastl::string> metadata;

        bool operator==(const Node& other) const {
            return name == other.name && parent == other.parent;
        }
    };

    /**
     * Represents a scene created by Godot
     */
    class GodotScene final : public IModel {
    public:
        static GodotScene load(const std::filesystem::path& filepath);

        ~GodotScene() override = default;

        entt::handle add_to_world(World& world_in, const eastl::optional<entt::handle>& parent_node) const override;

        /**
         * Finds the node with the specified path
         */
        eastl::optional<size_t> find_node(eastl::string_view path) const;

    private:
        std::filesystem::path file_path;

        Header header = {};

        eastl::unordered_map<eastl::string, ExternalResource> external_resources;

        eastl::vector<Node> nodes;

        /**
         * Reads the header from the provided string view, returning the number of characters this method consumed
         */
        size_t load_header(eastl::string_view string);

        /**
         * Reads the external resources from the provided string view, returning the number of characters this method consumed
         */
        size_t load_external_resources(eastl::string_view string);

        /**
         * Reads all the nodes from the string, constructing a node tree
         */
        void load_nodes(eastl::string_view string);

        entt::handle add_node_to_world(
            World& world, size_t node_index, eastl::vector<entt::handle>& node_entities
        ) const;
    };
}
