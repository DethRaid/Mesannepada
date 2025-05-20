#pragma once

#include <string>
#include <string_view>

/**
 * Decodes a stinky hex-encoded string into a useful string
 * @param input Hex-encoded string
 * @return Useful string
 */
std::string decode_percent_encoding(std::string_view input);
