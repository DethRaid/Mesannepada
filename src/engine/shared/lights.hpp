#ifndef LIGHTS_H
#define LIGHTS_H

#include "shared/prelude.h"

struct PointLightGPU {
    float3 location;
    float size;
    float3 color;
    float range;
};

struct SpotLightGPU {
    float3 location;
    float size;
    float3 color;
    float range;
    float inner_angle;
    float outer_angle;
};

#endif
