#pragma once

#include <string>

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <GLFW/glfw3.h>
#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include "render/backend/handles.hpp"

namespace render {
    class SarahRenderer;
}

class DebugUI {
public:
    explicit DebugUI(render::SarahRenderer& renderer_in);

    ~DebugUI();

    void draw();

private:
    GLFWwindow* window = nullptr;

    render::SarahRenderer& renderer;

    bool is_debug_menu_open = true;

    render::TextureHandle font_atlas_handle;

    bool show_demo = false;
    bool imgui_demo_open = false;

    VkDescriptorSet font_atlas_descriptor_set;

    int current_taa = 0;

    int current_dlss_mode = 1;

    int current_xess_mode = 1;

    int current_fsr_mode = 1;

    bool use_ray_reconstruction = false;

    int selected_gi_quality = 2;

    void create_font_texture();

    static void draw_fps_info();

    void draw_debug_menu();

    void draw_taa_menu();

    void draw_gi_menu();

    void draw_scene_outline();

    entt::entity selected_entity = {};
    bool show_entity_editor = false;

    void draw_entity_editor();

    static bool draw_component_helper(
        entt::entity entity, entt::meta_any instance, const entt::meta_custom& custom, bool readonly, int& gui_id
        );

    /**
     * Map from entity ID to whether its children are expanded
     */
    eastl::unordered_map<uint32_t, bool> expansion_map;

    void draw_entity(entt::entity entity, const entt::registry& registry, const eastl::string& prefix = "");

    static void draw_combo_box(const std::string& name, eastl::span<const std::string> items, int& selected_item);
};
