#include "world_serializer.hpp"

#include <fstream>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>

#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "scene/scene.hpp"

/**
 * How to save the world files. Should use binary when shipping, can use json during development
 */
enum class SaveFileFormat {
    Json,
    Binary,
};

static auto cvar_file_format = AutoCVar_Enum{"e.World.SaveFileFormat", "What file format to use when saving files",
                                             SaveFileFormat::Json};

static std::shared_ptr<spdlog::logger> logger;

void serialization::save_world_to_file(const std::filesystem::path& filepath, const World& scene) {
    if(logger == nullptr) {
        logger = SystemInterface::get().get_logger("WorldSerialization");
    }

    ZoneScoped;

    logger->info("Saving world to {}", filepath);

    const auto& registry = scene.get_registry();

    auto bits = std::ios::out | std::ios::trunc;
    if(cvar_file_format.get() == SaveFileFormat::Binary) {
        bits |= std::ios::binary;
    }

    auto file = std::ofstream{filepath, bits};
    if(!file) {
        throw std::runtime_error{"Could not open world file"};
    }

    auto output_archive = cvar_file_format.get() == SaveFileFormat::Binary
                              ? cereal::BinaryOutputArchive{file}
                              : cereal::JSONOutputArchive{file};

    auto num_sets = 0u;
    for(const auto& p : registry.storage()) {
        if(entt::resolve{p.first}.traits<reflection::Traits>() & reflection::Traits::Component) {
            num_sets++;
        }
    }
    serialize<true>(output_archive, num_sets);

    for(const auto& [id, set] : registry.storage()) {
        if(auto meta = entt::resolve(id)) {
            const auto traits = meta.traits<reflection::Traits>();
            if(traits & reflection::Traits::Component) {
                ZoneScopedN("Component");
                serialize<true>(output_archive, id);
                serialize<true>(output_archive, static_cast<uint32_t>(set.size()));
                for(const auto& entity : set) {
                    serialize<true>(output_archive, entity);
                    if(!(traits & reflection::Traits::Transient)) {
                        serialize<true>(output_archive, meta.from_void(set.value(entity)));
                    }
                }
            }
        }
    }

    logger->info("Saving complete");
}
