#pragma once

#include <volk.h>

#include <EASTL/fixed_vector.h>

namespace render {
    struct DescriptorInfo : VkDescriptorSetLayoutBinding {
        bool is_read_only = false;
    };

    struct DescriptorSetInfo {
        eastl::fixed_vector<DescriptorInfo, 16> bindings;

        bool has_variable_count_binding = false;
    };
}
