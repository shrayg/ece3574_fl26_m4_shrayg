#include "aiws/build_job.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <utility>

namespace aiws {

namespace {

constexpr int kSleepSliceMs = 10;

// returns false if the job got cancelled during the sleep
bool sleepUnlessCancelled(int delayMs, const std::atomic<bool>& cancelled) {
    int remainingMs = delayMs;
    while (remainingMs > 0) {
        if (cancelled.load()) {
            return false;
        }
        const int sliceMs = std::min(kSleepSliceMs, remainingMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(sliceMs));
        remainingMs -= sliceMs;
    }
    return !cancelled.load();
}

void runBuild(Workspace snapshot,
              int delayMsPerDocument,
              BuildJob::CoreFactory factory,
              const std::atomic<bool>& cancelled,
              std::promise<std::unique_ptr<ProcessingCore>> promise) {
    try {
        if (delayMsPerDocument > 0) {
            for (std::size_t i = 0; i < snapshot.documents().size(); ++i) {
                if (!sleepUnlessCancelled(delayMsPerDocument, cancelled)) {
                    promise.set_value(nullptr);
                    return;
                }
            }
        }

        auto core = factory != nullptr ? factory() : std::make_unique<ProcessingCore>();
        if (core == nullptr) {
            throw std::runtime_error("no processing core to build");
        }
        core->rebuild(snapshot);
        promise.set_value(std::move(core));
    } catch (const std::exception&) {
        promise.set_exception(std::current_exception());
    } catch (...) {
        promise.set_exception(
            std::make_exception_ptr(std::runtime_error("unknown build error")));
    }
}

}  // namespace

BuildJob::BuildJob(std::uint64_t version) : version_(version) {}

BuildJob::~BuildJob() {
    cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void BuildJob::start(Workspace snapshot, int delayMsPerDocument, CoreFactory factory) {
    if (worker_.joinable() || result_.valid()) {
        throw std::logic_error("build job already started");
    }

    std::promise<std::unique_ptr<ProcessingCore>> promise;
    result_ = promise.get_future();
    worker_ = std::thread(runBuild, std::move(snapshot), delayMsPerDocument, factory,
                          std::cref(cancelled_), std::move(promise));
}

void BuildJob::cancel() noexcept { cancelled_.store(true); }

bool BuildJob::isDone() const {
    return result_.valid()
           && result_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::unique_ptr<ProcessingCore> BuildJob::takeResult() {
    if (!result_.valid()) {
        throw std::logic_error("build job has no result");
    }
    // the promise is already set here, so this join only waits for runBuild to return
    if (worker_.joinable()) {
        worker_.join();
    }
    return result_.get();
}

std::uint64_t BuildJob::version() const noexcept { return version_; }

}  // namespace aiws
