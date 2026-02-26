# OSC Test Harness

This folder contains a lightweight standalone harness for OSC parser/router and Args edge-case tests.

Covered executables:
- `CommandParserTests.cpp`
- `CommandDispatchTests.cpp`
- `ArgsTests.cpp`

The harness compiles against test stubs in `tests/stubs/` and does not require building the full app.

Run locally:

```bash
./scripts/run_osc_tests.sh
```

Run with sanitizer instrumentation:

```bash
SANITIZER=address ./scripts/run_osc_tests.sh
SANITIZER=undefined ./scripts/run_osc_tests.sh
```
