#include "editor_ui.hpp"

#include <imgui.h>
#include <EASTL/array.h>
#include <glm/gtx/matrix_decompose.hpp>

#include "scene/transform_component.hpp"

namespace reflection {
    void get_editor_name(const char*& label, const PropertiesMap& properties) {
        if(const auto it = properties.find("name"_hs); it != properties.end()) {
            label = *it->second.try_cast<const char*>();
        }
    }

    void init_editor_scalar_params(const PropertiesMap& properties, const char*& label) {
        get_editor_name(label, properties);
    }

    bool editor_write_float3(glm::vec3& value, const PropertiesMap& properties) {
        auto label = "float3";

        init_editor_scalar_params(properties, label);

        return ImGui::InputFloat3(label, &value[0]);

    }

    bool editor_write_quat(glm::quat& value, const PropertiesMap& properties) {
        auto label = "quat";
        const float min = -180;
        const float max = 180;

        init_editor_scalar_params(properties, label);

        auto euler = glm::degrees(glm::eulerAngles(value));

        const auto changed = ImGui::SliderFloat3(label, &euler[0], min, max);

        if(changed) {
            value = glm::quat{glm::radians(euler)};
        }

        return changed;
    }

    void editor_read_float3(const float3 value, const PropertiesMap& properties) {
        auto label = "float3";
        get_editor_name(label, properties);
        ImGui::Text("%s: %f, %f, %f", label, value.x, value.y, value.z);
    }

    void editor_read_quat(const glm::quat value, const PropertiesMap& properties) {
        auto label = "quat";
        get_editor_name(label, properties);
        ImGui::Text("%s: %f, %f, %f, %f", label, value.w, value.x, value.y, value.z);
    }

    bool editor_write_string(eastl::string& value, const PropertiesMap& properties) {
        auto label = "string";
        get_editor_name(label, properties);
        eastl::array<char, 256> buffer{};
        value.copy(buffer.data(), buffer.size());
        if(ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            value.assign(buffer.data(), strlen(buffer.data()));
            return true;
        }
        return false;
    }

    void editor_read_string(const eastl::string& value, const PropertiesMap& properties) {
        auto label = "string";
        get_editor_name(label, properties);
        ImGui::Text("%.*s", static_cast<int>(value.size()), value.c_str());
    }

    bool editor_write_entity(entt::entity& value, const PropertiesMap& properties) {
        auto label = "entity";
        get_editor_name(label, properties);
        using T = eastl::underlying_type_t<entt::entity>;
        auto temp = value;
        ImGui::InputScalar(label,
                              scalar_to_imgui_data_type<T>(),
                              &temp,
                              nullptr,
                              nullptr,
                              scalar_to_format_string<T>(),
                              0);

        if(ImGui::IsItemDeactivatedAfterEdit()) {
            value = temp;
            return true;
        }
        return false;
    }

    bool editor_write_handle(entt::handle& value, const PropertiesMap& properties) {
        auto label = "entity";
        get_editor_name(label, properties);
        using T = eastl::underlying_type_t<entt::entity>;
        auto temp = value.entity();
        ImGui::InputScalar(label,
                              scalar_to_imgui_data_type<T>(),
                              &temp,
                              nullptr,
                              nullptr,
                              scalar_to_format_string<T>(),
                              0);

        if(ImGui::IsItemDeactivatedAfterEdit()) {
            value = entt::handle{*value.registry(), temp};
            return true;
        }
        return false;
    }

    void editor_read_entity(const entt::entity value, const PropertiesMap& properties) {
        auto label = "entity";
        get_editor_name(label, properties);
        if(value == entt::null) {
            ImGui::Text("%s: null", label);
        } else {
            ImGui::Text("%s: %u, v%u", label, entt::to_entity(value), static_cast<uint32_t>(entt::to_version(value)));
        }
    }

    void editor_read_handle(entt::handle value, const PropertiesMap& properties) {
        auto label = "entity";
        get_editor_name(label, properties);
        if(value.entity() == entt::null) {
            ImGui::Text("%s: null", label);
        } else {
            ImGui::Text("%s: %u, v%u", label, entt::to_entity(value.entity()), static_cast<uint32_t>(entt::to_version(value.entity())));
        }
    }
}
