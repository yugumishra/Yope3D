package physics;

import org.joml.Math;
import org.joml.Matrix3f;
import org.joml.Vector3f;

import visual.Launch;

//class that encapsulates collision hull information
public abstract class Hull {
	private Vector3f position;
	private Vector3f velocity;
	private Vector3f rotation;
	private Vector3f omega;
	private float mass;
	
	//physics flag
	private boolean fix;
	
	public Hull(Vector3f p, Vector3f v, float m, Vector3f r, Vector3f o) {
		this.position = p;
		this.velocity = v;
		this.rotation = r;
		this.omega = o;
		this.mass = m;
		fix = false;
	}
	
	public Vector3f getPosition() {
		return new Vector3f(position);
	}
	
	public Vector3f getVelocity() {
		return new Vector3f(velocity);
	}
	
	public Vector3f getRotation() {
		return new Vector3f(rotation);
	}
	
	public Vector3f getOmega() {
		return new Vector3f(omega);
	}
	
	public float getMass() {
		return mass;
	}
	
	public void setPosition(Vector3f position) {
		this.position = position;
	}

	public void setVelocity(Vector3f velocity) {
		this.velocity = velocity;
	}

	public void setRotation(Vector3f rotation) {
		this.rotation = rotation;
	}

	public void setOmega(Vector3f omega) {
		this.omega = omega;
	}

	public void setMass(float mass) {
		this.mass = mass;
	}

	public void addImpulse(Vector3f impulse) {
		//apply impulse after dividing by mass
		velocity.add(new Vector3f(impulse).div(mass));
		//damp it a lil
		velocity.mul(0.99f);
	}
	
	public void addVelocity(Vector3f change) {
		velocity.add(change);
	}
	
	public void addOmega(Vector3f change) {
		omega.add(change);
	}
	
	public void addAngularImpulse(Vector3f impulse) {
		//check if the impulse is 0 (can happen sometimes due to crossproduct)
		if(Math.abs(impulse.x) > 0.01f && Math.abs(impulse.y) > 0.01f && Math.abs(impulse.z) > 0.01f) {   
			//get the hull's inertia tensor (this method will be overriden by children)
			Matrix3f invInertiaTensor = genInertiaTensor();
			//convert to world coordinates
			Matrix3f transform = genTransform();
			invInertiaTensor = new Matrix3f(transform).mul(invInertiaTensor.mul(transform));
			
			//now apply the impulse
			omega.add(new Vector3f(impulse).mul(invInertiaTensor));
		}
		
		//damping happens even if no impulse is applied (because a 0 impulse is just a minimal interaction)
		//and even minimal interactions still have damping
		//damp it a lil
		omega.mul(0.99f);
	}
	
	public abstract Matrix3f genInertiaTensor();
	
	//generates transformation matrix (rotation only) from the rotation variable
	public Matrix3f genTransform() {
		return new Matrix3f().rotateXYZ(rotation);
	}
	
	public void fix() {
		fix = true;
	}
	
	public void unfix() {
		fix = false;
	}
	
	public boolean fixed() {
		return fix;
	}
	
	//generates model matrix from transformation matrix & position
	public org.joml.Matrix4f getModelMatrix() {
		org.joml.Matrix4f modelMat = new org.joml.Matrix4f();
		modelMat.set3x3(genTransform());
		modelMat.m30(position.x);
		modelMat.m31(position.y);
		modelMat.m32(position.z);
		return modelMat;
	}
	
	public void advance() {
		if(fix) return;
		position.add(new Vector3f(velocity).mul(Launch.world.getDT()));
		rotation.add(new Vector3f(omega).mul(Launch.world.getDT()));
		
		//gravity
		velocity.add(new Vector3f(0, -9.80665f * visual.Launch.world.getDT(), 0));
	}
}
