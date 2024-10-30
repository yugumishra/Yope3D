package physics;

import org.joml.Matrix3f;
import org.joml.Vector3f;

public class CSphere extends Hull{
	private float radius;
	
	public float getRadius() {
		return radius;
	}
	
	public void setRadius(float r) {
		this.radius = r;
	}
	
	public CSphere(float mass, float rad, Vector3f p, Vector3f v, Vector3f r, Vector3f o) {
		super(p,v,mass, r, o);
		this.radius = rad;
	}
	
	//generate inertia tensor for sphere
	@Override
	public Matrix3f genInertiaTensor() {
		Matrix3f tensor = new Matrix3f();
		float i = 0.4f * getMass() * radius * radius;
		tensor.m00(i);
		tensor.m11(i);
		tensor.m22(i);
		return tensor;
	}
}
