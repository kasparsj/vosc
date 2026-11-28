@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    let fixed_time = 0.25;
    if uv.y < 0.5 {
        if uv.x < 0.5 {
            return vec4f(1, fixed_time, 0, 1);
        }
        return vec4f(0, 128.0 / 255.0, 1, 1);
    }
    if uv.x < 0.5 {
        return vec4f(0.75, 0, 128.0 / 255.0, 1);
    }
    return vec4f(0, 1, fixed_time, 1);
}
