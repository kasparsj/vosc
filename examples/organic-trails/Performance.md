# Particle shader performance fix and reflection

## Result

The expensive shader was replaced in the existing running renderer through MCP.
There was no renderer restart, resolution reduction, particle-count reduction or
frame-cap change. All four default controls were restored afterward.

| Same-session observation | System GPU mean | FPS mean |
| --- | ---: | ---: |
| Original shader, four samples | 99.75% | 56.71 |
| Four segments, all particles per pixel; first trial including startup | 54.10% | 58.58 |
| Whole-trail-box culling, second trial | 48.40% | 60.00 |
| Spatial grouping, four segments | 20.42% | 60.00 |
| Final spatial grouping, eight segments, first normal window | 28.92% | 59.98 |
| Final, maximum trails / faster motion / brighter | 22.38% | 59.87 |
| Final, restored defaults, second normal window | 22.40% | 59.88 |

These are short system-wide GPU observations while other apps were running, not
isolated VOSC GPU timings or a precise shader-speedup ratio. The two final normal
windows contain 12 and 10 one-second samples; the original was already warmed and
sampled four times. The final first window followed four warmup samples. Captures
were outside measured windows. The application stayed at 1024x768 with 26 particles.
The final default windows peaked at 29% and 27% GPU. Desktop responsiveness has
not been measured directly; the large observed GPU reduction establishes headroom.

## What changed

The original fragment shader checked 26 particles times 32 trail points at every
pixel: nominally 832 trail iterations/pixel and 654 million/frame. A finite loop
bound did not make this an affordable full-screen workload.

The replacement distributes 26 particles across 30 cells and checks only a 3x3
neighborhood, rejecting pixels outside each particle footprint. Eight tapered line
segments form continuous trails, with segment bounds checked before expensive
coverage math. Maximum candidate segment iterations are 9x8=72 per pixel before
culling. Four segments were cheaper but showed corners at maximum trail length;
eight were retained for quality. The paths are redistributed for locality; this
is not pixel-identical to the old motion. No persistent feedback is implied.

The same four controls and globalColor remain active. The example bounds
motionSpeed to [-2,2], trailLength to [0,2.5], brightness to [0,4], and each coolTint
component to [0,2]. These are shader-specific clamps, not changes to the engine API.
Native previews verified the cyan/amber organic look, long trails, paused heads,
and restoration. Two paused captures at different frames were byte-identical.
