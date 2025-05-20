#pragma once

#include "shared/prelude.h"

#include "render/light_proxies.hpp"

namespace render {
    /**
     * A component for a point light
     * 
     * These are point lights with a radius - sphere lights, really
     */
    struct PointLightComponent {
        /**
         * HDR RGB color of the light, in lumens
         */
        float3 color;

        /**
         * Maximum range that the light can affect
         */
        float range;

        /**
         * Radius of the light emitter
         */
        float size;

        /**
         * Handle to the renderer's proxy
         */
        PointLightProxyHandle proxy = {};
    };

    /** 
     * A component for a spot lights
     */
    struct SpotLightComponent {
        float3 color;

        float range;

        float size;

        float inner_cone_angle;

        float outer_cone_angle;

        SpotLightProxyHandle proxy = {};
    };

    struct DirectionalLightComponent {
        float3 color;
    };
}
