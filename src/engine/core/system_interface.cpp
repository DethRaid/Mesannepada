#include "core/system_interface.hpp"

static SystemInterface* instance;

#if defined(_WIN32)
void SystemInterface::initialize(GLFWwindow* window_in, const std::filesystem::path& exe_folder) {
    instance = new Win32SystemInterface{ window_in, exe_folder };
}
#elif defined(__ANDROID__)
void SystemInterface::initialize(android_app* app) {
    instance = new AndroidSystemInterface{ app };
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

bool SystemInterface::is_renderdoc_loaded() const {
    return renderdoc != nullptr;
}

render::RenderDocWrapper& SystemInterface::get_renderdoc() const {
    return *renderdoc;
}

