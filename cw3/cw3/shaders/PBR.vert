#version 450

// outputs
layout( location = 0 ) out vec2 outTexcoord;

// vertex
vec3 plane[6] = vec3[] (
    vec3(1, 1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
    vec3(-1, -1, 0), vec3(1, 1, 0), vec3(1, -1, 0)
);

vec2 uv[6] = vec2[] (
    vec2(1, 1), vec2(0, 0), vec2(0, 1),
    vec2(0, 0), vec2(1, 1), vec2(1, 0)
);

void main()
{
	// Vertex attribute
	outTexcoord = uv[gl_VertexIndex];
	gl_Position = vec4( plane[gl_VertexIndex], 1.f ); 
}