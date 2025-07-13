#include "core/system_interface.hpp"

#include <fstream>

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <vulkan/vk_enum_string_helper.h>
#if __linux__
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#include "core/engine.hpp"
#include "input/player_input_manager.hpp"
#include "render/backend/render_backend.hpp"
#include "ui/ui_controller.hpp"

static void on_glfw_key(GLFWwindow* window, const int key, const int scancode, const int action, const int mods) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->on_glfw_key(window, key, scancode, action, mods);
}

static void on_glfw_cursor(GLFWwindow* window, const double xpos, const double ypos) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->set_cursor_position(glm::vec2{xpos, ypos});
}

static void on_glfw_focus(GLFWwindow* window, const int focused) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->set_focus(focused);
}

static void on_glfw_mouse_button(GLFWwindow* window, const int button, const int action, const int mods) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->on_glfw_mouse_button(window, button, action, mods);
}

static void on_glfw_char(GLFWwindow* window, const unsigned int codepoint) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    auto& ui_controller = win32_system_interface->get_ui_controller();
    ui_controller.on_glfw_char(window, codepoint);
}

static void on_glfw_cursor_enter(GLFWwindow* window, int entered) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    auto& ui_controller = win32_system_interface->get_ui_controller();
    ui_controller.on_glfw_cursor_enter(window, entered);
}

static void on_glfw_scroll(GLFWwindow* window, const double xoffset, const double yoffset) {
    auto* win32_system_interface = static_cast<SystemInterface*>(glfwGetWindowUserPointer(window));
    auto& ui_controller = win32_system_interface->get_ui_controller();
    if(ui_controller.in_blocking_ui()) {
        ui_controller.on_glfw_scroll(window, xoffset, yoffset);
    }
}

static SystemInterface* instance;

void SystemInterface::initialize(const std::filesystem::path& exe_folder) {
    instance = new SystemInterface{exe_folder};
}

SystemInterface& SystemInterface::get() {
    return *instance;
}

void SystemInterface::set_input_manager(PlayerInputManager& input_in) {
    input = &input_in;
}

void SystemInterface::set_ui_controller(ui::Controller* ui_controller_in) {
    ui_controller = ui_controller_in;
}

ui::Controller& SystemInterface::get_ui_controller() const { return *ui_controller; }

SystemInterface::SystemInterface(
    std::filesystem::path exe_folder_in
) :  exe_folder{std::move(exe_folder_in)} {
    logger = SystemInterface::get_logger("SystemInterface");
}

SystemInterface::~SystemInterface() {
    glfwTerminate();
}

void SystemInterface::create_window() {
    glfwInitVulkanLoader(render::RenderBackend::get_vk_load_proc_addr());

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto* monitor = glfwGetPrimaryMonitor();
    const auto* mode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    // GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "𒈩𒀭𒉌𒅆𒊒𒁕", monitor, nullptr);
    window = glfwCreateWindow(1920, 1080, "𒈩𒀭𒉌𒅆𒊒𒁕", nullptr, nullptr);

    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

    glfwSetWindowUserPointer(window, this);

    glfwSetKeyCallback(window, ::on_glfw_key);
    glfwSetCharCallback(window, on_glfw_char);
    glfwSetCursorEnterCallback(window, on_glfw_cursor_enter);
    glfwSetCursorPosCallback(window, on_glfw_cursor);
    glfwSetMouseButtonCallback(window, ::on_glfw_mouse_button);
    glfwSetScrollCallback(window, on_glfw_scroll);
    glfwSetWindowFocusCallback(window, on_glfw_focus);

    auto window_size = glm::ivec2{};
    glfwGetWindowSize(window, &window_size.x, &window_size.y);
    last_cursor_position = window_size / 2;

    if(glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
}

VkSurfaceKHR SystemInterface::get_surface() {
    // Create surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const auto result = glfwCreateWindowSurface(render::RenderBackend::get().get_instance(), window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        logger->error("Could not create Vulkan surface: {}", string_VkResult(result));
    }

    return surface;
}

static eastl::vector<std::shared_ptr<spdlog::logger>> all_loggers{};

std::shared_ptr<spdlog::logger> SystemInterface::get_logger(const std::string& name) {
    auto sinks = eastl::vector<spdlog::sink_ptr>{
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>(),
        std::make_shared<spdlog::sinks::basic_file_sink_mt>("sah.log", true),
    };
    sinks[0]->set_pattern("[%n] [%^%l%$] %v");
    auto new_logger = std::make_shared<spdlog::logger>(name.c_str(), sinks.begin(), sinks.end());

#ifndef NDEBUG
    new_logger->set_level(spdlog::level::debug);
#else
    new_logger->set_level(spdlog::level::warn);
#endif

    // Register the logger so we can access it later if needed
    spdlog::register_logger(new_logger);
    all_loggers.emplace_back(new_logger);

    return new_logger;
}

void SystemInterface::flush_all_loggers() {
    for(auto& log : all_loggers) {
        log->flush();
    }
}

std::filesystem::path SystemInterface::get_shaders_folder() const {
    return exe_folder / "shaders";
}

std::filesystem::path SystemInterface::get_data_folder() const {
    return exe_folder / "data";
}

#if _WIN32
#include <Shlobj.h>
#endif

std::filesystem::path SystemInterface::get_write_folder() {
    // TODO: https://specifications.freedesktop.org/basedir-spec/latest/
#if defined(__linux__)
    const char *homedir;

    if ((homedir = getenv("HOME")) == nullptr) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    return std::filesystem::path{homedir} / ".mesannepada";
#else
    PWSTR folder_path;
    const auto result = SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder_path);
    if (result != S_OK) {
        logger->error("Could not retrieve SavedGames folder path");
        return {};
    }

    const auto chonker = std::wstring{ folder_path };

    int count = WideCharToMultiByte(
        CP_UTF8,
        0,
        chonker.c_str(),
        static_cast<int32_t>(chonker.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, chonker.c_str(), -1, str.data(), count, nullptr, nullptr);
    return std::filesystem::path{ str } / "mesannapada";
#endif
}

eastl::optional<eastl::vector<std::byte>> SystemInterface::load_file(const std::filesystem::path& filepath) {
    // TODO: Integrate physfs and add the executable's directory to the search paths?

    auto* file = open_file(filepath);
    if (!file) {
        spdlog::warn("Could not open file {}", filepath.string());
        return eastl::nullopt;
    }

    fseek(file, 0, SEEK_END);
    const auto file_size = ftell(file);
    rewind(file);

    eastl::vector<std::byte> file_data(file_size);
    fread(file_data.data(), 1, file_size, file);

    fclose(file);

    return file_data;
}

FILE* SystemInterface::open_file(const std::filesystem::path& filepath) {
    const auto path_string = filepath.string();
    FILE* file = nullptr;
#if _WIN32
    const auto result = fopen_s(&file, path_string.c_str(), "rb");
    if (result != 0) {
        logger->error("Could not open file {}: error code {}", filepath.string(), result);
    }
#else
    file = fopen(path_string.c_str(), "rb");
    if(file == nullptr) {
        logger->error("Could not open file {}: {}", filepath.string(), strerror(errno));
    }
#endif

    return file;
}

void SystemInterface::write_file(
    const std::filesystem::path& filepath, const void* data, const uint32_t data_size
) {
    if(filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    auto file = std::ofstream{filepath, std::ios::binary};

    if(!file.is_open()) {
        spdlog::error("Could not open file {} for writing", filepath.string());
        return;
    }

    file.write(static_cast<const char*>(data), data_size);
}

void SystemInterface::write_file(std::filesystem::path& filepath, const std::string& data) {
    write_file(filepath, data.c_str(), data.size() * sizeof(char));
}

void SystemInterface::set_cursor_hidden(const bool hidden) {
    cursor_disabled = hidden;
    glfwSetInputMode(window, GLFW_CURSOR, hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void SystemInterface::poll_input(PlayerInputManager& player_input) {
    // raw_player_movement_axis = float3{ 0 };
    raw_cursor_input = float2{0};

    glfwPollEvents();

    if(!focused) {
        return;
    }

    player_input.set_player_movement(raw_player_movement_axis);

    player_input.set_player_rotation(raw_cursor_input * -1.f);

    auto window_size = glm::ivec2{};
    glfwGetWindowSize(window, &window_size.x, &window_size.y);
    // const auto half_window_size = window_size / 2;
    // glfwSetCursorPos(window, half_window_size.x, half_window_size.y);
}

glm::uvec2 SystemInterface::get_resolution() {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return glm::uvec2{width, height};
}

const std::filesystem::path& SystemInterface::get_native_library_dir() const {
    return exe_folder;
}

void SystemInterface::request_window_close() {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void SystemInterface::on_glfw_key(
    GLFWwindow* window_in, const int key, const int scancode, const int action, const int mods
) {
    if(ui_controller->in_blocking_ui()) {
        ui_controller->on_glfw_key(window_in, key, scancode, action, mods);
    } else {
        // TODO: Find some way to generalize this and not having key bindings hardcoded into the platform layer
        // The core should define a bunch of actions, then the platform layers can define which physical inputs map to which actions

        if(key == GLFW_KEY_W) {
            if(action == GLFW_PRESS) {
                raw_player_movement_axis.z += -1;
            } else if(action == GLFW_RELEASE) {
                raw_player_movement_axis.z -= -1;
            }
        } else if(key == GLFW_KEY_S) {
            if(action == GLFW_PRESS) {
                raw_player_movement_axis.z += 1;
            } else if(action == GLFW_RELEASE) {
                raw_player_movement_axis.z -= 1;
            }
        }

        if(key == GLFW_KEY_A) {
            if(action == GLFW_PRESS) {
                raw_player_movement_axis.x += -1;
            } else if(action == GLFW_RELEASE) {
                raw_player_movement_axis.x -= -1;
            }
        } else if(key == GLFW_KEY_D) {
            if(action == GLFW_PRESS) {
                raw_player_movement_axis.x += 1;
            } else if(action == GLFW_RELEASE) {
                raw_player_movement_axis.x -= 1;
            }
        }

        if(key == GLFW_KEY_SPACE) {
            if(action == GLFW_PRESS) {
                input->add_input_event({.button = InputButtons::Jump, .action = InputAction::Pressed});
            } else if(action == GLFW_RELEASE) {
                input->add_input_event({.button = InputButtons::Jump, .action = InputAction::Released});
            }
        } else if(key == GLFW_KEY_LEFT_CONTROL) {
            if(action == GLFW_PRESS) {
                input->add_input_event({.button = InputButtons::Crouch, .action = InputAction::Pressed});
            } else if(action == GLFW_RELEASE) {
                input->add_input_event({.button = InputButtons::Crouch, .action = InputAction::Released});
            }
        }

        if(key == GLFW_KEY_E) {
            if(action == GLFW_PRESS) {
                input->add_input_event({.button = InputButtons::Interact, .action = InputAction::Pressed});
            }
        }

        if(key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
            set_cursor_hidden(!cursor_disabled);
            Engine::get().set_player_controller_enabled(cursor_disabled);
        }
    }
}

void SystemInterface::on_glfw_mouse_button(
    GLFWwindow* window, const int button, const int action, const int mods
) const {
    if(ui_controller->in_blocking_ui()) {
        ui_controller->on_glfw_mouse_button(window, button, action, mods);
    } else {
        if(button == GLFW_MOUSE_BUTTON_2) {
            if(action == GLFW_PRESS) {
                input->add_input_event(
                    {.button = InputButtons::FlycamEnabled, .action = InputAction::Pressed});

            } else if(action == GLFW_RELEASE) {
                input->add_input_event(
                    {.button = InputButtons::FlycamEnabled, .action = InputAction::Released});
            }
        }
    }
}

void SystemInterface::set_cursor_position(const glm::vec2 new_position) {
    if(ui_controller->in_blocking_ui()) {
        ui_controller->on_glfw_cursor(window, new_position);
    } else {
        raw_cursor_input = new_position - last_cursor_position;
    }
    last_cursor_position = new_position;
}

void SystemInterface::set_focus(const bool focused_in) {
    focused = focused_in;
}

GLFWwindow* SystemInterface::get_glfw_window() const {
    return window;
}

bool SystemInterface::should_close() const {
    return glfwWindowShouldClose(window);
}
