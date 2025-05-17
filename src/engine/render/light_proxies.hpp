#pragma once

#include "render/backend/handles.hpp"
#include "core/object_pool.hpp"
#include "shared/lights.hpp"

namespace render {
    struct PointLightProxy {
        /**
         * Data that'll be uploaded to the GPU for this light
         */
        PointLightGPU gpu_data;
    };

    struct SpotLightProxy {
        /**
         * Data that'll be uploaded to the GPU for this light
         */
        SpotLightGPU gpu_data;
    };

    using PointLightProxyHandle = PooledObject<PointLightProxy>;

    using SpotLightProxyHandle = PooledObject<SpotLightProxy>;
}
