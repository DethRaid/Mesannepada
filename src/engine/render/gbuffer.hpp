#pragma once

#include "render/backend/handles.hpp"

namespace render {
    struct GBuffer {
        TextureHandle color = nullptr;
        TextureHandle normals = nullptr;
        TextureHandle data = nullptr;
        TextureHandle emission = nullptr;
        TextureHandle depth = nullptr;
    };
}
