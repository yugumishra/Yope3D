package visual;

import org.joml.Vector3f;

//encapsulator for lights
//different lights are handled differently
//these classes are for data, processing happens in the fragment shader
public class Light {
	private Vector3f color;
	//intensity + attenuation (linear) + attenuation (quadratic)
	private Vector3f lightCharacteristics;
	
	public Light(Vector3f color, Vector3f lightCharacteristics) {
		this.color = color;
		this.lightCharacteristics = lightCharacteristics;
	}
	
	public Vector3f getColor() {
		return color;
	}
	
	public void setColor(Vector3f n) {
		this.color = n;
	}
	
	public Vector3f getLightCharacteristics() {
		return lightCharacteristics;
	}
	
	public void setLightCharacteristics(Vector3f n) {
		this.lightCharacteristics = n;
	}
}
