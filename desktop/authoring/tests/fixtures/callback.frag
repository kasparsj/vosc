#version 330
uniform float u_vosc_callback;
uniform float gain = 0.125;
out vec4 fragColor;

void main() {
    fragColor = vec4(u_vosc_callback, gain, 0.0, 1.0);
}
