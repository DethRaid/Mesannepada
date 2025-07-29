#pragma once

#include <EASTL/string.h>
#include <cereal/cereal.hpp>
#include <cereal/details/traits.hpp>

namespace eastl {
    //! Serialization for basic_string types, if binary data is supported
    template<class Archive, class CharT, class Alloc>
    std::enable_if_t<cereal::traits::is_output_serializable<cereal::BinaryData<CharT>, Archive>::value, void>
    CEREAL_SAVE_FUNCTION_NAME(Archive& ar, eastl::basic_string<CharT, Alloc> const& str) {
        // Save number of chars + the data
        ar(cereal::make_size_tag(static_cast<cereal::size_type>(str.size())));
        ar(cereal::binary_data(str.data(), str.size() * sizeof(CharT)));
    }

    //! Serialization for basic_string types, if binary data is supported
    template<class Archive, class CharT, class Alloc>
    std::enable_if_t<cereal::traits::is_input_serializable<cereal::BinaryData<CharT>, Archive>::value, void>
    CEREAL_LOAD_FUNCTION_NAME(Archive& ar, eastl::basic_string<CharT, Alloc>& str) {
        cereal::size_type size;
        ar(cereal::make_size_tag(size));
        str.resize(static_cast<size_t>(size));
        ar(cereal::binary_data(const_cast<CharT*>(str.data()), static_cast<size_t>(size) * sizeof(CharT)));
    }
}
