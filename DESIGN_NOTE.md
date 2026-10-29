# M4 Design Note

Due Wed Oct 28, 11:59 pm. See specification Section 7.

## 1. Components and threads

The GUI thread runs `MainWindow` (all the slots plus new `startBuild()`, `pollBuild()`, and `finishBuild()`), its `QTimer pollTimer_`, and a new non-Qt class `BuildJob` with `start()`, `isDone()`, `takeResult()`, `cancel()`, and a destructor. The timer ticks every 20 ms, but only while a build is alive.

The worker `std::thread` runs only `runBuild()`. It sleeps the delay for each document in small slices, then calls `ProcessingCore::rebuild()` on its own new core.

The worker never touches Qt or `MainWindow`. It gets a copy of the Workspace, the delay, and the job's `std::atomic<bool> cancelled_`. It sends back a `std::unique_ptr<ProcessingCore>` (or an exception) through a `std::promise`. The GUI checks the matching `std::future` with `wait_for(0)`, so it never blocks. The only data both threads touch is the atomic flag and the promise/future state.

## 2. Corpus states

The states are `NotBuilt`, `Building`, `Current`, `OutOfDate`, and `Failed`. `MainWindow` also keeps `workspaceVersion_`, a counter that goes up on every add and every Reset.

Clicking Build from any state except `Building` moves to `Building`, and the job saves the current version. When the job finishes and its version matches, the state goes to `Current` and the chunk count is shown. If the job threw, it goes to `Failed` and the error is shown. Adding a document during `Building` cancels the job and goes to `OutOfDate`, and adding one while `Current` also goes to `OutOfDate`. Reset from any state cancels any job and goes to `NotBuilt`.

Search, Inspect, and Context are only enabled in `Current`. Build is enabled when there are documents and no job thread is alive, so there's never more than one build thread.

## 3. Threatening sequences

If a document is added during a build, the result is stale, and the version check in `finishBuild()` throws it away (R2). If Reset and then an add both happen during a build, the doc count can be the same while the workspace is different. That's why I compare versions instead of counts (R2). Clicking Build twice can't happen, because the button is disabled while a job is alive (R2, R4).

If the build throws, `runBuild()` catches it and calls `set_exception`. `finishBuild()` gets it from `future.get()`, shows "Build failed: ...", and goes to `Failed`. After that you can build again (R3).

On Exit or close mid-build, `~BuildJob()` sets the flag and joins. The delay checks the flag every 10 ms, so the join is quick (R1, R4). If a build finishes while the Exit dialog is open, the timer still fires inside the dialog, so the result is applied normally and "No" changes nothing (R2). With many builds in a row, each job is joined in `takeResult()` before the next one can start, and cores are `unique_ptr`s, so nothing piles up (R4). If the delay is changed mid-build, it doesn't matter, because it's copied at `start()`.

Walkthrough A, Reset then add mid-build. The build starts at version 4, and the job saves 4. Reset clears everything, sets the version to 5, cancels the job, and goes to `NotBuilt`. Adding a document sets the version to 6 and the state to `OutOfDate`. The worker sees the flag and returns early, and `pollBuild()` sees that the future is ready. `finishBuild()` compares 4 to 6, throws the core away, and stays `OutOfDate`. Build is enabled again.

Walkthrough B, Exit during a 30 s build. The user clicks Exit and says Yes, which calls `QApplication::quit()`. `main()` returns, so `~MainWindow` destroys `buildJob_`. `~BuildJob()` sets `cancelled_`, and the worker sees it within 10 ms and returns. `join()` returns and the app exits clean, with no detached thread.

## 4. Ownership of one built corpus

`runBuild()` makes the core with `std::make_unique<ProcessingCore>()` and rebuilds it from its own Workspace copy. `set_value(std::move(core))` moves it into the future's shared state. `takeResult()` joins and moves it out with `future.get()`. If `finishBuild()` accepts it, `processingCore_ = std::move(core)` frees the old core right there. If it's rejected, it's freed when `finishBuild()` returns. An accepted core lives until the next accepted build, Reset, or `~MainWindow`.

## 5. Rejected alternative

I thought about having the worker call `QMetaObject::invokeMethod(mainWindow, ...)` when it finishes. The worker would then need a pointer to `MainWindow`, which could be mid-destruction at exit. Polling a future with a `QTimer` keeps the worker free of Qt, and the GUI decides when to take the result.
