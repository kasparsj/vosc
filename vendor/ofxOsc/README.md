# macOS ofxOsc receiver repair

The receiver source and header derive from the ofxOsc addon in the pinned
openFrameworks 0.12.1 macOS release and retain the [MIT license](LICENSE).
The macOS builder verifies both upstream file hashes before selecting these
two files through the private compiler overlay.

The receiver keeps the bundled oscpack asynchronous wakeup pipe and atomic
break flag. The repair releases the bound socket if launching the receive
thread fails. It also marks an exited receive worker inactive so the next
start can join it, release its socket, and bind again.

Call setup, start, stop, and copy from one owner thread. Stop joins before
destroying the socket and must be called outside receive callbacks. Subclasses
stop reception before destroying state the callback can access.
`VOSC_RECEIVER_LIFECYCLE_TESTING` enables failure injection only in the
standalone test harness. See the [shutdown lifecycle](../../docs/shutdown-lifecycle.md)
for the application teardown order and test coverage.
