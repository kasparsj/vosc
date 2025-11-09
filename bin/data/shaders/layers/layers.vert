#version 400

#pragma include <of_default_uniforms.glsl>
#pragma include <of_default_vertex_in_attributes.glsl>

out VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

void main() {
    vec4 translation;
    translation.x = 0;
    translation.y = 0;
    translation.z = 0;
    translation.w =	1;
    
    vertex.texcoord = texcoord;
    
    mat4 perVertexModelMatrix;
    perVertexModelMatrix[0] = vec4(1,0,0,0);
    perVertexModelMatrix[1] = vec4(0,1,0,0);
    perVertexModelMatrix[2] = vec4(0,0,1,0);
    perVertexModelMatrix[3] = translation;
    
    vertex.position = modelViewMatrix * perVertexModelMatrix * position;
    
    gl_Position = projectionMatrix * vertex.position;
}
