#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>

namespace aiws {

// one background corpus build. every member function runs on the gui thread,
// the worker only gets its own workspace copy, the delay, the factory, and cancelled_
class BuildJob {
public:
    // makes the core the worker builds into, nullptr means a default ProcessingCore
    using CoreFactory = std::unique_ptr<ProcessingCore> (*)();

    explicit BuildJob(std::uint64_t version);
    ~BuildJob();

    BuildJob(const BuildJob&) = delete;
    BuildJob& operator=(const BuildJob&) = delete;

    void start(Workspace snapshot, int delayMsPerDocument, CoreFactory factory);
    void cancel() noexcept;
    bool isDone() const;

    // nullptr if the build was cancelled, rethrows if the build threw
    std::unique_ptr<ProcessingCore> takeResult();

    std::uint64_t version() const noexcept;

private:
    std::uint64_t version_;
    std::atomic<bool> cancelled_{false};
    std::future<std::unique_ptr<ProcessingCore>> result_;
    std::thread worker_;
};

}  // namespace aiws
