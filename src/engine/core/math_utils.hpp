#pragma once

#include <cstdint>

template<typename NumType>
NumType round_up(const NumType num, const NumType multiple) {
    if(multiple == 0) {
        return num;
    }

    auto remainder = num % multiple;
    if(remainder == 0) {
        return num;
    }

    return num + multiple - remainder;
}
