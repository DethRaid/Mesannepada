#ifndef VPL_HPP
#define VPL_HPP

#include "shared/prelude.h"

/**
 * A Virtual Point Light, packed into a single vec4
 *
 * x = xy of the position, halfs packed into a uint
 * y = b of the color and z of the position, halfs packed into a uint
 * z = rg color, hafls packed into a uint
 * w = normal, stored as snorm4
 *
 * Note that more clever normal encoding schemes are readily available. Storing it in two halfs would be great
 */
struct PackedVPL {
    uint4 data;
};

struct VPL {
    float3 position;
    half3 color;
    half3 normal;
};

#endif
