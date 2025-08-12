#include "debug_menu.hpp"

// Must be before Imguizmo
#include <imgui.h>

#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <magic_enum.hpp>
#include <EASTL/sort.h>
#include <GLFW/glfw3.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <glm/gtc/type_ptr.hpp>
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
#include "core/glm_jph_conversions.hpp"
#include "core/string_utils.hpp"
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
#include "scene/entity_info_component.hpp"
#include "scene/transform_component.hpp"

static auto cvar_imgui_theme = AutoCVar_Int{"ui.Imgui.Theme", "What theme to use for Dear ImGUI UI elements", 3};

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

    new_scene_name.resize(256);
}

DebugUI::~DebugUI() {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::draw() {
    ZoneScoped;

    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    ImGuizmo::BeginFrame();

    apply_theme();

    if(ImGui::IsKeyChordPressed(ImGuiKey_D | ImGuiMod_Alt)) {
        are_tools_visible = !are_tools_visible;
    }

    if(are_tools_visible) {
        handle_debug_input();

        if(show_demo) {
            ImGui::ShowDemoWindow(&show_demo);
        }

        draw_editor_menu();

        draw_perf_info_window();

        draw_world_info_window();

        draw_model_selector();

        draw_debug_window();

        draw_entity_editor_window();

        if(selected_entity) {
            draw_guizmos(selected_entity);
        }
    }

    ImGui::Render();
}

void DebugUI::apply_theme() {
    if(cvar_imgui_theme.get() == cached_theme) {
        return;
    }

    cached_theme = cvar_imgui_theme.get();

    switch(cached_theme) {
    case 0:
        ImGui::StyleColorsClassic();
        break;
    case 1:
        ImGui::StyleColorsDark();
        break;
    case 2:
        ImGui::StyleColorsLight();
        break;
    case 3:
        activate_style_dxhr();
        break;
    case 4:
        activate_style_deepdark();
        break;
    case 5:
        activate_style_pink();
        break;
    case 6:
        activate_style_corporategray();
        break;
    }
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

void DebugUI::handle_debug_input() {
    if(ImGui::IsKeyPressed(ImGuiKey_Delete) && selected_entity.valid()) {
        auto& world = Engine::get().get_world();
        world.destroy_entity(selected_entity);

        selected_entity = {};
    }
}

void DebugUI::draw_editor_menu() {
    if(ImGui::BeginMainMenuBar()) {
        if(ImGui::BeginMenu("Scene")) {
            if(ImGui::MenuItem("Save all Scenes", "CTRL+S")) {
                auto& scenes = Engine::get().get_loaded_scenes();
                for(auto& [name, scene] : scenes) {
                    scene.write_to_file(ResourcePath{eastl::string{"game://scenes/"} +  name});
                }
            }

            if(ImGui::MenuItem("Add Model")) {
                show_model_selector = true;
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

        const auto average_gpu_memory = static_cast<double>(perf.get_average_gpu_memory());

        ImGui::Text("GPU memory: %.1f MB", average_gpu_memory / 1000000.0);
    }

    ImGui::End();
}

void DebugUI::draw_debug_window() {
    if(ImGui::Begin("Debug Menu", &is_debug_menu_open)) {
        if(ImGui::Button("Reload UI")) {
            Engine::get().get_ui_controller().reload();
        }

        ImGui::SameLine();

        if(ImGui::Button("Show demo")) {
            show_demo = true;
        }

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

        if(ImGui::CollapsingHeader("cvars")) {
            const auto cvars = CVarSystem::Get();
            cvars->DrawImguiEditor();
        }
    }

    ImGui::End();
}

void DebugUI::draw_scene_unload_confirmation() {
    if(!show_scene_confirmation) {
        return;
    }

    if(scene_to_unload.empty()) {
        SAH_BREAKPOINT;
        return;
    }

    auto& engine = Engine::get();
    if(ImGui::Begin("Are you sure?", &show_scene_confirmation)) {
        ImGui::Text("Scene %s has unsaved changes - are you sure you want to unload it?", scene_to_unload.c_str());
        if(ImGui::Button("Yes I'm sure")) {
            engine.unload_scene(scene_to_unload);
            scene_to_unload.clear();
            show_scene_confirmation = false;
        }
        ImGui::SameLine();
        if(ImGui::Button("Actually, save it first")) {
            const auto scene_path = ResourcePath{eastl::string{"game://scenes/"} + scene_to_unload};
            auto& scene = engine.get_scene(scene_to_unload);
            scene.write_to_file(scene_path);
            engine.unload_scene(scene_to_unload);
            scene_to_unload.clear();
            show_scene_confirmation = false;
        }
        ImGui::SameLine();
        if(ImGui::Button("Forget it")) {
            show_scene_confirmation = false;
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

void DebugUI::load_selected_model(const ResourcePath& filename) {
    auto& engine = Engine::get();

    auto& scene = selected_scene.empty() ? engine.get_environment_scene() : engine.get_scene(selected_scene);

    const auto& obj = scene.add_object(filename, {}, true);
    const auto new_model = obj.entity;

    const auto player = engine.get_player();
    const auto& transform = player.get<TransformComponent>();
    const auto location = transform.location;
    const auto direction = float3{0, 0, 1} * transform.rotation;
    // Raycast from the player's location to the physics scene, place the new object at the hit point
    auto ray_cast = JPH::RRayCast{to_jolt(location + direction), to_jolt(direction)};

    JPH::RayCastResult ray_result;
    ray_result.mFraction = 1000.f;
    auto placement_location = location;
    if(engine.get_physics_world().cast_ray(ray_cast, ray_result)) {
        placement_location = to_glm(ray_cast.GetPointOnRay(ray_result.mFraction));
    }
    new_model.patch<TransformComponent>([&](TransformComponent& trans) {
        trans.location = placement_location;
    });

    selected_entity = new_model;
}

void DebugUI::draw_world_info_window() {
    auto& engine = Engine::get();

    if(ImGui::Begin("World Views")) {
        if(ImGui::BeginTabBar("SceneSelection")) {
            if(ImGui::BeginTabItem("Entities")) {
                auto& world = engine.get_world();
                auto& registry = world.get_registry();

                const auto& roots = world.get_top_level_entities();
                for(const auto root : roots) {
                    draw_entity(root, registry);
                }

                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Scene Objects")) {
                if(selected_scene.empty()) {
                    ImGui::Text("Select a scene to see its objects");
                } else {
                    auto& scene = engine.get_scene(selected_scene);
                    const auto& objects = scene.get_objects();

                    auto idx = 0;
                    for(const auto& obj : objects) {
                        ImGui::PushID(idx);
                        ImGui::Separator();
                        const auto pathstring = obj.filepath.to_string();
                        ImGui::Text("%s", pathstring.c_str());
                        if(ImGui::Button("Select")) {
                            if(obj.entity.valid()) {
                                selected_entity = obj.entity;
                            }
                        }
                        ImGui::PopID();
                        idx++;
                    }
                }

                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Scenes")) {
                if(ImGui::Button("New Scene")) {
                    engine.create_scene(eastl::string{new_scene_name.data()});
                    memset(new_scene_name.data(), '\0', new_scene_name.size());
                }
                ImGui::SameLine();
                ImGui::InputText("Scene Name",
                                 new_scene_name.data(),
                                 new_scene_name.size(),
                                 ImGuiInputTextFlags_CharsNoBlank);

                const auto& loaded_scenes = engine.get_loaded_scenes();

                auto loaded_scene_names = eastl::vector<eastl::string>{};
                loaded_scene_names.reserve(loaded_scenes.size());

                for(const auto& [name, scene] : loaded_scenes) {
                    loaded_scene_names.emplace_back(name);
                }

                // Add all the scenes on disk to the scene list
                auto all_scene_names = eastl::vector<eastl::string>{};
                const auto scenes_folder = Engine::get_scene_folder();
                for(auto const& dir_entry : std::filesystem::directory_iterator{scenes_folder}) {
                    const auto& path = dir_entry.path();
                    if(dir_entry.is_regular_file() && path.extension() == ".sscene") {
                        all_scene_names.emplace_back(path.filename().string().c_str());
                    }
                }

                // Add all the scenes in memory to the scene list, if they're not already there
                for(const auto& name : loaded_scene_names) {
                    if(eastl::find(all_scene_names.begin(), all_scene_names.end(), name) == all_scene_names.end()) {
                        all_scene_names.emplace_back(name);
                    }
                }

                // Sort them so I don't go insane
                eastl::sort(all_scene_names.begin(), all_scene_names.end());

                auto idx = 0;
                for(const auto& name : all_scene_names) {
                    ImGui::PushID(idx);
                    const auto is_loaded = eastl::find(loaded_scene_names.begin(), loaded_scene_names.end(), name)
                                           != loaded_scene_names.end();
                    if(is_loaded) {
                        if(ImGui::Button("Unload")) {
                            const auto& scene = engine.get_scene(name);
                            if(scene.is_dirty()) {
                                show_scene_confirmation = true;
                                scene_to_unload = name;
                            } else {
                                engine.unload_scene(name);
                            }
                        }
                    } else {
                        if(ImGui::Button("Load  ")) {
                            engine.load_scene(name);
                        }
                    }

                    ImGui::SameLine();

                    auto display_name = name;
                    if(name == selected_scene) {
                        display_name = name + "*";
                    }

                    if(ImGui::Button(display_name.c_str())) {
                        selected_scene = name;
                    }

                    ImGui::PopID();
                    idx++;
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    ImGui::End();

    draw_scene_unload_confirmation();
}

void DebugUI::draw_files(const std::filesystem::path& pwd, const std::filesystem::path& base_folder,
                         const eastl::string& prefix
    ) {
    auto folders = eastl::vector<std::filesystem::path>{};
    auto files = eastl::vector<std::filesystem::path>{};
    const auto pwd_full_path = base_folder / pwd;
    for(const auto& entry : std::filesystem::directory_iterator{pwd_full_path}) {
        const auto relative_path = std::filesystem::relative(entry.path(), base_folder);
        if(entry.is_regular_file()) {
            files.emplace_back(relative_path);
        } else if(entry.is_directory()) {
            folders.emplace_back(relative_path);
        }
    }

    eastl::sort(folders.begin(), folders.end());
    eastl::sort(files.begin(), files.end());

    for(const auto& folder : folders) {
        auto& is_dir_open = is_directory_open[folder];
        const auto folder_name_key = folder.string();
        ImGui::Text("%s", prefix.c_str());
        ImGui::SameLine();
        if(ImGui::ArrowButton(folder_name_key.c_str(), is_dir_open ? ImGuiDir_Down : ImGuiDir_Right)) {
            is_dir_open = !is_dir_open;
        }
        ImGui::SameLine();
        const auto folder_name = folder.stem();
        ImGui::Text("%s", folder_name.string().c_str());
        if(is_dir_open) {
            draw_files(folder, base_folder, prefix + "  ");
        }
    }

    for(const auto& file : files) {
        ImGui::Text("%s\t%s", prefix.c_str(), file.filename().string().c_str());
        if(file.extension() == ".glb" || file.extension() == ".gltf" || file.extension() == ".tscn") {
            ImGui::SameLine();

            const auto& style = ImGui::GetStyle();
            const float button_width = ImGui::CalcTextSize("Add").x + style.FramePadding.x * 2.f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button_width);
            const auto id = eastl::string{"Add##"} + file.string().c_str();
            if(ImGui::Button(id.c_str())) {
                // Ensure our paths are sensible
                auto raw_path_string = file.string();
                eastl::replace(raw_path_string.begin(), raw_path_string.end(), '\\', '/');
                load_selected_model(ResourcePath::game(raw_path_string));
            }
        }
    }
}

void DebugUI::draw_model_selector() {
    if(!show_model_selector) {
        return;
    }

    if(ImGui::Begin("Object Selector"), &show_model_selector) {
        const auto base_folder = SystemInterface::get().get_data_folder() / "game";
        draw_files(".", base_folder, "");
    }
    ImGui::End();
}

void DebugUI::draw_entity_editor_window() {
    if(ImGui::Begin("Entity Editor")) {
        ImGui::PushID(static_cast<int>(selected_entity.entity()));

        ImGui::Text("Entity %d", static_cast<uint32_t>(selected_entity.entity()));

        auto& registry = Engine::get().get_world().get_registry();

        // Iterate over all the components in the registry
        for(auto i = 0; auto&& [_, storage] : registry.storage()) {
            if(!storage.contains(selected_entity)) {
                continue;
            }

            const auto& name = storage.type().name();
            auto storage_name = std::string{name};
            if(const auto spacepos = name.find(' '); spacepos != std::string_view::npos) {
                storage_name = name.substr(spacepos + 1);
            }
            const auto id = entt::hashed_string{storage_name.c_str()}.value();
            if(ImGui::CollapsingHeader(storage_name.c_str())) {
                if(auto meta = entt::resolve(id)) {
                    draw_component_helper(selected_entity,
                                          meta.from_void(storage.value(selected_entity)),
                                          meta.custom(),
                                          false,
                                          i);
                }
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void DebugUI::draw_guizmos(const entt::handle entity) {
    static auto cur_operation = ImGuizmo::OPERATION::UNIVERSAL;
    static auto cur_mode = ImGuizmo::MODE::WORLD;

    if(ImGui::IsKeyPressed(ImGuiKey_Q)) {
        cur_operation = ImGuizmo::OPERATION::UNIVERSAL;
    } else if(ImGui::IsKeyPressed(ImGuiKey_W)) {
        cur_operation = ImGuizmo::OPERATION::TRANSLATE;
    } else if(ImGui::IsKeyPressed(ImGuiKey_E)) {
        cur_operation = ImGuizmo::OPERATION::ROTATE;
    } else if(ImGui::IsKeyPressed(ImGuiKey_R)) {
        cur_operation = ImGuizmo::OPERATION::SCALE;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());

    if(const auto* trans = entity.try_get<TransformComponent>()) {
        const auto& view = Engine::get().get_renderer().get_player_view();
        const auto& view_matrix = view.get_view();
        // Intentionally reverse near and far to get reversed Z
        // We need a finite projection matrix because ImGuizmo dies otherwise. This cost me multiple days
        const auto proj_matrix = glm::perspective(glm::radians(view.get_fov()), view.get_aspect_ratio(), 20000.f, view.get_near());
        auto matrix = trans->get_local_to_parent();
        if(ImGuizmo::Manipulate(glm::value_ptr(view_matrix), glm::value_ptr(proj_matrix), cur_operation, cur_mode, glm::value_ptr(matrix))) {
            entity.patch<TransformComponent>([&](auto& transform) {
                transform.set_local_transform(matrix);
            });
        }
    }
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

void DebugUI::draw_entity(entt::entity entity, entt::registry& registry, const eastl::string& prefix) {
    ImGui::PushID(static_cast<int32_t>(entity));

    const auto& transform = registry.get<TransformComponent>(entity);

    auto& children_expanded = expansion_map[static_cast<uint32_t>(entity)];

    if(!transform.children.empty()) {
        const auto string_name = std::to_string(static_cast<uint32_t>(entity));
        const auto id = string_name + "##children";
        if(ImGui::ArrowButton(id.c_str(), children_expanded ? ImGuiDir_Down : ImGuiDir_Right)) {
            children_expanded = !children_expanded;
        }
    } else {
        ImGui::Text("\t");
    }

    ImGui::SameLine();

    const auto string_object_name = fmt::format("{}", static_cast<uint32_t>(entity));
    auto object_name = eastl::string{string_object_name.c_str()};
    if(const auto* entity_info = registry.try_get<EntityInfoComponent>(entity)) {
        object_name = entity_info->name;
    } else if(const auto* game_object = registry.try_get<GameObjectComponent>(entity)) {
        object_name = game_object->game_object->name;
    }
    ImGui::Text("%sEntity %s", prefix.c_str(), object_name.c_str());

    ImGui::SameLine();

    if(ImGui::Button("Edit")) {
        selected_entity = entt::handle{registry, entity};
        show_entity_editor = true;
    }

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
 
void DebugUI::activate_style_dxhr() {
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle{};

    // from https://github.com/ocornut/imgui/issues/707#issuecomment-622934113
    // Edited a little bit
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.51f, 0.36f, 0.15f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.51f, 0.36f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.78f, 0.55f, 0.21f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.51f, 0.36f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.81f, 0.83f, 0.81f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.78f, 0.55f, 0.21f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.51f, 0.36f, 0.15f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.78f, 0.55f, 0.21f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.51f, 0.36f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.93f, 0.65f, 0.14f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.78f, 0.55f, 0.21f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.78f, 0.55f, 0.21f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.51f, 0.36f, 0.15f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.91f, 0.64f, 0.13f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.78f, 0.55f, 0.21f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    style.FramePadding = ImVec2(4, 2);
    style.ItemSpacing = ImVec2(10, 2);
    style.IndentSpacing = 12;
    style.ScrollbarSize = 10;

    // style.WindowRounding = 4;
    // style.FrameRounding = 4;
    // style.PopupRounding = 4;
    // style.ScrollbarRounding = 6;
    // style.GrabRounding = 4;
    // style.TabRounding = 4;

    style.DisplaySafeAreaPadding = ImVec2(4, 4);
}

void DebugUI::activate_style_deepdark() {
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle{};

    // from https://github.com/ocornut/imgui/issues/707#issuecomment-917151020
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    //colors[ImGuiCol_DockingPreview]         = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    //colors[ImGuiCol_DockingEmptyBg]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

    style.WindowPadding = ImVec2(8.00f, 8.00f);
    style.FramePadding = ImVec2(5.00f, 2.00f);
    style.CellPadding = ImVec2(6.00f, 6.00f);
    style.ItemSpacing = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = 7;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;
}

void DebugUI::activate_style_pink() {
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle{};

    // https://github.com/ocornut/imgui/issues/707#issuecomment-2282869426
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.01f, 0.02f, 0.94f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.71f, 0.60f, 0.91f, 0.33f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.07f, 0.12f, 0.89f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.28f, 0.34f, 0.94f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.41f, 0.18f, 0.56f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.60f, 0.20f, 0.87f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.65f, 0.24f, 0.88f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.88f, 0.06f, 0.47f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.86f, 0.18f, 0.61f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.76f, 0.21f, 0.74f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.40f, 0.10f, 0.52f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.97f, 0.21f, 0.49f, 0.31f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.87f, 0.37f, 0.65f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.78f, 0.10f, 0.30f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.18f, 0.86f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.42f, 0.13f, 0.69f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.55f, 0.04f, 0.80f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.78f, 0.50f, 0.87f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.54f, 0.14f, 0.92f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.51f, 0.04f, 0.86f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.23f, 0.13f, 0.40f, 0.86f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.23f, 0.86f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.30f, 0.17f, 0.76f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void DebugUI::activate_style_corporategray() {
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle{};

    // https://github.com/ocornut/imgui/issues/707#issuecomment-468798935
    ImVec4* colors = style.Colors;

    /// 0 = FLAT APPEARENCE
    /// 1 = MORE "3D" LOOK
    int is3D = 0;

    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.12f, 0.12f, 0.12f, 0.71f);
    colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.42f, 0.42f, 0.42f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.42f, 0.42f, 0.42f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.67f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.17f, 0.17f, 0.17f, 0.90f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.335f, 0.335f, 0.335f, 1.000f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.24f, 0.24f, 0.24f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.64f, 0.64f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.54f, 0.54f, 0.54f, 0.35f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.52f, 0.52f, 0.52f, 0.59f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.76f, 0.76f, 0.76f, 0.77f);
    colors[ImGuiCol_Separator] = ImVec4(0.000f, 0.000f, 0.000f, 0.137f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.700f, 0.671f, 0.600f, 0.290f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.702f, 0.671f, 0.600f, 0.674f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.73f, 0.73f, 0.73f, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

    style.PopupRounding = 3;

    style.WindowPadding = ImVec2(4, 4);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(6, 2);

    style.ScrollbarSize = 18;

    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = is3D;

    style.WindowRounding = 3;
    style.ChildRounding = 3;
    style.FrameRounding = 3;
    style.ScrollbarRounding = 2;
    style.GrabRounding = 3;

#ifdef IMGUI_HAS_DOCK
		style.TabBorderSize = is3D;
		style.TabRounding   = 3;

		colors[ImGuiCol_DockingEmptyBg]     = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
		colors[ImGuiCol_Tab]                = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
		colors[ImGuiCol_TabHovered]         = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
		colors[ImGuiCol_TabActive]          = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
		colors[ImGuiCol_TabUnfocused]       = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
		colors[ImGuiCol_DockingPreview]     = ImVec4(0.85f, 0.85f, 0.85f, 0.28f);

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
#endif
}
