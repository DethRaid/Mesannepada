#pragma once

#include <filesystem>

#include <EASTL/shared_ptr.h>
#include <fastgltf/core.hpp>

#include "animation/animation_system.hpp"
#include "audio/audio_controller.hpp"
#include "core/performance_tracker.hpp"
#include "game_framework/game_instance.hpp"
#include "input/player_input_manager.hpp"
#include "render/sarah_renderer.hpp"
#include "render/render_scene.hpp"
#include "scalability/settings_controller.hpp"
#include "ui/debug_menu.hpp"
#include "ui/ui_controller.hpp"
#include "physics/physics_scene.hpp"
#include "resources/prefab_loader.hpp"
#include "resources/resource_loader.hpp"
#include "scene/world.hpp"
#include "scene/scene_file.hpp"

class SystemInterface;

class Engine {
public:
    static Engine& get();

    explicit Engine();

    ~Engine();

    entt::handle add_model_to_world(
        const std::filesystem::path& model_path, const eastl::optional<entt::handle>& parent_node = eastl::nullopt
        );

    entt::handle add_prefab_to_world(const std::filesystem::path& prefab_path, const float4x4& transform);

    template<typename PlayerType>
    void instantiate_player();

    /**
     * Reads the window resolution from the system interface, and updates the renderer for the new resolution
     */
    void update_resolution() const;

    template<typename GameInstanceType>
    void initialize_game_instance();

    void tick();

    static void exit();

    render::SarahRenderer& get_renderer() const;

    ui::Controller& get_ui_controller() const;

    SettingsController& get_settings_controller();

    float get_frame_time() const;

    /**
     * @return Time since the application launched
     */
    float get_current_time() const;

    void set_player_controller_enabled(bool enabled);

    /**
     * Un-parents the player from anything it's parented to, and enables the player controller
     */
    void give_player_full_control();

    World& get_world();

    const World& get_world() const;

    void create_scene(const eastl::string& name);

    /**
     * Tries to load the scene with the specified name, and add it to the world
     * @return True if success, false if not
     */
    bool load_scene(const eastl::string& name);

    /**
     * Retrieves the main scene. This is the "bones" of the world - terrain, buildings, faraway geo, etc
     */
    Scene& get_main_scene();

    template<class Engine>
    constexpr auto&& get_scene(this Engine&& self, const eastl::string& name) {
        return self.loaded_scenes.at(name);
    }

    const eastl::unordered_map<eastl::string, Scene>& get_loaded_scenes() const;

    physics::PhysicsWorld& get_physics_world();

    AnimationSystem& get_animation_system();

    ResourceLoader& get_resource_loader();

    entt::handle get_player() const;

    const PerformanceTracker& get_perf_tracker() const;

private:
    std::chrono::high_resolution_clock::time_point application_start_time;

    float time_since_start = 0.f;

    std::chrono::high_resolution_clock::time_point last_frame_start_time;

    float delta_time = 0.f;

    eastl::unique_ptr<render::SarahRenderer> renderer;

    eastl::unique_ptr<ui::Controller> ui_controller;

    eastl::unique_ptr<audio::Controller> audio_controller;

    World world;

    PrefabLoader prefab_loader;

    physics::PhysicsWorld physics_world;

    eastl::unique_ptr<render::RenderWorld> render_world;

    SettingsController scalability;

    PlayerInputManager player_input;

    AnimationSystem animation_system;

    eastl::unique_ptr<DebugUI> debug_menu;

    ResourceLoader resource_loader;

    eastl::unique_ptr<GameInstance> game_instance;

    entt::handle player = {};

    void update_time();

    PerformanceTracker perf_tracker;

    void update_perf_tracker();

    /**
     * Spawns new GameObjects from SpawnGameObject components
     */
    void spawn_new_game_objects();

    /**
     * Map of all scenes that have been loaded into memory
     */
    eastl::unordered_map<eastl::string, Scene> loaded_scenes;
};

template<typename PlayerType>
void Engine::instantiate_player() {
    player = world.create_game_object<PlayerType>("Player");
    player_input.set_controlled_entity(player);
}

template<typename GameInstanceType>
void Engine::initialize_game_instance() {
    game_instance = eastl::make_unique<GameInstanceType>();
}
