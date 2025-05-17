#pragma once

#include <filesystem>

#include <EASTL/stack.h>
#include <EASTL/unique_ptr.h>
#include <glm/vec2.hpp>
#include <GLFW/glfw3.h>

#include "ui/ui_screen.hpp"

class SystemInterface_GLFW;

namespace Rml {
    class ElementDocument;
    class RenderInterface;
    class Context;
}

namespace ui {
    class Controller
    {
    public:
        Controller(Rml::RenderInterface* renderer, const glm::uvec2& resolution);

        ~Controller();

        /**
         * Shows the UI screen with the specified filepath, destroying the previous screen
         */
        template<typename ScreenClass>
        ScreenClass& show_screen();

        /**
         * Pops the screen at the top of the stack
         */
        void pop_screen();

        /**
         * Updates the context with new input, and renders the UI
         */
        void tick();

        bool in_blocking_ui() const;

        void on_glfw_key(GLFWwindow* window, int key, int scancode, int action, int mods);

        void on_glfw_char(GLFWwindow* window, unsigned int codepoint) const;

        void on_glfw_cursor(GLFWwindow* window, glm::vec2 new_position) const;

        void on_glfw_cursor_enter(GLFWwindow* window, int entered) const;

        void on_glfw_mouse_button(GLFWwindow* window, int button, int action, int mods);

        void on_glfw_scroll(GLFWwindow* glf_wwindow, double xoffset, double yoffset) const;

        Rml::Context& get_context() const;

        void reload();

    private:
        std::unique_ptr<SystemInterface_GLFW> system_interface;

        Rml::Context* context;

        eastl::stack<eastl::unique_ptr<Screen>> screens;

        int glfw_active_modifiers = 0;
        bool debugger_visible = false;
    };

    template <typename ScreenClass>
    ScreenClass& Controller::show_screen() {
        if (!screens.empty()) {
            screens.top()->hide();
        }

        auto screen = eastl::make_unique<ScreenClass>(*this);
        screen->show();

        screens.push(eastl::move(screen));

        return reinterpret_cast<ScreenClass&>(*screens.top());
    }
}
