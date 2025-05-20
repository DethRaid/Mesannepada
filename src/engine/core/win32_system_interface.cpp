#include "system_interface.hpp"

#if _WIN32

#include <fstream>

#include <Shlobj.h>
#include <renderdoc/renderdoc_app.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "input/player_input_manager.hpp"
#include "render/render_doc_wrapper.hpp"
#include "ui/ui_controller.hpp"

static void on_glfw_key(GLFWwindow* window, const int key, const int scancode, const int action, const int mods) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->on_glfw_key(window, key, scancode, action, mods);
}

static void on_glfw_cursor(GLFWwindow* window, const double xpos, const double ypos) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->set_cursor_position(glm::vec2{xpos, ypos});
}

static void on_glfw_focus(GLFWwindow* window, const int focused) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->set_focus(focused);
}

static void on_glfw_mouse_button(GLFWwindow* window, const int button, const int action, const int mods) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    win32_system_interface->on_glfw_mouse_button(window, button, action, mods);
}

static void on_glfw_char(GLFWwindow* window, const unsigned int codepoint) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    auto& ui_controller = win32_system_interface->get_ui_controller();
    ui_controller.on_glfw_char(window, codepoint);
}

static void on_glfw_cursor_enter(GLFWwindow* window, int entered) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    auto& ui_controller = win32_system_interface->get_ui_controller();
    ui_controller.on_glfw_cursor_enter(window, entered);
}

static void on_glfw_scroll(GLFWwindow* window, const double xoffset, const double yoffset) {
    auto* win32_system_interface = static_cast<Win32SystemInterface*>(glfwGetWindowUserPointer(window));
    auto& ui_controller = win32_system_interface->get_ui_controller();
    if(ui_controller.in_blocking_ui()) {
        ui_controller.on_glfw_scroll(window, xoffset, yoffset);
    }
}

Win32SystemInterface::Win32SystemInterface(
    GLFWwindow* window_in, std::filesystem::path exe_folder_in
) : window{window_in}, exe_folder{std::move(exe_folder_in)} {
    logger = get_logger("Win32SystemInterface");

    hwnd = glfwGetWin32Window(window);

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

    init_renderdoc_api();
}

static eastl::vector<std::shared_ptr<spdlog::logger>> all_loggers{};

std::shared_ptr<spdlog::logger> Win32SystemInterface::get_logger(const std::string& name) {
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

void Win32SystemInterface::flush_all_loggers() {
    for(auto& log : all_loggers) {
        log->flush();
    }
}

eastl::optional<eastl::vector<std::byte>> Win32SystemInterface::load_file(const std::filesystem::path& filepath) {
    // TODO: Integrate physfs and add the executable's directory to the search paths?
    std::ifstream file{filepath, std::ios::binary};

    if(!file.is_open()) {
        spdlog::warn("Could not open file {}", filepath.string());
        return eastl::nullopt;
    }

    // get its size:
    file.seekg(0, std::ios::end);
    const auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // read the data:
    eastl::vector<std::byte> file_data(file_size);
    file.read(reinterpret_cast<char*>(file_data.data()), file_size);

    return file_data;
}

FILE* Win32SystemInterface::open_file(const std::filesystem::path& filepath) {
    const auto path_string = filepath.string();
    FILE* file = nullptr;
    const auto result = fopen_s(&file, path_string.c_str(), "rb");
    if(result != 0) {
        logger->error("Could not open file {}", filepath.string());
    }

    return file;
}

void Win32SystemInterface::write_file(
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

void Win32SystemInterface::write_file(std::filesystem::path& filepath, const std::string& data) {
    write_file(filepath, data.c_str(), data.size() * sizeof(char));
}

void Win32SystemInterface::set_cursor_hidden(const bool hidden) {
    cursor_disabled = hidden;
    glfwSetInputMode(window, GLFW_CURSOR, hidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Win32SystemInterface::poll_input(PlayerInputManager& player_input) {
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

glm::uvec2 Win32SystemInterface::get_resolution() {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return glm::uvec2{width, height};
}

std::filesystem::path Win32SystemInterface::get_write_folder() {
    PWSTR folder_path;
    const auto result = SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder_path);
    if(result != S_OK) {
        logger->error("Could not retrieve SavedGames folder path");
        return {};
    }

    const auto chonker = std::wstring{folder_path};

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
    return std::filesystem::path{str} / "mesannapada";
}

const std::filesystem::path& Win32SystemInterface::get_native_library_dir() const {
    return exe_folder;
}

void Win32SystemInterface::request_window_close() {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

HWND Win32SystemInterface::get_hwnd() const {
    return hwnd;
}

HINSTANCE Win32SystemInterface::get_hinstance() const {
    return hinstance;
}

void Win32SystemInterface::on_glfw_key(
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
            if(cursor_disabled) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                cursor_disabled = false;
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                cursor_disabled = true;
            }
        }
    }
}

void Win32SystemInterface::on_glfw_mouse_button(
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

void Win32SystemInterface::set_cursor_position(const glm::vec2 new_position) {
    if(ui_controller->in_blocking_ui()) {
        ui_controller->on_glfw_cursor(window, new_position);
    } else {
        raw_cursor_input = new_position - last_cursor_position;
    }
    last_cursor_position = new_position;
}

void Win32SystemInterface::set_focus(const bool focused_in) {
    focused = focused_in;
}

GLFWwindow* Win32SystemInterface::get_glfw_window() const {
    return window;
}

void Win32SystemInterface::init_renderdoc_api() {
    if(auto mod = GetModuleHandleA("renderdoc.dll")) {
        RENDERDOC_API_1_1_2* rdoc_api = nullptr;
        auto renderdoc_get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
        int ret = renderdoc_get_api(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void**>(&rdoc_api));
        if(ret == 1) {
            renderdoc = eastl::make_unique<render::RenderDocWrapper>(rdoc_api);

        }
    }
}

#endif
