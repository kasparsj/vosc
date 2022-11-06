#version 400

#pragma include "shaders/common/of_default_uniforms.glsl"
#pragma include "shaders/common/of_default_vertex_in_attributes.glsl"

// outputs
out VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

void main( void )
{
    // outputs
    vertex.position = modelViewMatrix * position;
    vertex.color = color;
    vertex.normal = normal;
    vertex.texcoord = texcoord;
    
    // pass the position straight through as it's already -1.0f to 1.0f
    gl_Position = projectionMatrix * vertex.position;
    //gl_Position = position;
}
