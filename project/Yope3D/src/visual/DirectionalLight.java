package visual;

import org.joml.Vector3f;

public class DirectionalLight extends Light{
	private Vector3f direction;
	public DirectionalLight(Vector3f direction, Vector3f lightColor, Vector3f lightCharacteristics) {
		super(lightColor, lightCharacteristics);
		this.direction = direction;
	}
	
	public Vector3f getDirection() {
		return direction;
	}
	
	public void setDirection(Vector3f n) {
		this.direction = n;
	}
}
