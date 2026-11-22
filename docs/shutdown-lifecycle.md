# OSC receiver shutdown

The macOS builders compile vendor/ofxOsc/ofxOscReceiver.{h,cpp} through a mandatory compiler overlay. Shared openFrameworks sources remain unchanged. The standard macOS build applies the repair.

Lifecycle setup/start/stop/copy calls belong to one owner thread; never call stop from a receive callback. The worker owns a socket and multiplexer. A 10 ms timer reads an atomic stop request and calls Break on the worker thread. stop joins before destroying the receive state or message channel. This avoids oscpack's cross-thread volatile flag and startup reset race, at a cost of approximately 100 idle wakeups per second. The timer interval is not a hard real-time guarantee. Failed workers become inactive; restart first joins and discards the previous state.

InputSystem::shutdown() stops reception before VOSC control/render/resource/tidal teardown. The receiver destructor remains a final join boundary. Existing queued-message behavior is retained.

## Validation

The [instrumented shutdown procedure](https://github.com/kasparsj/vosc/blob/of-0.11.1/docs/shutdown-lifecycle.md#validation)
and its receipt-based diagnostics live on the openFrameworks 0.11.1 maintenance branch.
