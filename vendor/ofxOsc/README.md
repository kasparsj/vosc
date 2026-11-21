# Repository-controlled ofxOsc receiver

The receiver header/source are derived from the installed openFrameworks 0.11.1
ofxOsc addon, retaining its copyright notices. All message decoding remains
unchanged. The supported macOS builder overlays both files for every consumer.
Do not mix this header with the installed receiver object, or vice versa.

Original source SHA-256 (before this repair):

- `ofxOscReceiver.h`: `fe010d0369bdb95330a84e1ac4b96485a6d5a4171f86060db43cc32833581931`
- `ofxOscReceiver.cpp`: `73e69079b15b5948bc18b601d1d9a1ba5a56678c969badaf8279932be9516c6c`

Lifecycle calls are serialized on the owning thread. `stop()` requests shutdown,
joins the worker and only then destroys its socket/multiplexer. A 10 ms timer
calls oscpack `Break()` on the receiver thread, avoiding its cross-thread volatile
flag race and its Run-entry reset. This costs roughly 100 idle wakeups/second.
Stopping from a receiver callback is unsupported. Subclasses must stop before
destroying any state accessed by their callback.

A failed worker marks itself no longer listening. Restart joins that worker and
creates fresh state. The message queue and configured port retain their existing
lifetime across stop/restart. Test hooks exist only under
`VOSC_RECEIVER_LIFECYCLE_TESTING`, never in application builds.
