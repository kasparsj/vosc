# OSC Parser Test Scaffold

This folder contains lightweight parser tests for the new typed OSC command layer.

Current status:
- `CommandParserTests.cpp` is a simple assert-based executable scaffold.
- It is intentionally not wired into the openFrameworks build yet.

Suggested compile pattern (adjust include/library paths to local setup):

```bash
c++ -std=c++11 -Isrc -I$OF_ROOT/libs/openFrameworks -I$OF_ROOT/addons/ofxOsc/src \
  tests/osc/CommandParserTests.cpp \
  src/osc/ArgReader.cpp src/osc/CommandParser.cpp src/osc/CommandRouter.cpp \
  -o tests/osc/CommandParserTests
```

