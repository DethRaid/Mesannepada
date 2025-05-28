#pragma once

#if defined(_WIN32)

#include <Windows.h>
#include <GLFW/glfw3.h>

// Windows implementation
class Win32SystemInterface final : public GlfwSystemInterface {
public:
    explicit Win32SystemInterface(GLFWwindow* window_in, std::filesystem::path exe_folder_in);

    std::filesystem::path get_write_folder() override;

    HWND get_hwnd() const;

    HINSTANCE get_hinstance() const;

private:
    HINSTANCE hinstance = nullptr;
};
#endif
