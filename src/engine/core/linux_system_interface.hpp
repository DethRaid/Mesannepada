#pragma once
#include "glfw_system_interface.hpp"

/**
 * System interface for Linux-based systems
 */
class LinuxSystemInterface final : public GlfwSystemInterface {
public:
    explicit LinuxSystemInterface(GLFWwindow* window_in, std::filesystem::path exe_folder_in);

    std::filesystem::path get_write_folder() override;
};
