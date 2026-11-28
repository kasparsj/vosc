struct Frame {
    resolution: vec2f,
    time: f32,
}

@group(0) @binding(0) var<uniform> frame: Frame;

fn rotate(st: vec2f, angle: f32) -> vec2f {
    let center = st - vec2f(0.5);
    let sine = sin(angle);
    let cosine = cos(angle);
    return vec2f(cosine * center.x + sine * center.y, cosine * center.y - sine * center.x) + vec2f(0.5);
}

fn wave(st: vec2f, frequency: f32, sync: f32, offset: f32, angle: f32, speed: f32) -> vec3f {
    let second = rotate(st, radians(9.0) + speed * frame.time);
    let first = rotate(second, radians(angle) + speed * frame.time);
    let phase = (first.x + sync * frame.time) * frequency;
    return vec3f(0.5) + 0.5 * sin(vec3f(phase) + vec3f(-offset, 0.0, offset));
}

@fragment fn fs_main(@builtin(position) pixel: vec4f) -> @location(0) vec4f {
    let st = vec2f(pixel.x, frame.resolution.y - pixel.y) / frame.resolution;
    let coordinates = rotate(st, radians(17.0));
    let left = wave(coordinates, 11.0, 0.18, 0.7, 23.0, 0.15);
    let right = wave(coordinates, 7.0, -0.12, 1.1, -31.0, -0.2);
    let blend = 0.5;
    return vec4f(mix(left, right, vec3f(blend)), 1.0);
}
