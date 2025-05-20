#pragma once

#ifdef JPH_DEBUG_RENDERER
#include <cstdint>
#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <EASTL/atomic.h>
#include <EASTL/optional.h>
#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include "render/mesh_handle.hpp"
#include "render/backend/handles.hpp"
#include "shared/vertex_data.hpp"

namespace render {
    class MeshStorage;
    class RenderScene;
    struct GBuffer;
    class RenderGraph;

    class JoltDebugRenderer : public JPH::DebugRenderer {
    public:
        JoltDebugRenderer(MeshStorage& meshes_in);

        ~JoltDebugRenderer() override = default;

        void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override;

        void DrawTriangle(
            JPH::RVec3Arg v0, JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::ColorArg color, ECastShadow cast_shadow
        ) override;

        void DrawText3D(
            JPH::RVec3Arg position, const std::string_view& string, JPH::ColorArg color, float height
        ) override;

        void draw(RenderGraph& graph, const RenderScene& scene, const GBuffer& gbuffer, TextureHandle lit_scene_handle);

    protected:
        Batch CreateTriangleBatch(const Triangle* raw_triangles, int triangle_count) override;

        Batch CreateTriangleBatch(
            const Vertex* raw_vertices, int vertex_count, const uint32_t* indices, int index_count
        ) override;

        void DrawGeometry(
            JPH::RMat44Arg model_matrix, const JPH::AABox& world_space_bounds, float lod_scale_squared,
            JPH::ColorArg model_color, const GeometryRef& geometry, ECullMode cull_mode, ECastShadow cast_shadow,
            EDrawMode draw_mode
        ) override;

    private:
        MeshStorage& mesh_storage;

        GraphicsPipelineHandle jolt_debug_pipeline;

        eastl::vector<JoltDebugVertex> line_vertices;

        eastl::vector<JoltDebugVertex> triangle_vertices;

        BufferHandle line_vertices_buffer = nullptr;

        BufferHandle triangle_vertices_buffer = nullptr;

        eastl::optional<float3> camera_pos;

        GraphicsPipelineHandle mesh_debug_pipeline = nullptr;

        /**
         * Uploads data to a buffer. May recreate the buffer if needed
         */
        static void upload_to_buffer(const void* data, uint32_t num_bytes, BufferHandle& buffer);

        class BatchImpl final : public JPH::RefTargetVirtual {
        public:
            JPH_OVERRIDE_NEW_DELETE

                void AddRef() override {
                ++mRefCount;
            }

            void Release() override {
                if (--mRefCount == 0) delete this;
            }

            MeshHandle mesh;

        private:
            eastl::atomic<uint32_t> mRefCount = 0;
        };

        struct DrawcallDataGPU {
            float4x4 model_matrix;
            float4 tint_color;
            DeviceAddress vertex_position_buffer;
            DeviceAddress vertex_data_buffer;
        };

        struct Drawcall {
            ECullMode cull_mode;
            EDrawMode draw_mode;
            uint32_t num_indices;
            uint32_t first_index;
        };

        eastl::vector<Drawcall> drawcalls;

        eastl::vector<DrawcallDataGPU> gpu_data;
        BufferHandle drawcall_data_buffer = nullptr;

        TextureHandle depth_buffer = nullptr;
    };
}
#endif
