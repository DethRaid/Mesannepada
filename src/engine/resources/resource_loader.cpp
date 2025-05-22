#include "resource_loader.hpp"

#include <simdjson.h>

#include "core/engine.hpp"
#include "resources/godot_scene.hpp"
#include "resources/gltf_model.hpp"

static std::shared_ptr<spdlog::logger> logger;

ResourceLoader::ResourceLoader() : parser{
    fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_lights_punctual |
    fastgltf::Extensions::KHR_implicit_shapes |
    fastgltf::Extensions::KHR_physics_rigid_bodies
} {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("ResourceLoader");
    }
}

eastl::shared_ptr<IModel> ResourceLoader::get_model(const std::filesystem::path& model_path) {
    const auto model_path_string = eastl::string{model_path.string().c_str()};

    // If the model is already loaded, return it
    if(const auto itr = loaded_models.find(model_path_string); itr != loaded_models.end()) {
        return itr->second;
    }

    if(model_path.extension() == ".tscn") {
        load_godot_scene(model_path);
    } else if(model_path.extension() == ".glb" || model_path.extension() == ".gltf") {
        load_gltf_model(model_path);
    }

    return loaded_models.at(model_path_string);
}

void ResourceLoader::load_gltf_model(const std::filesystem::path& model_path) {
    ZoneScoped;

    const auto model_path_string = eastl::string{model_path.string().c_str()};

    logger->info("Beginning load of model {}", model_path.string());

    if(!exists(model_path)) {
        logger->error("Scene file {} does not exist!", model_path.string());
        throw std::runtime_error{"Scene does not exist"};
    }

    if(!model_path.has_parent_path()) {
        logger->warn("Scene path {} has no parent path!", model_path.string());
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(model_path);

    ExtrasData extras_data;
    parser.setExtrasParseCallback(
        [](
        // ReSharper disable once CppParameterMayBeConstPtrOrRef
        simdjson::dom::object* extras, const std::size_t object_index, const fastgltf::Category object_type,
        void* user_pointer
    ) {
            if(object_type == fastgltf::Category::Nodes) {
                auto* node_extras = static_cast<ExtrasData*>(user_pointer);
                const auto file_reference = extras->at_key("file_reference").get_string();
                if(file_reference.error() == simdjson::error_code::SUCCESS) {
                    node_extras->file_references_map.emplace(
                        object_index,
                        std::filesystem::path{file_reference.value_unsafe()});
                }

                const auto player_parent = extras->at_key("player_parent").get_bool();
                if(player_parent.error() == simdjson::error_code::SUCCESS) {
                    node_extras->player_parent_node = object_index;
                }

                const auto visible_to_rt = extras->at_key("visible_to_ray_tracing").get_bool();
                if(visible_to_rt.error() == simdjson::error_code::SUCCESS) {
                    node_extras->visible_to_ray_tracing.emplace(object_index, visible_to_rt.value_unsafe());
                }
            }
        });
    parser.setUserPointer(&extras_data);
    auto gltf = parser.loadGltf(data.get(), model_path.parent_path(), fastgltf::Options::LoadExternalBuffers);

    if(gltf.error() != fastgltf::Error::None) {
        logger->error("Could not load scene {}: {}", model_path.string(), fastgltf::getErrorMessage(gltf.error()));
        throw std::runtime_error{"Invalid glTF!"};
    }

    auto& renderer = Engine::get().get_renderer();
    auto imported_model = eastl::make_shared<GltfModel>(
        model_path,
        std::move(gltf.get()),
        renderer,
        eastl::move(extras_data));
    loaded_models.emplace(model_path_string, eastl::move(imported_model));
}

void ResourceLoader::load_godot_scene(const std::filesystem::path& scene_path) {
    ZoneScoped;

    const auto scene_path_string = eastl::string{scene_path.string().c_str()};

    auto scene = godot::GodotScene::load(scene_path);
    loaded_models.emplace(scene_path_string, eastl::make_shared<godot::GodotScene>(scene));
}
