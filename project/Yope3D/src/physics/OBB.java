package physics;

import org.joml.Matrix3f;
import org.joml.Quaternionf;
import org.joml.Vector3f;

public class OBB extends Hull {
	
	private Vector3f directionScales;

	public OBB(Vector3f position, Vector3f velocity, float mass, Quaternionf rotation, Vector3f omega) {
		super(position, velocity, mass, rotation, omega);
	}
	
	public OBB(Vector3f position, Vector3f velocity, float mass, Vector3f directionScales) {
		super(position, velocity, mass, new Quaternionf().identity(), new Vector3f());
		this.directionScales = directionScales;
	}
	
	public OBB(Vector3f position, Vector3f velocity, float mass) {
		super(position, velocity, mass, new Quaternionf().identity(), new Vector3f());
		//assume cube
		this.directionScales = new Vector3f(1,1,1);
	}
	
	public void setScales(Vector3f n) {
		directionScales = new Vector3f(n);
	}
	
	public Vector3f getScales() {
		return directionScales;
	}
	
	public Vector3f[] getOBBAxes() {
		Matrix3f transform = super.genTransform();
		return new Vector3f[] {
			transform.getColumn(0, new Vector3f()),
			transform.getColumn(1, new Vector3f()),
			transform.getColumn(2, new Vector3f())
		};
	}
	
	public Vector3f[] worldSpace() {
		Vector3f[] points = new Vector3f[8];
		for(int i = 0; i< 8; i++) {
			int dim1 = ((i & 4) != 0) ? -1 : 1;
	        int dim2 = ((i & 2) != 0) ? -1 : 1;
	        int dim3 = ((i & 1) != 0) ? -1 : 1;
	        
	        points[i] = new Vector3f(dim1 * directionScales.get(0), dim2 * directionScales.get(1), dim3 * directionScales.get(2));
		}
		super.transformVerticesToWorldSpace(points);
		return points;
	}
	
	
	@Override
	public Matrix3f inverseInertiaTensor() {
		float m = super.getMass();
		float ixx = (12.0f) / (m * (directionScales.y * directionScales.y + directionScales.z * directionScales.z));
		float iyy = (12.0f) / (m * (directionScales.x * directionScales.x + directionScales.z * directionScales.z));
		float izz = (12.0f) / (m * (directionScales.y * directionScales.y + directionScales.x * directionScales.x));
		
		Matrix3f inertiaTensor = new Matrix3f();
		inertiaTensor.m00(ixx);
		inertiaTensor.m11(iyy);
		inertiaTensor.m22(izz);
		return inertiaTensor;
	}
	
	@Override
	public boolean inside(Vector3f point) {
		Matrix3f inv = super.genTransform().transpose();
		point = new Vector3f(point).sub(super.getPosition()).mul(inv);
		return (point.x >= -directionScales.x && point.x <= directionScales.x) &&
                (point.y >= -directionScales.y && point.y <= directionScales.y) &&
                (point.z >= -directionScales.z && point.z <= directionScales.z);
	}
	
	@Override
	public void detect(Hull other) {
		other.detectCollision(this);
	}

	@Override
	public void detectCollision(Sphere sphere) {
		Collider.detectSphereOBB(sphere, this);
	}

	@Override
	public void detectCollision(AABB aabb) {
		Collider.detectAABBOBB(aabb, this);
	}

	@Override
	public void detectCollision(OBB obb) {
		Collider.detectOBBOBB(this, obb);
	}

	@Override
	public void collide(Hull other) {
		other.handleCollision(this);
	}

	@Override
	public void handleCollision(Sphere sphere) {
		Collider.collideSphereOBB(sphere, this);
	}

	@Override
	public void handleCollision(AABB aabb) {
		Collider.collideAABBOBB(aabb, this);
	}

	@Override
	public void handleCollision(OBB obb) {
		Collider.collideOBBOBB(this, obb);
	}

	
}
