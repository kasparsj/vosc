#version 400

#define BLEND_ALPHA 0
#define BLEND_MULTIPLY 1
#define BLEND_DIFFERENCE 2

#define MAX_LAYERS 8
#define MAX_LAYERS_2 16

uniform sampler2DArray tex0;
uniform sampler2DArray tex1;
uniform sampler2DArray tex2;
uniform sampler2DArray tex3;
uniform sampler2DArray tex4;
uniform sampler2DArray tex5;
uniform sampler2DArray tex6;
uniform sampler2DArray tex7;
uniform int numLayers;
uniform vec2 texSize[MAX_LAYERS];
uniform int texCount[MAX_LAYERS];
uniform vec2 texScale[MAX_LAYERS];
uniform vec2 adjust[MAX_LAYERS];
uniform float opacity[MAX_LAYERS];
uniform sampler2D mapTex0;
uniform sampler2D mapTex1;
uniform sampler2D mapTex2;
uniform sampler2D mapTex3;
uniform sampler2D mapTex4;
uniform sampler2D mapTex5;
uniform sampler2D mapTex6;
uniform int mapTexSize[MAX_LAYERS];
//uniform sampler2D weaveTexSprite;
uniform vec2 weaveTexSize[MAX_LAYERS];
//uniform sampler2D colorMapTex;
//uniform int colorMapSize;
uniform float cycle;
uniform float multiplier;
uniform float vignette;
uniform float vignetteMult;
uniform int blendMode;

uniform float scale;
uniform sampler2D bgTex;

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

out vec4 fragColor;

vec2 texCoord[MAX_LAYERS];
vec4 texColorLod[MAX_LAYERS];

vec3 blendMultiply(vec3 base, vec3 blend) {
    return base*blend;
}
vec3 blendMultiply(vec3 base, vec3 blend, float opacity) {
    return (blendMultiply(base, blend) * opacity + base * (1.0 - opacity));
}

vec3 blendDifference(vec3 base, vec3 blend) {
    return abs(base-blend);
}
vec3 blendDifference(vec3 base, vec3 blend, float opacity) {
    return (blendDifference(base, blend) * opacity + base * (1.0 - opacity));
}

vec3 blend(vec3 base, vec3 blend) {
    switch (blendMode) {
        case BLEND_MULTIPLY:
            return blendMultiply(base, blend);
        case BLEND_ALPHA:
            return mix(base, blend, 0.5);
        case BLEND_DIFFERENCE:
            return blendDifference(base, blend);
    }
}

vec3 blend(vec3 base, vec3 blend, float opacity) {
    switch (blendMode) {
        case BLEND_MULTIPLY:
            return blendMultiply(base, blend, opacity);
        case BLEND_ALPHA:
            return mix(base, blend, opacity / 2.0);
        case BLEND_DIFFERENCE:
            return blendDifference(base, blend, opacity);
    }
}

int toInt(vec3 tex) {
    ivec3 bytes = ivec3(tex * 255);
    return (bytes.r << 16) | (bytes.g << 8) | (bytes.b);
}

float toFloat(vec3 tex) {
    return float(toInt(tex)) / 0xFFFFFF;
}

vec3 toVec3(int val) {
    float rValue = (((val) >> 16) & 0xFF) / 255.0;
    float gValue = (((val) >> 8) & 0xFF) / 255.0;
    float bValue = ((val) & 0xFF) / 255.0;
    return vec3(rValue, gValue, bValue);
}

float whenEq(float x, float y) {
    return 1.0 - abs(sign(x - y));
}

float whenNeq(float x, float y) {
    return abs(sign(x - y));
}

float whenGt(float x, float y) {
    return max(sign(x - y), 0.0);
}

float whenLt(float x, float y) {
    return max(sign(y - x), 0.0);
}

float whenGe(float x, float y) {
    return 1.0 - whenLt(x, y);
}

float whenLe(float x, float y) {
    return 1.0 - whenGt(x, y);
}

float luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec4 textureWeave(sampler2DArray tex, int i) {
    vec4 texColor = texture(tex, vec3(texCoord[i], texColorLod[i].r * texCount[i]));
    texColor.a = texColorLod[i].a;
    vec2 texel = (1.0 / texSize[i]);
    vec2 weaveCoord = mod(floor((texCoord[i] - 0.5) / texel), weaveTexSize[i]);
    float luma = luma(texColor.rgb);
    vec2 weaveTexCoord = vec2(0.0);
    float maxSize = max(weaveTexSize[i].x, weaveTexSize[i].y);
    weaveTexCoord += vec2(weaveCoord.y / maxSize, (i*2.0) / MAX_LAYERS_2) * whenGt(luma, 0.5);
    weaveTexCoord += vec2(weaveCoord.x / maxSize, (i*2.0 + 1.0) / MAX_LAYERS_2) * whenLe(luma, 0.5);
    vec4 weaveColor = vec4(0.0);
    weaveColor += texColor * whenEq(weaveTexSize[i].x, 0);
    //weaveColor += texture(weaveTexSprite, weaveTexCoord) * whenNeq(weaveTexSize[i].x, 0);
    return weaveColor;
}

void main() {
    vec2 fragCoord = 0.5 + (vertex.texcoord - 0.5) * scale * multiplier;
    
    texCoord[0] = 0.5 + (fragCoord - 0.5) * texScale[0] - adjust[0];
    texCoord[1] = 0.5 + (fragCoord - 0.5) * texScale[1] - adjust[1];
    texCoord[2] = 0.5 + (fragCoord - 0.5) * texScale[2] - adjust[2];
    texCoord[3] = 0.5 + (fragCoord - 0.5) * texScale[3] - adjust[3];
    texCoord[4] = 0.5 + (fragCoord - 0.5) * texScale[4] - adjust[4];
    texCoord[5] = 0.5 + (fragCoord - 0.5) * texScale[5] - adjust[5];
    texCoord[6] = 0.5 + (fragCoord - 0.5) * texScale[6] - adjust[6];
    texCoord[7] = 0.5 + (fragCoord - 0.5) * texScale[7] - adjust[7];
    
    texColorLod[0] = texture(mapTex0, texCoord[1] / mapTexSize[0]);
    texColorLod[1] = texture(mapTex1, texCoord[2] / mapTexSize[1]);
    texColorLod[2] = texture(mapTex2, texCoord[3] / mapTexSize[2]);
    texColorLod[3] = texture(mapTex3, texCoord[4] / mapTexSize[3]);
    texColorLod[4] = texture(mapTex4, texCoord[5] / mapTexSize[4]);
    texColorLod[5] = texture(mapTex5, texCoord[6] / mapTexSize[5]);
    texColorLod[6] = texture(mapTex6, texCoord[7] / mapTexSize[6]);
    texColorLod[7] = vec4(0.0); // todo: temporary fix only
    texColorLod[7] = texColorLod[numLayers - 1];
    
    vec4 texColor[MAX_LAYERS];
    texColor[0] = textureWeave(tex0, 0);
    if (numLayers == 1) {
        fragColor = texColor[0];
        return;
    }
    
    texColor[1] = textureWeave(tex1, 1);
    texColor[2] = textureWeave(tex2, 2);
    texColor[3] = textureWeave(tex3, 3);
    texColor[4] = textureWeave(tex4, 4);
    texColor[5] = textureWeave(tex5, 5);
    texColor[6] = textureWeave(tex6, 6);
    texColor[7] = textureWeave(tex7, 7);
    
    vec3 outColor[MAX_LAYERS];
    vec3 mixedColor;
    outColor[0] = blend(vec3(0), texColor[0].rgb, opacity[0] * (cycle >= 0.0 ? (1.0 - cycle) : 1.0));
    outColor[1] = blend(outColor[0], texColor[1].rgb, opacity[1]);
    outColor[2] = blend(outColor[1], texColor[2].rgb, opacity[2]);
    outColor[3] = blend(outColor[2], texColor[3].rgb, opacity[3]);
    outColor[4] = blend(outColor[3], texColor[4].rgb, opacity[4]);
    outColor[5] = blend(outColor[4], texColor[5].rgb, opacity[5]);
    
    // todo: hotfix
    outColor[6] = blend(outColor[(numLayers - 3)], texColor[(numLayers - 2)].rgb, opacity[(numLayers - 2)] * (cycle >= 0.0 ? cycle : 1.0));
    mixedColor = outColor[6];
    //outColor[6] = blend(outColor[5], texColor[6].rgb, opacity[6]);
    //mixedColor = blend(outColor[(numLayers - 2)], texColor[7].rgb, opacity[7] * cycle);
    
    if (blendMode == BLEND_MULTIPLY) {
        mixedColor = mixedColor * pow(2, max(0, numLayers - 3)) * (1.0 + multiplier);
    }
    
    vec4 bgColor = texture(bgTex, vec2(0.5, 0.5) + vertex.position.xy / 2000 * vec2(1.0, -1.0));
    //mixedColor = blend(bgColor.rgb, mixedColor, 1.0);
  
    fragColor = vec4(mixedColor, (mixedColor.r + (1.0 - distance(vertex.texcoord, vec2(0.5, 0.5)) / (1.0 - vignette))) * vignetteMult);
    //fragColor = vec4(mixedColor, 1.0);
//    fragColor = vec4(mixedColor, 1.0) * whenEq(colorMapSize, 0);
    
//    float mixedFloat = toFloat(mixedColor);
//    int mixedInt = int(mixedFloat * colorMapSize * colorMapSize);
//    vec4 mapColor = texelFetch(colorMapTex, ivec2(mixedInt / colorMapSize, mixedInt % colorMapSize), 0);
//    fragColor += mapColor * whenNeq(colorMapSize, 0);
}
