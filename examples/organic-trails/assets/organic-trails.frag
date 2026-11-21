#version 330
uniform float time;
uniform vec2 resolution;
uniform vec4 globalColor;
uniform float motionSpeed;
uniform float trailLength;
uniform float brightness;
uniform vec3 coolTint;
out vec4 fragColor;

float hash(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

// Local motion stays within (+/-.42, +/-.38) of its spatial cell.
vec2 path(float t, float seed) {
    return vec2(.30 * sin(t * .31 + seed) + .12 * sin(t * .63 + seed * 2.1),
                .26 * cos(t * .27 + seed * 1.7) + .12 * sin(t * .51 + seed));
}

void main() {
    vec2 size = max(resolution, vec2(1.0));
    vec2 grid = vec2(6.0, 5.0);
    vec2 cellSize = vec2(size.x / size.y, 1.0) / grid;
    vec2 p = (gl_FragCoord.xy - .5 * size) / size.y;
    vec2 pixelCell = floor(gl_FragCoord.xy / size * grid);
    float clock = time * clamp(motionSpeed, -2.0, 2.0);
    float duration = 3.0 * clamp(trailLength, 0.0, 2.5);
    vec3 color = vec3(.0015, .003, .009);

    // There are 26 particles in 30 cells. Only the local 3x3 neighborhood can
    // overlap this pixel, including the finite head-glow and trail footprints.
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 cell = pixelCell + vec2(float(x), float(y));
            if (any(lessThan(cell, vec2(0.0))) || any(greaterThanEqual(cell, grid))) continue;
            float index = cell.y * 6.0 + cell.x;
            if (index == 3.0 || index == 12.0 || index == 20.0 || index == 28.0) continue;

            float seed = hash(index + 3.0) * 6.28318;
            float depth = .45 + .75 * hash(index + 19.0);
            float radius = (.015 + .0175 * hash(index + 70.0)) * min(cellSize.x, cellSize.y) * depth;
            vec2 center = (cell + .5 - grid * .5) * cellSize;
            center += (vec2(hash(index + 31.0), hash(index + 47.0)) - .5) * .06 * cellSize;
            if (any(greaterThan(abs(p - center), vec2(.42, .38) * cellSize + vec2(12.0 * radius)))) continue;

            vec3 tint = mix(clamp(coolTint, vec3(0.0), vec3(2.0)),
                            vec3(1.0, .46, .17), step(.72, hash(index + 9.0)));
            float phase = clock * 1.1 + seed * 5.0;
            vec2 head = center + path(phase, seed) * cellSize;
            vec2 delta = p - head;
            float distanceSquared = dot(delta, delta);
            float radiusSquared = radius * radius;
            float glow = 0.0;
            if (distanceSquared < 144.0 * radiusSquared) {
                glow = 1.5 * exp(-distanceSquared / radiusSquared)
                     + .035 * exp(-distanceSquared / (36.0 * radiusSquared));
            }

            float trail = 0.0;
            vec2 start = head;
            for (int segment = 0; segment < 8; ++segment) {
                float ageStart = float(segment) * .125;
                vec2 end = center + path(phase - (ageStart + .125) * duration, seed) * cellSize;
                vec2 extent = abs(end - start) * .5 + vec2(4.0 * radius);
                if (all(lessThanEqual(abs(p - (start + end) * .5), extent))) {
                    vec2 edge = end - start;
                    float along = clamp(dot(p - start, edge) / max(dot(edge, edge), 1e-10), 0.0, 1.0);
                    float age = ageStart + along * .125;
                    vec2 offset = p - (start + along * edge);
                    float width = radius * (1.0 - .72 * age);
                    float fade = (1.0 - age) * (1.0 - age);
                    trail = max(trail, exp(-dot(offset, offset) / (2.0 * width * width)) * fade);
                }
                start = end;
            }
            color += tint * (glow + .5 * trail) * depth;
        }
    }
    color = vec3(1.0) - exp(-color * clamp(brightness, 0.0, 4.0));
    fragColor = vec4(color, 1.0) * globalColor;
}
