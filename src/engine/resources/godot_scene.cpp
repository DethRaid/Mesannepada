#include "godot_scene.hpp"

#include <entt/meta/factory.hpp>
#include <entt/core/hashed_string.hpp>

#include "core/engine.hpp"
#include "core/issue_breakpoint.hpp"
#include "core/spawn_gameobject_component.hpp"
#include "core/string_utils.hpp"
#include "core/system_interface.hpp"
#include "resources/model_components.hpp"
#include "scene/game_object_component.hpp"
#include "scene/scene.hpp"
#include "scene/transform_component.hpp"

using namespace entt::literals;

namespace godot {
    static std::shared_ptr<spdlog::logger> logger;

    static eastl::string_view get_key_value_pair(eastl::string_view str, eastl::string_view key);

    /**
     * Retrieves the text of a value, removing quotations if needed
     */
    static eastl::string_view get_value(eastl::string_view str, eastl::string_view key);

    /**
     * Retrieves a value from the string, converting it to the specified type. You need to make template
     * specializations for each type you care about
     */
    template<typename ValueType>
    static ValueType get_value(eastl::string_view str, eastl::string_view key);

    GodotScene GodotScene::load(const std::filesystem::path &filepath) {
        if (logger == nullptr) {
            logger = SystemInterface::get().get_logger("GodotScene");
        }

        const auto file_data = SystemInterface::get().load_file(filepath);
        const auto file_string = eastl::string_view{
            reinterpret_cast<const char *>(file_data->data()), file_data->size()
        };

        GodotScene scene;

        scene.file_path = filepath;

        // The first line of the file should be the header
        auto new_start = scene.load_header(file_string);

        new_start += scene.load_external_resources(
            eastl::string_view{file_string.begin() + new_start, file_string.size() - new_start});

        scene.load_nodes(eastl::string_view{file_string.begin() + new_start, file_string.size() - new_start});

        return eastl::move(scene);
    }

    entt::handle GodotScene::add_to_scene(Scene &scene_in, const eastl::optional<entt::handle> &parent_node) const {
        // Traverse the node tree, creating EnTT entities for each node. Hook up parent/child relationships as we go.
        // Save a map from node index to node entity for future use. Load external models
        eastl::vector<entt::handle> node_entities;
        node_entities.reserve(nodes.size());
        const auto root_entity = add_node_to_scene(scene_in, 0, node_entities);

        root_entity.emplace<ImportedModelComponent>(node_entities);

        if (!parent_node) {
            scene_in.add_top_level_entities(eastl::array{root_entity});
        } else {
            scene_in.parent_entity_to_entity(root_entity, *parent_node);
        }

        return root_entity;
    }

    eastl::optional<size_t> GodotScene::find_node(const eastl::string_view path) const {
        if (path == ".") {
            return 0;
        }

        const auto path_parts = split(path, '/');

        size_t cur_node_idx = 0;
        for (const auto &part: path_parts) {
            auto found_child = false;
            // Find a node in the current node's children that has the provided path part
            const auto &cur_node = nodes.at(cur_node_idx);
            for (const auto child_idx: cur_node.children) {
                if (nodes.at(child_idx).name == part) {
                    cur_node_idx = child_idx;
                    found_child = true;
                    break;
                }
            }

            if (!found_child) {
                // If we didn't find the node in the children of the current node, return nullopt
                return eastl::nullopt;
            }
        }

        return cur_node_idx;
    }

    size_t GodotScene::load_header(const eastl::string_view string) {
        const auto start = string.find('[');
        const auto end = string.find(']');

        const auto header_view = eastl::string_view{string.begin() + start + 1, end - start - 1};

        assert(header_view.find("gd_scene") == 0);

        header.load_steps = get_value<uint32_t>(header_view, "load_steps");

        header.format = get_value<uint32_t>(header_view, "format");

        header.uid = get_value(header_view, "uid");

        return end + 1;
    }

    size_t GodotScene::load_external_resources(const eastl::string_view string) {
        size_t search_start = 0;
        size_t block_start = 0;
        while ((block_start = string.find('[', search_start)) != eastl::string_view::npos) {
            const auto block_name_pointer = string.begin() + block_start + 1;
            const auto block_end = string.find(']', block_start);
            const auto block_view = eastl::string_view{block_name_pointer, block_end - block_start - 1};

            if (!block_view.starts_with("ext_resource")) {
                break;
            }

            ExternalResource resource;

            const auto type = get_value(block_view, "type");
            if (type == "PackedScene") {
                resource.type = ResourceType::PackedScene;
            } else {
                // We only care about packed scenes
                continue;
            }

            resource.uid = get_value(block_view, "uid");

            resource.path = get_value(block_view, "path");

            resource.id = get_value(block_view, " id");

            external_resources.emplace(resource.id, resource);

            search_start = block_end + 1;
        }

        return search_start;
    }

    void GodotScene::load_nodes(const eastl::string_view string) {
        size_t search_start = 0;
        size_t block_start = 0;
        while ((block_start = string.find('[', search_start)) != eastl::string_view::npos) {
            const auto block_name_pointer = string.begin() + block_start + 1;
            const auto block_end = string.find(']', block_start);
            const auto block_view = eastl::string_view{block_name_pointer, block_end - block_start - 1};

            search_start = block_end + 1;

            // Ignore blocks that are not nodes
            if (!block_view.starts_with("node")) {
                continue;
            }

            const auto my_index = nodes.size();
            Node node;

            node.name = get_value(block_view, "name");

            // Nodes may have a parent node, they may have an instance of an external resource, and a bunch of nodes
            // are just edits I've made to the glTF scenes so they look nicer in Godot. The nodes in the files I have
            // are in a sensible order, with child nodes appearing later in the file than their parents. The root node
            // is always the first, this assumption will carry me to my grave

            const auto parent_name = get_value(block_view, "parent");
            if (!parent_name.empty()) {
                // Find a node with the provided name
                // If the name is a single ., the parent is the root node
                // If the name is not, the parent is a node other than the root node

                const auto parent_index = find_node(parent_name);
                if (parent_index) {
                    node.parent = parent_index;
                    nodes[*parent_index].children.emplace_back(my_index);
                } else {
                    // This can happen when a Node specifies some edits to a glTF node
                    logger->warn(
                        "Node {} has parent {}, but that parent does not exist! Skipping adding node",
                        node.name.c_str(),
                        std::string_view{parent_name.data(), parent_name.size()});
                    continue;
                }
            }

            const auto instance_id = get_value(block_view, "instance");
            if (!instance_id.empty()) {
                // Parse out the inner ID, we don't need the rest
                const auto begin_quote_pos = instance_id.find('"');
                const auto end_quote_pos = instance_id.find('"', begin_quote_pos + 1);
                node.instance = eastl::string{
                    instance_id.begin() + begin_quote_pos + 1, end_quote_pos - begin_quote_pos - 1
                };
            }

            auto body_block_end = string.find('[', search_start);
            if(body_block_end == eastl::string_view::npos) {
                body_block_end = string.size();
            }
            const auto block_body = eastl::string_view{
                string.begin() + search_start, body_block_end - search_start - 1
            };

            // Does the node have a non-default transform?
            if (const auto transform_start = block_body.find("transform");
                transform_start != eastl::string_view::npos) {
                const auto transform_nums_begin = block_body.find('(', transform_start);
                const auto transform_nums_end = block_body.find(')', transform_start);
                const auto transform_nums_view = eastl::string_view{
                    block_body.begin() + transform_nums_begin + 1, transform_nums_end - transform_nums_begin - 1
                };

                const auto nums = split(transform_nums_view, ',');

                // Godot stores the upper 3x3 of the transform matrix first
                auto num_idx = 0;
                for (auto x = 0; x < 3; x++) {
                    for (auto y = 0; y < 3; y++) {
                        node.transform[y][x] = from_string<float>(nums.at(num_idx));
                        num_idx++;
                    }
                }

                // Then the origin of the object
                for (auto y = 0; y < 3; y++) {
                    node.transform[3][y] = from_string<float>(nums.at(num_idx));
                    num_idx++;
                }
            }

            // Do we have any metadata keys?
            auto metadata_start = block_body.find(eastl::string{"metadata/"});
            while (metadata_start != eastl::string_view::npos) {
                const auto metadata_end = block_body.find('\n', metadata_start);
                // Subtract 9 to account for `metadata/`
                const auto metadata_line = eastl::string_view{block_body.begin() + metadata_start + 9, metadata_end - metadata_start - 9};

                const auto key_end = metadata_line.find(' ');
                const auto value_begin = metadata_line.find(' ', key_end + 1);

                const auto key = eastl::string_view{metadata_line.begin(), key_end};
                const auto value = eastl::string_view{metadata_line.begin() + value_begin + 2, metadata_line.size() - value_begin - 3};
                node.metadata.emplace(eastl::string{key}, eastl::string{value});

                metadata_start = block_body.find("metadata/", metadata_end);
            }

            nodes.emplace_back(node);
        }
    }

    entt::handle GodotScene::add_node_to_scene(
        Scene &scene, const size_t node_index, eastl::vector<entt::handle> &node_entities
    ) const {
        const auto &node = nodes.at(node_index);
        // Create this node
        const auto entity = scene.create_game_object(node.name);
        entity.patch<TransformComponent>(
            [&](TransformComponent &transform) {
                transform.set_local_transform(node.transform);
            });

        if (node_entities.size() <= node_index) {
            node_entities.resize(node_index + 1);
        }
        node_entities[node_index] = entity;

        if (auto itr = node.metadata.find("spawn_gameobject"); itr != node.metadata.end()) {
            entity.emplace<SpwanPrefabComponent>(itr->second);
        }

        if (node.instance) {
            const auto &resource = external_resources.at(*node.instance);
            const auto resource_path = resource.path.substr(6); // Lop off "res://"
            auto full_resource_path = std::filesystem::path{"data"} / "game" / resource_path.c_str();
            full_resource_path.make_preferred();
            auto &resources = Engine::get().get_resource_loader();
            const auto instanced_model = resources.get_model(full_resource_path);
            instanced_model->add_to_scene(scene, entity);
        }

        for (const auto &child_node: node.children) {
            const auto child_entity = add_node_to_scene(scene, child_node, node_entities);
            scene.parent_entity_to_entity(child_entity, entity);
        }

        return entity;
    }

    template<typename ValueType>
    ValueType get_value(const eastl::string_view str, const eastl::string_view key) {
        return from_string<ValueType>(get_value(str, key));
    }

    static eastl::string_view get_value(eastl::string_view str);

    eastl::string_view get_value(const eastl::string_view str, const eastl::string_view key) {
        return get_value(get_key_value_pair(str, key));
    }

    eastl::string_view get_value(const eastl::string_view str) {
        const auto view = str.substr(str.find('=') + 1);
        if (view.starts_with('"')) {
            // This view is of a string, trim off the quotes
            return eastl::string_view{view.begin() + 1, view.size() - 2};
        } else {
            // This is a view of a number, return as-id
            return view;
        }
    }

    eastl::string_view get_key_value_pair(const eastl::string_view str, const eastl::string_view key) {
        const auto start = str.find(key);
        if (start == eastl::string_view::npos) {
            return {};
        }

        const auto end = str.find(' ', start + 1);
        if (end != eastl::string_view::npos) {
            return eastl::string_view{str.begin() + start, end - start};
        } else {
            return eastl::string_view{str.begin() + start, str.size() - start};
        }
    }
}
