#version 330
uniform sampler2D srctex;
uniform vec2 resolution;
uniform float decay;
out vec4 fragColor;
void main() {
    vec4 previous=texture(srctex,gl_FragCoord.xy/resolution);
    fragColor=vec4(decay*previous.r,max(0.0,previous.g-1.0/16.0),.25,1);
}
