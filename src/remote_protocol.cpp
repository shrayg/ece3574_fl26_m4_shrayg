#include "aiws/ipc/remote_protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aiws::ipc {

namespace {

constexpr int kPollSliceMs = 100;

int msUntil(Clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return static_cast<int>(std::clamp<long long>(left, 0, kPollSliceMs));
}

bool shouldStop(const std::atomic<bool>* stop) {
    return stop != nullptr && stop->load();
}

}  // namespace

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {}

UniqueFd::~UniqueFd() { reset(); }

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        reset(other.fd_);
        other.fd_ = -1;
    }
    return *this;
}

int UniqueFd::get() const noexcept { return fd_; }

bool UniqueFd::valid() const noexcept { return fd_ >= 0; }

void UniqueFd::reset(int fd) noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = fd;
}

std::string defaultSocketPath() {
    const char* user = std::getenv("USER");
    const std::string name =
        (user != nullptr && *user != '\0') ? std::string(user) : std::to_string(::getuid());
    return "/tmp/aiws-" + name + ".sock";
}

std::string encodeMessage(unsigned char kind, const std::string& payload) {
    const auto length = static_cast<std::uint32_t>(payload.size());
    std::string message;
    message.reserve(kHeaderSize + payload.size());
    message.push_back(static_cast<char>(kind));
    message.push_back(static_cast<char>((length >> 24) & 0xFF));
    message.push_back(static_cast<char>((length >> 16) & 0xFF));
    message.push_back(static_cast<char>((length >> 8) & 0xFF));
    message.push_back(static_cast<char>(length & 0xFF));
    message += payload;
    return message;
}

bool decodeHeader(const unsigned char* header, unsigned char& kind, std::uint32_t& length) {
    kind = header[0];
    length = (static_cast<std::uint32_t>(header[1]) << 24)
             | (static_cast<std::uint32_t>(header[2]) << 16)
             | (static_cast<std::uint32_t>(header[3]) << 8)
             | static_cast<std::uint32_t>(header[4]);
    return length <= kMaxPayload;
}

IoResult readExact(int fd, char* out, std::size_t count, Clock::time_point deadline,
                   const std::atomic<bool>* stop) {
    std::size_t done = 0;
    while (done < count) {
        if (shouldStop(stop)) {
            return IoResult::Stopped;
        }
        if (Clock::now() >= deadline) {
            return IoResult::TimedOut;
        }

        pollfd waitFor{fd, POLLIN, 0};
        const int ready = ::poll(&waitFor, 1, msUntil(deadline));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return IoResult::Failed;
        }
        if (ready == 0) {
            continue;
        }

        const ssize_t got = ::recv(fd, out + done, count - done, 0);
        if (got == 0) {
            return IoResult::Closed;
        }
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return IoResult::Failed;
        }
        done += static_cast<std::size_t>(got);
    }
    return IoResult::Ok;
}

IoResult writeAll(int fd, const std::string& data, Clock::time_point deadline,
                  const std::atomic<bool>* stop) {
    std::size_t done = 0;
    while (done < data.size()) {
        if (shouldStop(stop)) {
            return IoResult::Stopped;
        }
        if (Clock::now() >= deadline) {
            return IoResult::TimedOut;
        }

        pollfd waitFor{fd, POLLOUT, 0};
        const int ready = ::poll(&waitFor, 1, msUntil(deadline));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return IoResult::Failed;
        }
        if (ready == 0) {
            continue;
        }

        // MSG_NOSIGNAL so a client that hung up gives EPIPE instead of killing us
        const ssize_t sent =
            ::send(fd, data.data() + done, data.size() - done, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return IoResult::Failed;
        }
        done += static_cast<std::size_t>(sent);
    }
    return IoResult::Ok;
}

}  // namespace aiws::ipc
