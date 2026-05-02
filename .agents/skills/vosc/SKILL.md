```markdown
# vosc Development Patterns

> Auto-generated skill from repository analysis

## Overview

This skill teaches you how to contribute to the `vosc` codebase, a Python project (with significant C++ components) focused on shader manipulation, OSC command processing, and inspector tooling. You'll learn the project's coding conventions, how to develop new features or fix bugs using established workflows, and how to write and organize tests. This guide includes step-by-step instructions and command suggestions for common development tasks.

## Coding Conventions

### File Naming

- **PascalCase** is used for file names, e.g.:
  - `ShaderInspector.cpp`
  - `VariablePool.h`
  - `BaseTex.cpp`

### Import Style

- **Relative imports** are preferred.
  - Example (Python):
    ```python
    from .Shader import Shader
    ```
  - Example (C++):
    ```cpp
    #include "ShaderInspector.hpp"
    ```

### Export Style

- **Named exports** are used.
  - Example (Python):
    ```python
    def my_function():
        pass
    ```
  - Example (C++ header):
    ```cpp
    class ShaderInspector {
      // ...
    };
    ```

### Commit Patterns

- Commit messages are freeform, typically concise (~25 characters).
- No strict prefixing required.

---

## Workflows

### Shader Feature Development

**Trigger:** When you want to add or modify shader functionality (e.g., new shader classes, passes, fragments, or inspectors).  
**Command:** `/new-shader-feature`

1. Edit or create shader C++ files:
    - `src/shader/Shader.cpp`, `src/shader/Shader.h`
    - `src/shader/ShaderPass.cpp`, `src/shader/ShaderPass.h`
2. Edit or add shader fragment files:
    - `bin/data/shaders/**/*.frag`
3. Optionally update SuperCollider scripts for shader usage:
    - `SuperCollider/*.scd`
4. Edit or update inspector files for shaders/textures:
    - `src/inspector/ShaderInspector.cpp`, `src/inspector/TextureInspector.cpp`, etc.
5. Edit or update related texture or variable pool files if needed.

**Example:**
```cpp
// src/shader/Shader.cpp
#include "Shader.h"
// ...implement new shader logic
```
```glsl
// bin/data/shaders/new_effect.frag
void main() {
    // shader code here
}
```

---

### Documentation Migration or Expansion

**Trigger:** When you need to update, migrate, or generate documentation or reference material.  
**Command:** `/update-docs`

1. Edit or add markdown files in `docs/` (e.g., `docs/*.md`)
2. Edit `README.md`
3. Optionally add or update documentation scripts (`scripts/*.sh`, `scripts/*.py`)
4. Optionally update CI workflows for doc generation (`.github/workflows/*.yml`)

**Example:**
```markdown
# New Feature
Detailed documentation for the new feature.
```
```bash
# scripts/check_md_links.sh
./check_md_links.sh
```

---

### OSC Command Parser or Router Refactor

**Trigger:** When you want to improve or extend the OSC command handling pipeline.  
**Command:** `/refactor-osc-parser`

1. Edit or create OSC parser/router C++ files:
    - `src/osc/CommandParser.cpp`, `src/osc/CommandRouter.cpp`, etc.
2. Edit or create related header files:
    - `src/osc/*.hpp`
3. Edit or update main VOSC files to integrate changes:
    - `src/VOSC.cpp`, `src/VOSC.hpp`
4. Update or add tests for OSC commands:
    - `tests/osc/CommandParserTests.cpp`, `tests/osc/README.md`

**Example:**
```cpp
// src/osc/CommandParser.cpp
#include "CommandParser.hpp"
// ...refactored parsing logic
```
```cpp
// tests/osc/CommandParserTests.cpp
void testNewParserFeature() {
    // test implementation
}
```

---

### Inspector Feature Development or Bugfix

**Trigger:** When you want to add, improve, or fix inspector functionality.  
**Command:** `/update-inspector`

1. Edit or create inspector C++ files:
    - `src/inspector/*.cpp`, `src/inspector/*.hpp`
2. Optionally update related shader, texture, or variable files.
3. Optionally update SuperCollider scripts for inspector testing.

**Example:**
```cpp
// src/inspector/Inspector.cpp
#include "Inspector.hpp"
// ...new inspector logic
```

---

### Texture or Variable Pool Extension

**Trigger:** When you want to extend or fix texture/variable pool functionality.  
**Command:** `/update-pool`

1. Edit or create pool C++ files:
    - `src/tex/*.cpp`, `src/tex/*.h`
    - `src/var/*.cpp`, `src/var/*.h`
2. Edit or update main VOSC files or related shader/inspector files.
3. Optionally update SuperCollider scripts for pool usage.

**Example:**
```cpp
// src/var/VariablePool.cpp
#include "VariablePool.h"
// ...add new variable management
```

---

## Testing Patterns

- **Framework:** Unknown (not explicitly detected).
- **File Pattern:** Test files follow the pattern `*.test.ts` (for TypeScript) and `tests/osc/*.cpp` (for C++).
- **Organization:** Tests are grouped by feature or module, often mirroring the source directory structure.
- **Example:**
    ```cpp
    // tests/osc/CommandParserTests.cpp
    void testCommandParsing() {
        // test code here
    }
    ```

---

## Commands

| Command               | Purpose                                                |
|-----------------------|--------------------------------------------------------|
| /new-shader-feature   | Start a new shader-related feature or extension        |
| /update-docs          | Migrate, expand, or generate documentation             |
| /refactor-osc-parser  | Refactor or extend OSC command parser/router           |
| /update-inspector     | Develop, extend, or fix inspector features             |
| /update-pool          | Extend or fix texture/variable pool functionality      |
```
