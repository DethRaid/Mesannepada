#pragma once

#include <EASTL/vector.h>
#include <cereal/cereal.hpp>
#include <cereal/details/traits.hpp>

namespace eastl {
    //! Serialization for basic_string types, if binary data is supported
    template<class Archive, class ElemType, class Alloc>
    void CEREAL_SAVE_FUNCTION_NAME(Archive& ar, eastl::vector<ElemType, Alloc> const& str) {
        // Save number of chars + the data
        ar(cereal::make_size_tag(static_cast<cereal::size_type>(str.size())));
        for(const auto& elem : str) {
            ar(elem);
        }
    }

    //! Serialization for basic_string types, if binary data is supported
    template<class Archive, class ElemType, class Alloc>
    void CEREAL_LOAD_FUNCTION_NAME(Archive& ar, eastl::vector<ElemType, Alloc>& str) {
        cereal::size_type size;
        ar(cereal::make_size_tag(size));
        str.resize(static_cast<size_t>(size));
        for(auto& elem : str) {
            ar(elem);
        }
    }
}
