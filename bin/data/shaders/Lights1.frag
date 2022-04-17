#version 150

#define DEFAULT_COLOR vec3(0.4615, 0.615, 1.0)
#define MAX_VALUES 8

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;
uniform float values[MAX_VALUES];

out vec4 fragColor;

void main(void){
    vec2 m;
    float t;
    vec2 fragCoord = offset + gl_FragCoord.xy;
    for(float i = 0.0; i < 4.0; i+=1.0){
        m = vec2(sin(time * 5.0 * i) * 0.9, cos(time * 5.2 + i) * 0.8);
        vec2 pos = (fragCoord.xy * 2.0 - resolution) / min(resolution.x, resolution.y);
        t += (sin(i * 30.0) * 0.05 + 0.2) / length(m - pos);
    }
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    fragColor = vec4(col.rgb * 1.3 * t, col.a);
}
