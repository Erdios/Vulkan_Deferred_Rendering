#version 450

#define PI 3.1415926535897932384626433832795
#define MAX_LIGHTS 128

// Light properties
struct Light
{
	vec4 lightPos;
	vec4 diffuse;

	vec4 specular;
	float radian;
};



//[ input ]
layout( location = 0) in vec2 uv;

//[ uniform ]
layout( set = 0,binding = 0 ) uniform sampler2D inAlbedo;
layout( set = 0,binding = 1 ) uniform sampler2D inNormal;
layout( set = 0,binding = 2 ) uniform sampler2D inMaterial;
layout( set = 0,binding = 3 ) uniform sampler2D inDepth;
layout( set = 0,binding = 4, std140) uniform ULight
{
	int lightCount;
	vec4 ambient;
	Light light[MAX_LIGHTS];
}uLight;
layout( set = 1, binding = 0, std140) uniform UScene
{
	mat4 projCam;
	vec3 camPos;
}uScene;

//[ output ]
layout( location = 0 ) out vec4 oColor;

vec4 GetPosition()
{
	float depth = texture(inDepth,uv).r ;

	vec4 position = inverse(uScene.projCam) * vec4((uv.xy)*2.0 -1.0, depth, 1.0);

	position = position/position.w;

	return  position;
}



mat4 rotationX(float angle)
{
	return mat4(
		1.0, 0.0, 0.0, 0.0,
		0.0, cos(angle), -sin(angle), 0.0,
		0.0, sin(angle), cos(angle), 0.0,
		0.0, 0.0, 0.0, 1.0
	);
}


mat4 rotationY(float angle)
{
	return mat4(
		cos(angle), 0.0, sin(angle), 0.0,
		0.0, 1.0, 0.0, 0.0,
		-sin(angle), 0.0, cos(angle), 0.0,
		0.0, 0.0, 0.0, 1.0
	);
}



vec3 GetLight(Light light)
{
	vec3 inPosition = GetPosition().xyz;
	vec3 albedo = texture(inAlbedo, uv).xyz;
	float shininess = texture(inAlbedo, uv).w;
	float metalness = texture(inMaterial, uv).w;
	vec3 normal = texture(inNormal, uv).xyz;

	// View direction
	vec3 viewDir = normalize( uScene.camPos - inPosition);

	// Light direction
	vec3 lightPos = ( rotationY(light.radian) * light.lightPos).xyz;
	vec3 lightDir = normalize(lightPos - inPosition);

	vec3 H = normalize(lightDir + viewDir);
	vec3 N = normalize(normal);

	
	vec3 F0 = (1.0-metalness) * vec3(0.04,0.04,0.04) + metalness * albedo.xyz;

	vec3 F = F0 + (1.0 - F0) * pow(1.0- dot(H, viewDir), 5.0);
	
	vec3 ld = albedo.xyz/PI * (vec3(1.0,1.0,1.0) - F) * (1.0 - metalness); 

//-> Task6
	float roughness = 1.0/sqrt(shininess);

	float FD90 = 0.5 + 2.0 * roughness * pow(dot(viewDir, H), 2.0);
	
	vec3 ld_disney = albedo.xyz/PI * (1.0 + ( FD90 - 1.0)* pow(  1.0 - dot(lightDir, N) ,5.0)) * (1.0 + (FD90 -1.0) * pow( 1.0 - dot(viewDir,N),5.0)); 

	float gamma = 3.0; // recommand [1, 2] in paper Burley 2012; seems that if gamma go bigger, the highlight peak is bigger

	float D_GTR = pow(roughness,2.0) * max(0.0, dot(N,H)) / (PI * pow( pow(roughness * dot(H,N), 2.0) +  (1.0- pow(dot(H,N),2.0) ), gamma)+ 1e-25);
	
	//((gamma -1.0)*(pow(roughness,2.0) - 1.0))/ (PI *(1.0 -pow(roughness, 2.0 * (1.0-gamma)) ) * pow( 1.0 +(roughness*roughness -1.0) * pow(dot(N,H),2), gamma)+ 1e-25) ;
//-> End 
	
	float D = (shininess + 2.0)/PI * 0.5 * pow(max(0.0, dot(N, H)),shininess);

	float G = min(1.0, min(	2.0* max(0.0, dot(N,H)) * max(0.0, dot(N, viewDir))/(dot(viewDir, H)+ 1e-25), 
					2.0* max(0.0,dot(N, H)) * max(0.0,dot(N, lightDir))/(dot(viewDir, H)+ 1e-25)));
	
	vec3 Fr = ld + (D * F * G)/((4.0 * max(0.0, dot(viewDir, N)) * max(0.0, dot(lightDir, N))) + 1e-25);
	
	
	return Fr * light.diffuse.xyz * max(0.0, dot(N, lightDir)); 
	

}


void main()
{

	vec3 albedo = texture(inAlbedo, uv).xyz;
	
	vec3 la = uLight.ambient.xyz * albedo.xyz;
	
	vec3 emissive = texture(inMaterial, uv).xyz;

	vec3 lightSum = emissive + la;

	for(int i =0; i< uLight.lightCount; ++i)
	{
		lightSum = lightSum + GetLight(uLight.light[i]);
	}
	
	

	oColor =  vec4(lightSum,  1.0);

}