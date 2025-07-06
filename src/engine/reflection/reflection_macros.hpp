#pragma once

#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>
#include <entt/entt.hpp>

#include "reflection/reflection_types.hpp"

/**
 * \file reflection_macros.hpp
 *
 * \brief Macros to help with reflection
 */

namespace reflection {
#define TRAITS(TraitsV) .traits(TraitsV)
#define DATA(Type, Member, ...)                                                             \
    .data<&Type :: Member, entt::as_ref_t>(#Member##_hs)                                    \
    .custom<PropertiesMap>(PropertiesMap{{"name"_hs, #Member} __VA_OPT__(, __VA_ARGS__)})

#define REFLECT_SCALAR(Scalar)                                                                              \
entt::meta_factory<Scalar>()                                                                                \
    .func<&editor_write_scalar<Scalar>>("editor_write"_hs)                                                  \
    .func<&editor_read_scalar<Scalar>>("editor_read"_hs)                                                    \
    .func<serialization::serialize<cereal::BinaryInputArchive, Scalar>>("serialize_from_binary"_hs)         \
    .func<serialization::serialize<cereal::BinaryOutputArchive, const Scalar>>("serialize_to_binary"_hs)    \
    .func<serialization::serialize<cereal::JSONInputArchive, Scalar>>("serialize_from_json"_hs)             \
    .func<serialization::serialize<cereal::JSONOutputArchive, const Scalar>>("serialize_to_json"_hs)        \
    .traits(Traits::Trivial)

#define REFLECT_COMPONENT(T, ...)                                                                   \
__VA_OPT__(static_assert(!((__VA_ARGS__) & Traits::Trivial) || eastl::is_trivially_copyable_v<T>);) \
entt::meta_factory<T>{}                                                                             \
    .type(entt::hashed_string{#T}.value())                                                          \
    .traits(Traits::Component __VA_OPT__(| __VA_ARGS__))                                            \
    .func<[](entt::registry* registry, entt::entity entity) { registry->emplace<T>(entity); }>("emplace_default"_hs)    \
    .func<[](entt::registry* registry, entt::entity entity, T& value) { registry->emplace_or_replace<T>(entity, eastl::move(value)); }>("emplace_move"_hs)
}
