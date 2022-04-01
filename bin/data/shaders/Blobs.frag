#version 120

#define MAX_VALUES 8

uniform float time;
uniform vec2 resolution;
uniform vec2 offset;
uniform int index;
uniform vec4 color;
uniform int random;
uniform float values[MAX_VALUES];
uniform int visible;

void main(void){
    vec2 m;
    float t;
    vec2 fragCoord = offset + gl_FragCoord.xy;
    for(float i = 0.0; i < 60.0; i+=1.0){
        m = vec2(sin(time * sin(i+3.0) *4.0) * 1.0, cos(time * cos(i + 3.0) * 4.0) * 1.0);
        vec2 pos = (fragCoord.xy * 1.0 - resolution) / min(resolution.x, resolution.y);
        t += sin(i * 2.0) / length(m - pos);
    }
    vec4 col = color;
    if (col.rgb == vec3(0)) col = vec4(1, 1, 1, col.a);
    gl_FragColor = vec4(vec3(t) / 0.1 * col.rgb + vec3(0.0, 0.0, 0.5), col.a);
}
