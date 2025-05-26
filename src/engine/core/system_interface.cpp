#include "core/system_interface.hpp"

static SystemInterface* instance;

#if defined(_WIN32)
#include "core/win32_system_interface.hpp"
void SystemInterface::initialize(GLFWwindow* window_in, const std::filesystem::path& exe_folder) {
    instance = new Win32SystemInterface{ window_in, exe_folder };
}
#elif defined(__linux__)
#include "core/linux_system_interface.hpp"
void SystemInterface::initialize(GLFWwindow* window_in, const std::filesystem::path& exe_folder) {
    instance = new LinuxSystemInterface{ window_in, exe_folder };
}
#endif

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
