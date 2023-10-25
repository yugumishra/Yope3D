package visual;

import org.joml.Vector3f;

//this class encapsulates some of the properties found in mtl files that 
//define materials used in shading calculations
public class Material {
	//the things being stored right now for material properties
	private Vector3f ambientColor;
	private Vector3f specularColor;
	private float spExponent;
	
	//the constructor
	public Material(Vector3f ambientColor, Vector3f specularColor, float spExponent) {
		this.ambientColor = ambientColor;
		this.specularColor = specularColor;
		this.spExponent = spExponent;
	}
	
	//getter
	public Vector3f getAmbient() {
		return ambientColor;
	}
	
	public Vector3f getSpecular() {
		return specularColor;
	}
	
	public float getExponent() {
		return spExponent;
	}
}
