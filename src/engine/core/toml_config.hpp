#pragma once

#include <toml.hpp>

struct TomlTypeConfig {
    using comment_type = toml::preserve_comments;

    using boolean_type = bool;
    using integer_type = std::uint32_t;
    using floating_type = float;
    using string_type = std::string;

    template <typename T>
    using array_type = std::vector<T>; // XXX
    template <typename K, typename T>
    using table_type = std::unordered_map<K, T>; // XXX

    static toml::result<integer_type, toml::error_info>
    parse_int(const std::string& str, const toml::source_location& src, const std::uint8_t base) {
        return toml::read_int<integer_type>(str, src, base);
    }

    static toml::result<floating_type, toml::error_info>
    parse_float(const std::string& str, const toml::source_location& src, const bool is_hex) {
        return toml::read_float<floating_type>(str, src, is_hex);
    }
};
