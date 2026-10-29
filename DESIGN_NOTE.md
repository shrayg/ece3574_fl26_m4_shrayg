# M4 Design Note

Due Wed Oct 28, 11:59 pm. See specification Section 7.

## 1. Components and threads

- GUI thread: `MainWindow` (all slots plus `startBuild()`, `pollBuild()`, `finishBuild()`), its `QTimer pollTimer_` (20 ms, only runs while a build is alive), and `BuildJob` (new non-Qt class: `start()`, `isDone()`, `takeResult()`, `cancel()`, destructor).
- Worker `std::thread`: `runBuild()`, which sleeps the delay per document in slices and then calls `ProcessingCore::rebuild()` on its own new core.

The worker never touches Qt or `MainWindow`. It gets a Workspace copy, the delay, and the job's `std::atomic<bool> cancelled_`. It sends back a `std::unique_ptr<ProcessingCore>` (or an exception) through a `std::promise`. The GUI checks the `std::future` with `wait_for(0)`, so it never blocks. The only shared data is the atomic flag and the promise/future state.

## 2. Corpus states

States: `NotBuilt`, `Building`, `Current`, `OutOfDate`, `Failed`. `MainWindow` also keeps `workspaceVersion_`, a counter that goes up on every add and every Reset.

| From | Event | To |
|---|---|---|
| any but Building | Build clicked | Building (job remembers the version) |
| Building | job done, versions match | Current, show "N chunks" |
| Building | job threw | Failed, show the error |
| Building | document added | OutOfDate, job cancelled |
| Current | document added | OutOfDate |
| any | Reset | NotBuilt, job cancelled |

Search, Inspect, and Context are enabled only in `Current`. Build is enabled when there are documents and no job thread is alive, so there's only ever one build thread.

## 3. Threatening sequences

- **Add during a build (R2):** the result is stale, so the version check in `finishBuild()` throws it away.
- **Reset then add during a build (R2):** same doc count but a different workspace, which is why I compare versions and not counts.
- **Build clicked twice (R2, R4):** Build is disabled while a job is alive.
- **Build throws (R3):** `runBuild()` catches it and calls `set_exception`. `finishBuild()` gets it from `future.get()`, shows "Build failed: ...", and goes to `Failed`. After that you can build again.
- **Exit or close mid-build (R1, R4):** `~BuildJob()` sets the flag and joins. The delay checks the flag every 10 ms, so the join is quick.
- **Build finishes while the Exit dialog is open (R2):** the timer still fires inside the dialog, so the result is applied normally and "No" changes nothing.
- **Many builds (R4):** each job is joined in `takeResult()` before the next one can start, and cores are `unique_ptr`s.
- **Delay changed mid-build:** the delay is copied at `start()`, so only the next build uses it.

**Walkthrough A, Reset then add mid-build:**
1. Build starts at version 4, and the job saves 4.
2. Reset clears everything, sets the version to 5, cancels the job, and moves to `NotBuilt`.
3. Adding a document sets the version to 6 and the state to `OutOfDate`.
4. The worker sees the flag and returns early. `pollBuild()` sees the future is ready.
5. `finishBuild()` compares 4 to 6, discards the core, and stays `OutOfDate`. Build is enabled again.

**Walkthrough B, Exit during a 30 s build:**
1. Exit, then the callback, then Yes calls `QApplication::quit()`.
2. `main()` returns, so `~MainWindow` destroys `buildJob_`.
3. `~BuildJob()` sets `cancelled_`. The worker sees it within 10 ms and returns.
4. `join()` returns and the app exits clean, with no detached thread.

## 4. Ownership of one built corpus

`runBuild()` makes it with `std::make_unique<ProcessingCore>()` and rebuilds it from its own Workspace copy. `set_value(std::move(core))` moves it into the future's shared state. `takeResult()` joins and moves it out with `future.get()`. If `finishBuild()` accepts it, `processingCore_ = std::move(core)`, and the old core is freed right there. If it's rejected, it's freed when `finishBuild()` returns. An accepted core lives until the next accepted build, Reset, or `~MainWindow`.

## 5. Rejected alternative

I considered having the worker call `QMetaObject::invokeMethod(mainWindow, ...)` when it finishes. The worker would then need a pointer to `MainWindow`, which could be mid-destruction at exit. Polling a future with a `QTimer` keeps the worker free of Qt, and the GUI decides when to take the result.
