#pragma once

namespace render {
    class ResourceAllocator;
    class RenderBackend;

    static inline ResourceAllocator* g_global_allocator = nullptr;
}
