#version 450

// input
layout( location = 0 ) in vec3 inNormal;
layout( location = 1 ) in vec3 inPosition;



// output
layout( location = 0 ) out vec4 oColor;
layout( location = 1 ) out vec4 oNormal;
layout( location = 2 ) out vec4 oMaterial;

// PBR material
layout( set = 2, binding = 0, std140) uniform UMaterial
{
	vec4 emissive;
	vec4 albedo;
	float shininess;
	float metalness;
} uMaterial;

void main()
{

	oColor = vec4(uMaterial.albedo.xyz, uMaterial.shininess);
	oNormal = vec4( inNormal,1.0);
	oMaterial = vec4(uMaterial.emissive.xyz, uMaterial.metalness);
}