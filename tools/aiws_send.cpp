// aiws_send [--socket PATH] add FILE
// exit 0: accepted, 1: rejected (reason on stderr), 2: app not reachable or talking failed

#include "aiws/ipc/remote_protocol.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>

namespace {

using aiws::ipc::Clock;
using aiws::ipc::IoResult;
using aiws::ipc::UniqueFd;

constexpr int kAccepted = 0;
constexpr int kRejected = 1;
constexpr int kUnreachable = 2;
constexpr int kUsage = 64;
constexpr auto kConnectTimeout = std::chrono::seconds(2);
constexpr auto kTalkTimeout = std::chrono::seconds(5);
constexpr auto kRetryPause = std::chrono::milliseconds(50);

int usage() {
    std::cerr << "usage: aiws_send [--socket PATH] add FILE\n";
    return kUsage;
}

// keeps retrying for a bit in case the app is still starting up
UniqueFd connectWithin(const std::string& socketPath, Clock::time_point deadline) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socketPath.empty() || socketPath.size() >= sizeof(address.sun_path)) {
        return UniqueFd();
    }
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);

    while (true) {
        UniqueFd socketFd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
        if (!socketFd.valid()) {
            return UniqueFd();
        }
        if (::connect(socketFd.get(), reinterpret_cast<const sockaddr*>(&address),
                      sizeof(address)) == 0) {
            return socketFd;
        }
        const bool worthRetrying = errno == ENOENT || errno == ECONNREFUSED
                                   || errno == EAGAIN || errno == EINTR;
        if (!worthRetrying || Clock::now() + kRetryPause >= deadline) {
            return UniqueFd();
        }
        std::this_thread::sleep_for(kRetryPause);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socketPath = aiws::ipc::defaultSocketPath();
    int next = 1;
    if (next < argc && std::string(argv[next]) == "--socket") {
        if (next + 1 >= argc) {
            return usage();
        }
        socketPath = argv[next + 1];
        next += 2;
    }
    if (argc - next != 2 || std::string(argv[next]) != "add") {
        return usage();
    }

    // relative paths are relative to where aiws_send was run, not the app
    std::error_code pathError;
    const std::string filePath = std::filesystem::absolute(argv[next + 1], pathError).string();
    if (pathError) {
        std::cerr << "aiws_send: " << pathError.message() << "\n";
        return kUnreachable;
    }
    if (filePath.size() > aiws::ipc::kMaxPayload) {
        std::cerr << "aiws_send: path is too long\n";
        return kRejected;
    }

    const UniqueFd connection = connectWithin(socketPath, Clock::now() + kConnectTimeout);
    if (!connection.valid()) {
        std::cerr << "aiws_send: could not reach the app at " << socketPath << "\n";
        return kUnreachable;
    }

    const auto deadline = Clock::now() + kTalkTimeout;
    const std::string request = aiws::ipc::encodeMessage(aiws::ipc::kAddRequest, filePath);
    if (aiws::ipc::writeAll(connection.get(), request, deadline) != IoResult::Ok) {
        std::cerr << "aiws_send: sending the request failed\n";
        return kUnreachable;
    }

    unsigned char header[aiws::ipc::kHeaderSize];
    unsigned char kind = 0;
    std::uint32_t length = 0;
    if (aiws::ipc::readExact(connection.get(), reinterpret_cast<char*>(header),
                             aiws::ipc::kHeaderSize, deadline) != IoResult::Ok
        || !aiws::ipc::decodeHeader(header, kind, length)) {
        std::cerr << "aiws_send: no reply from the app\n";
        return kUnreachable;
    }

    std::string reason(length, '\0');
    if (length > 0 && aiws::ipc::readExact(connection.get(), reason.data(), length,
                                           deadline) != IoResult::Ok) {
        std::cerr << "aiws_send: reply got cut off\n";
        return kUnreachable;
    }

    if (kind == aiws::ipc::kAccepted) {
        return kAccepted;
    }
    if (kind == aiws::ipc::kRejected) {
        std::cerr << "aiws_send: rejected: " << reason << "\n";
        return kRejected;
    }
    std::cerr << "aiws_send: unexpected reply\n";
    return kUnreachable;
}
