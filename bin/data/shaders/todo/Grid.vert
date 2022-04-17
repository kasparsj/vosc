#version 120

varying vec3 vertex;

void main() {
    gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xyz, 1.0);
    vertex = vec3(gl_Vertex.x * 3.0, gl_Vertex.y * 6.0, gl_Vertex.z * 3.0);
}
