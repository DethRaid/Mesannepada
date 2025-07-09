#include "debug_menu.hpp"

#include <imgui.h>
#include <magic_enum.hpp>
#include <imgui_impl_glfw.h>
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
#include "core/system_interface.hpp"
#include "physics/collider_component.hpp"
#include "reflection/reflection_types.hpp"
#include "render/sarah_renderer.hpp"
#include "render/backend/render_backend.hpp"
#include "render/backend/resource_allocator.hpp"
#include "render/components/skeletal_mesh_component.hpp"
#include "render/components/static_mesh_component.hpp"
#include "render/visualizers/visualizer_type.hpp"
#include "scene/camera_component.hpp"
#include "scene/transform_component.hpp"

#include <GLFW/glfw3.h>

#include "ImGuiFileDialog.h"
#include "scene/entity_info_component.hpp"

DebugUI::DebugUI(render::SarahRenderer& renderer_in) :
    renderer{renderer_in},
    frame_time_samples(300) {
    const auto& system_interface = SystemInterface::get();
    window = system_interface.get_glfw_window();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplGlfw_InitForVulkan(window, true);

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

#if SAH_USE_STREAMLINE
    use_ray_reconstruction = *CVarSystem::Get()->GetIntCVar("r.DLSS-RR.Enabled") != 0;
#endif
}

DebugUI::~DebugUI() {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::draw() {
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    if(ImGui::IsKeyChordPressed(ImGuiKey_D | ImGuiMod_Alt)) {
        are_tools_visible = !are_tools_visible;
    }

    if(are_tools_visible) {
        if(show_demo) {
            ImGui::ShowDemoWindow(&show_demo);
        }

        draw_editor_menu();

        draw_perf_info_window();

        draw_entity_hierarchy();

        draw_debug_window();

        draw_entity_editor_window();

        if(ImGuiFileDialog::Instance()->Display("open_model_dialog")) {
        }
    }

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

void DebugUI::draw_editor_menu() {
    if(ImGui::BeginMainMenuBar()) {
        if(ImGui::BeginMenu("World")) {
            if(ImGui::MenuItem("Save", "CTRL+S")) {
                // save_world();
            }

            if(ImGui::MenuItem("Open", "CTRL+O")) {
                // open_world();
            }

            if(ImGui::MenuItem("Add Packaged Model")) {
                const auto config = IGFD::FileDialogConfig{
                    .path = SystemInterface::get().get_data_folder().string(),
                    .flags = ImGuiFileDialogFlags_Default | ImGuiFileDialogFlags_DisableCreateDirectoryButton
                };
                ImGuiFileDialog::Instance()->OpenDialog("open_packaged_model", "Open Packaged Model", ".glb,.gltf", config);
            }

            if(ImGui::MenuItem("Add Prefab")) {
                // Show a menu that lets us select a prefab. We can put all the prefabs in data/game/prefabs/ to make life
                // easier. We'll send a ray from the player's camera and spawn the prefab at whatever it intersects with
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void DebugUI::draw_perf_info_window() {
    const auto& engine = Engine::get();

    const auto perf = engine.get_perf_tracker();

    if(ImGui::Begin("Stats")) {
        const auto average_frame_time = perf.get_average_frame_time();
        ImGui::Text("Frame time: %.1f ms (%.0f FPS)", average_frame_time * 1000, 1.f / average_frame_time);

        const auto average_gpu_memory = perf.get_average_gpu_memory();
        ImGui::Text("GPU memory: %lu bytes", average_gpu_memory);
    }

    ImGui::End();
}

void DebugUI::draw_debug_window() {
    if(ImGui::Begin("Debug Menu", &is_debug_menu_open)) {
        if(ImGui::CollapsingHeader("Visualizers")) {
            auto selected_visualizer = renderer.get_active_visualizer();
            for(const auto visualizer : magic_enum::enum_values<render::RenderVisualization>()) {
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
            const auto cvars = CVarSystem::Get();
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

void DebugUI::draw_entity_hierarchy() {
    if(ImGui::Begin("Entity Hierarchy")) {
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

void DebugUI::draw_entity_editor_window() {
    if(ImGui::Begin("Entity Editor")) {
        ImGui::PushID(static_cast<int>(selected_entity));

        auto& registry = Engine::get().get_scene().get_registry();

        // Iterate over all the components in the registry
        for(auto i = 0; auto&& [id, storage] : registry.storage()) {
            if(!storage.contains(selected_entity)) {
                continue;
            }

            const auto storage_name = std::string{storage.type().name()};
            ImGui::SeparatorText(storage_name.c_str());

            if(auto meta = entt::resolve(id)) {
                draw_component_helper(selected_entity,
                                      meta.from_void(storage.value(selected_entity)),
                                      meta.custom(),
                                      false,
                                      i);
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
}

bool DebugUI::draw_component_helper(
    const entt::entity entity, entt::meta_any instance, const entt::meta_custom& custom, bool readonly, int& gui_id
    ) {
    using namespace entt::literals;

    auto meta = instance.type();
    readonly = readonly || meta.traits<reflection::Traits>() & reflection::Traits::EditorReadOnly;

    // If the type has a bespoke EditorWrite or EditorRead function, use that. Otherwise, recurse over data members.
    reflection::PropertiesMap properties = {};
    if(auto* mp = static_cast<const reflection::PropertiesMap*>(custom)) {
        properties = *mp;
    }

    auto changed = false;
    if(auto editor_write = meta.func("editor_write"_hs); editor_write && !readonly) {
        changed |= editor_write.invoke(instance, properties).cast<bool>();
    } else if(auto editor_read = meta.func("editor_read"_hs)) {
        editor_read.invoke(instance, properties);
    } else if(meta.is_sequence_container()) {
        auto is_open = false;
        if(auto it = properties.find("name"_hs); it != properties.end()) {
            is_open = ImGui::TreeNodeEx(instance.try_cast<void>(),
                                        0,
                                        "%s: %d",
                                        it->second.cast<const char*>(),
                                        static_cast<int>(instance.as_sequence_container().size()));
        } else {
            auto name = std::string{meta.info().name()};
            is_open = ImGui::TreeNodeEx(instance.try_cast<void>(),
                                        0,
                                        "%s: %d",
                                        name.c_str(),
                                        static_cast<int>(instance.as_sequence_container().size()));
        }
        if(is_open) {
            for(auto element : instance.as_sequence_container()) {
                auto eType = element.type();
                ImGui::PushID(gui_id++);
                changed |= draw_component_helper(entity, element.as_ref(), eType.custom(), readonly, gui_id);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    } else if(meta.is_associative_container()) {
        ImGui::Text("TODO: associative containers");
        // TODO: Make two-column table.
        for(auto element : instance.as_associative_container()) {
            // auto eType = element.second.type();
            // if (auto traits = eType.traits<Traits>(); traits & Traits::EDITOR || traits & Traits::EDITOR_READ)
            //{
            //   ImGui::PushID(guiId++);
            //   ImGui::Indent();
            //   DrawComponentHelper(element.second.get(eType.id()), eType.custom(), readonly || traits & Traits::EDITOR_READ, guiId);
            //   ImGui::Unindent();
            //   ImGui::PopID();
            // }
        }
    } else if(meta.is_enum()) {
        auto is_open = false;
        if(auto it = properties.find("name"_hs); it != properties.end()) {
            is_open = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s", it->second.cast<const char*>());
        } else {
            auto name = std::string{meta.info().name()};
            is_open = ImGui::TreeNodeEx(instance.try_cast<void>(), 0, "%s", name.c_str());
        }
        if(is_open) {
            for(auto [id, data] : meta.data()) {
                reflection::PropertiesMap data_props = {};
                if(auto* mp = static_cast<const reflection::PropertiesMap*>(data.custom())) {
                    data_props = *mp;
                }

                if(auto it = data_props.find("name"_hs); it != data_props.end()) {
                    ImGui::PushID(gui_id++);
                    auto name = it->second.cast<const char*>();
                    if(ImGui::Selectable(name,
                                         instance == data.get({}),
                                         readonly ? ImGuiSelectableFlags_Disabled : 0)) {
                        instance.assign(data.get({}));
                        changed = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
    } else {
        for(auto [id, data] : meta.data()) {
            if(const auto traits = data.traits<reflection::Traits>(); !(traits & reflection::Traits::NoEditor)) {
                ImGui::PushID(gui_id++);
                ImGui::Indent();
                changed |= draw_component_helper(
                    entity,
                    data.get(instance),
                    data.custom(),
                    readonly || traits & reflection::Traits::EditorReadOnly,
                    gui_id);
                ImGui::Unindent();
                ImGui::PopID();
            }
        }
    }

    return changed;
}

void DebugUI::draw_entity(entt::entity entity, const entt::registry& registry, const eastl::string& prefix) {
    ImGui::PushID(static_cast<int32_t>(entity));

    auto object_name = eastl::string{eastl::string::CtorSprintf{}, "%d", static_cast<uint32_t>(entity)};
    if(const auto* entity_info = registry.try_get<EntityInfoComponent>(entity)) {
        object_name = entity_info->name;
    } else if(const auto* game_object = registry.try_get<GameObjectComponent>(entity)) {
        object_name = game_object->game_object->name;
    }
    ImGui::Text("%sEntity %s", prefix.c_str(), object_name.c_str());

    ImGui::SameLine();
    if(ImGui::Button("Edit")) {
        selected_entity = entity;
        show_entity_editor = true;
    }

    const auto& transform = registry.get<TransformComponent>(entity);

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

    ImGui::PopID();
}

void DebugUI::draw_combo_box(const std::string& name, const eastl::span<const std::string> items, int& selected_item) {
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
