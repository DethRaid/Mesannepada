#include "serializers.hpp"

#include <fstream>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include "console/cvars.hpp"
#include "core/system_interface.hpp"
#include "reflection/reflection_macros.hpp"
#include "scene/scene_file.hpp"
#include "scene/world.hpp"
#include "reflection/serialization/glm.hpp"
#include "reflection/serialization/simdjson_eastl_adapters.hpp"
#include "reflection/serialization/serializers.hpp"

#define SERIALIZE_SCALAR(Scalar) \
    entt::meta_factory<Scalar>()                                                                        \
        .func<serialize_scalar<cereal::BinaryInputArchive, Scalar>>("serialize_from_binary"_hs)         \
        .func<serialize_scalar<cereal::BinaryOutputArchive, const Scalar>>("serialize_to_binary"_hs)    \
        .func<from_json_scalar<Scalar>>("from_json"_hs)

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

        entt::meta_factory<eastl::string>{}
            .func<from_json_scalar<eastl::string>>("from_json"_hs);
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

    bool from_json(simdjson::simdjson_result<simdjson::ondemand::value> json, entt::meta_any value) {
        ZoneScoped;

        // If the type has a bespoke serialization function, use that
        if(auto func = value.type().func("from_json"_hs); func) {
            func.invoke({}, entt::forward_as_meta(json), value.as_ref());
            return true;
        }

        auto success = true;

        // Handle containers
        if(value.type().is_sequence_container()) {
            auto sequence = value.as_sequence_container();

            auto array = json.get_array();
            const auto size = array.count_elements();
            if(size.value_unsafe() > 0) {
                sequence.resize(size.value_unsafe());
                auto i = 0u;
                for(auto element : array) {
                    success |= from_json(element, sequence[i].as_ref());
                    i++;
                }
            }

            return success;
        }

        // Handle enums as their underlying types
        if(value.type().is_enum()) {
            auto to_underlying = value.type().func("to_underlying"_hs);
            assert(to_underlying);
            auto underlying = to_underlying.ret().construct();
            success = from_json(json, underlying.as_ref());
            value.assign(underlying);

            return success;
        }

        // If we're here then we have a complex type that can be reflected
        for(auto [id, data] : value.type().data()) {
            if(data.traits<reflection::Traits>() & reflection::Traits::Transient) {
                continue;
            }

            // For each member, try and find a JSON element with that name. If we find one, load it

            // If there's a value in the JSON with the name of one of the component's members, deserialize it
            const auto& custom = data.custom();
            reflection::PropertiesMap properties = {};
            if(auto* mp = static_cast<const reflection::PropertiesMap*>(custom)) {
                properties = *mp;
            }
            if(const auto it = properties.find("name"_hs); it != properties.end()) {
                const auto member_name = it->second.cast<const char*>();
                auto json_member_data = json[member_name];
                if(json_member_data.error() == simdjson::SUCCESS) {
                    success |= from_json(json_member_data, data.get(value).as_ref());
                }
            }
        }

        return success;
    }
}
