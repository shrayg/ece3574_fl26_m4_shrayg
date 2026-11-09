# M4 Decision Record

## 1. Threads and ownership

The GUI thread runs `MainWindow` and all the Qt code. Each build runs on a `std::thread` owned by `BuildJob`, held in `MainWindow::buildJob_`. Remote add runs on a `std::thread` owned by `RemoteAddServer`, which `main()` declares after the window so it goes away first. Nothing is detached. `~BuildJob()` cancels and joins, and `RemoteAddServer::stop()` joins, then closes and unlinks the socket.

Mutable data two threads can touch: `BuildJob::cancelled_` is an atomic the GUI writes in `cancel()` and the worker reads in `sleepUnlessCancelled()`. The build's `std::promise`/`std::future` is set once by the worker and only read by the GUI after `isDone()`, and the standard library syncs that. `RemoteAddServer::stopping_` is an atomic the GUI writes in `stop()` and the IPC thread reads. `quitSignalled` in `main.cpp` is a `volatile sig_atomic_t` set by the signal handler. The IPC thread only reaches the GUI through `QMetaObject::invokeMethod` with `Qt::QueuedConnection`, and Qt locks that queue itself. The worker's `Workspace` is its own copy, and `listenFd_` is never touched while the IPC thread is running.

## 2. When a finished build counts

`workspaceVersion_` goes up in `invalidate_corpus()` (every add, local or remote) and in `reset_application()`. Each `BuildJob` remembers the version it started with. `finishBuild()` keeps the core only if it isn't null (null means cancelled) and the versions still match, and that's the only place the state ever becomes `Current`. Build stays disabled while any job thread is alive, cancelled ones included. Those end within about 10 ms because cancel cuts the delay short.

## 3. Changes since the checkpoint

The core now gets freed as soon as the workspace changes, so `processingCore_` is non-null exactly when the corpus is `Current`. I added `set_core_factory()` so tests can force a build to fail. A failure from an already stale build gets ignored.

## 4. CR-1 impact

New: `RemoteAddServer`, `remote_protocol` (shared with `aiws_send`), and `tools/aiws_send.cpp`. Changed: `main()` (`--ipc-socket`, starting the server, quitting on SIGTERM/SIGINT) and a new slot, `remote_add_requested()`, that just calls `add_document_file()`. None of the build code changed. A remote add goes through the same `add_document_file()` and `invalidate_corpus()` as a local one, so it already bumps the version and cancels a running build.

The IPC thread checks the file itself (a regular file it can open), replies, then queues the add, so it never waits on the GUI. A duplicate still gets exit 0, and the status bar says it's already there.

Message format, one request per connection. Request: byte 0 = 0x41, bytes 1 to 4 = length N as a big-endian uint32 (1 to 4096), then N bytes of absolute UTF-8 path. Reply: byte 0 = 0x00 (accepted) or 0x01 (rejected), bytes 1 to 4 = length M, then M bytes of UTF-8 reason. `aiws_send` makes relative paths absolute before sending, and `readExact()` loops until every byte shows up, so split writes work.

## 5. Fault switches

`AIWS_FAULT_ACCEPT_STALE` is in `MainWindow::finishBuild()` and forces `isStale` to false. `staleBuildIsDroppedAfterAdd` and `staleBuildIsDroppedAfterResetAndReadd` catch it. `AIWS_FAULT_LOSE_FAILURE` returns early from the catch block in `finishBuild()`, so the app never leaves `Building`. `failedBuildIsReportedAndAppStaysUsable` catches it.

## Pair programming log

Partner: Subhan Hassan

Session 1: Nov 8, 9:30 pm to 10:15 pm, on Subhan's code. We walked through his background build worker and `MainWindow::build_finished()` (the workspace version check), then `RemoteAddServer::handle_client()` and the message format. Session 1 roles: he drove 9:30 to 9:52, I drove 9:52 to 10:15.

Session 2: Nov 8, 10:15 pm to 11:00 pm, on my code. We reviewed `BuildJob`, `runBuild()`, `MainWindow::finishBuild()` and the fault switches, and why `RemoteAddServer` is declared after `MainWindow` in `main.cpp`. Session 2 roles: I drove 10:15 to 10:37, he drove 10:37 to 11:00.

Something I contributed as navigator: I asked what happens if an older build finishes after the workspace has already moved on. We walked through `finishBuild()` comparing `workspaceVersion_` to the job's saved version, and ran `staleBuildIsDroppedAfterAdd` and `staleBuildIsDroppedAfterResetAndReadd` to confirm a stale core never becomes `Current`.

Something my partner contributed as navigator: Subhan asked whether `AIWS_FAULT_ACCEPT_STALE` is really caught, since a cancelled stale job can return nullptr and get dropped either way. I showed the stale tests run with no delay so the worker always finishes a real core, and we built with the macro to confirm those tests fail.

What worked best for us: the driver explaining each part out loud while the navigator asked what-if questions about timing, then checking the answer by running a test.
