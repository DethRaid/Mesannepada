#include "win32_system_interface.hpp"

#if _WIN32

#include <Shlobj.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

Win32SystemInterface::Win32SystemInterface(GLFWwindow* window_in, std::filesystem::path exe_folder_in) : GlfwSystemInterface{window_in, eastl::move(exe_folder_in)} {
    
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

HWND Win32SystemInterface::get_hwnd() const {
    return glfwGetWin32Window(window);
}

HINSTANCE Win32SystemInterface::get_hinstance() const {
    return hinstance;
}

#endif
