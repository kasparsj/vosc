# Actual OF color constructor regression

`python3 scripts/test_ofcolor_constructors.py --output /tmp/vosc-color-check`
compiles the installed OF 0.11.1 header and source through the generated VFS
overlay and calls the actual constructors with UBSAN enabled.

Use `--expect-original-failure` and a separate output directory to reproduce the
original static-initialization diagnostic without applying the overlay. This
mode succeeds only when the original process fails with that known diagnostic.
