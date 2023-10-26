#version 330 core

in vec3 Pos;
in vec3 Normal;
in vec2 TexCoords;

out vec4 color;

uniform vec3 lightPos;
uniform vec3 cameraPos;

uniform sampler2D image;
//encapsulates any game states into one variable for different shading
uniform int state;

void main() {
    vec3 lightColor = vec3(1,1,1);
    //ambient
    float ambient = 0.1;
    vec3 ambientColor = ambient * lightColor;
    //diffuse
    vec3 lightDirection = normalize(lightPos- Pos);
    float diffuse = max(dot(lightDirection, Normal), 0.0);
    vec3 diffuseColor = diffuse * lightColor;
    //specular
    vec3 viewDir = normalize(cameraPos-Pos);
    vec3 h = normalize(lightDirection + viewDir);
    float specular = pow(max(dot(Normal, h), 0.0), 64);
    vec3 specularColor = specular * lightColor;
    //combine into one color
    vec3 resultant = (ambientColor + diffuseColor + specularColor) * vec3(texture(image, TexCoords));
    //pause
    if(state == 1) {
       
        resultant = resultant * 0.3;
    }
    //debug
    if(state == 2) {
         resultant = (ambientColor + diffuseColor + specularColor) * vec3(TexCoords, 1.0);
    }
    resultant = resultant * 0.8;
    color = vec4(resultant, 1.0);
}