#include "aiws/gui/main_window.hpp"
#include "aiws/ipc/remote_add_server.hpp"
#include "aiws/ipc/remote_protocol.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QTimer>

#include <csignal>
#include <iostream>
#include <string>

namespace {

constexpr int kSignalCheckMs = 100;

volatile std::sig_atomic_t quitSignalled = 0;

void onQuitSignal(int) { quitSignalled = 1; }

// sigterm/sigint can't touch qt directly, so the gui thread checks a flag instead
class QuitSignalWatcher : public QObject {
    Q_OBJECT

public:
    QuitSignalWatcher() {
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &QuitSignalWatcher::checkSignal);
        timer->start(kSignalCheckMs);
    }

private slots:
    void checkSignal() {
        if (quitSignalled != 0) {
            QApplication::quit();
        }
    }
};

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
    const QCommandLineOption socketOption("ipc-socket",
                                          "Socket aiws_send connects to.", "PATH");
    parser.addOption(delayOption);
    parser.addOption(socketOption);
    parser.process(app);

    bool isNumber = false;
    const int buildDelayMs = parser.value(delayOption).toInt(&isNumber);
    if (!isNumber || buildDelayMs < 0) {
        std::cerr << "--build-delay-ms needs a whole number >= 0\n";
        return 1;
    }
    const std::string socketPath = parser.isSet(socketOption)
                                       ? parser.value(socketOption).toStdString()
                                       : aiws::ipc::defaultSocketPath();

    std::signal(SIGTERM, onQuitSignal);
    std::signal(SIGINT, onQuitSignal);
    QuitSignalWatcher quitWatcher;

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirmExit);
    window.set_build_delay_ms(buildDelayMs);

    // declared after window so it stops and joins before window goes away
    aiws::ipc::RemoteAddServer server(socketPath, &window);
    std::string serverError;
    if (!server.start(serverError)) {
        std::cerr << "remote add is off: " << serverError << "\n";
    }

    window.show();
    return app.exec();
}

#include "main.moc"
