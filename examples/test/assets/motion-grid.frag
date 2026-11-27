#version 330
uniform float time;
uniform vec2 resolution;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    vec2 grid = uv * vec2(8.0, 6.0);
    vec2 cell = floor(grid);
    float alternate = mod(cell.x + cell.y, 2.0);
    vec3 tile = mix(vec3(0.04, 0.11, 0.20), vec3(0.30, 0.61, 0.70), alternate);
    float sweep = 1.0 - smoothstep(0.0, 0.12, abs(uv.x - (0.5 + 0.46 * sin(time * 1.5))));
    fragColor = vec4(tile + sweep * vec3(0.57, 0.30, 0.06), 1.0);
}
