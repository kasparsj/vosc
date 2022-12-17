#version 150

// Hmm, do we really need to give the path to the shader if it's in the same folder?
#pragma include "../include/ShaderHelpers.glsl"
#pragma include "../include/SimplexNoiseDerivatives4D.glsl"

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex;

uniform sampler2D srctex;
uniform sampler2D srctex1;

uniform float time;
uniform float timeStep;

uniform float particleMaxAge;

uniform float noisePositionScale = 1.5; // some start values in case we don't set any
uniform float noiseMagnitude = 0.075;
uniform float noiseTimeScale = 1.0 / 4000.0;
uniform float noisePersistence = 0.2;
uniform vec3 baseSpeed = vec3( 0.5, 0.0, 0.0 );

const int OCTAVES = 3;

out vec4 outData[gl_MaxDrawBuffers];

// -----------------------------------------------------------
void main (void)
{
	vec2 texCoord = vertex.texcoord;
	
	vec4 posAndAge = texture( srctex, texCoord );
	
	vec3 pos = posAndAge.xyz;
	float age = posAndAge.w;
	
	age += timeStep;
	
	if( age > particleMaxAge )
	{
		age = 0.0;
		
		float spawnRadius = 0.1;
		pos = randomPointOnSphere( vec3( rand( texCoord + pos.xy ), rand( texCoord.xy + pos.yz ), rand( texCoord.yx + pos.yz ))) * spawnRadius;
	}
	
	vec3 noisePosition = pos  * noisePositionScale;
	float noiseTime    = time * noiseTimeScale;
	
	vec3 noiseVelocity = curlNoise( noisePosition, noiseTime, OCTAVES, noisePersistence )  * noiseMagnitude;
	vec3 totalVelocity = baseSpeed + noiseVelocity;
	
	vec3 newPos = pos + totalVelocity * timeStep;
	vec3 vel = newPos - pos;
	
	pos = newPos;
	
    outData[0] = vec4( pos, age );
    outData[1] = vec4( vel, 1.0 );
	
}

