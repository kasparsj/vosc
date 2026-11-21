#version 330
uniform sampler2D srctex;
uniform vec2 resolution;
uniform float time;
out vec4 fragColor;
void main() {
    vec2 uv=gl_FragCoord.xy/resolution;
    vec2 d=1.0/resolution;
    vec3 previousAverage=(texture(srctex,uv+vec2(d.x,0)).rgb+texture(srctex,uv-vec2(d.x,0)).rgb+texture(srctex,uv+vec2(0,d.y)).rgb+texture(srctex,uv-vec2(0,d.y)).rgb)*.25;
    float v=dot(uv,vec2(12.9898,78.233))+time*.7;
    for(int i=0;i<2;i++) v=sin(v*1.31+float(i)*.17)+cos(v*.73-float(i)*.11);
    vec3 seed=vec3(.5+.5*sin(v),.5+.5*cos(v+uv.x*6.283185),.5+.5*sin(v+uv.y*9.424778));
    fragColor=vec4(.94*previousAverage+.06*seed,1);
}
