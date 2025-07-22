#pragma once

#include <EASTL/string.h>
#include <simdjson.h>

namespace simdjson {
    // This tag_invoke MUST be inside simdjson namespace
    template<typename simdjson_value>
    auto tag_invoke(deserialize_tag, simdjson_value& json, eastl::string& value) {
        std::string_view view;
        const auto error = json.get_string(view);
        if(error) {
            return error;
        }

        value = eastl::string{view.data(), view.size()};

        return simdjson::SUCCESS;
    }
} // namespace simdjson

