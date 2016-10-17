#version 330

in layout(location=0) vec3 inPosition;
in layout(location=1) vec2 inTexCoord;
in layout(location=2) vec3 inNormal;

// Transforming
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

void main(void)
{
	// MVP
	mat4 MV = viewMatrix * modelMatrix;
	mat4 MVP = projectionMatrix * MV;

	// Vertex position
	gl_Position = MVP * vec4(inPosition, 1.0);
}
