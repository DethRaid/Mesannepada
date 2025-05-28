#include "engine.hpp"

#include <imgui.h>
#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

#include "system_interface.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "resources/gltf_model.hpp"
#include "resources/gltf_model_component.hpp"
#include "player/first_person_player.hpp"
#include "scene/game_object_component.hpp"
#include "scene/transform_component.hpp"
#include "ui/ui_controller.hpp"

static std::shared_ptr<spdlog::logger> logger;

static Engine* instance = nullptr;

Engine& Engine::get() {
    assert(instance);
    return *instance;
}

Engine::Engine() : physics_scene{ scene }, animation_system{ scene } {
    ZoneScoped;

    instance = this;

    logger = SystemInterface::get().get_logger("Application");
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::critical);
    spdlog::flush_on(spdlog::level::err);
    spdlog::flush_on(spdlog::level::warn);

    logger->set_level(spdlog::level::debug);

    application_start_time = std::chrono::high_resolution_clock::now();

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

    const auto model_root = imported_model->add_to_scene(scene, parent_node->entity());

    logger->info("Loaded scene {}", scene_path.string());

    return entt::handle{scene.get_registry(), model_root};
}

void Engine::update_resolution() const {
    const auto& screen_resolution = SystemInterface::get().get_resolution();
    renderer->set_output_resolution(screen_resolution);
}

void Engine::tick() {
    ZoneScoped;

    update_time();

    // Input

    SystemInterface::get().poll_input(player_input);

    player_input.tick(delta_time, scene);

    auto& registry = scene.get_registry();
    registry.view<GameObjectComponent>().each(
        [&](const GameObjectComponent& go) {
            if (go->enabled) {
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

            const auto local_to_world = transform.cached_parent_to_world * transform.local_to_parent;
            transform.cached_parent_to_world = float4x4{1.f};
            transform.local_to_parent = local_to_world;
        });
    registry.patch<GameObjectComponent>(
        player,
        [&](const GameObjectComponent& comp) {
            auto& fp_player = static_cast<FirstPersonPlayer&>(*comp.game_object);
            const auto& transform = registry.get<TransformComponent>(player);

            float3 scale;
            glm::quat orientation;
            float3 translation;
            float3 skew;
            float4 perspective;
            glm::decompose(transform.local_to_parent, scale, orientation, translation, skew, perspective);

            fp_player.set_worldspace_location(float3{ translation });

            fp_player.set_pitch_and_yaw(pitch(orientation), PI - yaw(orientation));

            fp_player.enabled = true;
        });

    if(parent_entity) {
        registry.patch<TransformComponent>(
            *parent_entity,
            [&](TransformComponent& transform) {
                transform.children.erase_first_unsorted(player);
            });
    }

    scene.add_top_level_entities(eastl::array{player.entity()});

    set_player_controller_enabled(true);
}

Scene& Engine::get_scene() {
    return scene;
}

const Scene& Engine::get_scene() const {
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

entt::handle Engine::get_player() const { return player; }

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

void Engine::exit() {
    SystemInterface::get().request_window_close();
}
