#version 330 core

in vec3 Pos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 lightColor;

out vec4 color;

uniform vec3 lightPos;

void main() {
    //ambient
    float ambient = 0.1;
    vec3 ambientColor = ambient * lightColor;
    //diffuse
    vec3 lightDirection = normalize(lightPos- Pos);
    float diffuse = max(dot(lightDirection, Normal), 0.0);
    vec3 diffuseColor = diffuse * lightColor;
    //specular
    float specularStrength = 0.5;
    vec3 reflected = reflect(-lightDirection, Normal);
    vec3 viewDir = normalize(-Pos);
    float specular = max(pow(dot(reflected, viewDir), 32), 0);
    vec3 specularColor = specularStrength * specular * lightColor;
    //combine into one color
    vec3 resultant = ambientColor + diffuseColor + specularColor;
    color = vec4(resultant, 1.0);
}