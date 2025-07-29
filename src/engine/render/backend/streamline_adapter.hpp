#pragma once

#if SAH_USE_STREAMLINE
#include <vulkan/vulkan_core.h>

namespace render {
    class StreamlineAdapter {
    public:
        StreamlineAdapter();

        ~StreamlineAdapter();

        PFN_vkGetInstanceProcAddr try_load_interposer();

        bool is_initialized() const;

    private:
        bool initialized = false;
    };
}
#endif
