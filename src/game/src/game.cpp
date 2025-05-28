#include "game.hpp"

#include <core/system_interface.hpp>

#include <core/engine.hpp>
#include <tracy/Tracy.hpp>

#include "game_instance/mesannapada_game_instance.hpp"

int main(const int argc, const char** argv) {
    //try {

    const auto exe_path = std::filesystem::path{ argv[0] };
    const auto exe_folder = exe_path.parent_path();

    SystemInterface::initialize(exe_folder);

    Engine engine;

    engine.initialize_game_instance<MesannepadaGameInstance>();

    while(!SystemInterface::get().should_close()) {
        ZoneScopedN("Frame");

        engine.tick();
    }

    //} catch(const std::exception& e) {
    //    spdlog::error("Unable to execute application: {}", e.what());
    //}
}
