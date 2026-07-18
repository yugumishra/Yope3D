package physics;

import org.joml.Matrix3f;
import org.joml.Quaternionf;
import org.joml.Vector3f;

//class that encapsulates collision hull information
public abstract class Hull {
	private Vector3f position;
	private Vector3f velocity;
	private Quaternionf rotation;
	private Vector3f omega;
	private float mass;
	
	private float inverseMass;
	private Matrix3f inverseInertiaTensor;
	private Matrix3f rotTransform;
	
	private Vector3f linearImpulse = new Vector3f();
	private Vector3f angularImpulse = new Vector3f();
	
	//physics integrate info
	private float dtLeft;
	
	//physics flags
	private boolean fix;
	private boolean gravity;
	public boolean tangible;
	
	public Hull(Vector3f position, Vector3f velocity, float mass, Quaternionf rotation, Vector3f omega) {
		this.position = position;
		this.velocity = velocity;
		this.rotation = rotation;
		this.omega = omega;
		this.mass = mass;
		fix = false;
		gravity = true;
		
		dtLeft = 1.0f;
		
		linearImpulse = new Vector3f();
		angularImpulse = new Vector3f();
		
		inverseMass = 1.0f;
		inverseInertiaTensor = new Matrix3f();
		rotTransform = new Matrix3f();
		
		tangible = true;
	}
	
	public void enableGravity() {
		gravity = true;
	}
	
	public void disableGravity() {
		gravity = false;
	}
	
	public Vector3f getPosition() {
		return new Vector3f(position);
	}
	
	public Vector3f getVelocity() {
		if(fix) return new Vector3f();
		return new Vector3f(velocity);
	}
	
	public Quaternionf getRotation() {
		return new Quaternionf(rotation);
	}
	
	public Vector3f getOmega() {
		if(fix) return new Vector3f();
		return new Vector3f(omega);
	}
	
	public float getMass() {
		if(fix) return Float.POSITIVE_INFINITY;
		return mass;
	}
	
	public float getInverseMass() {
		return inverseMass;
	}
	
	public Matrix3f getInverseInertiaTensorWorld() {
		Matrix3f invInertiaTensor = new Matrix3f(getRotTransform());
		invInertiaTensor.mul(getInverseInertiaTensor());
		invInertiaTensor.mul(new Matrix3f(getRotTransform()).transpose());
		
		return invInertiaTensor;
	}
	
	public Matrix3f getInverseInertiaTensor() {
		return inverseInertiaTensor;
	}
	
	public Matrix3f getRotTransform() {
		return rotTransform;
	}
	
	//used only if you want to change the fixed position (since if fixed, position will not update)
	public void fixPosition(Vector3f position) {
		this.position = position;
	}
	
	//same for rot
	public void fixRotation(Quaternionf rotation) {
		this.rotation = rotation;
	}
	
	public void setPosition(Vector3f position) {
		if(!fix) this.position = position;
	}

	public void setVelocity(Vector3f velocity) {
		if(!fix) this.velocity = velocity;
	}

	public void setRotation(Quaternionf rotation) {
		if(!fix) this.rotation = rotation;
	}

	public void setOmega(Vector3f omega) {
		if(!fix) this.omega = omega;
	}

	public void setMass(float mass) {
		this.mass = mass;
	}

	public void addImpulse(Vector3f impulse) {
		linearImpulse.add(impulse);
	}
	
	public void applyLinearImpulse() {
		if(fix) return;
		//apply impulse after dividing by mass
		velocity.add(new Vector3f(linearImpulse).div(mass));
		
		linearImpulse = new Vector3f();
	}
	
	public void addVelocity(Vector3f change) {
		if(!fix) {
			velocity.add(change);
		}
	}
	
	public void addOmega(Vector3f change) {
		if(!fix) omega.add(change);
	}
	
	public void addAngularImpulse(Vector3f impulse) {
	    angularImpulse.add(impulse);
	}
	
	public void applyAngularImpulse() {
		if (fix || unRotateable(this.getClass())) return;
	    
	    Matrix3f invInertiaTensor = getInverseInertiaTensorWorld();
	    
	    //apply the angular impulse: w += i^-1 * impulse
	    omega.add(new Vector3f(angularImpulse).mul(invInertiaTensor));
	    
	    angularImpulse = new Vector3f();
	}
	
	//generates transformation matrix (rotation only) from the rotation variable
	public Matrix3f genTransform() {
		return new Matrix3f().rotate(rotation);
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
	
	public void applyImpulses() {
		applyLinearImpulse();
		applyAngularImpulse();
	}
	
	public void transformVerticesToWorldSpace(Vector3f[] vertices) {
	    for (int i = 0; i < vertices.length; i++) {
	        vertices[i].rotate(rotation).add(getPosition());
	    }
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
	
	public void advance(float dtPortion) {
		if(fix || !tangible) return;
		
		if(dtPortion > dtLeft) dtPortion = dtLeft;
		
		float dt = dtPortion * visual.Launch.world.getDT();
		
		//gravity
		if(dtLeft == 1.0f) if(gravity) velocity.add(new Vector3f(0, -9.80665f * dt, 0));
		
		applyImpulses();
		
		position.add(new Vector3f(velocity).mul(dt));
		
		Quaternionf dq = new Quaternionf(rotation);
		dq.mul(dt/2);
		dq.mul(-omega.x, -omega.y, -omega.z, 0);
		rotation.add(dq);
		rotation.normalize();
		
		dtLeft -= dtPortion;
	}
	
	//the last call to advance ever made in a single frame
	//current scheme is the collider collides every hull (may need partial advance)
	public void advance() {
		advance(dtLeft);
		dtLeft = 1.0f;
		initiateState();
	}
	
	private void initiateState() {
		linearImpulse = new Vector3f(0, 0, 0);
		angularImpulse = new Vector3f(0, 0, 0);
		inverseInertiaTensor = inverseInertiaTensor();
		rotTransform = genTransform();
		inverseMass = (fix) ? (0.0f) : (1.0f/mass);
	}
	
	public static boolean unRotateable(Class<?> a) {
		return a == AABB.class;
	}
	
	//abstract methods
	public abstract Matrix3f inverseInertiaTensor();
	public abstract boolean inside(Vector3f point);
	
	//abstract collision methtods
	public abstract void detect(Hull other);
	public abstract void detectCollision(Sphere sphere);
	public abstract void detectCollision(AABB aabb);
	public abstract void detectCollision(OBB obb);
	
	public abstract void collide(Hull other);
    public abstract void handleCollision(Sphere sphere);
    public abstract void handleCollision(AABB aabb);
    public abstract void handleCollision(OBB obb);
}
