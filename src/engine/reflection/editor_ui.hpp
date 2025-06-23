#pragma once

#include <imgui.h>
#include <EASTL/string.h>

#include "EASTL/fixed_vector.h"
#include "reflection/reflection_types.hpp"
#include "shared/prelude.h"

// "Inspired" by https://github.com/JuanDiegoMontoya/Viator/blob/voxels/src/Core/Reflection.cpp

using namespace entt::literals;

namespace reflection {
    template<typename Scalar>
    consteval ImGuiDataType scalar_to_imgui_data_type() {
        if constexpr(eastl::is_same_v<Scalar, int8_t>) {
            return ImGuiDataType_S8;
        }
        if constexpr(eastl::is_same_v<Scalar, uint8_t>) {
            return ImGuiDataType_U8;
        }
        if constexpr(eastl::is_same_v<Scalar, int16_t>) {
            return ImGuiDataType_S16;
        }
        if constexpr(eastl::is_same_v<Scalar, uint16_t>) {
            return ImGuiDataType_U16;
        }
        if constexpr(eastl::is_same_v<Scalar, int32_t>) {
            return ImGuiDataType_S32;
        }
        if constexpr(eastl::is_same_v<Scalar, uint32_t>) {
            return ImGuiDataType_U32;
        }
        if constexpr(eastl::is_same_v<Scalar, int64_t>) {
            return ImGuiDataType_S64;
        }
        if constexpr(eastl::is_same_v<Scalar, uint64_t>) {
            return ImGuiDataType_U64;
        }
        if constexpr(eastl::is_same_v<Scalar, float>) {
            return ImGuiDataType_Float;
        }
        if constexpr(eastl::is_same_v<Scalar, double>) {
            return ImGuiDataType_Double;
        }

        throw std::runtime_error{"Error: unsupported type"};
    }

    template<typename Scalar>
    consteval const char* scalar_to_format_string() {
        if constexpr(eastl::is_same_v<Scalar, bool>) {
            return "%d";
        }
        if constexpr(eastl::is_same_v<Scalar, int8_t>) {
            return "%d";
        }
        if constexpr(eastl::is_same_v<Scalar, uint8_t>) {
            return "%u";
        }
        if constexpr(eastl::is_same_v<Scalar, int16_t>) {
            return "%d";
        }
        if constexpr(eastl::is_same_v<Scalar, uint16_t>) {
            return "%u";
        }
        if constexpr(eastl::is_same_v<Scalar, int32_t>) {
            return "%d";
        }
        if constexpr(eastl::is_same_v<Scalar, uint32_t>) {
            return "%u";
        }
        if constexpr(eastl::is_same_v<Scalar, int64_t>) {
            return "%lld";
        }
        if constexpr(eastl::is_same_v<Scalar, uint64_t>) {
            return "%llu";
        }
        if constexpr(eastl::is_same_v<Scalar, float>) {
            return "%.3f";
        }
        if constexpr(eastl::is_same_v<Scalar, double>) {
            return "%.3f";
        }

        throw std::runtime_error{"Error: unsupported type"};
    }

    void get_editor_name(const char*& label, const PropertiesMap& properties);

    void init_editor_scalar_params(const PropertiesMap& properties, const char*& label);

    template<typename Scalar>
    bool editor_write_scalar(Scalar& f, const PropertiesMap& properties);

    bool editor_write_float3(float3& value, const PropertiesMap& properties);

    bool editor_write_quat(glm::quat& value, const PropertiesMap& properties);

    bool editor_write_string(eastl::string& value, const PropertiesMap& properties);

    bool editor_write_entity(entt::entity& value, const PropertiesMap& properties);

    bool editor_write_handle(entt::handle& value, const PropertiesMap& properties);

    template<typename Scalar>
    void editor_read_scalar(Scalar s, const PropertiesMap& properties);

    void editor_read_float3(glm::vec3 value, const PropertiesMap& properties);

    void editor_read_quat(glm::quat value, const PropertiesMap& properties);

    void editor_read_string(const eastl::string& value, const PropertiesMap& properties);

    void editor_read_entity(entt::entity value, const PropertiesMap& properties);

    void editor_read_handle(entt::handle value, const PropertiesMap& properties);

    template<typename Scalar>
    bool editor_write_scalar(Scalar& f, const PropertiesMap& properties) {
        const char* label = "float";

        init_editor_scalar_params(properties, label);

        if constexpr(eastl::is_same_v<Scalar, bool>) {
            return ImGui::Checkbox(label, &f);
        } else {
            return ImGui::InputScalar(label,
                                      scalar_to_imgui_data_type<Scalar>(),
                                      &f,
                                      nullptr,
                                      nullptr,
                                      scalar_to_format_string<Scalar>(),
                                      0);

        }
    }

    template<typename Scalar>
    void editor_read_scalar(Scalar s, const PropertiesMap& properties) {
        const char* label = "scalar";
        get_editor_name(label, properties);
        ImGui::Text((eastl::string("%s: ") + scalar_to_format_string<Scalar>()).c_str(), label, s);
    }
}
