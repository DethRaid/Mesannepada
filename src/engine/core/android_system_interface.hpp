#pragma once

#include "core/system_interface.hpp"

#if defined(__ANDROID__)

#include <game-activity/native_app_glue/android_native_app_glue.h>

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
#endif

