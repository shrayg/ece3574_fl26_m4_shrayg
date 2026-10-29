# M4 Design Note

Due Wed Oct 28, 11:59 pm. See specification Section 7.

## 1. Components and threads

Almost everything stays on the GUI thread. `MainWindow` keeps all its M3 slots, and I'm adding `startBuild()`, `pollBuild()`, and `finishBuild()` to it. It also gets a `QTimer pollTimer_` that only runs while a build is going. I'm adding a plain C++ class called `BuildJob` that owns the worker thread. It has `start()`, `isDone()`, `takeResult()`, `cancel()`, and a destructor, and all of those get called from the GUI thread too.

The worker `std::thread` runs only `runBuild()`. It gets its own copy of the Workspace, the delay, and the job's `std::atomic<bool> cancelled_`. It sleeps the delay for each document in short chunks so it can notice a cancel, then calls `rebuild()` on a fresh `ProcessingCore`. The finished core (or an exception) goes back through a `std::promise`. The worker never touches Qt or `MainWindow`. The only things both threads share are the atomic flag and the promise/future, and both of those are made to be used across threads.

## 2. Corpus states

The states are `NotBuilt`, `Building`, `Current`, `OutOfDate`, and `Failed`. On top of that, `MainWindow` has `workspaceVersion_`, which goes up every time a document is added or Reset is clicked.

Clicking Build starts a job, saves the current version in it, and moves to `Building`. When the job comes back, it goes to `Current` with the chunk count if the versions still match, or to `Failed` with the error message if it threw. Adding a document cancels any running job and moves to `OutOfDate`. Reset cancels any job and goes back to `NotBuilt`.

Search, Inspect, and Context only work in `Current`. Build is only enabled when there are documents and no job thread is alive, so there's never more than one worker.

## 3. Threatening sequences

The big R2 problem is a build finishing after the workspace has already changed. Comparing versions in `finishBuild()` catches that. I went with a counter instead of the document count because Reset followed by an add can land on the same count with different documents. Double-clicking Build isn't a problem, because the button is off while a job is alive.

For R3, `runBuild()` catches anything the build throws and passes it along with `set_exception`. `future.get()` rethrows it in `finishBuild()`, which shows "Build failed: ..." and moves to `Failed`, and the user can just build again.

For R1 and R4, the GUI only ever calls `wait_for(0)`, so it never waits on the worker. If the user exits mid-build, `~BuildJob()` sets the cancel flag and joins. The worker checks that flag every 10 ms, so the window doesn't hang on close. Every job is joined in `takeResult()` before another one can start, so threads don't pile up. A build finishing while the Exit dialog is open is fine too, because the timer still fires during the dialog and the result just gets applied normally. Changing the delay mid-build only affects the next build, since `start()` copies it.

Walkthrough A, Reset then add during a build. The build starts at version 4. Reset bumps the version to 5, cancels the job, and goes to `NotBuilt`. Adding a file bumps it to 6 and sets `OutOfDate`. The worker sees the flag and stops early. On the next tick, `pollBuild()` sees the future is ready and calls `finishBuild()`, which sees 4 isn't 6, drops the core, and leaves the state at `OutOfDate` with Build enabled again.

Walkthrough B, Exit during a long build. The user clicks Exit and says yes, so `QApplication::quit()` runs and `main()` returns. The window on the stack gets destroyed, which destroys `buildJob_`. `~BuildJob()` sets `cancelled_`, the worker notices within about 10 ms and returns, `join()` finishes, and the app closes without a detached thread.

## 4. Ownership of one built corpus

`runBuild()` creates the core with `std::make_unique<ProcessingCore>()` and builds it. `set_value(std::move(core))` hands it to the future. On the GUI thread, `takeResult()` joins the thread and moves the core out with `future.get()`. If `finishBuild()` accepts it, it moves into `processingCore_`, and the old core gets freed right there. If it's rejected, it's freed when `finishBuild()` returns. An accepted core lasts until the next good build, a Reset, or the window closing.

## 5. Rejected alternative

At first I was going to have the worker call `QMetaObject::invokeMethod(mainWindow, ...)` when it was done. The problem is the worker would need a pointer to `MainWindow`, and at exit that window could already be getting destroyed. Polling the future with a `QTimer` keeps the worker out of Qt completely, and the GUI decides when it picks up the result.
