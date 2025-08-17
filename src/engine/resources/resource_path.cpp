#include "resource_path.hpp"

#include "core/string_utils.hpp"
#include "core/system_interface.hpp"

ResourcePath::ResourcePath(const eastl::string_view path_in) {
    parse_from_string(path_in);
}

ResourcePath::ResourcePath(const std::string_view path_in) :
    ResourcePath{eastl::string_view{path_in.data(), path_in.size()}} {
}

static const char* to_string(const ResourcePath::Scope scope) {
    switch(scope) {
    case ResourcePath::Scope::Resource:
        return "res://";
    case ResourcePath::Scope::Shader:
        return "shader://";
    case ResourcePath::Scope::Game:
        return "game://";
    case ResourcePath::Scope::File:
        [[fallthrough]];
    default:
        return "file://";
    }
}

bool ResourcePath::operator==(const ResourcePath& other) const {
    return scope == other.scope && path == other.path;
}

bool ResourcePath::ends_with(const eastl::string_view end) const {
    return path.extension() == std::string_view{end.data(), end.size()};
}

std::filesystem::path ResourcePath::to_filepath() const {
    auto base_path = std::filesystem::path{};
    switch(scope) {
    case Scope::File:
        // Base path unchanged
        break;
    case Scope::Resource:
        base_path = SystemInterface::get().get_data_folder();
        break;
    case Scope::Shader:
        base_path = SystemInterface::get().get_shaders_folder();
        break;
    case Scope::Game:
        base_path = SystemInterface::get().get_data_folder() / "game";
        break;
    }

    return base_path.empty() ? path : base_path / path;
}

eastl::string ResourcePath::to_string() const {
    return eastl::string{::to_string(scope)} + path.string().c_str();
}


void ResourcePath::parse_from_string(const eastl::string_view raw_string) {
    auto good_path = eastl::string{raw_string};
    // Get rid of stinky Windows paths
    eastl::replace(good_path.begin(), good_path.end(), '\\', '/');
    auto str = eastl::string_view{good_path};
    if(str.starts_with("file://")) {
        scope = Scope::File;
        path = std::string{str.begin() + 7, str.end()};
    } else if(str.starts_with("res://")) {
        scope = Scope::Resource;
        path = std::string{str.begin() + 6, str.end()};
    } else if(str.starts_with("shader://")) {
        scope = Scope::Shader;
        path = std::string{str.begin() + 9, str.end()};
    } else if(str.starts_with("game://")) {
        scope = Scope::Game;
        path = std::string{str.begin() + 7, str.end()};
    } else {
        // No explicit scope, or an unrecognized scope. Assume it's a generic file
        scope = Scope::File;
        path = std::string{str.begin(), str.end()};
    }
}

ResourcePath operator ""_res(const char* path, const size_t size) {
    return ResourcePath{eastl::string_view{path, size}};
}
