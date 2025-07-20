#pragma once

#include <EASTL/hash_map.h>
#include <cereal/types/concepts/pair_associative_container.hpp>

//! Saving for std-like pair associative containers
template<class Archive, typename KeyType, typename ValueType, typename = typename eastl::hash_map<
             KeyType, ValueType>::mapped_type>
inline
void CEREAL_SAVE_FUNCTION_NAME(Archive& ar, eastl::hash_map<KeyType, ValueType> const& map) {
    ar(cereal::make_size_tag(static_cast<cereal::size_type>(map.size())));

    for(const auto& i : map) {
        ar(cereal::make_map_item(i.first, i.second));
    }
}

//! Loading for std-like pair associative containers
template<class Archive, typename KeyType, typename ValueType, typename = typename eastl::hash_map<
             KeyType, ValueType>::mapped_type>
inline
void CEREAL_LOAD_FUNCTION_NAME(Archive& ar, eastl::hash_map<KeyType, ValueType>& map) {
    cereal::size_type size;
    ar(cereal::make_size_tag(size));

    map.clear();

    auto hint = map.begin();
    for(size_t i = 0; i < size; ++i) {
        typename eastl::hash_map<KeyType, ValueType>::key_type key;
        typename eastl::hash_map<KeyType, ValueType>::mapped_type value;

        ar(cereal::make_map_item(key, value));
        hint = map.emplace_hint(hint, std::move(key), std::move(value));
    }
}
