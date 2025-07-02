#include "engine.hpp"

#include <imgui.h>
#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

#include "animation/animation_event_component.hpp"
#include "core/system_interface.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "player/first_person_player.hpp"
#include "reflection/reflection_subsystem.hpp"
#include "render/components/skeletal_mesh_component.hpp"
#include "resources/gltf_model.hpp"
#include "scene/entity_info_component.hpp"
#include "scene/game_object_component.hpp"
#include "scene/spawn_gameobject_component.hpp"
#include "scene/transform_component.hpp"
#include "ui/ui_controller.hpp"

static std::shared_ptr<spdlog::logger> logger;

static Engine* instance = nullptr;

Engine& Engine::get() {
    assert(instance);
    return *instance;
}

Engine::Engine() :
    physics_scene{scene}, animation_system{scene} {
    ZoneScoped;

    instance = this;

    logger = SystemInterface::get().get_logger("Application");
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::critical);
    spdlog::flush_on(spdlog::level::err);
    spdlog::flush_on(spdlog::level::warn);

    logger->set_level(spdlog::level::debug);

    application_start_time = std::chrono::high_resolution_clock::now();

    reflection::ReflectionSubsystem::register_types();

    SystemInterface::get().set_input_manager(player_input);

    render::RenderBackend::construct_singleton();

    SystemInterface::get().create_window();
    render::RenderBackend::get().init(SystemInterface::get().get_surface(), SystemInterface::get().get_resolution());

    renderer = eastl::make_unique<render::SarahRenderer>();

    const auto& screen_resolution = SystemInterface::get().get_resolution();
    ui_controller = eastl::make_unique<ui::Controller>(renderer->get_rmlui_renderer(), screen_resolution);

    SystemInterface::get().set_ui_controller(ui_controller.get());

    render_scene = eastl::make_unique<render::RenderScene>(
        renderer->get_mesh_storage(),
        renderer->get_material_storage()
        );

    render_scene->setup_observers(scene);

    renderer->set_scene(*render_scene);

#ifdef JPH_DEBUG_RENDERER
    JPH::DebugRenderer::sInstance = renderer->get_jolt_debug_renderer();
#endif

    last_frame_start_time = std::chrono::high_resolution_clock::now();

    debug_menu = eastl::make_unique<DebugUI>(*renderer);

    update_resolution();

    logger->info("HELLO HUMAN");
}

Engine::~Engine() {
    render::RenderBackend::get().deinit();

    logger->warn("REMAIN INDOORS");
}

entt::handle Engine::add_model_to_scene(
    const std::filesystem::path& scene_path, const eastl::optional<entt::handle>& parent_node
    ) {
    ZoneScoped;

    logger->info("Beginning import of scene {}", scene_path.string());

    const auto& imported_model = resource_loader.get_model(scene_path);

    const auto model_root = imported_model->add_to_scene(scene, parent_node);

    logger->info("Loaded scene {}", scene_path.string());

    return entt::handle{scene.get_registry(), model_root};
}

entt::handle Engine::add_prefab_to_scene(const std::filesystem::path& scene_path, const float4x4& transform) {
    return prefab_loader.load_prefab(scene_path, scene, transform);
}

void Engine::update_resolution() const {
    const auto& screen_resolution = SystemInterface::get().get_resolution();
    renderer->set_output_resolution(screen_resolution);
}

void Engine::tick() {
    ZoneScoped;

    update_time();

    update_perf_tracker();

    spawn_new_game_objects();

    SystemInterface::get().poll_input(player_input);

    player_input.tick(delta_time, scene);

    auto& registry = scene.get_registry();
    registry.view<GameObjectComponent>().each(
        [&](const GameObjectComponent& go) {
            if(go->enabled) {
                go->tick(delta_time, scene);
            }
        });

    physics_scene.tick(delta_time, scene);

    animation_system.tick(delta_time);

    scene.propagate_transforms(delta_time);

    // UI

    ui_controller->tick();

    debug_menu->draw();

    // Rendering

    render_scene->tick(scene);

    renderer->set_imgui_commands(ImGui::GetDrawData());

    renderer->render();
}

render::SarahRenderer& Engine::get_renderer() const {
    return *renderer;
}

ui::Controller& Engine::get_ui_controller() const {
    return *ui_controller;
}

SettingsController& Engine::get_settings_controller() {
    return scalability;
}

float Engine::get_frame_time() const {
    return static_cast<float>(delta_time);
}

float Engine::get_current_time() const {
    return time_since_start;
}

void Engine::set_player_controller_enabled(const bool enabled) {
    player_input.enabled = enabled;
}

void Engine::give_player_full_control() {
    auto& registry = scene.get_registry();
    eastl::optional<entt::entity> parent_entity = eastl::nullopt;
    registry.patch<TransformComponent>(
        player,
        [&](TransformComponent& transform) {
            if(transform.parent != entt::null) {
                parent_entity = transform.parent;
                transform.parent = entt::null;
            }

            const auto local_to_world = transform.get_local_to_world();
            transform.cached_parent_to_world = float4x4{1.f};
            transform.set_local_transform(local_to_world);
        });
    registry.patch<GameObjectComponent>(
        player,
        [&](const GameObjectComponent& comp) {
            auto& fp_player = static_cast<FirstPersonPlayer&>(*comp.game_object);
            const auto& transform = registry.get<TransformComponent>(player);

            fp_player.set_worldspace_location(transform.location);

            fp_player.set_pitch_and_yaw(0, PI - yaw(transform.rotation));

            fp_player.enabled = true;
        });

    if(parent_entity) {
        registry.patch<TransformComponent>(
            *parent_entity,
            [&](TransformComponent& transform) {
                transform.children.erase_first_unsorted(player);
            });
    }

    scene.add_top_level_entities(eastl::array{player});

    set_player_controller_enabled(true);
}

World& Engine::get_scene() {
    return scene;
}

const World& Engine::get_scene() const {
    return scene;
}

physics::PhysicsScene& Engine::get_physics_scene() {
    return physics_scene;
}

AnimationSystem& Engine::get_animation_system() {
    return animation_system;
}

ResourceLoader& Engine::get_resource_loader() {
    return resource_loader;
}

entt::handle Engine::get_player() const {
    return player;
}

const PerformanceTracker& Engine::get_perf_tracker() const {
    return perf_tracker;
}

void Engine::save_world_to_file(const std::filesystem::path& filepath) {

}

void Engine::update_time() {
    const auto frame_start_time = std::chrono::high_resolution_clock::now();
    const auto last_frame_duration = frame_start_time - last_frame_start_time;
    delta_time = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(last_frame_duration).count())
                 / 1000000.f;
    last_frame_start_time = frame_start_time;

    // TODO: Support pausing. Maybe we can add the delta time to the time since start? I'm worried about precision,
    // should probably use doubles

    const auto application_duration = frame_start_time - application_start_time;
    time_since_start = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(application_duration).
                           count())
                       / 1000000.f;
}

void Engine::update_perf_tracker() {
    perf_tracker.add_frame_time_sample(get_frame_time());
}

void Engine::spawn_new_game_objects() {
    auto& registry = scene.get_registry();
    registry.view<SpwanPrefabComponent>().each(
        [&](const entt::entity entity, const SpwanPrefabComponent& spawn_prefab_comp
        ) {
            const auto transform = registry.get<TransformComponent>(entity);
            // Replace this node with the prefab
            const auto instance = add_prefab_to_scene(spawn_prefab_comp.prefab_path.c_str(),
                                                      transform.get_local_to_world());

            eastl::string name = "Spawned Entity";
            const auto* entity_info = registry.try_get<EntityInfoComponent>(entity);
            if(entity_info != nullptr) {
                name = entity_info->name;
            } else {
                const auto* game_object = registry.try_get<GameObjectComponent>(entity);
                if(game_object != nullptr) {
                    name = (*game_object)->name;
                }
            }
            instance.emplace_or_replace<EntityInfoComponent>(EntityInfoComponent{.name = name});

            scene.destroy_entity(entity);
        });
}

void Engine::exit() {
    SystemInterface::get().request_window_close();
}
