#version 150

const vec3 DEFAULT_COLOR = vec3(0.15, 0.75, 1.0);
const int MAX_VALUES = 8;

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;
uniform float values[MAX_VALUES];

out vec4 fragColor;

float rand(float n){return fract(sin(n) * 43758.5453123);}

float noise(float p){
    float fl = floor(p);
    float fc = fract(p);
    return mix(rand(fl), rand(fl + 1.0), fc);
}

float rand(vec2 n) {
    return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

float noise(vec2 n) {
    const vec2 d = vec2(0.0, 1.0);
    vec2 b = floor(n), f = smoothstep(vec2(0.0), vec2(1.0), fract(n));
    return mix(mix(rand(b), rand(b + d.yx), f.x), mix(rand(b + d.xy), rand(b + d.yy), f.x), f.y);
}

void main (void){
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 uv = vec2(0.5, 0.5 * (resolution.y / resolution.x)) - fragCoord.xy / resolution.x;
    float dist = sqrt(dot(uv, uv));
    float radius = cos(time) * 0.1 + 0.2;
    float border = sin(time) * 0.2 + 0.05;
    float t = smoothstep(radius + border, radius - border, dist);
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    //fragColor = vec4(col.rgb * 1.5 * t * noise(vec2(random, index)), t);
    fragColor = vec4(col.rgb * 2.0 * t * noise(vec2(random, index)), t);
}
