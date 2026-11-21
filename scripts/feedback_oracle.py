"""Independent bottom-left spatial recurrence oracle; no product shader helpers."""
from __future__ import annotations

WIDTH, HEIGHT = 800, 600


def spatial_frames(steps=(1, 2, 4, 8), *, source_dx=-3, source_dy=-2):
    previous = bytearray([255]) * (WIDTH * HEIGHT * 4)
    for step in range(1, max(steps) + 1):
        current = bytearray(len(previous))
        for y in range(HEIGHT):
            for x in range(WIDTH):
                index = (y * WIDTH + x) * 4
                rx = min(WIDTH - 1, max(0, x + source_dx))
                gy = min(HEIGHT - 1, max(0, y + source_dy))
                rseed = int((x // 7 + 2 * (y // 11)) % 5 == 0)
                gseed = int((3 * (x // 5) + y // 9) % 7 < 2)
                current[index] = round(.5 * previous[(y * WIDTH + rx) * 4] + .25 * 255 * rseed)
                current[index + 1] = round(.5 * previous[(gy * WIDTH + x) * 4 + 1] + .25 * 255 * gseed)
                current[index + 2] = round(255 * (.125 + .25 * (x % 17) / 16 + .125 * (y % 13) / 12))
                current[index + 3] = 255
        previous = current
        if step in steps:
            yield step, bytes(current)


def compare_png_pixels(top_down_rgba, expected):
    if len(top_down_rgba) != WIDTH * HEIGHT * 4:
        raise ValueError('spatial oracle requires 800x600 RGBA pixels')
    bad = 0
    maximum = 0
    for row in range(HEIGHT):
        for x in range(WIDTH):
            actual_index=(row*WIDTH+x)*4
            expected_index=((HEIGHT-1-row)*WIDTH+x)*4
            delta=max(abs(top_down_rgba[actual_index+c]-expected[expected_index+c]) for c in range(4))
            maximum=max(maximum,delta)
            bad += delta > 2
    return {'bad_pixels':bad,'maximum_error':maximum,'pixels':WIDTH*HEIGHT}
