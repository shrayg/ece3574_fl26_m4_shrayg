#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>

namespace {

void confirmExit(QWidget* parent) {
    const auto answer = QMessageBox::question(parent, "Exit",
                                              "Are you sure you want to exit?",
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirmExit);
    window.show();

    return app.exec();
}
