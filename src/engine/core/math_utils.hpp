#pragma once

#include <cstdint>

inline uint32_t round_up(const uint32_t num, const uint32_t multiple) {
    if(multiple == 0) {
        return num;
    }

    auto remainder = num % multiple;
    if(remainder == 0) {
        return num;
    }

    return num + multiple - remainder;
}
