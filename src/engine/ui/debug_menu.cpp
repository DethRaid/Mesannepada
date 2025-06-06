#include "debug_menu.hpp"

#include <imgui.h>
#include <magic_enum.hpp>
#if SAH_USE_STREAMLINE
#include <sl.h>
#include <sl_dlss.h>
#endif
#if SAH_USE_FFX
#include <ffx_api/ffx_upscale.h>
#endif
#if SAH_USE_XESS
#include <xess/xess.h>
#endif

#include "console/cvars.hpp"
#include "core/engine.hpp"
#include "core/glfw_system_interface.hpp"
#include "core/system_interface.hpp"
#include "physics/collider_component.hpp"
#include "render/sarah_renderer.hpp"
#include "render/components/static_mesh_component.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_allocator.hpp"
#include "render/visualizers/visualizer_type.hpp"
#include "scene/camera_component.hpp"
#include "scene/transform_component.hpp"

static bool g_MouseJustPressed[5] = {false, false, false, false, false};

#if defined(_WIN32)
#include <GLFW/glfw3.h>

static GLFWmousebuttonfun prev_mouse_button_callback;
static GLFWscrollfun prev_scroll_callback;
static GLFWkeyfun prev_key_callback;
static GLFWcharfun prev_char_callback;

static eastl::array<GLFWcursor*, ImGuiMouseCursor_COUNT> mouse_cursors = {};

static const char* get_clipboard_text(void* user_data) {
    return glfwGetClipboardString(static_cast<GLFWwindow*>(user_data));
}

static void set_clipboard_text(void* user_data, const char* text) {
    glfwSetClipboardString(static_cast<GLFWwindow*>(user_data), text);
}

static void mouse_button_callback(GLFWwindow* window, const int button, const int action, const int mods) {
    if(prev_mouse_button_callback != nullptr) {
        prev_mouse_button_callback(window, button, action, mods);
    }

    if(action == GLFW_PRESS && button >= 0 && button < IM_ARRAYSIZE(g_MouseJustPressed)) {
        g_MouseJustPressed[button] = true;
    }
}

static void scroll_callback(GLFWwindow* window, const double x_offset, const double y_offset) {
    if(prev_scroll_callback != nullptr) {
        prev_scroll_callback(window, x_offset, y_offset);
    }

    auto& io = ImGui::GetIO();
    io.MouseWheelH += static_cast<float>(x_offset);
    io.MouseWheel += static_cast<float>(y_offset);
}

static void key_callback(GLFWwindow* window, const int key, const int scancode, const int action, const int mods) {
    if(prev_key_callback != nullptr) {
        prev_key_callback(window, key, scancode, action, mods);
    }

    // TODO: Use the new io.AddKeyEvent API? We'd have to convert from raw keycode to ImGUI named key, which is slightly annoying
    auto& io = ImGui::GetIO();
    if(action == GLFW_PRESS) {
        io.KeysDown[key] = true;
    }
    if(action == GLFW_RELEASE) {
        io.KeysDown[key] = false;
    }

    // Modifiers are not reliable across systems
    io.KeyMods = ImGuiModFlags_None;

    io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
    io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
    io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
    io.KeySuper = false;

    if(io.KeyCtrl) {
        io.KeyMods |= ImGuiModFlags_Ctrl;
    }
    if(io.KeyShift) {
        io.KeyMods |= ImGuiModFlags_Shift;
    }
    if(io.KeyAlt) {
        io.KeyMods |= ImGuiModFlags_Alt;
    }
}

static void char_callback(GLFWwindow* window, const unsigned int c) {
    if(prev_char_callback != nullptr) {
        prev_char_callback(window, c);
    }

    auto& io = ImGui::GetIO();
    io.AddInputCharacter(c);
}
#endif

DebugUI::DebugUI(render::SarahRenderer& renderer_in) : renderer{renderer_in} {
    ImGui::CreateContext();

#if defined(_WIN32)
    auto& system_interface = reinterpret_cast<GlfwSystemInterface&>(SystemInterface::get());
    window = system_interface.get_glfw_window();
#endif

    ImGui::CreateContext();

    auto& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendPlatformName = "Windows";
    io.BackendRendererName = "SAH Engine";

    io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;

#if defined(_WIN32)
    io.SetClipboardTextFn = set_clipboard_text;
    io.GetClipboardTextFn = get_clipboard_text;
    io.ClipboardUserData = window;

    io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
    io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
    io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
    io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
    io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    io.KeyMap[ImGuiKey_KeyPadEnter] = GLFW_KEY_KP_ENTER;
    io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;

    mouse_cursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    mouse_cursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    mouse_cursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    mouse_cursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    mouse_cursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    mouse_cursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);

    prev_mouse_button_callback = glfwSetMouseButtonCallback(window, mouse_button_callback);
    prev_scroll_callback = glfwSetScrollCallback(window, scroll_callback);
    prev_key_callback = glfwSetKeyCallback(window, key_callback);
    prev_char_callback = glfwSetCharCallback(window, char_callback);
#endif

    create_font_texture();

    switch(*CVarSystem::Get()->GetIntCVar("r.AntiAliasing")) {
    case 2:
        current_taa = 2;
        break;
    case 3:
        current_taa = 0;
        break;
    case 4:
        current_taa = 1;
        break;
    default:
        current_taa = 3;
    }

    use_ray_reconstruction = *CVarSystem::Get()->GetIntCVar("r.DLSS-RR.Enabled") != 0;
}

void DebugUI::draw() {
    auto& io = ImGui::GetIO();
    IM_ASSERT(
        io.Fonts->IsBuilt() &&
        "Font atlas not built! It is generally built by the renderer back-end. Missing call to renderer _NewFrame() function? e.g. ImGui_ImplOpenGL3_NewFrame()."
    );

    // Setup display size (every frame to accommodate for window resizing)
    int w, h;
    int display_w, display_h;
#if defined(_WIN32)
    glfwGetWindowSize(window, &w, &h);
    glfwGetFramebufferSize(window, &display_w, &display_h);
#endif
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    if(w > 0 && h > 0) {
        io.DisplayFramebufferScale = ImVec2(
            static_cast<float>(display_w) / static_cast<float>(w),
            static_cast<float>(display_h) / static_cast<float>(h)
        );
    }

    // Setup time step
#if defined(_WIN32)
    const auto current_time = glfwGetTime();
    io.DeltaTime = last_start_time > 0.0 ? static_cast<float>(current_time - last_start_time) : 1.0f / 60.0f;
    last_start_time = current_time;
#else
    io.DeltaTime = 1.f / 60.f;
#endif

#if defined(_WIN32)
    update_mouse_pos_and_buttons();
    update_mouse_cursor();
#endif

    ImGui::NewFrame();

    if(show_demo) {
        ImGui::ShowDemoWindow(&imgui_demo_open);
    }

    draw_fps_info();

    draw_scene_outline();

    draw_debug_menu();

    ImGui::Render();
}

void DebugUI::create_font_texture() {
    ZoneScoped;
    const auto& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    auto& backend = render::RenderBackend::get();
    auto& allocator = backend.get_global_allocator();

    font_atlas_handle = allocator.create_texture(
        "Dear ImGUI Font Atlas",
        {VK_FORMAT_R8_UNORM, {width, height}, 1, render::TextureUsage::StaticImage}
    );

    auto& upload_queue = backend.get_upload_queue();
    upload_queue.enqueue(
        render::TextureUploadJob{
            .destination = font_atlas_handle, .mip = 0,
            .data = eastl::vector<uint8_t>(pixels, pixels + static_cast<ptrdiff_t>(width * height))
        }
    );

    font_atlas_descriptor_set = *render::vkutil::DescriptorBuilder::begin(
                                     backend,
                                     backend.get_persistent_descriptor_allocator()
                                 )
                                 .bind_image(
                                     0,
                                     {
                                         .sampler = backend.get_default_sampler(),
                                         .image = font_atlas_handle,
                                         .image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                     },
                                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                     VK_SHADER_STAGE_FRAGMENT_BIT
                                 )
                                 .build();

    io.Fonts->TexID = reinterpret_cast<ImTextureID>(font_atlas_descriptor_set);
}

#if defined(_WIN32)
void DebugUI::update_mouse_pos_and_buttons() const {
    // Update buttons
    auto& io = ImGui::GetIO();
    for(auto i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++) {
        // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are
        // shorter than 1 frame.
        io.MouseDown[i] = g_MouseJustPressed[i] || glfwGetMouseButton(window, i) != 0;
        g_MouseJustPressed[i] = false;
    }

    // Update mouse position
    const auto mouse_pos_backup = io.MousePos;
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

    const auto focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
    if(focused) {
        if(io.WantSetMousePos) {
            glfwSetCursorPos(window, static_cast<double>(mouse_pos_backup.x), static_cast<double>(mouse_pos_backup.y));
        } else {
            double mouse_x, mouse_y;
            glfwGetCursorPos(window, &mouse_x, &mouse_y);
            io.MousePos = ImVec2(static_cast<float>(mouse_x), static_cast<float>(mouse_y));
        }
    }
}

void DebugUI::update_mouse_cursor() const {
    auto& io = ImGui::GetIO();
    if((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(window, GLFW_CURSOR) ==
        GLFW_CURSOR_DISABLED) {
        return;
    }

    const auto imgui_cursor = ImGui::GetMouseCursor();
    if(imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    } else {
        // Show OS mouse cursor
        glfwSetCursor(
            window,
            mouse_cursors[imgui_cursor] ? mouse_cursors[imgui_cursor] : mouse_cursors[ImGuiMouseCursor_Arrow]
        );
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}
#endif

void DebugUI::draw_fps_info() {
    const auto& application = Engine::get();

    const auto frame_time = application.get_frame_time();

    if(ImGui::Begin("Stats")) {
        ImGui::Text("Frame time: %.1f ms (%.0f FPS)", frame_time * 1000, 1.f / frame_time);
    }

    ImGui::End();
}

void DebugUI::draw_debug_menu() {
    if(ImGui::Begin("Debug Menu", &is_debug_menu_open)) {
        if(ImGui::CollapsingHeader("Visualizers")) {
            auto selected_visualizer = renderer.get_active_visualizer();
            for(auto visualizer : magic_enum::enum_values<render::RenderVisualization>()) {
                const auto name = to_string(visualizer);
                if(ImGui::Selectable(name, selected_visualizer == visualizer)) {
                    selected_visualizer = visualizer;
                }
            }

            renderer.set_active_visualizer(selected_visualizer);
        }

        if(ImGui::Button("Reload UI")) {
            Engine::get().get_ui_controller().reload();
        }

        if(ImGui::Button("Show demo")) {
            show_demo = true;
        }

        if(ImGui::CollapsingHeader("cvars")) {
            auto cvars = CVarSystem::Get();
            cvars->DrawImguiEditor();
        }
    }

    ImGui::End();
}

void DebugUI::draw_taa_menu() {
    ImGui::Text("TAA");
    ImGui::Separator();
    draw_combo_box("TAA Technique", eastl::array<std::string, 4>{"DLSS", "XeSS", "FSR", "None"}, current_taa);

#if SAH_USE_STREAMLINE
    if(current_taa == 0) {
        CVarSystem::Get()->SetIntCVar("r.AntiAliasing", static_cast<int32_t>(render::AntiAliasingType::DLSS));

        draw_combo_box(
            "DLSS Mode",
            eastl::array<std::string, 4>{"DLAA", "Quality", "Balanced", "Performance"},
            current_dlss_mode);
        auto* dlss_quality = CVarSystem::Get()->GetIntCVar("r.DLSS.Mode");
        switch(current_dlss_mode) {
        case 0:
            *dlss_quality = static_cast<int32_t>(sl::DLSSMode::eDLAA);
            break;
        case 1:
            *dlss_quality = static_cast<int32_t>(sl::DLSSMode::eMaxQuality);
            break;
        case 2:
            *dlss_quality = static_cast<int32_t>(sl::DLSSMode::eBalanced);
            break;
        case 3:
            *dlss_quality = static_cast<int32_t>(sl::DLSSMode::eMaxPerformance);
            break;
        }

        ImGui::Checkbox("DLSS Ray Reconstruction", &use_ray_reconstruction);
        CVarSystem::Get()->SetIntCVar("r.DLSS-RR.Enabled", use_ray_reconstruction);

    } else
#endif
#if SAH_USE_XESS
        if(current_taa == 1) {
            CVarSystem::Get()->SetIntCVar("r.AntiAliasing", static_cast<int32_t>(render::AntiAliasingType::XeSS));

            draw_combo_box(
                "XeSS Mode",
                eastl::array<std::string, 7>{
                    "AA", "Ultra Quality Plus", "Ultra Quality", "Quality", "Balanced", "Performance",
                    "Ultra Performance"
                },
                current_xess_mode);
            auto* xess_quality = CVarSystem::Get()->GetIntCVar("r.XeSS.Mode");
            switch(current_xess_mode) {
            case 0:
                *xess_quality = XESS_QUALITY_SETTING_AA;
                break;
            case 1:
                *xess_quality = XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
                break;
            case 2:
                *xess_quality = XESS_QUALITY_SETTING_ULTRA_QUALITY;
                break;
            case 3:
                *xess_quality = XESS_QUALITY_SETTING_QUALITY;
                break;
            case 4:
                *xess_quality = XESS_QUALITY_SETTING_BALANCED;
                break;
            case 5:
                *xess_quality = XESS_QUALITY_SETTING_PERFORMANCE;
                break;
            case 6:
                *xess_quality = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
                break;
            }

        } else
#endif
#if SAH_USE_FFX
        if(current_taa == 2) {
            CVarSystem::Get()->SetIntCVar("r.AntiAliasing", static_cast<int32_t>(render::AntiAliasingType::FSR3));

            draw_combo_box(
                "FSR Mode",
                eastl::array<std::string, 5>{
                    "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"
                },
                current_fsr_mode);
            auto* fsr_quality = CVarSystem::Get()->GetIntCVar("r.FSR3.Quality");
            switch(current_fsr_mode) {
            case 0:
                *fsr_quality = FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
                break;
            case 1:
                *fsr_quality = FFX_UPSCALE_QUALITY_MODE_QUALITY;
                break;
            case 2:
                *fsr_quality = FFX_UPSCALE_QUALITY_MODE_BALANCED;
                break;
            case 3:
                *fsr_quality = FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
                break;
            case 4:
                *fsr_quality = FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
                break;
            }

        } else
#endif
        {
            CVarSystem::Get()->SetIntCVar("r.AntiAliasing", static_cast<int32_t>(render::AntiAliasingType::None));
        }

    ImGui::Separator();
}

void DebugUI::draw_gi_menu() {
    ImGui::Text("Global Illumination");

    ImGui::Separator();

    auto* cvars = CVarSystem::Get();

    draw_combo_box("GI Quality", eastl::array<std::string, 3>{"Low", "Medium", "High"}, selected_gi_quality);
    switch(selected_gi_quality) {
    case 0:
        cvars->SetEnumCVar("r.GI.Mode", render::GIMode::LPV);
        cvars->SetEnumCVar("r.AO", render::AoTechnique::Off);
        break;

    case 1:
        cvars->SetEnumCVar("r.GI.Mode", render::GIMode::LPV);
        cvars->SetEnumCVar("r.AO", render::AoTechnique::RTAO);
        break;

    case 2:
        cvars->SetEnumCVar("r.GI.Mode", render::GIMode::RT);
        cvars->SetEnumCVar("r.AO", render::AoTechnique::Off);
        break;

    default:
        cvars->SetEnumCVar("r.GI.Mode", render::GIMode::Off);
        cvars->SetEnumCVar("r.AO", render::AoTechnique::Off);
    }
}

void DebugUI::draw_scene_outline() {
    if(ImGui::Begin("Scene Outline")) {
        const auto& application = Engine::get();
        const auto& scene = application.get_scene();
        const auto& registry = scene.get_registry();

        const auto& roots = scene.get_top_level_entities();
        for(const auto root : roots) {
            draw_entity(root, registry);
        }
    }

    ImGui::End();
}

void DebugUI::draw_entity(entt::entity entity, const entt::registry& registry, const eastl::string& prefix) {
    auto object_name = std::to_string(static_cast<uint32_t>(entity));
    const auto* game_object = registry.try_get<GameObjectComponent>(entity);
    if(game_object) {
        object_name = game_object->game_object->name.c_str();
    }
    ImGui::Text("%sEntity %s", prefix.c_str(), object_name.c_str());
    const auto& transform = registry.get<TransformComponent>(entity);
    const auto local_to_world = transform.cached_parent_to_world * transform.local_to_parent;
    ImGui::Text(
        "%s\tLocation: %.1f, %.1f, %.1f",
        prefix.c_str(),
        local_to_world[3][0],
        local_to_world[3][1],
        local_to_world[3][2]);

    if(registry.all_of<render::StaticMeshComponent>(entity)) {
        const auto& mesh = registry.get<render::StaticMeshComponent>(entity);
        for(const auto& primitive : mesh.primitives) {
            ImGui::Text("%s\tStaticMeshPrimitive %u", prefix.c_str(), primitive.proxy.index);
        }
    }

    if(registry.all_of<CameraComponent>(entity)) {
        ImGui::Text("%s\tCameraComponent", prefix.c_str());
    }

    if(registry.all_of<physics::CollisionComponent>(entity)) {
        const auto& collision = registry.get<physics::CollisionComponent>(entity);
        ImGui::Text("%s\tCollisionComponent Body=%d", prefix.c_str(), collision.body_id.GetIndexAndSequenceNumber());
    }

    auto& children_expanded = expansion_map[static_cast<uint32_t>(entity)];

    if(!transform.children.empty()) {
        const auto string_name = std::to_string(static_cast<uint32_t>(entity));
        const auto id = string_name + "##children";
        if(ImGui::ArrowButton(id.c_str(), children_expanded ? ImGuiDir_Down : ImGuiDir_Right)) {
            children_expanded = !children_expanded;
        }
        ImGui::SameLine();
    }
    ImGui::Text(
        "%s%u %s",
        prefix.c_str(),
        static_cast<uint32_t>(transform.children.size()),
        transform.children.size() > 1 ? "children" : "child");

    if(children_expanded) {
        for(const auto child : transform.children) {
            draw_entity(child, registry, "\t" + prefix);
        }
    }
}


void DebugUI::draw_combo_box(const std::string& name, eastl::span<const std::string> items, int& selected_item) {
    if(ImGui::BeginCombo(name.c_str(), items[selected_item].c_str(), ImGuiComboFlags_None)) {
        for(auto i = 0; i < items.size(); i++) {
            const auto selected = selected_item == i;
            if(ImGui::Selectable(items[i].c_str(), selected)) {
                selected_item = i;
            }
        }
        ImGui::EndCombo();
    }
}
