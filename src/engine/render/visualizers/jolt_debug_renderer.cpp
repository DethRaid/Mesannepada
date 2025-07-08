#include "jolt_debug_renderer.hpp"

#ifdef JPH_DEBUG_RENDERER
#include "core/glm_jph_conversions.hpp"
#include "render/gbuffer.hpp"
#include "render/mesh_storage.hpp"
#include "render/render_scene.hpp"
#include "render/scene_view.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/render_graph.hpp"

namespace render {
    JoltDebugRenderer::JoltDebugRenderer(MeshStorage& meshes_in) : mesh_storage{ meshes_in } {
        Initialize();

        jolt_debug_pipeline = RenderBackend::get().begin_building_pipeline("Jolt Debug")
            .set_vertex_shader("shaders/jolt/debug.vert.spv")
            .set_fragment_shader("shaders/jolt/debug.frag.spv")
            .add_dynamic_state(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY)
            .build();

        mesh_debug_pipeline = RenderBackend::get().begin_building_pipeline("Jolt Geometry Debug")
            .set_vertex_shader("shaders/jolt/mesh.vert.spv")
            .set_fragment_shader("shaders/jolt/mesh.frag.spv")
            .add_dynamic_state(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY)
            .add_dynamic_state(VK_DYNAMIC_STATE_POLYGON_MODE_EXT)
            .build();

        line_vertices.reserve(1024 * 2);
        triangle_vertices.reserve(1024 * 3);
    }

    JoltDebugRenderer::~JoltDebugRenderer() {
    }

    void JoltDebugRenderer::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, const JPH::ColorArg color) {
        line_vertices.emplace_back(JoltDebugVertex{ .position = to_glm(from), .color = color.GetUInt32() });
        line_vertices.emplace_back(JoltDebugVertex{ .position = to_glm(to), .color = color.GetUInt32() });
    }

    void JoltDebugRenderer::DrawTriangle(
        JPH::RVec3Arg v0, JPH::RVec3Arg v1, JPH::RVec3Arg v2, const JPH::ColorArg color, ECastShadow cast_shadow
    ) {
        triangle_vertices.emplace_back(to_glm(v0), color.GetUInt32());
        triangle_vertices.emplace_back(to_glm(v1), color.GetUInt32());
        triangle_vertices.emplace_back(to_glm(v2), color.GetUInt32());
    }

    void JoltDebugRenderer::DrawText3D(
        JPH::RVec3Arg position, const std::string_view& string, JPH::ColorArg color, float height
    ) {
    }

    void JoltDebugRenderer::draw(
        RenderGraph& graph, const RenderScene& scene, const GBuffer& gbuffer, const TextureHandle lit_scene_handle
    ) {
        const auto& view = scene.get_player_view();
        camera_pos = view.get_position();

        if (!line_vertices.empty()) {
            upload_to_buffer(line_vertices.data(), line_vertices.size() * sizeof(JoltDebugVertex), line_vertices_buffer);
        }
        if (!triangle_vertices.empty()) {
            upload_to_buffer(
                triangle_vertices.data(),
                triangle_vertices.size() * sizeof(JoltDebugVertex),
                triangle_vertices_buffer);
        }
        if (!gpu_data.empty()) {
            upload_to_buffer(gpu_data.data(), gpu_data.size() * sizeof(DrawcallDataGPU), drawcall_data_buffer);
        }

        if (depth_buffer == nullptr) {
            depth_buffer = RenderBackend::get().get_global_allocator().create_texture(
                "Jolt depth buffer",
                {
                    .format = VK_FORMAT_D32_SFLOAT,
                    .resolution = lit_scene_handle->get_resolution(),
                    .usage = TextureUsage::RenderTarget
                }
            );
        }

        const auto& sun = scene.get_sun_light();

        auto barriers = BufferUsageList{
            {
                .buffer = view.get_buffer(),
                .stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
            },
            {
                .buffer = sun.get_constant_buffer(),
                .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .access = VK_ACCESS_2_SHADER_READ_BIT,
            }
        };
        if (!line_vertices.empty()) {
            barriers.emplace_back(
                BufferUsageToken{
                    .buffer = line_vertices_buffer,
                    .stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                });
        }
        if (!triangle_vertices.empty()) {
            barriers.emplace_back(
                BufferUsageToken{
                    .buffer = triangle_vertices_buffer,
                    .stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                });
        }
        if (!drawcalls.empty()) {
            barriers.emplace_back(
                BufferUsageToken{
                    .buffer = triangle_vertices_buffer,
                    .stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    .access = VK_ACCESS_2_SHADER_READ_BIT,
                });
        }

        graph.add_render_pass(
            {
                .name = "Jolt Debug",
                .buffers = barriers,
                .color_attachments = {{.image = lit_scene_handle}},
                .depth_attachment = RenderingAttachmentInfo{
                    .image = depth_buffer,
                    .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .clear_value = {.depthStencil = {.depth = 0.f}}
                },
                .execute = [&](CommandBuffer& commands) {
                    commands.bind_buffer_reference(0, view.get_buffer());
                    commands.bind_buffer_reference(2, sun.get_constant_buffer());

                    commands.bind_pipeline(jolt_debug_pipeline);
                    commands.set_cull_mode(VK_CULL_MODE_BACK_BIT);
                    commands.set_front_face(VK_FRONT_FACE_CLOCKWISE);

                    if (!line_vertices.empty()) {
                        commands.set_topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

                        commands.bind_buffer_reference(4, line_vertices_buffer);

                        commands.draw(static_cast<uint32_t>(line_vertices.size()));
                    }

                    if (!triangle_vertices.empty()) {
                        commands.set_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                        commands.bind_buffer_reference(4, triangle_vertices_buffer);

                        commands.draw(static_cast<uint32_t>(triangle_vertices.size()));
                    }

                    if (!drawcalls.empty()) {
                        commands.bind_pipeline(mesh_debug_pipeline);
                        commands.set_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                        commands.bind_buffer_reference(4, drawcall_data_buffer);

                        auto drawcall_index = 0u;
                        for (const auto& drawcall : drawcalls) {
                            switch (drawcall.cull_mode) {
                            case ECullMode::CullBackFace:
                                commands.set_cull_mode(VK_CULL_MODE_BACK_BIT);
                                break;
                            case ECullMode::CullFrontFace:
                                commands.set_cull_mode(VK_CULL_MODE_FRONT_BIT);
                                break;
                            case ECullMode::Off:
                                commands.set_cull_mode(VK_CULL_MODE_NONE);
                                break;
                            }

                            switch (drawcall.draw_mode) {
                            case EDrawMode::Solid:
                                commands.set_fill_mode(VK_POLYGON_MODE_FILL);
                                break;
                            case EDrawMode::Wireframe:
                                commands.set_fill_mode(VK_POLYGON_MODE_LINE);
                                break;
                            }

                            commands.set_push_constant(6, drawcall_index);

                            commands.draw_indexed(drawcall.num_indices, 1, drawcall.first_index, 0, 0);

                            drawcall_index++;
                        }
                    }

                    line_vertices.clear();
                    triangle_vertices.clear();
                    drawcalls.clear();
                    gpu_data.clear();
                }
            });

        NextFrame();
    }

    JPH::DebugRenderer::Batch JoltDebugRenderer::CreateTriangleBatch(
        const Triangle* raw_triangles, const int triangle_count
    ) {
        auto* batch = new BatchImpl{};
        if (raw_triangles == nullptr || triangle_count == 0) {
            return batch;
        }

        const auto triangles = eastl::span{ raw_triangles, static_cast<size_t>(triangle_count) };

        // We need indexed meshes, so let's just make a stinky index buffer
        eastl::vector<uint32_t> indices;
        indices.resize(triangles.size() * 3);
        for (auto i = 0u; i < indices.size(); i++) {
            indices[i] = i;
        }

        auto bounds = Box{
            .min = float3{eastl::numeric_limits<float>::max()},
            .max = float3{eastl::numeric_limits<float>::min()}
        };

        eastl::vector<StandardVertex> vertices;
        vertices.reserve(triangles.size() * 3);
        for (const auto& triangle : triangles) {
            for (const auto& vertex : triangle.mV) {
                const auto& new_vertex = vertices.emplace_back(
                    StandardVertex{
                        .position = to_glm(vertex.mPosition),
                        .normal = to_glm(vertex.mNormal),
                        .texcoord = to_glm(vertex.mUV),
                        .color = vertex.mColor.GetUInt32()
                    });

                bounds.max = max(bounds.max, new_vertex.position);
                bounds.min = min(bounds.min, new_vertex.position);
            }
        }

        batch->mesh = *mesh_storage.add_mesh(vertices, indices, bounds);

        return batch;
    }

    JPH::DebugRenderer::Batch JoltDebugRenderer::CreateTriangleBatch(
        const Vertex* raw_vertices, const int vertex_count, const uint32_t* indices, const int index_count
    ) {
        auto* batch = new BatchImpl{};
        if (raw_vertices == nullptr || vertex_count == 0 || indices == nullptr || index_count == 0) {
            return batch;
        }

        const auto jolt_vertices = eastl::span{ raw_vertices, static_cast<size_t>(vertex_count) };

        auto bounds = Box{
            .min = float3{eastl::numeric_limits<float>::max()},
            .max = float3{eastl::numeric_limits<float>::min()}
        };

        eastl::vector<StandardVertex> vertices;
        vertices.reserve(jolt_vertices.size());
        for (const auto& vertex : jolt_vertices) {
            const auto& new_vertex = vertices.emplace_back(
                StandardVertex{
                    .position = to_glm(vertex.mPosition),
                    .normal = to_glm(vertex.mNormal),
                    .texcoord = to_glm(vertex.mUV),
                    .color = vertex.mColor.GetUInt32()
                });

            bounds.max = max(bounds.max, new_vertex.position);
            bounds.min = min(bounds.min, new_vertex.position);
        }

        batch->mesh = *mesh_storage.add_mesh(vertices, eastl::span{ indices, static_cast<size_t>(index_count) }, bounds);

        return batch;
    }

    void JoltDebugRenderer::DrawGeometry(
        JPH::RMat44Arg model_matrix, const JPH::AABox& world_space_bounds, const float lod_scale_squared,
        const JPH::ColorArg model_color, const GeometryRef& geometry, const ECullMode cull_mode,
        const ECastShadow cast_shadow,
        const EDrawMode draw_mode
    ) {
        const LOD* lod = geometry->mLODs.data();
        if (camera_pos) {
            lod = &geometry->GetLOD(to_jolt(*camera_pos), world_space_bounds, lod_scale_squared);
        }

        const auto* batch = static_cast<const BatchImpl*>(lod->mTriangleBatch.GetPtr());

        // Add a drawcall and a GPU data for this batch
        drawcalls.emplace_back(
            Drawcall{
                .cull_mode = cull_mode,
                .draw_mode = draw_mode,
                .num_indices = batch->mesh->num_indices,
                .first_index = static_cast<uint32_t>(batch->mesh->first_index)
            });

        gpu_data.emplace_back(
            DrawcallDataGPU{
                .model_matrix = to_glm(model_matrix),
                .tint_color = to_glm(model_color.ToVec4()),
                .vertex_position_buffer = mesh_storage.get_vertex_position_buffer()->address + batch->mesh->first_vertex *
                sizeof(StandardVertexPosition),
                .vertex_data_buffer = mesh_storage.get_vertex_data_buffer()->address + batch->mesh->first_vertex *
                sizeof(StandardVertexData),
            });
    }

    void JoltDebugRenderer::upload_to_buffer(const void* data, const uint32_t num_bytes, BufferHandle& buffer) {
        auto& backend = RenderBackend::get();
        auto& allocator = backend.get_global_allocator();

        if (buffer != nullptr && buffer->create_info.size < num_bytes) {
            allocator.destroy_buffer(buffer);
            buffer = nullptr;

        }

        if (buffer == nullptr) {
            buffer = allocator.create_buffer(
                "Jolt Debug Buffer",
                num_bytes * 2,
                BufferUsage::StorageBuffer);
        }

        const auto data_span = eastl::span{ static_cast<const uint8_t*>(data), num_bytes };
        backend.get_upload_queue().upload_to_buffer<uint8_t>(buffer, data_span);
    }
}
#endif
