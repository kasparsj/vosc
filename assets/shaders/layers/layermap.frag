#version 400

#define STRAT_PARENT 0
#define STRAT_RANDOM 1
#define STRAT_NOISE 2

uniform int texCount;
uniform float compScale;
uniform sampler2DArray parentTex;
uniform vec2 parentTexSize;
uniform sampler2D parentMapTex;
uniform vec2 parentMapTexSize;
uniform vec2 size;
uniform int dist;
uniform float lod;

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

out vec4 fragColor;

int toInt(vec3 tex) {
    ivec3 bytes = ivec3(tex * 255);
    return (bytes.r << 16) | (bytes.g << 8) | (bytes.b);
}

float toFloat(vec3 tex) {
    return float(toInt(tex)) / 0xFFFFFF;
}

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

vec3 texelSize(sampler2DArray tex, float lod) {
    ivec3 texSize = textureSize(tex, int(lod));
    return (1.0 / texSize);
}

void main() {
    vec2 position = vertex.position.xy + size / 2.0;
    vec2 texCoord = mod(position, parentTexSize.xy * compScale) / parentTexSize.xy * compScale; // 0 to 1
    vec4 mapColor;
    
    if (dist == STRAT_PARENT) {
        // todo: better color reduction
        vec2 parentTexCoord = 0.5 + vertex.position.xy / parentTexSize.xy / parentMapTexSize;
        vec4 parentMapColor = floor(texture(parentMapTex, parentTexCoord) * texCount) / texCount;
        float texZ = toFloat(parentMapColor.rgb) * texCount;
        vec4 parentColor = floor(textureLod(parentTex, vec3(texCoord, texZ), lod) * texCount) / texCount;
        float parentFloat = toFloat(parentColor.rgb);
        mapColor = vec4(parentFloat, 0.0, 0.0, 1.0);
    }
    else if (dist == STRAT_NOISE) {
        
    }
    else { // if (dist == STRAT_RANDOM)
        vec3 texelLod = texelSize(parentTex, lod);
        float random = rand(floor(texCoord / texelLod.xy) * texelLod.xy);
        mapColor = vec4(random, 0.0, 0.0, 1.0);
    }
    
    fragColor = mapColor;
    
}
