#version 430

in vec2 midTexCoord;
in vec3 midNormal;
in vec3 vertexPosition;
in vec3 midSunDirection;

out vec4 outputColor;

uniform sampler2D colorTexture;

void main(void)
{	
	const vec3 normal = normalize(midNormal);
	const vec3 lightPos = vec3(1.0, 1.0, 0.0);
	
	// Texture
	outputColor = 0.3 * texture2D(colorTexture, midTexCoord);
	
	/*
	// Diffuse lighting
	if(abs(diffuse) > 0.00005) {
		float diffuseCoefficient = max(diffuse * dot(lightPos, normal), 0.0);
		outputColor += vec4(diffuseCoefficient * texture2D(texture, midTexCoord).xyz, 1.0);
	}

	// Specular lighting
	if(abs(specular) > 0.00005) {	
		vec3 halfAngleVector = 2.0 * dot(lightPos, normal) * normal - lightPos;
		float specularCoefficient = specular * pow(clamp(dot(halfAngleVector, normal), 0.0, 1.0), shininess);
		outputColor += specularCoefficient;
	}
	*/
	outputColor = clamp(outputColor, 0.0, 1.0);
}
