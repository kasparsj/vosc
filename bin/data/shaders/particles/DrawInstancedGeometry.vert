#version 120
#extension GL_EXT_gpu_shader4 : require

#pragma include "shaders/common/of_default_uniforms.glsl"
#pragma include "shaders/common/ShaderHelpers.glsl"

uniform vec2 resolution;

uniform float time;

uniform sampler2D tex0;
uniform sampler2D tex1;

uniform float particleMaxAge;

uniform vec4 particleStartColor;
uniform vec4 particleEndColor;

uniform int numLights = 0;

varying vec3 v_normal;
varying vec3 v_eyeVec;

#define MAX_LIGHTS 8
varying vec3 v_lightDir[MAX_LIGHTS];

varying vec4 v_particleColor;


// ----------------------------
void main ()
{
	// Figure out the texture coordinate our data is on from the instance ID
	vec2 texCoord;
	texCoord.x = mod(gl_InstanceID, resolution.x) / resolution.x;
	texCoord.y = floor(gl_InstanceID / resolution.x) / resolution.y;
	
	// Grab our data
	vec4 particleData = texture2D( tex0, texCoord );
	vec3 particleVel = texture2D( tex1, texCoord ).xyz;

	float ageFrac = particleData.w / particleMaxAge;
	vec4 vertexPos = gl_Vertex;
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
	vec3 vertexNormal = gl_Normal;
	
	// Rotate the normal just as we did the vertex, then apply the canera transform
	vertexNormal = (lookAt * vec4(vertexNormal, 0)).xyz;
	v_normal = normalize(gl_NormalMatrix * vertexNormal).xyz;
	
	// We do lighting clculations in view (camera) space
	vec4 viewSpaceVertex = modelViewMatrix * vec4(newVertexPos, 1.0);
	v_eyeVec = -viewSpaceVertex.xyz;
	
	for ( int i = 0; i < numLights; ++i )
	{
		v_lightDir[i] = vec3(gl_LightSource[i].position.xyz - viewSpaceVertex.xyz);
	}
	
}
