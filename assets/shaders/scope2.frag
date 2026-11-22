#version 150

#define MAX_SAMPLES 256

const vec3 DEFAULT_COLOR = vec3(1, 1, 1);

uniform vec2 resolution;
uniform vec2 offset;
uniform float data[MAX_SAMPLES];
// todo: should be int
uniform float dataSize = MAX_SAMPLES;
uniform float y = 0.5;
uniform float thickness = 1.f;
uniform float scale = 0.1;
uniform vec4 color;
uniform float intensity = 10.;

out vec4 fragColor;

// Lifted from:https://stackoverflow.com/questions/849211/shortest-distance-between-a-point-and-a-line-segment
float DistanceToLineSegment(vec2 p0, vec2 p1, vec2 p)
{
    float distanceP0 = length(p0 - p);
    float distanceP1 = length(p1 - p);
    
    float l2 =pow(length(p0 - p1), 2.);
    float t = max(0., min(1., dot(p - p0, p1 - p0) / l2));
    vec2 projection = p0 + t * (p1 - p0);
    float distanceToProjection = length(projection - p);
    
    return min(min(distanceP0, distanceP1), distanceToProjection);
}

float Function(float x)
{
    float perfectSamplePosition = (dataSize-1) * x;
    int leftSampleIndex = int (floor (perfectSamplePosition));
    int rightSampleIndex = int (ceil (perfectSamplePosition));
    return mix (data[leftSampleIndex], rightSampleIndex < dataSize ? data[rightSampleIndex] : 0, fract (perfectSamplePosition));
}

float DistanceToFunction(vec2 p, float xDelta)
{
    float result = 100.;
    
    for (float i = -3.; i < 3.; i += 1.)
    {
        vec2 q = p;
        q.x += xDelta * i;
        
        vec2 p0 = vec2(q.x, y+Function(q.x)*-scale);
        vec2 p1 = vec2(q.x + xDelta, y+Function(q.x + xDelta)*-scale);
        result = min(result, DistanceToLineSegment(p0, p1, p));
    }

    return result;
}

void main()
{
    vec2 fragCoord = offset + gl_FragCoord.xy;
    vec2 uv = fragCoord / resolution;

    float distanceToPlot = DistanceToFunction(uv, 1. / resolution.x);
    float alpha = smoothstep(0., 1., 1. - distanceToPlot * (1 / thickness) * resolution.y);
    alpha = pow(alpha, 1./intensity);
    
    vec4 col = color;
    if (col == vec4(0)) col = vec4(DEFAULT_COLOR, 1.0);
    col = vec4(vec3(col.rgb) * alpha, col.a * alpha);

    fragColor = col;
}
