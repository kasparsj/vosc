#version 150

uniform mat4 modelViewMatrix;
uniform mat4 modelViewProjectionMatrix;
uniform vec3 lightLocation;

in vec2 texCoordVar;
in vec3 vertNormal;
in vec4 vertPosition;
in vec4 vertColor;

out vec4 outputColor;

void main()
{
    //calculate normal in world coordinates
    mat3 normalMatrix = transpose(inverse(mat3(modelViewMatrix)));
    vec3 vertexNormal = normalize(normalMatrix * vertNormal);
    
    //calculate the location of this pixel in world coordinates
    vec3 fragPosition = vec3(modelViewMatrix * vertPosition);
    vec3 lightPosition = vec3(modelViewMatrix * vec4(lightLocation, 1.0));
    
    //calculate the vector from this pixels surface to the light source
    vec3 surfaceToLight = normalize(lightPosition - fragPosition);
    
    //calculate the cosine of the angle of incidence (brightness)
    float brightness = max(0.0, dot(vertexNormal, surfaceToLight));
    
    //calculate final color of the pixel
    outputColor = vec4(brightness * vertColor.rgb, 1.0);

}
