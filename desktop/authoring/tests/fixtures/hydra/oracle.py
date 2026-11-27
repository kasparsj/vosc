"""Independent pixel oracle for the M4 acceptance fixture. Requires Pillow."""
import argparse
import json
from math import cos, radians, sin
from pathlib import Path

from PIL import Image

WIDTH = 640
HEIGHT = 480


def rotate(x, y, angle):
    u, v = x - 0.5, y - 0.5
    return 0.5 + cos(angle) * u + sin(angle) * v, 0.5 - sin(angle) * u + cos(angle) * v


def fixture(mix, animated, outer_angle, mutation=None):
    time = 0.25 if animated and mutation != "zero-time" else 0.0
    direction = -1 if mutation == "rotation-sign" else 1
    if mutation == "stuck-mix":
        mix = 0.15

    def branch(x, y, frequency, sync, offset, angle, speed):
        for degrees in (9, angle):
            x, y = rotate(x, y, direction * (radians(degrees) + speed * time))
        phase = (x + sync * time) * frequency
        return [0.5 + 0.5 * sin(phase + channel * offset) for channel in (-1, 0, 1)]

    pixels = []
    for row in range(HEIGHT):
        v = (HEIGHT - row - 0.5) / HEIGHT
        if mutation == "inverted-y":
            v = 1 - v
        for column in range(WIDTH):
            x, y = rotate((column + 0.5) / WIDTH, v, direction * radians(outer_angle))
            left = branch(x, y, 11, 0.18, 0.7, 23, 0.15)
            right = branch(x, y, 7, -0.12, 1.1, -31, -0.2)
            pixels.append(tuple(round(255 * ((1 - mix) * a + mix * b))
                                for a, b in zip(left, right)) + (255,))
    image = Image.new("RGBA", (WIDTH, HEIGHT))
    image.putdata(pixels)
    return image


def compare(expected, actual):
    if actual.size != (WIDTH, HEIGHT) or actual.mode not in ("RGBA", "RGB"):
        raise ValueError(f"Unexpected capture format: {actual.mode} {actual.size}")
    observed = actual.convert("RGBA").load()
    reference = expected.load()
    maximum = [0, 0, 0, 0]
    first = None
    for row in range(1, HEIGHT - 1):
        for column in range(1, WIDTH - 1):
            error = [abs(a - b) for a, b in zip(observed[column, row], reference[column, row])]
            maximum = [max(a, b) for a, b in zip(maximum, error)]
            if first is None and (max(error[:3]) > 2 or error[3]):
                first = [column, row]
    return {"dimensions": [WIDTH, HEIGHT], "captureMode": actual.mode,
            "maxAbsoluteRGBA": maximum, "firstOverThreshold": first, "pass": first is None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path)
    parser.add_argument("--actual", type=Path)
    parser.add_argument("--mix", type=float, required=True, choices=(0.15, 0.5, 0.85))
    parser.add_argument("--animated", action="store_true")
    parser.add_argument("--outer-angle", type=float, default=17)
    parser.add_argument("--mutation-controls", action="store_true")
    args = parser.parse_args()
    expected = fixture(args.mix, args.animated, args.outer_angle)
    expected.save(args.expected)
    result = {}
    if args.mutation_controls:
        if not args.animated or args.mix != 0.85:
            parser.error("Mutation controls use the animated fixture at mix 0.85")
        for mutation in ("inverted-y", "rotation-sign", "stuck-mix", "zero-time"):
            changed = fixture(args.mix, args.animated, args.outer_angle, mutation)
            changed.save(args.expected.with_name(f"{mutation}.png"))
            result[mutation] = compare(expected, changed)
    if args.actual:
        with Image.open(args.actual) as actual:
            result["capture"] = compare(expected, actual)
    print(json.dumps(result, indent=2))
    return int(any(value["pass"] == (key != "capture") for key, value in result.items()))


if __name__ == "__main__":
    raise SystemExit(main())
