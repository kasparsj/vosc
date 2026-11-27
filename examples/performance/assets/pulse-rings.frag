#version 330
uniform float time;
uniform vec2 resolution;
out vec4 fragColor;

void main() {
    float radius = length((gl_FragCoord.xy - 0.5 * resolution) / resolution.y);
    float phase = radius * 10.0 - time * 0.85;
    float ring = smoothstep(0.56, 0.94, cos(6.283185 * phase));
    float falloff = 1.0 - smoothstep(0.08, 0.67, radius);
    float beat = 0.55 + 0.45 * sin(time * 2.2);
    vec3 base = vec3(0.10, 0.04, 0.18);
    vec3 light = vec3(0.60, 0.23, 0.94);
    fragColor = vec4(base + ring * falloff * beat * light, 1.0);
}
