#include "ui_controller.hpp"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <EASTL/string.h>
#include <EASTL/array.h>
#include <RmlUi/Debugger/Debugger.h>
#include <RmlUi/Core/Factory.h>

#include <tracy/Tracy.hpp>

#include "external/rmlui/RmlUi_Platform_GLFW.h"
#include "console/cvars.hpp"
#include "core/system_interface.hpp"


namespace ui {
    const static eastl::string CONTEXT_NAME = "game_ui";
    static constexpr auto FONT_NAMES = eastl::array{
        "data/ui/shared/LatoLatin-Regular.ttf",
        "data/ui/shared/LatoLatin-Italic.ttf",
        "data/ui/shared/LatoLatin-Bold.ttf",
        "data/ui/shared/LatoLatin-BoldItalic.ttf",
        "data/ui/shared/NotoEmoji-Regular.ttf",
    };

    const static auto FALLBACK_FONT = "data/fonts/NotoSansCuneiform-Regular.ttf";

    static std::shared_ptr<spdlog::logger> logger = nullptr;

    static auto cvar_enable_ui_debugger = AutoCVar_Int{
        "ui.Debugger.Enable", "Whether to enable the RmlUI debugger", true
    };

    Controller::Controller(Rml::RenderInterface* renderer, const glm::uvec2& resolution) {
        ZoneScoped;

        if(logger == nullptr) {
            logger = SystemInterface::get().get_logger("UIController");
        }

        system_interface = std::make_unique<SystemInterface_GLFW>();

        // TODO: Figure out something to do if we ever support Android again
        auto& my_system_interface = dynamic_cast<Win32SystemInterface&>(SystemInterface::get());
        system_interface->SetWindow(my_system_interface.get_glfw_window());

        Rml::SetSystemInterface(system_interface.get());
        Rml::SetRenderInterface(renderer);

        auto result = Rml::Initialise();
        if(!result) {
            logger->error("Could not initialize RmlUI");
        }

        context = Rml::CreateContext(
            CONTEXT_NAME.c_str(),
            {static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y)});
        if(context == nullptr) {
            logger->error("Could not initialize RmlUI context");
        }

        if(cvar_enable_ui_debugger.Get()) {
            result = Rml::Debugger::Initialise(context);
            if(!result) {
                logger->error("Could not initialize RmlUI debugger");
            } else {
                Rml::Debugger::SetVisible(debugger_visible);
            }
        }

        for(const auto& font_name : FONT_NAMES) {
            result = Rml::LoadFontFace(font_name);
            if(!result) {
                logger->error("Could not load font {}", font_name);
            }
        }

        result = Rml::LoadFontFace(FALLBACK_FONT, true);
        if(!result) {
            logger->error("Could not load fallback font {}", FALLBACK_FONT);
        }
    }

    Controller::~Controller() {
        while(!screens.empty()) {
            screens.pop();
        }

        if(cvar_enable_ui_debugger.Get()) {
            Rml::Debugger::Shutdown();
        }
        Rml::RemoveContext(CONTEXT_NAME.c_str());
        Rml::Shutdown();
    }

    void Controller::pop_screen() {
        if(!screens.empty()) {
            screens.pop();
        }

        if(!screens.empty()) {
            screens.top()->show();
        }
    }

    void Controller::tick() {
        ZoneScoped;
        context->Update();
        context->Render();
    }

    bool Controller::in_blocking_ui() const {
        if(!screens.empty()) {
            return screens.top()->is_blocking();
        } else {
            return false;
        }
    }

    void Controller::on_glfw_key(
        GLFWwindow* window, const int glfw_key, const int glfw_scancode, const int glfw_action, const int glfw_mods
    ) {
        if(!context) {
            return;
        }

        // Store the active modifiers for later because GLFW doesn't provide them in the callbacks to the mouse input events.
        glfw_active_modifiers = glfw_mods;

        // Override the default key event callback to add global shortcuts for the samples.
        //KeyDownCallback key_down_callback = key_down_callback;

        switch(glfw_action) {
        case GLFW_PRESS:
            [[fallthrough]];
        case GLFW_REPEAT: {
            const Rml::Input::KeyIdentifier key = RmlGLFW::ConvertKey(glfw_key);
            const int key_modifier = RmlGLFW::ConvertKeyModifiers(glfw_mods);
            float dp_ratio = 1.f;
            glfwGetWindowContentScale(window, &dp_ratio, nullptr);

            // Global shortcuts
            if(glfw_key == GLFW_KEY_ESCAPE) {
                if (!screens.empty() && screens.top()->on_escape()) {
                    break;
                }
            }
            if(glfw_key == GLFW_KEY_F8 && cvar_enable_ui_debugger.Get()) {
                debugger_visible = !debugger_visible;
                Rml::Debugger::SetVisible(debugger_visible);
                break;
            }

            // Otherwise, let RMLUI handle the key
            if(!RmlGLFW::ProcessKeyCallback(context, glfw_key, glfw_action, glfw_mods)) {
                break;
            }
            // The key was not consumed by the context either, try keyboard shortcuts of lower priority.
            //if (key_down_callback && !key_down_callback(context, key, key_modifier, dp_ratio, false)) {
            //    break;
            // }
        }
        break;
        case GLFW_RELEASE:
            RmlGLFW::ProcessKeyCallback(context, glfw_key, glfw_action, glfw_mods);
            break;
        }
    }

    void Controller::on_glfw_char(GLFWwindow* window, const unsigned int codepoint) const {
        RmlGLFW::ProcessCharCallback(context, codepoint);
    }

    void Controller::on_glfw_cursor(GLFWwindow* window, const glm::vec2 new_position) const {
        RmlGLFW::ProcessCursorPosCallback(context, window, new_position.x, new_position.y, glfw_active_modifiers);
    }

    void Controller::on_glfw_cursor_enter(GLFWwindow* window, const int entered) const {
        RmlGLFW::ProcessCursorEnterCallback(context, entered);
    }

    void Controller::on_glfw_mouse_button(GLFWwindow* window, const int button, const int action, const int mods) {
        glfw_active_modifiers = mods;
        RmlGLFW::ProcessMouseButtonCallback(context, button, action, mods);
    }

    void Controller::on_glfw_scroll(GLFWwindow* glf_wwindow, const double xoffset, const double yoffset) const {
        RmlGLFW::ProcessScrollCallback(context, yoffset, glfw_active_modifiers);
    }

    Rml::Context& Controller::get_context() const {
        return *context;
    }

    void Controller::reload() {
        Rml::Factory::ClearStyleSheetCache();
        Rml::Factory::ClearTemplateCache();

        if(!screens.empty()) {
            screens.top()->reload();
        }
    }
}
