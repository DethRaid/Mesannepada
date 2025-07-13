#include "serializers.hpp"

#include <fstream>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "glm.hpp"
#include "reflection/reflection_macros.hpp"
#include "scene/world.hpp"
#include "scene/scene_file.hpp"

#define SERIALIZE_SCALAR(Scalar) \
    entt::meta_factory<Scalar>()                                                                        \
        .func<serialize_scalar<cereal::BinaryInputArchive, Scalar>>("serialize_from_binary"_hs)         \
        .func<serialize_scalar<cereal::BinaryOutputArchive, const Scalar>>("serialize_to_binary"_hs)    \
        .func<serialize_scalar<cereal::JSONInputArchive, Scalar>>("serialize_from_json"_hs)             \
        .func<serialize_scalar<cereal::JSONOutputArchive, const Scalar>>("serialize_to_json"_hs)

namespace serialization {
    /**
     * How to save the world files. Should use binary when shipping, can use json during development
     */
    enum class SaveFileFormat {
        Json,
        Binary,
    };

    static std::shared_ptr<spdlog::logger> logger;

    void register_serializers() {
        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("Serialization");
        }

        ZoneScoped;

        SERIALIZE_SCALAR(bool);
        SERIALIZE_SCALAR(float);
        SERIALIZE_SCALAR(double);
        SERIALIZE_SCALAR(int8_t);
        SERIALIZE_SCALAR(uint8_t);
        SERIALIZE_SCALAR(int16_t);
        SERIALIZE_SCALAR(uint16_t);
        SERIALIZE_SCALAR(int32_t);
        SERIALIZE_SCALAR(uint32_t);

        entt::meta_factory<float4x4>()
            .func<glm::serialize<cereal::BinaryInputArchive> >("serialize_from_binary"_hs)
            .func<glm::serialize<cereal::BinaryOutputArchive> >("serialize_to_binary"_hs);

        entt::meta_factory<SceneObject>()
            .traits(reflection::Traits::Trivial)
            DATA(SceneObject, filepath)
            DATA(SceneObject, location)
            DATA(SceneObject, orientation)
            DATA(SceneObject, scale);
    }

    template<typename Archive>
    void save_world_to_file_impl(std::ostream& file, const entt::registry& registry) {
        auto output_archive = Archive{file};

        auto num_sets = 0u;
        for(const auto& p : registry.storage()) {
            if(entt::resolve(p.first).traits<reflection::Traits>() & reflection::Traits::Component) {
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
    }

    void save_world_to_file(const std::filesystem::path& filepath, const World& world) {
        ZoneScoped;

        logger->info("Saving world to {}", filepath);

        const auto& registry = world.get_registry();

        auto file = std::ofstream{filepath, std::ios::out | std::ios::trunc | std::ios::binary};
        if(!file) {
            throw std::runtime_error{"Could not open world file"};
        }

        save_world_to_file_impl<cereal::BinaryOutputArchive>(file, registry);

        logger->info("Saving complete");
    }
}
