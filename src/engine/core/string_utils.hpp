#pragma once

#include <sstream>
#include <EASTL/vector.h>
#include <EASTL/string_view.h>

eastl::vector<eastl::string_view> split(eastl::string_view s, char delimiter);

template<typename DestType>
DestType from_string(eastl::string_view str);

template <typename DestType>
DestType from_string(const eastl::string_view str) {
    DestType val{ 0 };

    // C++26 will let this constructor take a string_view. For now we need to copy. Cry.
    std::istringstream iss{ std::string{str.data(), str.size()} };
    iss >> val;
    if (iss.fail()) {
        return eastl::numeric_limits<DestType>::max();
    }
    return val;
}
