#version 330
uniform float time;
uniform vec2 resolution;
uniform vec4 color;
uniform float glow = 1.0;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    float wave = 0.24 * sin(9.0 * uv.y + time * 1.5);
    float ribbon = abs(sin(13.0 * (uv.x + wave)));
    float width = mix(0.07, 0.58, clamp(glow, 0.0, 1.0));
    float intensity = 0.10 + 0.90 * (1.0 - smoothstep(width, width + 0.14, ribbon));
    vec4 ink = color == vec4(0.0) ? vec4(0.16, 0.75, 1.0, 1.0) : color;
    fragColor = vec4(ink.rgb * intensity, ink.a);
}
