#version 450

// inputs
layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec2 inTexcoord;
layout( location = 2 ) in vec3 inNormal;

// outputs
layout( location = 0 ) out vec3 outNormal;
layout( location = 1 ) out vec3 outPosition;


// vp matrix
layout(set = 0, binding = 0, std140) uniform UScene
{

	mat4 projCam;
	vec3 camPos;
	
}uScene;



void main()
{

	// Vertex attribute
	outNormal = inNormal;

	// Get view coordinate
	vec4 screenPosition = uScene.projCam * vec4(inPosition, 1.0);

	screenPosition  = screenPosition /screenPosition .w;


	outPosition = inPosition;


	gl_Position = uScene.projCam * vec4( inPosition.xyz, 1.f ); 
}