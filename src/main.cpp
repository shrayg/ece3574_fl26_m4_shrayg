#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

#include <iostream>

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

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption delayOption("build-delay-ms",
                                         "Extra build time per document.", "N", "0");
    parser.addOption(delayOption);
    parser.process(app);

    bool isNumber = false;
    const int buildDelayMs = parser.value(delayOption).toInt(&isNumber);
    if (!isNumber || buildDelayMs < 0) {
        std::cerr << "--build-delay-ms needs a whole number >= 0\n";
        return 1;
    }

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirmExit);
    window.set_build_delay_ms(buildDelayMs);
    window.show();

    return app.exec();
}
