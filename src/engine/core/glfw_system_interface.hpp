#pragma once

#include "core/system_interface.hpp"

/**
 * A SystemInterface for GLFW-based systems
 */
class GlfwSystemInterface : public SystemInterface {
public:
    explicit GlfwSystemInterface(GLFWwindow* window_in, std::filesystem::path exe_folder_in);

    std::shared_ptr<spdlog::logger> get_logger(const std::string& name) override;

    void flush_all_loggers() override;

    eastl::optional<eastl::vector<std::byte>> load_file(const std::filesystem::path& filepath) override;

    FILE* open_file(const std::filesystem::path& filepath) override;

    void write_file(const std::filesystem::path& filepath, const void* data, uint32_t data_size) override;

    void write_file(std::filesystem::path& filepath, const std::string& data) override;

    void set_cursor_hidden(bool hidden) override;

    void poll_input(PlayerInputManager& player_input) override;

    glm::uvec2 get_resolution() override;

    const std::filesystem::path& get_native_library_dir() const override;

    void request_window_close() override;

    void on_glfw_key(GLFWwindow* window_in, int key, int scancode, int action, int mods);

    void on_glfw_mouse_button(GLFWwindow* window, int button, int action, int mods) const;

    void set_cursor_position(glm::vec2 new_position);

    void set_focus(bool focused_in);

    GLFWwindow* get_glfw_window() const;

protected:
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
