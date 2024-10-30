package visual;

import org.joml.Vector3f;

public class PointLight extends Light{
	private Vector3f position;
	public PointLight(Vector3f position, Vector3f lightColor, Vector3f lightCharacteristics) {
		super(lightColor, lightCharacteristics);
		this.position = position;
	}
	
	public Vector3f getPosition() {
		return position;
	}
	
	public void setPosition(Vector3f n) {
		this.position = n;
	}
}
