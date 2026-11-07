#include "aiws/ipc/remote_add_server.hpp"

#include "aiws/gui/main_window.hpp"

#include <QMetaObject>
#include <QString>

#include <cerrno>
#include <cstring>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace aiws::ipc {

namespace {

constexpr int kAcceptPollMs = 100;
constexpr int kBacklog = 8;
constexpr auto kRequestTimeout = std::chrono::seconds(5);

std::string errnoText(int error) {
    return std::generic_category().message(error);
}

bool fillAddress(const std::string& path, sockaddr_un& address) {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

// true if some other process is already accepting on this socket
bool someoneIsListening(const sockaddr_un& address) {
    UniqueFd probe(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!probe.valid()) {
        return false;
    }
    return ::connect(probe.get(), reinterpret_cast<const sockaddr*>(&address),
                     sizeof(address)) == 0;
}

}  // namespace

RemoteAddServer::RemoteAddServer(std::string socketPath, gui::MainWindow* window)
    : socketPath_(std::move(socketPath)), window_(window) {}

RemoteAddServer::~RemoteAddServer() { stop(); }

bool RemoteAddServer::start(std::string& error) {
    if (thread_.joinable()) {
        error = "already serving";
        return false;
    }

    sockaddr_un address{};
    if (!fillAddress(socketPath_, address)) {
        error = "socket path is empty or too long: " + socketPath_;
        return false;
    }

    // a socket file left over from a crash is fine to replace, a live one isn't
    struct stat info {};
    if (::lstat(socketPath_.c_str(), &info) == 0) {
        if (!S_ISSOCK(info.st_mode)) {
            error = socketPath_ + " exists and is not a socket";
            return false;
        }
        if (someoneIsListening(address)) {
            error = "another app is already using " + socketPath_;
            return false;
        }
        ::unlink(socketPath_.c_str());
    }

    UniqueFd listener(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!listener.valid()) {
        error = "socket: " + errnoText(errno);
        return false;
    }
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0) {
        error = "bind " + socketPath_ + ": " + errnoText(errno);
        return false;
    }
    if (::listen(listener.get(), kBacklog) != 0) {
        error = "listen: " + errnoText(errno);
        ::unlink(socketPath_.c_str());
        return false;
    }

    listenFd_ = std::move(listener);
    stopping_.store(false);
    try {
        thread_ = std::thread(&RemoteAddServer::serve, this);
    } catch (const std::system_error& threadError) {
        error = std::string("could not start ipc thread: ") + threadError.what();
        listenFd_.reset();
        ::unlink(socketPath_.c_str());
        return false;
    }
    return true;
}

void RemoteAddServer::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stopping_.store(true);
    thread_.join();
    listenFd_.reset();
    ::unlink(socketPath_.c_str());
}

const std::string& RemoteAddServer::socketPath() const noexcept { return socketPath_; }

void RemoteAddServer::serve() {
    while (!stopping_.load()) {
        pollfd waitFor{listenFd_.get(), POLLIN, 0};
        const int ready = ::poll(&waitFor, 1, kAcceptPollMs);
        if (ready <= 0) {
            continue;
        }

        UniqueFd client(::accept4(listenFd_.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (client.valid()) {
            handleClient(client.get());
        }
    }
}

void RemoteAddServer::handleClient(int clientFd) {
    const auto deadline = Clock::now() + kRequestTimeout;

    unsigned char header[kHeaderSize];
    if (readExact(clientFd, reinterpret_cast<char*>(header), kHeaderSize, deadline,
                  &stopping_) != IoResult::Ok) {
        return;
    }

    unsigned char kind = 0;
    std::uint32_t length = 0;
    if (!decodeHeader(header, kind, length) || kind != kAddRequest || length == 0) {
        writeAll(clientFd, encodeMessage(kRejected, "bad request"), deadline, &stopping_);
        return;
    }

    std::string path(length, '\0');
    if (readExact(clientFd, path.data(), length, deadline, &stopping_) != IoResult::Ok) {
        return;
    }

    const std::string reason = checkFile(path);
    if (!reason.empty()) {
        writeAll(clientFd, encodeMessage(kRejected, reason), deadline, &stopping_);
        return;
    }

    // queued, so this never waits on the gui thread
    QMetaObject::invokeMethod(window_, "remote_add_requested", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(path)));
    writeAll(clientFd, encodeMessage(kAccepted, ""), deadline, &stopping_);
}

std::string RemoteAddServer::checkFile(const std::string& path) const {
    if (path.find('\0') != std::string::npos) {
        return "path has a nul byte in it";
    }
    if (path.front() != '/') {
        return "path is not absolute: " + path;
    }

    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        return "cannot read " + path + ": " + errnoText(errno);
    }
    if (!S_ISREG(info.st_mode)) {
        return "not a regular file: " + path;
    }

    UniqueFd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!file.valid()) {
        return "cannot read " + path + ": " + errnoText(errno);
    }
    return "";
}

}  // namespace aiws::ipc
