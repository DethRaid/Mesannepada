#include "resource_path.hpp"

#include "core/string_utils.hpp"
#include "core/system_interface.hpp"

ResourcePath::ResourcePath(const eastl::string_view path_in) {
    if(path_in.starts_with("file://")) {
        scope = Scope::File;
        path = {path_in.begin() + 7, path_in.end()};
    } else if(path_in.starts_with("res://")) {
        scope = Scope::Resource;
        path = {path_in.begin() + 6, path_in.end()};
    } else if(path_in.starts_with("shader://")) {
        scope = Scope::Shader;
        path = {path_in.begin() + 9, path_in.end()};
    } else if(path_in.starts_with("game://")) {
        scope = Scope::Game;
        path = {path_in.begin() + 7, path_in.end()};
    } else {
        // No explicit scope, or an unrecognized scope. Assume it's a generic file
        scope = Scope::File;
        path = path_in;
    }
}

ResourcePath::ResourcePath(const std::string_view path_in) :
    ResourcePath{eastl::string_view{path_in.begin(), path_in.size()}} {
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

bool ResourcePath::ends_with(eastl::string_view end) const {
    return eastl::string_view{path}.ends_with(end);
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

    return base_path.empty() ? std::filesystem::path{path.c_str()} : base_path / path.c_str();
}

eastl::string ResourcePath::to_string() const {
    return format("%s%s", ::to_string(scope), path.c_str());
}

const eastl::string& ResourcePath::get_path() const {return path;
}

ResourcePath operator ""_res(const char* path, size_t size) {
    return ResourcePath{eastl::string_view{path, size}};
}
