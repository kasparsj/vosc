#version 330
uniform float time;
uniform vec2 resolution;
out vec4 fragColor;

void main() {
    vec2 p = (gl_FragCoord.xy - 0.5 * resolution) / resolution.y;
    vec2 direction = vec2(cos(time * 0.32), sin(time * 0.32));
    float band = 12.0 * dot(p, direction) + 0.4 * sin(16.0 * length(p) - time);
    vec3 spectrum = 0.5 + 0.5 * cos(band + vec3(0.0, 2.094, 4.189));
    fragColor = vec4(0.08 + 0.9 * spectrum, 1.0);
}
