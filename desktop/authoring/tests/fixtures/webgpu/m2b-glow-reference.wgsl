struct Frame {
    resolution: vec2f,
    time: f32,
}

@group(0) @binding(0) var<uniform> frame: Frame;

@fragment fn fs_main(@builtin(position) pixel: vec4f) -> @location(0) vec4f {
    let uv = vec2f(pixel.x, frame.resolution.y - pixel.y) / frame.resolution;
    let wave = 0.24 * sin(9.0 * uv.y + frame.time * 1.5);
    let ribbon = abs(sin(13.0 * (uv.x + wave)));
    let glow = 1.0;
    let width = mix(0.07, 0.58, glow);
    let intensity = 0.10 + 0.90 * (1.0 - smoothstep(width, width + 0.14, ribbon));
    return vec4f(vec3f(0.16, 0.75, 1.0) * intensity, 1.0);
}
