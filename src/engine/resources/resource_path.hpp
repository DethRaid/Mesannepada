#pragma once

#include <filesystem>

#include <EASTL/string.h>
#include <spdlog/fmt/bundled/base.h>

/**
 * A path to a resource. Automatically resolved to one of our known directories when you get the filepath
 *
 * Resource paths are URIs. They can have a few beginnings:
 *  - res:// is a generic resource in the data/ directory
 *  - shader:// is a shader file in the shader/ directory
 *  - game:// is a generic resource in the data/game/ directory
 *  - file:// is a generic file, either relative to the working directory or an absolute filepath
 */
struct ResourcePath {
    enum class Scope {
        File,
        Resource,
        Shader,
        Game
    };

    static ResourcePath file(const std::string_view path_in) {
        return ResourcePath{Scope::File, path_in};
    }

    ResourcePath() = default;

    /**
     * Constructs a resource path from the given string. We extract the scope from the path
     */
    explicit ResourcePath(eastl::string_view path_in);

    explicit ResourcePath(std::string_view path_in);

    /**
     * Constructs a resource path with an explicit scope
     */
    explicit ResourcePath(const Scope scope_in, const std::string_view path_in) :
        scope{scope_in}, path{path_in.begin(), path_in.size()} {
    }

    bool operator==(const ResourcePath& other) const;

    bool ends_with(eastl::string_view end) const;

    std::filesystem::path to_filepath() const;

    eastl::string to_string() const;

    Scope get_scope() const {
        return scope;
    }

    const eastl::string& get_path() const;

    Scope scope = Scope::File;

    eastl::string path = {};
};

ResourcePath operator ""_res(const char* path, size_t size);

template<>
struct fmt::formatter<ResourcePath> : formatter<const char*> {
    auto format(const ResourcePath& c, format_context& ctx) const {
        const auto str = c.to_string();
        return formatter<const char*>::format(str.c_str(), ctx);
    }
};

template<>
struct eastl::hash<ResourcePath> {
    size_t operator()(const ResourcePath& path) const {
        uint32_t  result = 2166136261U; // Fallout New Vegas 1 hash
        result = (result * 16777619) ^ eastl::hash<uint32_t>{}(static_cast<uint32_t>(path.get_scope()));
        result = (result * 16777619) ^ eastl::hash<const char*>{}(path.get_path().c_str());
        return result;
    }
};
