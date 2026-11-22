#version 330 core

uniform sampler2DArray srctex;
uniform float time;
uniform vec2 resolution;
uniform int random;
uniform int texIndex;  // Layer index for array texture

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

out vec4 fragColor;

void main() {
    vec2 uv = vertex.texcoord;

    // Determine which quadrant we're in
    // Top-left (0,0 to 0.5,0.5): original
    // Top-right (0.5,0 to 1,0.5): horizontal mirror
    // Bottom-left (0,0.5 to 0.5,1): vertical mirror
    // Bottom-right (0.5,0.5 to 1,1): both horizontal and vertical mirror
    
    vec2 quadrantUV = fract(uv * 2.0);  // Scale to [0,1] within each quadrant
    
    if (uv.x < 0.5 && uv.y < 0.5) {
        // Top-left: original
        fragColor = texture(srctex, vec3(quadrantUV, texIndex));
    }
    else if (uv.x >= 0.5 && uv.y < 0.5) {
        // Top-right: horizontal mirror
        fragColor = texture(srctex, vec3(1.0 - quadrantUV.x, quadrantUV.y, texIndex));
    }
    else if (uv.x < 0.5 && uv.y >= 0.5) {
        // Bottom-left: vertical mirror
        fragColor = texture(srctex, vec3(quadrantUV.x, 1.0 - quadrantUV.y, texIndex));
    }
    else {
        // Bottom-right: both horizontal and vertical mirror
        fragColor = texture(srctex, vec3(1.0 - quadrantUV.x, 1.0 - quadrantUV.y, texIndex));
    }
}

