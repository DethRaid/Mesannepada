#include "game.hpp"

#include <core/system_interface.hpp>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <core/engine.hpp>
#include <tracy/Tracy.hpp>

#include "game_instance/mesannapada_game_instance.hpp"

int main(const int argc, const char** argv) {
    //try {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto* monitor = glfwGetPrimaryMonitor();
    const auto* mode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "𒈩𒀭𒉌𒅆𒊒𒁕", monitor, nullptr);
    // GLFWwindow* window = glfwCreateWindow(1920, 1080, "𒈩𒀭𒉌𒅆𒊒𒁕", nullptr, nullptr);

    const auto exe_path = std::filesystem::path{ argv[0] };
    const auto exe_folder = exe_path.parent_path();

    SystemInterface::initialize(window, exe_folder);

    Engine engine;

    engine.initialize_game_instance<MesannepadaGameInstance>();

    while(!glfwWindowShouldClose(window)) {
        ZoneScopedN("Frame");

        engine.tick();
    }

    glfwTerminate();
    //} catch(const std::exception& e) {
    //    spdlog::error("Unable to execute application: {}", e.what());
    //}
}
