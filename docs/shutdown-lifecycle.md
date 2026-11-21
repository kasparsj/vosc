# OSC receiver shutdown

The macOS builders compile vendor/ofxOsc/ofxOscReceiver.{h,cpp} through a mandatory compiler overlay. Shared openFrameworks sources remain unchanged. The standard macOS build applies the repair. The diagnostic builders below use openFrameworks 0.11.1. Run them from the [standard installation layout](../README.md#building-with-xcode-apple-silicon) or set `--of-root` to a 0.11.1 SDK.

Lifecycle setup/start/stop/copy calls belong to one owner thread; never call stop from a receive callback. The worker owns a socket and multiplexer. A 10 ms timer reads an atomic stop request and calls Break on the worker thread. stop joins before destroying the receive state or message channel. This avoids oscpack's cross-thread volatile flag and startup reset race, at a cost of approximately 100 idle wakeups per second. The timer interval is not a hard real-time guarantee. Failed workers become inactive; restart first joins and discards the previous state.

InputSystem::shutdown() stops reception before VOSC control/render/resource/tidal teardown. The receiver destructor remains a final join boundary. Existing queued-message behavior is retained.

## Validation

Create a fresh private directory outside the checkout and shared OF installation. Build the private core with build_macos_recovery_core.py --of-root OF_ROOT --output OUTPUT/core --sanitizers address --sanitizers undefined. Build the app with build_macos_legacy.py --of-root OF_ROOT --output OUTPUT/app --core-library OUTPUT/core/products/openFrameworksDebug.a --sanitizers address --sanitizers undefined --recovery-test-hooks.

Run run_receiver_lifecycle_tests.py --of-root OF_ROOT --build-output OUTPUT/app --output OUTPUT/receiver --sanitizers address,undefined, then separately with --output OUTPUT/receiver-tsan --sanitizers thread --core-library OF_ROOT/libs/openFrameworksCompiled/lib/osx/openFrameworksDebug.a. The TSan harness uses the ordinary core read-only to avoid linking ASan and TSan together; core and third-party code in that run is not TSan-instrumented. The native harness uses real OF channels and POSIX oscpack sockets for immediate/pre-Run stop, idle wakeup, callback completion, repeated stop/restart and failure recovery. Deadlines report failure rather than successful shutdown.

Run test_shutdown_runtime.py --build-output OUTPUT/app --output OUTPUT/runtime. It uses only its own executable, copied writable data, unique OSC ports and Unix sockets. It verifies ordinary OSC onset changes, graceful exit through the existing recovery-testing seam, input-stop before downstream teardown, zero exit, released port, removed socket and subsequent launch. Idle and active traffic are tested separately. Test hooks are absent in ordinary builds.

Retain source/binary/core hashes, compiler identity, resolved settings, complete sanitizer logs and PID-specific cleanup receipts. Run test_sanitizer_runtime.py negative controls for address and undefined sanitizers. Verify shared input hashes and original checkout status/diff before and after. Third-party prebuilt archives remain an instrumentation limit. Clean runs do not establish the cause of the historical truncated sanitizer report.


## Reviewed local acceptance — 2026-11-20

The corrected builder fingerprints replacement contents into the overlay filename. An independent tiny Xcode reproduction confirmed that editing a replacement recompiles the object and changes the executable; retaining the old fixed overlay filename reused the stale object. The content-specific compiler command is required for trustworthy incremental build receipts.

Final private ASan+UBSan core/application build passed. Application SHA-256: `a7b69fb95eb1301cfd84e97037fa532cb23ef502d6dfc6e067c7d771f59c7663`. Eight real receiver lifecycle cases passed with ASan+UBSan and separately TSan; actual application idle, active-OSC and subsequent-launch shutdown passed. All 75 Python tests passed with Python 3.12 and explicit `VISUALS_QUARK_PATH`; build integration accounts for 17 of these tests. Existing OSC checks passed 41 cases; local control passed 13 cases/67 assertions; performance and protected-trial checks passed. Generated reference and Markdown link checks passed.
