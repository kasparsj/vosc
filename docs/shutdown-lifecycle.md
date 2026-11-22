# OSC receiver shutdown

The macOS build uses the ofxOsc and oscpack sources bundled with the pinned
openFrameworks 0.12.1 release. A [private compiler overlay](../vendor/ofxOsc/README.md)
supplies a repaired receiver header and source. The installed addon is
read-only during the build.

Bundled oscpack keeps an atomic break flag and wakes an idle receiver through
a pipe. A stop request issued before the worker enters the receive loop still
takes effect. Stop joins the worker before releasing the socket.
`InputSystem::shutdown()` stops reception before VOSC tears down control,
rendering, resources, and TidalCycles state. The receiver destructor is a
final join boundary.

## Qualification

The [maintenance branch lifecycle procedure](https://github.com/kasparsj/vosc/blob/of-0.11.1/docs/shutdown-lifecycle.md#validation)
provides the receiver and application case matrix. Its build commands target
the 0.11.1 maintenance SDK. For 0.12.1 comparisons, use the current macOS
builders and private outputs with the matching receiver and oscpack sources.

The upstream 0.12.1 receiver passed immediate stop, idle stop, active callback
completion, repeated stop/start, bind recovery, and graceful application exit
with port release. Injected thread-launch failure escaped setup, and injected
receive-loop failure left the worker marked as listening. The repair retains
the upstream stop mechanism and restores launch and receive-loop recovery.

Receiver tests use actual loopback UDP, bounded deadlines, and separate
ASan+UBSan and TSan runs. App tests use `build_macos_authoring.py
--recovery-test-hooks` and private sockets and data. They verify OSC delivery,
input-first shutdown, exit status, UDP rebind, Unix socket removal, and
relaunch on the same port under idle and active traffic. Recovery hooks are
excluded from ordinary builds. Instrumentation of the app or receiver harness
does not instrument prebuilt third-party libraries.

The receiver passed ASan+UBSan and TSan, and the app passed the shutdown
checks under ASan. Whole-app UBSan flagged the same openFrameworks core
findings in the baseline and migration builds, in integer color construction
and programmable renderer drawing. Whole-app UBSan qualification remains open.
