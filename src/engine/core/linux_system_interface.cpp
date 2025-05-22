#include "linux_system_interface.hpp"

LinuxSystemInterface::LinuxSystemInterface(GLFWwindow *window_in, std::filesystem::path exe_folder_in) : GlfwSystemInterface{window_in, eastl::move(exe_folder_in)} {
}

std::filesystem::path LinuxSystemInterface::get_write_folder() {
    return "~/.mesannepada";
}
