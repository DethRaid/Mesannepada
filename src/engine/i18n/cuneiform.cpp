#include "cuneiform.hpp"

#include <EASTL/array.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

eastl::string i18n::to_cuneiform(const uint32_t number) {
    const auto u_lookup = eastl::unordered_map<uint32_t, eastl::string>{
        {1, "𒌋"},
        {2, "𒌋𒌋"},
        {3, "𒌋𒌋𒌋"},
        {4, "𒐏"},
        {5, "𒐐"},
    };

    const auto dis_lookup = eastl::unordered_map<uint32_t, eastl::string>{
        {1, "𒁹"},
        {2, "𒁹𒁹"},
        {3, "𒐈"},
        {4, "𒐉"},
        {5, "𒐊"},
        {6, "𒐋"},
        {7, "𒐌"},
        {8, "𒐍"},
        {9, "𒐎"},

    };

    auto full_number = eastl::vector<eastl::string>{};
    full_number.reserve(number / 60);

    auto working_number = number; // intentional copy
    while(working_number > 0) {
        const auto place = working_number % 60;

        const auto ones = place % 10;
        const auto tens = (place / 10) % 6;

        auto sumerian_numeral = eastl::string{};

        if(const auto& itr = u_lookup.find(tens); itr != u_lookup.end()) {
            sumerian_numeral += itr->second;
        }

        if(const auto& itr = dis_lookup.find(ones); itr != dis_lookup.end()) {
            sumerian_numeral += itr->second;
        }

        full_number.emplace_back(sumerian_numeral);

        working_number /= 60;
    }

    auto sumerian_number = eastl::string{};

    for(auto itr = full_number.rbegin(); itr < full_number.rend(); ++itr) {
        sumerian_number += *itr;
    }

    return sumerian_number;
}
