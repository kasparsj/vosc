#version 150

#pragma include "../include/of_default_uniforms.glsl"
#pragma include "../include/ShaderHelpers.glsl"
#pragma include "../include/Noise2D.glsl"

layout (lines) in;
layout (triangle_strip, max_vertices = 14) out;

in VertexAttrib {
    vec4 position;
    vec4 color;
    vec3 normal;
    vec2 texcoord;
} vertex[];

uniform float time;

//uniform vec3 cameraWorldPos;

uniform float stalkHalfWidth;
uniform float stalkHeight;
uniform float stalkSwayingMaxAngle;

uniform float animationTimeMaxDifference;

uniform float grassSwayingNoiseFrequency;
uniform float grassSwayingTimeScale;

out vec4 frontColor;

//-------------------------------------------------------------------------------------------------------------------------------------
//
void main()
{
	// Let's get the vertices of the line, calculate our stalk up and save how long it was when it came in
	vec4 p0 = gl_in[0].gl_Position;
	vec4 p1 = gl_in[1].gl_Position;
	vec4 stalkUp = p1 - p0;
	float stalkOrigLength = length(stalkUp);
	stalkUp = stalkUp / stalkOrigLength;
	
	// Screen-aligned axes, will come in handy for billboarding
	vec3 right		= normalize(vec3(modelViewMatrix[0][0], modelViewMatrix[1][0], modelViewMatrix[2][0]));
	vec3 up			= normalize(vec3(modelViewMatrix[0][1], modelViewMatrix[1][1], modelViewMatrix[2][1]));
	vec3 forward    = normalize(vec3(modelViewMatrix[0][2], modelViewMatrix[1][2], modelViewMatrix[2][2]));
	
	float stalkLength = stalkHeight * stalkOrigLength;
	
	// Lets calculate two angles and create a swaying rotation matrix for each stalk
	float swayTime = time * grassSwayingTimeScale;
	
	// The noise is in the range -1..1
	float swayNoise1 = snoise(  p0.xz                        * grassSwayingNoiseFrequency);
	float swayNoise2 = snoise( (p0.zx + vec2(123.4f,567.8f)) * grassSwayingNoiseFrequency);
	
	float timeOffset1 = swayNoise1 * animationTimeMaxDifference;
	float timeOffset2 = swayNoise2 * animationTimeMaxDifference;

	float swayingAngleRad1 = sin(swayTime + timeOffset1) * stalkSwayingMaxAngle;
	float swayingAngleRad2 = sin(swayTime + timeOffset2) * stalkSwayingMaxAngle;

	mat4 swayingMat = rotationMatrix( vec3(1,0,0), swayingAngleRad1 ) * rotationMatrix( vec3(0,0,1), swayingAngleRad2 );
	
	//color.r = map( sin(swayTime + timeOffset1), -1.0, 1.0, 0.0, 1.0 );
	//color.g = map( cos(swayTime + timeOffset2), -1.0, 1.0, 0.0, 1.0 );
	//color.b = 1.0 - color.g; // Todo: try replacing it with a palette texture.
	
	vec3 stalkSide = right * stalkHalfWidth;
	vec4 stalkVertexPos = p0;
	
	const int vertexOutAmount = 7; // not making this a uniform for now, could be a bit less compatible, non static loop lengths
	float stepMagnitude = stalkLength / float(vertexOutAmount - 1);
	
	for( int i = 0; i < vertexOutAmount; i++ )
	{
		float tmpFrac = float(i) / float(vertexOutAmount-1);
		
		// Let's add a bit of fake light by darkening the stalk towards the bottom
		float darkenAmount = map( smoothstep( 0.4, 1.0, tmpFrac ), 0.0, 1.0,		0.3, 1.0 );
		vec4 darkenColor = vec4( darkenAmount, darkenAmount, darkenAmount, 1.0 );
		
		vec3 tmpStalkHalfSide = stalkSide * (1.0-tmpFrac);
		vec3 tmpStalkNormal = cross( normalize(tmpStalkHalfSide), normalize(stalkUp.xyz) );
		
		gl_Position = modelViewProjectionMatrix * vec4((stalkVertexPos.xyz - tmpStalkHalfSide), 1.0);
		frontColor = vertex[0].color * darkenColor;
		EmitVertex();
		
		gl_Position = modelViewProjectionMatrix * vec4((stalkVertexPos.xyz + tmpStalkHalfSide), 1.0);
		frontColor = vertex[0].color * darkenColor;
		EmitVertex();
		
		stalkUp = stalkUp * swayingMat;
		stalkVertexPos += stalkUp * stepMagnitude;
	}
}
