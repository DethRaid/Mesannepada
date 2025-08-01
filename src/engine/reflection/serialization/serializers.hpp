#pragma once

#include <filesystem>

#include <cereal/cereal.hpp>
#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>
#include <simdjson.h>

#include "reflection/reflection_types.hpp"
#include "shared/prelude.h"

class World;
using namespace entt::literals;

namespace serialization {
    void register_serializers();

    void save_world_to_file(const std::filesystem::path& filepath, const World& world);

    template<typename Archive, typename ValueType>
    void serialize_scalar(Archive& ar, ValueType& value);

    template<bool Save, typename Archive>
    void serialize(Archive& ar, entt::meta_any value);

    // Kinda a hack, would be better to be able to save an entity as a prefab with the existing serialize machinery
    template<typename ValueType>
    bool from_json_scalar(simdjson::simdjson_result<simdjson::ondemand::value> json, ValueType& value);

    // Kinda a hack, would be better to be able to save an entity as a prefab with the existing serialize machinery
    bool from_json(simdjson::simdjson_result<simdjson::ondemand::value> json, entt::meta_any value);

    template<typename Archive, typename ValueType>
    void serialize_scalar(Archive& ar, ValueType& value) {
        ar(value);
    }

    template<bool Save, typename Archive>
    void serialize(Archive& ar, entt::meta_any value) {
        ZoneScoped;

        // If the type is trivial, serialize it trivially
        if(value.type().traits<reflection::Traits>() & reflection::Traits::Trivial) {
            auto* vp = const_cast<void*>(value.base().data());
            auto binary = cereal::binary_data(vp, value.type().size_of());
            ar(binary);
            return;
        }

        // If the type has a bespoke serialization function, use that
        // We gotta check for a function that takes an archive, since we use the same code for serialization and
        // deserialization
        const auto archive_hash = entt::type_id<Archive>();
        for(const auto& [id, func] : value.type().func()) {
            if(func.arg(0).info() == archive_hash) {
                auto ret = func.invoke({}, entt::forward_as_meta(ar), value.as_ref());
                assert(ret);
                return;
            }
        }

        // Handle containers
        if(value.type().is_sequence_container()) {
            auto sequence = value.as_sequence_container();

            if constexpr(Save) {
                auto size = static_cast<uint32_t>(sequence.size());
                serialize<Save>(ar, entt::forward_as_meta(size));
            } else {
                auto size = 0u;
                serialize<Save>(ar, entt::forward_as_meta(size));
                sequence.resize(size);
            }

            if(sequence.size() > 0 &&
               sequence.value_type().traits<reflection::Traits>() & reflection::Traits::Trivial) {
                // Assume the sequence container is contiguous and can be treated like a binary blob. This is likely
                // true
                auto* vp = const_cast<void*>(sequence.begin().base().data());
                auto binary = cereal::binary_data(vp, sequence.size() * sequence.value_type().size_of());
                ar(binary);
            } else {
                for(auto element : sequence) {
                    serialize<Save>(ar, element.as_ref());
                }
            }

            return;
        }

        // Handle enums as their underlying types
        if(value.type().is_enum()) {
            auto to_underlying = value.type().func("to_underlying"_hs);
            assert(to_underlying);
            if constexpr(Save) {
                serialize<Save>(ar, to_underlying.invoke({}, value));
            } else {
                auto underlying = to_underlying.ret().construct();
                serialize<Save>(ar, underlying.as_ref());
                value.assign(underlying);
            }

            return;
        }

        // If we're here then we have a complex type that can be reflected
        for(auto [id, data] : value.type().data()) {
            if(data.traits<reflection::Traits>() & reflection::Traits::Transient) {
                continue;
            }

            if(data.traits<reflection::Traits>() & reflection::Traits::Trivial) {
                auto* vp = const_cast<void*>(data.get(value.as_ref()).base().data());
                auto binary = cereal::binary_data(vp, data.type().size_of());
                ar(binary);
            } else {
                serialize<Save>(ar, data.get(value).as_ref());
            }
        }
    }

    template<typename ValueType>
    bool from_json_scalar(simdjson::simdjson_result<simdjson::ondemand::value> json, ValueType& value) {
        return json.get(value) == simdjson::SUCCESS;
    }
} // namespace serialization
