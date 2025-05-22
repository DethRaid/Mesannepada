#pragma once

#include <filesystem>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>
#include <EASTL/optional.h>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <spdlog/logger.h>
#include <toml11/types.hpp>

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
    
    virtual const std::filesystem::path& get_native_library_dir() const = 0;

    virtual void request_window_close() = 0;

protected:
    PlayerInputManager* input = nullptr;

    ui::Controller* ui_controller = nullptr;
};
