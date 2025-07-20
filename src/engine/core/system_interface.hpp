#pragma once

#include <filesystem>
#include <string>

#include <volk.h>
#include <EASTL/optional.h>
#include <EASTL/vector.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <spdlog/logger.h>
#include <spdlog/fmt/std.h>

#include "resources/resource_path.hpp"

namespace ui {
    class Controller;
}

struct GLFWwindow;
class PlayerInputManager;

/**
 * Interface to the system
 */
class SystemInterface
{
public:
    static void initialize(const std::filesystem::path& exe_folder);

    static SystemInterface& get();

    explicit SystemInterface(std::filesystem::path exe_folder);

    ~SystemInterface();

    void create_window();

    VkSurfaceKHR get_surface();

    /**
     * Gets a system logger with the specified name
     *
     * The logger may print to a file, to the system logs, to stdout, to somewhere else
     */
    std::shared_ptr<spdlog::logger> get_logger(const std::string& name);

    void flush_all_loggers();

    std::filesystem::path get_working_directory() const;

    /**
     * Gets the folder where compiled shaders live. Usually exe_dir / shaders
     */
    std::filesystem::path get_shaders_folder() const;

    /**
     * Gets the fodler where game data lives. Usually exe_dir / data
     */
    std::filesystem::path get_data_folder() const;

    /**
     * Retrieves a writable folder where we can store game data
     */
    std::filesystem::path get_write_folder();

    /**
     * Reads a file in its entirety
     *
     * This method returns an empty optional if the file can't be read. It returns a zero-length vector if the file can
     * be read but just happens to have no data
     */
    eastl::optional<eastl::vector<std::byte>> load_file(const ResourcePath& filepath);

    FILE* open_file(const ResourcePath& resource_path);

    /**
     * Writes some data to a file
     */
    void write_file(const std::filesystem::path& filepath, const void* data, uint32_t data_size);

    void write_file(std::filesystem::path& filepath, const std::string& data);

    void set_cursor_hidden(bool hidden);

    /**
     * Polls the platform's input state and pushes it to the input manager
     */
    void poll_input(PlayerInputManager& input);

    glm::uvec2 get_resolution();

    void set_input_manager(PlayerInputManager& input_in);

    void set_ui_controller(ui::Controller* ui_controller_in);

    ui::Controller& get_ui_controller() const;
    
    const std::filesystem::path& get_native_library_dir() const;

    void request_window_close();

    void on_glfw_key(GLFWwindow* window_in, int key, int scancode, int action, int mods);

    void on_glfw_mouse_button(GLFWwindow* window, int button, int action, int mods) const;

    void set_cursor_position(glm::vec2 new_position);

    void set_focus(bool focused_in);

    GLFWwindow* get_glfw_window() const;

    bool should_close() const;

protected:
    PlayerInputManager* input = nullptr;

    ui::Controller* ui_controller = nullptr;

    std::shared_ptr<spdlog::logger> logger;

    GLFWwindow* window = nullptr;

    std::filesystem::path exe_folder;

    glm::vec2 raw_cursor_input = {};

    glm::vec2 last_cursor_position = {};

    bool focused = true;

    glm::vec3 raw_player_movement_axis = glm::vec3{ 0 };

    bool cursor_disabled = true;

    void init_renderdoc_api();
};
