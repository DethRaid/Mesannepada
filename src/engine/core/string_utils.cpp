#include "string_utils.hpp"

eastl::vector<eastl::string_view> split(const eastl::string_view s, const char delimiter) {
    eastl::vector<eastl::string_view> tokens;
    size_t pos = 0;
    size_t last_pos = 0;
    while((pos = s.find(delimiter, last_pos)) != eastl::string_view::npos) {
        tokens.emplace_back(s.substr(last_pos, pos - last_pos));

        last_pos = pos + 1;
    }
    tokens.emplace_back(s.substr(last_pos));

    return tokens;
}
