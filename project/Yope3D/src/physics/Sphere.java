package physics;

import org.joml.Matrix3f;
import org.joml.Quaternionf;
import org.joml.Vector3f;

//this class encapsulates the behavior of a sphere object
//this class extends mesh to provide additional functionality beyond rendering
//like collisions and mesh generation
public class Sphere extends Hull {
	private float radius;
	
	public Sphere(Vector3f position, Vector3f velocity, float mass, Quaternionf rotation, Vector3f omega) {
		super(position, velocity, mass, rotation, omega);
		radius = 1.0f;
	}
	
	public Sphere(Vector3f position, Vector3f velocity, float mass, Quaternionf rotation, Vector3f omega, float radius) {
		super(position, velocity, mass, rotation, omega);
		this.radius = radius;
	}
	
	public Sphere(Vector3f position, Vector3f velocity, float mass, float radius) {
		super(position, velocity, mass, new Quaternionf().identity(), new Vector3f());
		this.radius = radius;
	}
	
	public float getRadius() {
		return radius;
	}
	
	public void setRadius(float n) {
		radius = n;
	}

	@Override
	public Matrix3f inverseInertiaTensor() {
		float i = 0.4f * getMass() * radius * radius;
		i = (1.0f / i);
		Matrix3f inertiaTensor = new Matrix3f();
		inertiaTensor.m00(i);
		inertiaTensor.m11(i);
		inertiaTensor.m22(i);
		return inertiaTensor;
	}
	
	@Override
	public boolean inside(Vector3f point) {
		return (new Vector3f(point).sub(getPosition()).lengthSquared() < radius * radius);
	}
	
	@Override
	public void detect(Hull other) {
		other.detectCollision(this);
	}

	@Override
	public void detectCollision(Sphere sphere) {
		Collider.detectSphereSphere(this, sphere);
	}

	@Override
	public void detectCollision(AABB aabb) {
		Collider.detectSphereAABB(this, aabb);
	}

	@Override
	public void detectCollision(OBB obb) {
		Collider.detectSphereOBB(this, obb);
	}

	@Override
	public void collide(Hull other) {
		other.handleCollision(this);
	}

	@Override
	public void handleCollision(Sphere sphere) {
		Collider.collideSphereSphere(this, sphere);
	}

	@Override
	public void handleCollision(AABB aabb) {
		Collider.collideSphereAABB(this, aabb);
	}

	@Override
	public void handleCollision(OBB obb) {
		Collider.collideSphereOBB(this, obb);
	}
}
