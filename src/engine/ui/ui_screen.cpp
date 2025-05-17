#include "ui_screen.hpp"

#include <RmlUi/Core/Context.h>

#include <RmlUi/Core/ElementDocument.h>

#include "ui/ui_controller.hpp"

namespace ui {
    Screen::~Screen() {
        if(document) {
            document->Close();
        }
    }

    bool Screen::is_blocking() const {
        return is_blocking_screen;
    }

    void Screen::show() const {
        document->Show();
    }

    void Screen::hide() const {
        document->Hide();
    }

    void Screen::reload() {
        if(document) {
            document->Close();
        }

        document = controller.get_context().LoadDocument(document_path.string());

        show();
    }

    bool Screen::on_escape() { return false; }

    Screen::Screen(Controller& controller_in) : controller{controller_in} {}

    void Screen::load_document(const std::filesystem::path& filepath) {
        document_path = filepath;
        document = controller.get_context().LoadDocument(filepath.string());
        if(!document) {
            throw std::runtime_error{"Could not load UI screen"};
        }

        show();
    }
}
