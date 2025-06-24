#pragma once

#include <NRD.h>

#include "shared/prelude.h"

namespace render {
    /**
     * Wraps Nvidia's Realtime Denoisers
     *
     * We're using the shadow and diffuse denoisers for now, we can use the specular denoiser when we get RT reflections
     */
    class NvidiaRealtimeDenoiser {
    public:
        NvidiaRealtimeDenoiser();

        ~NvidiaRealtimeDenoiser();

        void set_resolution(const uint2& resolution);

    private:
        uint2 cached_resolution = uint2{0};

        nrd::Instance* instance = nullptr;
    };
} // render
