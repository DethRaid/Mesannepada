#pragma once

#include <cstdint>

#include <EASTL/string_view.h>

enum class FidelityLevel : uint8_t {
    Low,
    Medium,
    High,
    Ultra,
};

inline const char* to_string(const FidelityLevel e) {
    switch(e) {
    case FidelityLevel::Low: return "Low";
    case FidelityLevel::Medium: return "Medium";
    case FidelityLevel::High: return "High";
    case FidelityLevel::Ultra: return "Ultra";
    default: return "unknown";
    }
}

inline FidelityLevel from_string(const eastl::string_view view) {
    if(view == "Low") {
        return FidelityLevel::Low;
    } else if(view == "Medium") {
        return FidelityLevel::Medium;
    } else if(view == "High") {
        return FidelityLevel::High;
    } else if(view == "Ultra") {
        return FidelityLevel::Ultra;
    }

    return FidelityLevel::Low;
}
