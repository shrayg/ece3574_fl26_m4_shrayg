#include "aiws/build_job.hpp"
#include "aiws/chunking_strategy.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/gui/main_window.hpp"
#include "aiws/ipc/remote_add_server.hpp"
#include "aiws/ipc/remote_protocol.hpp"
#include "aiws/retrieval_engine.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>

using aiws::gui::MainWindow;

namespace {

constexpr int kWaitMs = 5000;
const QByteArray kSampleText =
    "retrieval systems index documents so a search can find the right chunk quickly\n";

class BrokenChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document&, std::size_t) const override {
        throw std::runtime_error("chunker blew up");
    }
};

std::unique_ptr<aiws::ProcessingCore> makeBrokenCore() {
    return std::make_unique<aiws::ProcessingCore>(std::make_unique<BrokenChunker>(),
                                                  std::make_unique<aiws::RetrievalEngine>(),
                                                  std::make_unique<aiws::ContextBuilder>());
}

QString writeFile(const QString& dir, const QString& name, const QByteArray& text = kSampleText) {
    const QString path = dir + "/" + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text);
    }
    return path;
}

QPushButton* findButton(QWidget& root, const QString& text) {
    for (QPushButton* button : root.findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QString labelText(QWidget& root, const QString& name) {
    const auto* label = root.findChild<QLabel*>(name);
    return label != nullptr ? label->text() : QString();
}

QString corpusStatus(QWidget& root) { return labelText(root, "corpusStatusLabel"); }

QListWidget* documentList(QWidget& root) { return root.findChild<QListWidget*>(); }

int documentCount(QWidget& root) { return documentList(root)->count(); }

int threadCount() {
    return QDir("/proc/self/task").entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
}

struct SendResult {
    int exitCode;
    QString errorText;
};

SendResult runSend(const QString& socketPath, const QString& file,
                   const QString& workDir = QString()) {
    QProcess process;
    if (!workDir.isEmpty()) {
        process.setWorkingDirectory(workDir);
    }
    process.start(AIWS_SEND_PATH, {"--socket", socketPath, "add", file});
    process.waitForFinished(10000);
    const int code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return {code, QString::fromUtf8(process.readAllStandardError())};
}

aiws::ipc::UniqueFd connectTo(const QString& socketPath) {
    aiws::ipc::UniqueFd client(::socket(AF_UNIX, SOCK_STREAM, 0));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const QByteArray pathBytes = socketPath.toUtf8();
    std::memcpy(address.sun_path, pathBytes.constData(), pathBytes.size() + 1);
    if (::connect(client.get(), reinterpret_cast<const sockaddr*>(&address),
                  sizeof(address)) != 0) {
        client.reset();
    }
    return client;
}

}  // namespace

class StudentTests : public QObject {
    Q_OBJECT

private slots:
    // documents and builds (R1 to R4)
    void addDocumentFileRejectsUnreadableFile();
    void buildDoesNotBlockTheGui();
    void buildDelayIsPerDocument();
    void staleBuildIsDroppedAfterAdd();
    void staleBuildIsDroppedAfterResetAndReadd();
    void failedBuildIsReportedAndAppStaysUsable();
    void closingDuringBuildIsQuick();
    void repeatedBuildsDoNotLeakThreads();
    void buildJobDeliversCancelAndErrors();

    // remote add (cr-1, e1 to e4)
    void remoteAddUsesSendersDirectory();
    void remoteAddHandlesOddPaths();
    void remoteAddRejectsUnreadableFile();
    void remoteAddWithoutAppFails();
    void remoteAddArrivesInPieces();
    void remoteAddDuringBuildMakesCorpusStale();
    void stopRemovesSocketQuickly();
    void appQuitsCleanlyOnSigterm();
};

void StudentTests::addDocumentFileRejectsUnreadableFile() {
    QTemporaryDir dir;
    MainWindow window;

    QVERIFY(!window.add_document_file(dir.path() + "/missing.txt"));
    QVERIFY(!window.add_document_file(dir.path()));
    QCOMPARE(documentCount(window), 0);
    QVERIFY(!findButton(window, "Build Corpus")->isEnabled());

    const QString path = writeFile(dir.path(), "a.txt");
    QVERIFY(window.add_document_file(path));
    QVERIFY(!window.add_document_file(path));
    QCOMPARE(documentCount(window), 1);
    QVERIFY(findButton(window, "Build Corpus")->isEnabled());
}

void StudentTests::buildDoesNotBlockTheGui() {
    QTemporaryDir dir;
    MainWindow window;
    window.set_build_delay_ms(300);
    window.add_document_file(writeFile(dir.path(), "a.txt"));
    window.add_document_file(writeFile(dir.path(), "b.txt"));
    QPushButton* build = findButton(window, "Build Corpus");

    // the build takes at least 600 ms, so the click has to come back way before that
    QElapsedTimer timer;
    timer.start();
    build->click();
    QVERIFY(timer.elapsed() < 200);
    QCOMPARE(corpusStatus(window), QString("Building..."));
    QVERIFY(!build->isEnabled());
    QVERIFY(!findButton(window, "Inspect")->isEnabled());

    QTest::qWait(50);
    QVERIFY(window.add_document_file(writeFile(dir.path(), "c.txt")));
    QCOMPARE(documentCount(window), 3);
    QCOMPARE(corpusStatus(window), QString("Out of date"));

    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QCOMPARE(corpusStatus(window), QString("Out of date"));

    window.set_build_delay_ms(0);
    build->click();
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QVERIFY(corpusStatus(window).contains("chunk"));
    QVERIFY(findButton(window, "Inspect")->isEnabled());
}

void StudentTests::buildDelayIsPerDocument() {
    QTemporaryDir dir;
    MainWindow window;
    window.set_build_delay_ms(150);
    for (const char* name : {"a.txt", "b.txt", "c.txt"}) {
        window.add_document_file(writeFile(dir.path(), name));
    }
    QPushButton* build = findButton(window, "Build Corpus");

    QElapsedTimer timer;
    timer.start();
    build->click();
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QVERIFY(timer.elapsed() >= 400);
    QVERIFY(corpusStatus(window).contains("chunk"));
}

void StudentTests::staleBuildIsDroppedAfterAdd() {
    QTemporaryDir dir;
    MainWindow window;
    window.add_document_file(writeFile(dir.path(), "a.txt"));
    QPushButton* build = findButton(window, "Build Corpus");

    // with no delay the worker always finishes a real core, so only the version check can drop it
    build->click();
    window.add_document_file(writeFile(dir.path(), "b.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QCOMPARE(corpusStatus(window), QString("Out of date"));
    QVERIFY(!findButton(window, "Inspect")->isEnabled());

    build->click();
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QVERIFY(corpusStatus(window).contains("chunk"));
    QVERIFY(findButton(window, "Inspect")->isEnabled());
}

void StudentTests::staleBuildIsDroppedAfterResetAndReadd() {
    QTemporaryDir dir;
    MainWindow window;
    const QString path = writeFile(dir.path(), "a.txt");
    window.add_document_file(path);
    QPushButton* build = findButton(window, "Build Corpus");

    // same document count before and after, only the workspace version differs
    build->click();
    findButton(window, "Reset")->click();
    QVERIFY(window.add_document_file(path));
    QCOMPARE(documentCount(window), 1);

    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QCOMPARE(corpusStatus(window), QString("Not built"));
    QVERIFY(!findButton(window, "Inspect")->isEnabled());
}

void StudentTests::failedBuildIsReportedAndAppStaysUsable() {
    QTemporaryDir dir;
    MainWindow window;
    window.set_core_factory(&makeBrokenCore);
    window.add_document_file(writeFile(dir.path(), "a.txt"));
    QPushButton* build = findButton(window, "Build Corpus");

    build->click();
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QCOMPARE(corpusStatus(window), QString("Build failed"));
    QVERIFY(labelText(window, "statusLabel").contains("chunker blew up"));
    QVERIFY(!findButton(window, "Inspect")->isEnabled());

    window.set_core_factory(nullptr);
    build->click();
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QVERIFY(corpusStatus(window).contains("chunk"));
}

void StudentTests::closingDuringBuildIsQuick() {
    QTemporaryDir dir;
    auto window = std::make_unique<MainWindow>();
    window->set_build_delay_ms(60000);
    window->add_document_file(writeFile(dir.path(), "a.txt"));
    const int threadsBefore = threadCount();

    findButton(*window, "Build Corpus")->click();
    QCOMPARE(corpusStatus(*window), QString("Building..."));
    QCOMPARE(threadCount(), threadsBefore + 1);

    QElapsedTimer timer;
    timer.start();
    window.reset();
    QVERIFY(timer.elapsed() < 1000);
    QCOMPARE(threadCount(), threadsBefore);
}

void StudentTests::repeatedBuildsDoNotLeakThreads() {
    QTemporaryDir dir;
    MainWindow window;
    window.add_document_file(writeFile(dir.path(), "a.txt"));
    QPushButton* build = findButton(window, "Build Corpus");

    build->click();
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    const int threadsBefore = threadCount();

    for (int i = 0; i < 20; ++i) {
        build->click();
        QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    }
    QCOMPARE(threadCount(), threadsBefore);
    QVERIFY(corpusStatus(window).contains("chunk"));
}

void StudentTests::buildJobDeliversCancelAndErrors() {
    aiws::Workspace workspace;
    workspace.add_document(aiws::Document("a", "a", kSampleText.toStdString()));

    aiws::BuildJob finished(1);
    finished.start(workspace, 0, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(finished.isDone(), kWaitMs);
    const auto core = finished.takeResult();
    QVERIFY(core != nullptr);
    QVERIFY(core->chunk_count() > 0);

    aiws::BuildJob cancelled(7);
    cancelled.start(workspace, 60000, nullptr);
    QVERIFY(!cancelled.isDone());
    cancelled.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(cancelled.isDone(), 1000);
    QVERIFY(cancelled.takeResult() == nullptr);
    QCOMPARE(cancelled.version(), std::uint64_t{7});

    aiws::BuildJob broken(2);
    broken.start(workspace, 0, &makeBrokenCore);
    QTRY_VERIFY_WITH_TIMEOUT(broken.isDone(), kWaitMs);
    bool threw = false;
    try {
        broken.takeResult();
    } catch (const std::runtime_error& error) {
        threw = std::string(error.what()) == "chunker blew up";
    }
    QVERIFY(threw);
}

void StudentTests::remoteAddUsesSendersDirectory() {
    QTemporaryDir dir;
    MainWindow window;
    aiws::ipc::RemoteAddServer server((dir.path() + "/s.sock").toStdString(), &window);
    std::string error;
    QVERIFY2(server.start(error), error.c_str());
    writeFile(dir.path(), "notes.txt");

    // blocking here also shows the reply doesn't need the gui thread
    const SendResult result = runSend(dir.path() + "/s.sock", "notes.txt", dir.path());
    QCOMPARE(result.exitCode, 0);
    QTRY_COMPARE_WITH_TIMEOUT(documentCount(window), 1, kWaitMs);
    QCOMPARE(documentList(window)->item(0)->text(), QString("notes.txt"));
    QVERIFY(findButton(window, "Build Corpus")->isEnabled());
}

void StudentTests::remoteAddHandlesOddPaths() {
    QTemporaryDir dir;
    MainWindow window;
    aiws::ipc::RemoteAddServer server((dir.path() + "/s.sock").toStdString(), &window);
    std::string error;
    QVERIFY2(server.start(error), error.c_str());

    const QStringList names = {QString("my notes.txt"), QString::fromUtf8("résumé ü.txt"),
                               QString("two\nlines.txt")};
    for (const QString& name : names) {
        const SendResult result =
            runSend(dir.path() + "/s.sock", writeFile(dir.path(), name));
        QCOMPARE(result.exitCode, 0);
    }

    QTRY_COMPARE_WITH_TIMEOUT(documentCount(window), 3, kWaitMs);
    for (int row = 0; row < 3; ++row) {
        QVERIFY(names.contains(documentList(window)->item(row)->text()));
    }
}

void StudentTests::remoteAddRejectsUnreadableFile() {
    QTemporaryDir dir;
    MainWindow window;
    aiws::ipc::RemoteAddServer server((dir.path() + "/s.sock").toStdString(), &window);
    std::string error;
    QVERIFY2(server.start(error), error.c_str());

    const SendResult missing = runSend(dir.path() + "/s.sock", dir.path() + "/missing.txt");
    QCOMPARE(missing.exitCode, 1);
    QVERIFY(missing.errorText.contains("cannot read"));

    const SendResult folder = runSend(dir.path() + "/s.sock", dir.path());
    QCOMPARE(folder.exitCode, 1);
    QVERIFY(!folder.errorText.isEmpty());

    QTest::qWait(200);
    QCOMPARE(documentCount(window), 0);
}

void StudentTests::remoteAddWithoutAppFails() {
    QTemporaryDir dir;
    QElapsedTimer timer;
    timer.start();
    const SendResult result = runSend(dir.path() + "/nobody.sock", "a.txt");
    QCOMPARE(result.exitCode, 2);
    QVERIFY(timer.elapsed() < 4000);
}

void StudentTests::remoteAddArrivesInPieces() {
    QTemporaryDir dir;
    MainWindow window;
    const QString socketPath = dir.path() + "/s.sock";
    aiws::ipc::RemoteAddServer server(socketPath.toStdString(), &window);
    std::string error;
    QVERIFY2(server.start(error), error.c_str());

    const std::string message = aiws::ipc::encodeMessage(
        aiws::ipc::kAddRequest, writeFile(dir.path(), "a.txt").toStdString());
    const aiws::ipc::UniqueFd client = connectTo(socketPath);
    QVERIFY(client.valid());

    // split inside the header and inside the path, with pauses in between
    const std::size_t cuts[] = {0, 2, 7, message.size()};
    for (std::size_t i = 0; i + 1 < std::size(cuts); ++i) {
        const std::string piece = message.substr(cuts[i], cuts[i + 1] - cuts[i]);
        QCOMPARE(::send(client.get(), piece.data(), piece.size(), MSG_NOSIGNAL),
                 static_cast<ssize_t>(piece.size()));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    char reply[aiws::ipc::kHeaderSize];
    const auto deadline = aiws::ipc::Clock::now() + std::chrono::seconds(5);
    QVERIFY(aiws::ipc::readExact(client.get(), reply, sizeof(reply), deadline)
            == aiws::ipc::IoResult::Ok);
    QCOMPARE(static_cast<unsigned char>(reply[0]), aiws::ipc::kAccepted);
    QTRY_COMPARE_WITH_TIMEOUT(documentCount(window), 1, kWaitMs);
}

void StudentTests::remoteAddDuringBuildMakesCorpusStale() {
    QTemporaryDir dir;
    MainWindow window;
    window.set_build_delay_ms(300);
    aiws::ipc::RemoteAddServer server((dir.path() + "/s.sock").toStdString(), &window);
    std::string error;
    QVERIFY2(server.start(error), error.c_str());
    window.add_document_file(writeFile(dir.path(), "a.txt"));
    QPushButton* build = findButton(window, "Build Corpus");

    build->click();
    QCOMPARE(runSend(dir.path() + "/s.sock", writeFile(dir.path(), "b.txt")).exitCode, 0);
    QTRY_COMPARE_WITH_TIMEOUT(documentCount(window), 2, kWaitMs);
    QCOMPARE(corpusStatus(window), QString("Out of date"));

    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), kWaitMs);
    QCOMPARE(corpusStatus(window), QString("Out of date"));
    QVERIFY(!findButton(window, "Inspect")->isEnabled());
}

void StudentTests::stopRemovesSocketQuickly() {
    QTemporaryDir dir;
    MainWindow window;
    const QString socketPath = dir.path() + "/s.sock";
    aiws::ipc::RemoteAddServer server(socketPath.toStdString(), &window);
    std::string error;
    QVERIFY2(server.start(error), error.c_str());
    QVERIFY(QFile::exists(socketPath));

    // a client that stops halfway through its request shouldn't hold up the exit
    const aiws::ipc::UniqueFd staller = connectTo(socketPath);
    QVERIFY(staller.valid());
    QCOMPARE(::send(staller.get(), "A", 1, MSG_NOSIGNAL), static_cast<ssize_t>(1));
    QTest::qWait(150);

    QElapsedTimer timer;
    timer.start();
    server.stop();
    QVERIFY(timer.elapsed() < 500);
    QVERIFY(!QFile::exists(socketPath));
    QCOMPARE(runSend(socketPath, writeFile(dir.path(), "a.txt")).exitCode, 2);
}

void StudentTests::appQuitsCleanlyOnSigterm() {
    QTemporaryDir dir;
    const QString socketPath = dir.path() + "/s.sock";
    QProcess app;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    app.setProcessEnvironment(environment);
    app.start(AIWS_APP_PATH, {"--ipc-socket", socketPath, "--build-delay-ms", "0"});
    QVERIFY(app.waitForStarted());

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(socketPath), kWaitMs);
    QCOMPARE(runSend(socketPath, writeFile(dir.path(), "a.txt")).exitCode, 0);

    app.terminate();
    QVERIFY(app.waitForFinished(3000));
    QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    QCOMPARE(app.exitCode(), 0);
    QVERIFY(!QFile::exists(socketPath));
}

QTEST_MAIN(StudentTests)
#include "student_tests.moc"
