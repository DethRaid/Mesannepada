#pragma once

#include <string>
#include <volk.h>

#include "render/backend/texture_state.hpp"

namespace render {
    VkAccessFlags to_access_mask(TextureState state);

    VkImageLayout to_layout(TextureState state);

    VkPipelineStageFlags to_stage_flags(TextureState state);

    bool is_depth_format(VkFormat format);
}
