#version 150

#define THICKNESS 0.01

uniform vec2 resolution;
uniform vec2 offset;
uniform float data[256];
//uniform samplerBuffer data;
uniform float y = 0.5;

out vec4 fragColor;

float getAmplitudeForXPos (float xPos)
{
    float perfectSamplePosition = 255.0 * xPos / resolution.x;
    int leftSampleIndex = int (floor (perfectSamplePosition));
    int rightSampleIndex = int (ceil (perfectSamplePosition));
    return mix (data[leftSampleIndex], data[rightSampleIndex], fract (perfectSamplePosition));
}

void main()
{
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 st = fragCoord.xy / resolution.xy;
    float amplitude = getAmplitudeForXPos (gl_FragCoord.x);
//    float amplitude = texelFetch(data, st.x * textureSize(data));
    

    // Centers & Reduces Wave Amplitude
    amplitude = y - amplitude / 2.5;
    float r = abs (THICKNESS / (amplitude-st.y));

    fragColor = vec4 (r - abs (r * 0.2), r - abs (r * 0.2), r - abs (r * 0.2), 1.0);
}
