#pragma once

#include <EASTL/optional.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/variant.h>
#include <entt/entt.hpp>

namespace ai {
    class Blackboard {
    public:
        using Value = eastl::variant<int32_t, float, eastl::string, entt::handle>;

        template<typename ValueType>
        void set_value(const eastl::string& key, const ValueType& value);

        template<typename ValueType>
        eastl::optional<const ValueType&> get_value(const eastl::string& key);

    private:
        eastl::unordered_map<eastl::string, Value> blackboard_values;
    };

    template<typename ValueType>
    void Blackboard::set_value(const eastl::string& key, const ValueType& value) {
        blackboard_values.insert_or_assign(key, eastl::variant<ValueType>{value});
    }

    template<typename ValueType>
    eastl::optional<const ValueType&> Blackboard::get_value(const eastl::string& key) {
        if(const auto itr = blackboard_values.find(key); itr != blackboard_values.end()) {
            return eastl::get<ValueType>(itr->second);
        }

        return eastl::nullopt;
    }
} // ai
