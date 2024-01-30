package physics;

import org.joml.Vector3f;

public class Barrier {
	private Vector3f normal;
	private Vector3f position;
	
	public Barrier(Vector3f normal, Vector3f position) {
		this.normal = normal;
		this.position = position;
	}
	
	public Vector3f getNormal() {
		return normal;
	}
	
	public Vector3f getPosition() {
		return position;
	}
	
	public void setNormal(Vector3f n) {
		normal = n;
	}
	
	public void setPosition(Vector3f n) {
		position = n;
	}
}
