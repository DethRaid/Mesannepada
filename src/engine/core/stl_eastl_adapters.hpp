#pragma once

/**
 * Adapters so the STL and EASTL can live together in harmony
 */

#include <filesystem>

#include <EASTL/functional.h>

template<>
struct eastl::hash<std::filesystem::path> {
    size_t operator()(const std::filesystem::path& path) const {
        return std::hash<std::filesystem::path>{}(path);
    }
};

