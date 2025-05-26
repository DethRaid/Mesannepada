#pragma once

#include <filesystem>

#include "EASTL/stack.h"

namespace Rml {
    class ElementDocument;
    class Context;
}

namespace ui {
    class Controller;

    /**
     * Interface for top-level screens
     */
    class Screen
    {
    public:
        virtual ~Screen();

        bool is_blocking() const;

        void show() const;

        void hide() const;

        void reload();

        /**
         * Called when the screen should respond to the escape key. Might show a conformation pop-up
         *
         * @return True if we've consumed the escape key, false to let it fall through to RmlUI
         */
        virtual bool on_escape();

    protected:
        Controller& controller;

        std::filesystem::path document_path;

        Rml::ElementDocument* document = nullptr;

        /**
         * Whether this screen should block input. Should be true for menus, false for HUDs
         */
        bool is_blocking_screen = false;

        explicit Screen(Controller& controller_in);

        void load_document(const std::filesystem::path& filepath);
    };
}
