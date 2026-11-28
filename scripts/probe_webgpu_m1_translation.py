#!/usr/bin/env python3
"""Compile two known fragment fixtures offline through glslang and Tint."""

import argparse
import base64
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def probe(name, original, args):
    output = args.output
    direct = output / f"{name}.gl330.frag"
    direct.write_text(original)
    spirv = output / f"{name}.spv"
    compile_glsl = [args.glslang, "-V", "-S", "frag", "-o", spirv]
    raw = subprocess.run([*compile_glsl, direct], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / f"{name}.raw.log").write_text(raw.stdout)
    print(f"{name}: unmodified compile exit {raw.returncode}")
    for line in raw.stdout.splitlines():
        if "ERROR:" in line:
            print(line)

    # These edits only cover the two fixtures. Preserve each uniform's
    # initializer for the host and adapt the fragment's coordinate origin.
    uniforms = re.compile(r"^uniform (\w+) (\w+)(?: = ([^;]+))?;\s*$", flags=re.M)
    fields = uniforms.findall(original)
    if not any(name == "resolution" for _, name, _ in fields):
        raise ValueError("Fixture has no resolution uniform")
    declaration = ["#version 450", "layout(std140, set = 0, binding = 0) uniform FrameInputs {"]
    declaration.extend(f"    {kind} {name};" for kind, name, _ in fields)
    declaration.append("};")
    adapted = uniforms.sub("", original).replace("#version 330", "\n".join(declaration))
    adapted = re.sub(r"^out ", "layout(location = 0) out ", adapted, flags=re.M)
    adapted = adapted.replace("gl_FragCoord.xy", "vec2(gl_FragCoord.x, resolution.y - gl_FragCoord.y)")
    vulkan_glsl = output / f"{name}.vulkan.frag"
    vulkan_glsl.write_text(adapted)

    subprocess.run([*compile_glsl, vulkan_glsl], check=True)
    wgsl = output / f"{name}.wgsl"
    subprocess.run([args.tint, "--format", "wgsl", "-o", wgsl, spirv], check=True)
    subprocess.run([args.tint, "--format", "wgsl", "--validate", "true",
                    "-o", output / f"{name}.checked.wgsl", wgsl], check=True)
    defaults = {uniform: default for _, uniform, default in fields if default}
    print(f"{name}: {wgsl.stat().st_size} bytes of validated WGSL, host defaults {defaults}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslang", type=Path, required=True)
    parser.add_argument("--tint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.glslang = args.glslang.absolute()
    args.tint = args.tint.absolute()
    args.output.mkdir(parents=True)
    fixture = json.loads((ROOT / "desktop/authoring/tests/fixtures/hydra/legacy-m4.vosc-draft").read_text())
    accepted = base64.b64decode(fixture["accepted"]["project"]["source"]["base64"]).decode()
    probe("glow-ribbons", (ROOT / "examples/shaders/assets/glow-ribbons.frag").read_text(), args)
    probe("hydra-m4", accepted, args)


if __name__ == "__main__":
    main()
