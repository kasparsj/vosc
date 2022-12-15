#version 330

#pragma include "shaders/include/of_default_uniforms.glsl"
#pragma include "shaders/include/of_default_vertex_in_attributes.glsl"

uniform vec2 resolution;
uniform float fovYScale;

out vec3 eyePos;
out vec3 dir;
out vec3 cameraForward;

void main(void)
{
	eyePos = -(modelViewMatrix[3].xyz) * mat3(modelViewMatrix);
	dir = vec3( position.x*fovYScale * (resolution.x / resolution.y), position.y*fovYScale, -1.0 ) * mat3(modelViewMatrix);
	cameraForward = vec3(0,0,-1.0)*mat3(modelViewMatrix);
	
	gl_Position = position;
}
