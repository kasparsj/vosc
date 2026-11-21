# VOSC commit verification

- Review the staged diff with `git diff --cached --check` and `git diff --cached` before each commit. Reuse passing results when the tested code, inputs, and environment still match.
- Run the smallest focused check the change needs. Use a simple scene as a smoke check when the change affects scene loading, rendering, or the scene itself. Choose a scene that exercises the changed behavior.
- Run broader suites when the change affects multiple test surfaces, a regression warrants them, or the user asks for them.
