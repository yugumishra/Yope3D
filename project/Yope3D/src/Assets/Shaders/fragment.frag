#version 460 core

in vec3 Pos;
in vec3 Normal;
in vec2 TexCoords;

layout(std430, binding = 1) readonly buffer LightBuffer {
	vec4 lightData[];
};
uniform int numLights;
uniform vec3 cameraPos;

uniform sampler2D image;
//encapsulates any game states into one variable for different shading
uniform int state;
//color of the mesh if not textured
uniform vec3 col;

bool isPositiveInfinity(float value) {
  	return value > 1000000000;
}

void main() {
	int diff = state - 1729;
	//non ui
	if(diff < 0) {
		if(state != 314) {
			vec3 totalDiffuse = vec3(0.0);
			vec3 totalSpecular = vec3(0.0);
			vec3 totalAmbient = vec3(0.0);
			for(int i= 0; i< numLights; i++) {
				vec4 a = lightData[i*3 + 0];
				vec4 b = lightData[i*3 + 1];
				vec4 c = lightData[i*3 + 2];
				//no need to do anything if no light lol
				if(c.x == 0) continue;
				
				

				//intensity (for directional & point)
				float intensity = c.x;
				
				vec3 diffuseColor = vec3(0.0);
				vec3 specularColor = vec3(0.0);
				
				if(!isPositiveInfinity(a.x) && isPositiveInfinity(a.w)) {
					//dist between light and fragment
					float distance = length(a.xyz - Pos);
					
					//calc attenuation
					float attenuation = 1.0/(1+c.y * distance + c.z * distance * distance);
					vec3 col = attenuation * b.xyz;
					//point light
					
			    	//diffuse
			    	vec3 lightDirection = normalize(a.xyz- Pos);
			    	float diffuse = max(dot(lightDirection, Normal), 0.0);
			    	diffuseColor = diffuse * col * intensity;
			    	
			    	//specular
			    	vec3 viewDir = normalize(cameraPos-Pos);
			    	vec3 h = normalize(lightDirection + viewDir);
			    	float specular = pow(max(dot(Normal, h), 0.0), 64);
			    	specularColor = specular * col * intensity;
			    	
		    	}else if(isPositiveInfinity(a.x) && isPositiveInfinity(a.w)) {
		    		//directional light
		    		
		    		//diffuse
		    		vec3 dir = -c.yzw;
		    		
		    		float diffuse = max(dot(Normal, dir), 0.0);
		    		diffuseColor = b.xyz * diffuse * intensity;
		    		//specular
		    		vec3 viewDir = normalize(cameraPos-Pos);
		    		vec3 reflectDir = reflect(-dir, Normal);
		    		float specular = pow(max(dot(viewDir, reflectDir), 0.0), 64);
		    		specularColor = specular * b.xyz * intensity;
		    		
		    	}else if(!isPositiveInfinity(a.x) && !isPositiveInfinity(a.w)) {
					float linearAttenuation = c.y;
					
					float distance = length(a.xyz - Pos);
					
					//calc attenuation
					float attenuation = 1.0/(1+linearAttenuation * distance + c.z * distance * distance);
					vec3 col = attenuation * b.xyz;
					
		    		float co = cos(b.w);
		    		vec3 dir = vec3(co * cos(a.w), sin(b.w), co * sin(a.w));
		    		vec3 lightDir = normalize(vec3(a.xyz - Pos));
		    		float dot = dot(-lightDir, dir);
		    		
		    		
		    		float unpackedLambda = 0.8;
		    		float e = c.w - unpackedLambda;
			    	intensity = clamp((dot - unpackedLambda)/e, 0.0, 1.0);
			    	
		    		float diffuse = max(dot(lightDir, Normal), 0.0);
		    		diffuseColor = diffuse * col * intensity;
		    		
		    		vec3 viewDir = normalize(cameraPos - Pos);
		    		vec3 h = normalize(lightDir + viewDir);
		    		float specular = pow(max(dot(Normal, h), 0.0), 64);
		    		specularColor = specular * col * intensity;
		    	}
		    	
		    	totalAmbient += b.xyz * 0.01;
			    totalSpecular += specularColor;
			    totalDiffuse += diffuseColor;
			}
	    	
	    	//combine into one color
	    	vec3 resultant = vec3(0.0,0.0,0.0);
	    	
	    	//clamp each component
	    	totalAmbient = clamp(totalAmbient, 0, 1);
	    	
	    	totalDiffuse = clamp(totalDiffuse, 0, 1);
	    	
	    	totalSpecular = clamp(totalSpecular, 0, 1);
	    	
	    	//add and combine
	    	resultant = (totalAmbient + totalDiffuse + totalSpecular);
	    	
	    	if(state == 5) {
	    		resultant *= vec3(texture(image, TexCoords));
	    	}else if (state == 2) {
	    		resultant *= col;
	    	}
	    	
	    	//final clamp
	    	resultant = clamp(resultant, 0, 1);
	    	//set
	    	gl_FragColor = vec4(resultant, 1.0);
	    }else {
	    	gl_FragColor = vec4(col, 1.0);
	    }
    }else {
    	//ui components
    	if(diff == 0) {
    		gl_FragColor = vec4(col.x, col.y, col.z, 1.0);
    	}else if(diff == 1) {
    		gl_FragColor = vec4(col, texture(image, TexCoords).r);
    	}else if(diff == 2) {
    		gl_FragColor = vec4(texture(image, TexCoords));
    	}else {
    		gl_FragColor = vec4(0.0,0.0,0.0,1.0);
    	}
    	
    }
}