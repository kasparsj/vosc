struct Renderer {
    resolution: vec2f,
    time: f32,
}

@group(0) @binding(0) var<uniform> renderer: Renderer;

@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    // Verify that the framebuffer dimensions match the normalized UVs.
    let screen_position = floor(uv * renderer.resolution) + vec2f(0.5);
    let pixel_center = vec2f(0.5) / renderer.resolution;
    let at_pixel_center = all(abs(screen_position / renderer.resolution - uv) < pixel_center * 0.01);
    let quadrant = select(vec2f(0.0), vec2f(1.0), uv >= vec2f(0.5));
    return vec4f(quadrant.x, quadrant.y, select(0.0, 1.0, at_pixel_center), clamp(renderer.time, 0.0, 1.0));
}
