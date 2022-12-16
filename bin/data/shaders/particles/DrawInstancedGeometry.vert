#version 150

#pragma include "../include/ShaderHelpers.glsl"

#define MAX_LIGHTS 8

//these are passed in by openframeworks
uniform mat4 projectionMatrix;
uniform mat4 modelViewMatrix;
uniform mat4 modelViewProjectionMatrix;

in vec4 position;
in vec4 color;
in vec3 normal;
in vec2 texcoord;

uniform vec2 resolution;
uniform float time;
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform float particleMaxAge;
uniform vec4 particleStartColor;
uniform vec4 particleEndColor;
uniform int numLights = 0;
uniform vec3 lights[MAX_LIGHTS];

out vec3 v_normal;
out vec3 v_eyeVec;
out vec3 v_lightDir[MAX_LIGHTS];
out vec4 v_particleColor;


// ----------------------------
void main ()
{
	// Figure out the texture coordinate our data is on from the instance ID
	vec2 texCoord;
	texCoord.x = mod(gl_InstanceID, resolution.x) / resolution.x;
	texCoord.y = floor(gl_InstanceID / resolution.x) / resolution.y;
	
	// Grab our data
	vec4 particleData = texture( tex0, texCoord );
	vec3 particleVel = texture( tex1, texCoord ).xyz;

	float ageFrac = particleData.w / particleMaxAge;
	vec4 vertexPos = position;
	vec3 particlePos = particleData.xyz;
	
	// Pass the particle color along to the fragment shader
	v_particleColor = mix(particleStartColor, particleEndColor, ageFrac );
	
	// We are going to scale the particle so it comes in and out, as the vertex position is in model space, we can just multiply it to grow or shrink it
	vertexPos *= smoothStepInOut( 0.0, 0.1, 0.9, 1.0, ageFrac );
	
	// Rotate the vertex of our mesh (in model space) to face the velocity direction
	mat4 lookAt = makeLookAt( vec3(0,0,0), particleVel, vec3(0,1,0) );
	vertexPos = lookAt * vertexPos;
	
	// We add the rotated model space vertex pos to the particle pos to get the final position in space
	vec3 newVertexPos = particlePos + vertexPos.xyz;
	
	gl_Position = modelViewProjectionMatrix * vec4(newVertexPos, 1.0);
	
	// Light stuff
	vec3 vertexNormal = normal;
    mat3 normalMatrix = transpose(inverse(mat3(modelViewMatrix)));
	
	// Rotate the normal just as we did the vertex, then apply the canera transform
	vertexNormal = (lookAt * vec4(vertexNormal, 0)).xyz;
	v_normal = normalize(normalMatrix * vertexNormal).xyz;
	
	// We do lighting clculations in view (camera) space
	vec4 viewSpaceVertex = modelViewMatrix * vec4(newVertexPos, 1.0);
	v_eyeVec = -viewSpaceVertex.xyz;
	
	for ( int i = 0; i < numLights; ++i )
	{
		v_lightDir[i] = vec3(lights[i] - viewSpaceVertex.xyz);
	}
	
}
