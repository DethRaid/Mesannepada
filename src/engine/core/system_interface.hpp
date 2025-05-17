#pragma once

#include <filesystem>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/optional.h>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <spdlog/logger.h>

#if defined(__ANDROID__)
#include <game-activity/native_app_glue/android_native_app_glue.h>

#elif defined(_WIN32)
#include <Windows.h>
#include <GLFW/glfw3.h>
#endif

#include <toml11/types.hpp>

#include "render/render_doc_wrapper.hpp"

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
#if defined(_WIN32)
    static void initialize(GLFWwindow* window_in, const std::filesystem::path& exe_folder);
#elif defined(__ANDROID__)
    static void initialize(android_app* app);
#endif

    static SystemInterface& get();

    virtual ~SystemInterface() = default;

    /**
     * Gets a system logger with the specified name
     *
     * The logger may print to a file, to the system logs, to stdout, to somewhere else
     */
    virtual std::shared_ptr<spdlog::logger> get_logger(const std::string& name) = 0;

    virtual void flush_all_loggers() = 0;

    /**
     * Retrieves a writable folder where we can store game data
     */
    virtual std::filesystem::path get_write_folder() = 0;

    /**
     * Reads a file in its entirety
     *
     * This method returns an empty optional if the file can't be read. It returns a zero-length vector if the file can
     * be read but just happens to have no data
     */
    virtual eastl::optional<eastl::vector<std::byte>> load_file(const std::filesystem::path& filepath) = 0;

    virtual FILE* open_file(const std::filesystem::path& filepath) = 0;

    /**
     * Writes some data to a file
     */
    virtual void write_file(const std::filesystem::path& filepath, const void* data, uint32_t data_size) = 0;

    virtual void write_file(std::filesystem::path& filepath, const std::string& data) = 0;

    virtual void set_cursor_hidden(bool hidden) = 0;

    /**
     * Polls the platform's input state and pushes it to the input manager
     */
    virtual void poll_input(PlayerInputManager& input) = 0;

    virtual glm::uvec2 get_resolution() = 0;

    void set_input_manager(PlayerInputManager& input_in);

    void set_ui_controller(ui::Controller* ui_controller_in);

    ui::Controller& get_ui_controller() const;

    bool is_renderdoc_loaded() const;

    render::RenderDocWrapper& get_renderdoc() const;

    virtual const std::filesystem::path& get_native_library_dir() const = 0;

    virtual void request_window_close() = 0;

protected:
    eastl::unique_ptr<render::RenderDocWrapper> renderdoc;

    PlayerInputManager* input = nullptr;

    ui::Controller* ui_controller = nullptr;
};

#if defined(__ANDROID__)
// Android implementation
/**
 * Android implementation of the system interface
 */
class AndroidSystemInterface : public SystemInterface {
public:
    AndroidSystemInterface(android_app* app);

    std::shared_ptr<spdlog::logger> get_logger(const std::string& name) override;

    void flush_all_loggers() override;

    tl::optional<eastl::vector<uint8_t>> load_file(const std::filesystem::path& filepath) override;

    void write_file(const std::filesystem::path& filepath, const void* data, uint32_t data_size) override;

    void poll_input(InputManager& input) override;

    glm::uvec2 get_resolution() override;

    std::string get_native_library_dir() const override;

    android_app* get_app() const;

    ANativeWindow* get_window();

    AAssetManager* get_asset_manager();

private:
    android_app* app = nullptr;

    AAssetManager* asset_manager = nullptr;

    ANativeWindow* window = nullptr;
};

#elif defined(_WIN32)
// Windows implementation
class Win32SystemInterface final : public SystemInterface {
public:
    explicit Win32SystemInterface(GLFWwindow* window_in, std::filesystem::path exe_folder_in);

    std::shared_ptr<spdlog::logger> get_logger(const std::string& name) override;

    void flush_all_loggers() override;

    eastl::optional<eastl::vector<std::byte>> load_file(const std::filesystem::path& filepath) override;

    FILE* open_file(const std::filesystem::path& filepath) override;

    void write_file(const std::filesystem::path& filepath, const void* data, uint32_t data_size) override;

    void write_file(std::filesystem::path& filepath, const std::string& data) override;

    void set_cursor_hidden(bool hidden) override;

    void poll_input(PlayerInputManager& player_input) override;

    glm::uvec2 get_resolution() override;

    std::filesystem::path get_write_folder() override;

    const std::filesystem::path& get_native_library_dir() const override;

    void request_window_close() override;

    HWND get_hwnd() const;

    HINSTANCE get_hinstance() const;

    void on_glfw_key(GLFWwindow* window_in, int key, int scancode, int action, int mods);

    void on_glfw_mouse_button(GLFWwindow* window, int button, int action, int mods) const;

    void set_cursor_position(glm::vec2 new_position);

    void set_focus(bool focused_in);

    GLFWwindow* get_glfw_window() const;

private:
    std::shared_ptr<spdlog::logger> logger;

    GLFWwindow* window = nullptr;

    std::filesystem::path exe_folder;

    HWND hwnd = nullptr;

    HINSTANCE hinstance = nullptr;

    glm::vec2 raw_cursor_input = {};

    glm::vec2 last_cursor_position = {};

    bool focused = true;

    glm::vec3 raw_player_movement_axis = glm::vec3{ 0 };

    bool cursor_disabled = true;

    void init_renderdoc_api();
};
#endif
