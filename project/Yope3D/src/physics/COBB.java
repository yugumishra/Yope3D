package physics;

import org.joml.Matrix3f;
import org.joml.Vector3f;

//collision oriented bounding box
public class COBB extends Hull{
	private Vector3f extent;
	
	public COBB(Vector3f extent, float mass, Vector3f position, Vector3f rotation, Vector3f velocity, Vector3f omega) {
		super(position, velocity, mass, rotation, omega);
		this.extent = extent;
	}
	
	//simple getter
	public Vector3f getExtent() {
		return extent;
	}
	
	//inertia tensor for this OBB
	@Override
	public Matrix3f genInertiaTensor() {
		Matrix3f tensor = new Matrix3f();
		float m = getMass();
		tensor.m00(0.3333f * m * (extent.x * extent.x + extent.y * extent.y));
		tensor.m11(0.3333f * m * (extent.z * extent.z + extent.x * extent.x));
		tensor.m22(0.3333f * m * (extent.z * extent.z + extent.y * extent.y));
		return tensor;
	}
}
