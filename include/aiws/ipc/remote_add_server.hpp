#pragma once

#include "aiws/ipc/remote_protocol.hpp"

#include <atomic>
#include <string>
#include <thread>

namespace aiws::gui {
class MainWindow;
}

namespace aiws::ipc {

// listens on a unix socket on its own thread for aiws_send. it checks the file
// and replies by itself, then queues the add onto the gui thread.
// the window has to outlive the server.
class RemoteAddServer {
public:
    RemoteAddServer(std::string socketPath, gui::MainWindow* window);
    ~RemoteAddServer();

    RemoteAddServer(const RemoteAddServer&) = delete;
    RemoteAddServer& operator=(const RemoteAddServer&) = delete;

    // binds the socket and starts the thread, fills error and returns false if it can't
    bool start(std::string& error);

    // stops serving within about 100 ms, joins, and removes the socket file
    void stop();

    const std::string& socketPath() const noexcept;

private:
    void serve();
    void handleClient(int clientFd);
    std::string checkFile(const std::string& path) const;

    std::string socketPath_;
    gui::MainWindow* window_;
    UniqueFd listenFd_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace aiws::ipc
