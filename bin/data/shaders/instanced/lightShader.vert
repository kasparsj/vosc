#version 150

//these are passed in by openframeworks
uniform mat4 projectionMatrix;
uniform mat4 modelViewMatrix;
uniform mat4 modelViewProjectionMatrix;
uniform samplerBuffer tex;
uniform samplerBuffer colors;

in vec4 position;
in vec4 color;
in vec3 normal;
in vec2 texcoord;

out vec4 vertPosition;
out vec4 vertColor;
out vec3 vertNormal;
out vec2 texCoordVar;

void main()
{
    int x = gl_InstanceID * 4;
    mat4 transformMatrix = mat4(
                                texelFetch(tex, x),
                                texelFetch(tex, x+1),
                                texelFetch(tex, x+2),
                                texelFetch(tex, x+3)
                                );
    
    texCoordVar = texcoord;
    vertPosition = position;
    vertColor = texelFetch(colors, gl_InstanceID % textureSize(colors));
    
//    vertNormal = normal * inverse(mat3(transformMatrix));
    mat3 normalMatrix = mat3(modelViewProjectionMatrix);
    normalMatrix = inverse(normalMatrix);
    normalMatrix = transpose(normalMatrix);
    vertNormal = normalize(normal * normalMatrix);
    
    gl_Position = projectionMatrix * modelViewMatrix * transformMatrix * position;
}
