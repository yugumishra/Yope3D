package visual;

import org.joml.Vector3f;

public class SpotLight extends Light {
	private Vector3f position;
	private Vector3f direction;
	//actually the dot product representation
	private float innerConeAngle;
	private float outerConeAngle;
	
	public SpotLight(Vector3f position, Vector3f direction, float innerConeAngle, float outerConeAngle, Vector3f lightColor, Vector3f lightCharacteristics) {
		super(lightColor, lightCharacteristics);
		this.position = position;
		this.direction = direction;
		this.innerConeAngle = innerConeAngle;
		this.outerConeAngle = outerConeAngle;
	}

	public void setPosition(Vector3f position) {
		this.position = position;
	}

	public void setDirection(Vector3f direction) {
		this.direction = direction;
	}

	public void setInnerConeAngle(float coneAngle) {
		this.innerConeAngle = coneAngle;
	}
	
	public void setOuterConeAngle(float outerConeAngle) { 
		this.outerConeAngle = outerConeAngle;
	}

	public Vector3f getPosition() {
		return position;
	}

	public Vector3f getDirection() {
		return direction;
	}

	public float getInnerConeAngle() {
		return innerConeAngle;
	}
	
	public float getOuterConeAngle() {
		return outerConeAngle;
	}
}
