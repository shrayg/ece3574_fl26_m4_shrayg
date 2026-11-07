#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// wire format shared by the app and aiws_send. every message is
//   byte 0      kind
//   bytes 1-4   payload length, uint32 big endian, at most kMaxPayload
//   bytes 5...  payload
// request kind is kAddRequest with the absolute file path (utf-8, no nul) as payload.
// reply kind is kAccepted (empty payload) or kRejected (utf-8 reason as payload).
namespace aiws::ipc {

constexpr unsigned char kAddRequest = 'A';
constexpr unsigned char kAccepted = 0;
constexpr unsigned char kRejected = 1;
constexpr std::size_t kHeaderSize = 5;
constexpr std::uint32_t kMaxPayload = 4096;

using Clock = std::chrono::steady_clock;

// closes the file descriptor it holds
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept;
    ~UniqueFd();

    UniqueFd(UniqueFd&& other) noexcept;
    UniqueFd& operator=(UniqueFd&& other) noexcept;
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const noexcept;
    bool valid() const noexcept;
    void reset(int fd = -1) noexcept;

private:
    int fd_{-1};
};

enum class IoResult { Ok, Closed, TimedOut, Stopped, Failed };

// /tmp/aiws-$USER.sock, or the numeric uid if USER isn't set
std::string defaultSocketPath();

std::string encodeMessage(unsigned char kind, const std::string& payload);

// false if the length is over kMaxPayload
bool decodeHeader(const unsigned char* header, unsigned char& kind, std::uint32_t& length);

// both keep going across partial reads/writes until done, the deadline, or stop is set
IoResult readExact(int fd, char* out, std::size_t count, Clock::time_point deadline,
                   const std::atomic<bool>* stop = nullptr);
IoResult writeAll(int fd, const std::string& data, Clock::time_point deadline,
                  const std::atomic<bool>* stop = nullptr);

}  // namespace aiws::ipc
