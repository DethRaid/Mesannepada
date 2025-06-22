#include "debug_drawer.hpp"

#include "imgui.h"
#include "scene/transform_component.hpp"

using namespace entt;

bool draw_editor_float(float& f, const PropertiesMap& properties) {
    auto label = "float";
    float min = 0;
    float max = 1;

    // Find the min and max in the custom properties
    if(const auto itr = properties.find("name"_hs); itr != properties.end()) {
        label = *itr->second.try_cast<const char*>();
    }
    if(const auto itr = properties.find("max"_hs); itr != properties.end()) {
        max = *itr->second.try_cast<float>();
    }
    if(const auto itr = properties.find("min"_hs); itr != properties.end()) {
        min = *itr->second.try_cast<float>();
    }

    return ImGui::SliderFloat(label, &f, min, max);
}
