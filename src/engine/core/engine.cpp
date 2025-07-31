#include "engine.hpp"

#include <imgui.h>
#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

#include "animation/animation_event_component.hpp"
#include "core/string_utils.hpp"
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
    physics_world{world}, animation_system{world} {
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
    serialization::register_serializers();

    SystemInterface::get().set_input_manager(player_input);

    render::RenderBackend::construct_singleton();

    SystemInterface::get().create_window();
    render::RenderBackend::get().init(SystemInterface::get().get_surface(), SystemInterface::get().get_resolution());

    renderer = eastl::make_unique<render::SarahRenderer>();

    const auto& screen_resolution = SystemInterface::get().get_resolution();
    ui_controller = eastl::make_unique<ui::Controller>(renderer->get_rmlui_renderer(), screen_resolution);

    SystemInterface::get().set_ui_controller(ui_controller.get());

    render_world = eastl::make_unique<render::RenderWorld>(
        renderer->get_mesh_storage(),
        renderer->get_material_storage()
        );

    render_world->setup_observers(world);

    renderer->set_world(*render_world);

#ifdef JPH_DEBUG_RENDERER
    JPH::DebugRenderer::sInstance = renderer->get_jolt_debug_renderer();
#endif

    last_frame_start_time = std::chrono::high_resolution_clock::now();

    debug_menu = eastl::make_unique<DebugUI>(*renderer);

    update_resolution();

    if(!load_scene("environment.sscene")) {
        create_scene("environment.sscene");
    }

    instantiate_player();

    logger->info("HELLO HUMAN");
}

Engine::~Engine() {
    render::RenderBackend::get().deinit();

    logger->warn("REMAIN INDOORS");
}

entt::handle Engine::add_model_to_world(
    const ResourcePath& model_path, const eastl::optional<entt::handle>& parent_node
    ) {
    ZoneScoped;

    logger->info("Beginning import of model {}", model_path);

    const auto& imported_model = resource_loader.get_model(model_path);

    const auto model_root = imported_model->add_to_world(world, parent_node);

    logger->info("Loaded model {}", model_path);

    return entt::handle{world.get_registry(), model_root};
}

entt::handle Engine::add_prefab_to_world(const ResourcePath& prefab_path, const float4x4& transform) {
    return prefab_loader.load_prefab(prefab_path, world, transform);
}

entt::handle Engine::instantiate_player() {
    player = world.create_entity();
    player.emplace<EntityInfoComponent>("Player");
    player.emplace<TransformComponent>();
    player.emplace<FirstPersonPlayerComponent>().init(player);

    player_input.set_controlled_entity(player);

    return player;
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

    player_input.tick(delta_time, world);

    if(player) {
        player.get<FirstPersonPlayerComponent>().tick(delta_time);
    }

    game_instance->tick(delta_time);

    auto& registry = world.get_registry();
    registry.view<GameObjectComponent>().each(
        [&](const GameObjectComponent& go) {
            if(go->enabled) {
                go->tick(delta_time, world);
            }
        });

    physics_world.tick(delta_time, world);

    animation_system.tick(delta_time);

    world.propagate_transforms(delta_time);

    // UI

    ui_controller->tick();

    debug_menu->draw();

    // Rendering

    render_world->tick(world);

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
    auto& registry = world.get_registry();
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
    registry.patch<FirstPersonPlayerComponent>(
        player,
        [&](FirstPersonPlayerComponent& fp_player) {
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

    world.add_top_level_entities(eastl::array{player});

    set_player_controller_enabled(true);
}

World& Engine::get_world() {
    return world;
}

const World& Engine::get_world() const {
    return world;
}

std::filesystem::path Engine::get_scene_folder() {
    return SystemInterface::get().get_data_folder() / "game" / "scenes";
}

void Engine::create_scene(const eastl::string& name) {
    loaded_scenes.emplace(name, Scene{});
}

bool Engine::load_scene(const eastl::string& name) {
    try {
        const auto scene_file_path = ResourcePath::game(std::filesystem::path{"scenes"} /  name.c_str());
        auto scene = Scene::load_from_file(scene_file_path);
        scene.add_new_objects_to_world();

        loaded_scenes.emplace(name, eastl::move(scene));
        return true;

    } catch(const std::exception& e) {
        logger->error("Could not load scene {}: {}", name, e.what());
        return false;
    }
}

void Engine::unload_scene(const eastl::string& name) {
    loaded_scenes.erase(name);
}

Scene& Engine::get_environment_scene() {
    return get_scene("environment.sscene");
}

Scene& Engine::get_scene(const eastl::string& name) {
    logger->debug("Searching for scene {}", name);
    return loaded_scenes.at(name);
}

const Scene& Engine::get_scene(const eastl::string& name) const {
    logger->debug("Searching for scene {}", name);
    return loaded_scenes.at(name);
}

physics::PhysicsWorld& Engine::get_physics_world() {
    return physics_world;
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

uint64_t Engine::get_gpu_memory() {
    const auto& backend = render::RenderBackend::get();

    return backend.get_global_allocator().get_memory_usage();
}

void Engine::update_perf_tracker() {
    perf_tracker.add_frame_time_sample(get_frame_time());
    perf_tracker.add_memory_sample(get_gpu_memory());
}

void Engine::spawn_new_game_objects() {
    auto& registry = world.get_registry();
    registry.view<SpwanPrefabComponent>().each(
        [&](const entt::entity entity, const SpwanPrefabComponent& spawn_prefab_comp
        ) {
            const auto transform = registry.get<TransformComponent>(entity);
            // Replace this node with the prefab
            const auto instance = add_prefab_to_world(spawn_prefab_comp.prefab_path,
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

            world.destroy_entity(entity);
        });
}

void Engine::exit() {
    SystemInterface::get().request_window_close();
}
