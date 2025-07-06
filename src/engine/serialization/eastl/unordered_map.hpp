#pragma once

#include <cereal/types/concepts/pair_associative_container.hpp>
#include <EASTL/hash_map.h>

    //! Saving for std-like pair associative containers
  template <class Archive, typename... Args, typename = typename eastl::hash_map<Args...>::mapped_type> inline
  void CEREAL_SAVE_FUNCTION_NAME( Archive & ar, eastl::hash_map<Args...> const & map )
  {
    ar( cereal::make_size_tag( static_cast<cereal::size_type>(map.size()) ) );

    for( const auto & i : map )
      ar( make_map_item(i.first, i.second) );
  }

  //! Loading for std-like pair associative containers
  template <class Archive, typename... Args, typename = typename eastl::hash_map<Args...>::mapped_type> inline
  void CEREAL_LOAD_FUNCTION_NAME( Archive & ar, eastl::hash_map<Args...> & map )
  {
    cereal::size_type size;
    ar( cereal::make_size_tag( size ) );

    map.clear();

    auto hint = map.begin();
    for( size_t i = 0; i < size; ++i )
    {
      typename eastl::hash_map<Args...>::key_type key;
      typename eastl::hash_map<Args...>::mapped_type value;

      ar( make_map_item(key, value) );
      #ifdef CEREAL_OLDER_GCC
      hint = map.insert( hint, std::make_pair(std::move(key), std::move(value)) );
      #else // NOT CEREAL_OLDER_GCC
      hint = map.emplace_hint( hint, std::move( key ), std::move( value ) );
      #endif // NOT CEREAL_OLDER_GCC
    }
  }