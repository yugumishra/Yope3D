package physics;

import org.joml.Matrix3f;
import org.joml.Quaternionf;
import org.joml.Vector3f;

//encapsulates axis aligned bounding box info
public class AABB extends Hull {
	private Vector3f directionScales;

	public AABB(Vector3f position, Vector3f velocity, float mass) {
		super(position, velocity, mass, new Quaternionf().identity(), new Vector3f());
		//assume cube
		directionScales = new Vector3f(1,1,1f);
	}
	
	public AABB(Vector3f position, Vector3f velocity, float mass, Vector3f directionScales) {
		super(position, velocity, mass, new Quaternionf().identity(), new Vector3f());
		this.directionScales = directionScales;
	}
	
	public AABB(Vector3f position, Vector3f directionScales) {
		super(position, new Vector3f(), Float.POSITIVE_INFINITY, new Quaternionf().identity(), new Vector3f());
		fix();
		this.directionScales = directionScales;
	}
	
	@Override
	public Quaternionf getRotation() {
		return new Quaternionf().identity();
	}
	
	@Override
	public Vector3f getOmega() {
		return new Vector3f();
	}
	
	public void setScales(Vector3f n) {
		directionScales = new Vector3f(n);
	}
	
	public Vector3f getScales() {
		return directionScales;
	}

	@Override
	public Matrix3f inverseInertiaTensor() {
		float ixx = (12.0f) / (getMass() * (directionScales.y * directionScales.y + directionScales.z * directionScales.z));
		float iyy = (12.0f) / (getMass() * (directionScales.x * directionScales.x + directionScales.z * directionScales.z));
		float izz = (12.0f) / (getMass() * (directionScales.y * directionScales.y + directionScales.x * directionScales.x));
		
		Matrix3f inertiaTensor = new Matrix3f();
		inertiaTensor.m00(ixx);
		inertiaTensor.m11(iyy);
		inertiaTensor.m22(izz);
		return inertiaTensor;
	}
	
	@Override
	public boolean inside(Vector3f point) {
		Vector3f pos = super.getPosition();
		Vector3f min = new Vector3f(pos).sub(directionScales);
		Vector3f max = new Vector3f(pos).add(directionScales);
		boolean inX = (point.x >= min.x && point.x <= max.x);
		boolean inY = (point.y >= min.y && point.y <= max.y);
		boolean inZ = (point.z >= min.z && point.z <= max.z);
		return inX &&
                inY &&
                inZ;
	}
	
	@Override
	public void detect(Hull other) {
		other.detectCollision(this);
	}

	@Override
	public void detectCollision(Sphere sphere) {
		Collider.detectSphereAABB(sphere, this);
	}

	@Override
	public void detectCollision(AABB aabb) {
		Collider.detectAABBAABB(aabb, this);
	}

	@Override
	public void detectCollision(OBB obb) {
		Collider.detectAABBOBB(this, obb);
	}

	@Override
	public void collide(Hull other) {
		other.handleCollision(this);
	}

	@Override
	public void handleCollision(Sphere sphere) {
		Collider.collideSphereAABB(sphere, this);
	}

	@Override
	public void handleCollision(AABB aabb) {
		Collider.collideAABBAABB(this, aabb);
	}

	@Override
	public void handleCollision(OBB obb) {
		Collider.collideAABBOBB(this, obb);
	}
	
}
