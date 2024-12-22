package visual;

import org.joml.Matrix4f;
import org.joml.Vector3f;

public class Camera {
	// variables concerning the camera
	// fov to represent range of view
	private float fov;
	// width and height for window width and height
	private int windowWidth;
	private int windowHeight;
	// aspect ratio that is the ratio of width to height
	private float aspectRatio;

	// variables that represent the camera's state
	private Vector3f position;
	private Vector3f rotation;
	private Vector3f velocity;

	// move speed scalar variable to adjust camera move speed
	private float moveSpeed;

	// camera constructor
	// fov is initialized to a constant of 60 (in rad)
	// width and height are ported over
	// WARNING: REQUIRES OPENGL CONTEXT TO ALREADY HAVE BEEN CREATED
	public Camera(int width, int height) {
		// set fov to constant
		fov = (float) Math.toRadians(90);
		// port over width and height
		this.windowWidth = width;
		this.windowHeight = height;
		// divide the 2 to get aspectRatio
		aspectRatio = (float) windowWidth / windowHeight;

		// send the projection matrix here
		// the reason is that the projection matrix remains constant despite any meshes
		// so we don't want to be sending it unnecessarily
		// only when the window is created or its size is changed do we want to change
		// it
		Launch.renderer.sendMat4(Util.projectionMatrix, Util.genProjectionMatrix(fov, aspectRatio), false);

		// initialize position, velocity, and rotation
		position = new Vector3f(0, 500, 0);
		rotation = new Vector3f(0, 0, 0);
		velocity = new Vector3f(0, 0, 0);

		// initialize it to 2
		moveSpeed = 2.0f;
	}

	// this method resends the projection matrix after a change in window width or
	// height (due to user shifting)
	// WARNING: REQUIRES OPENGL CONTEXT TO ALREADY HAVE BEEN CREATED
	public void windowChanged(int newWidth, int newHeight) {
		// it is here we update the transformation matrix
		// update window width and height
		this.windowWidth = newWidth;
		this.windowHeight = newHeight;
		// recalculate aspect ratio
		aspectRatio = (float) windowWidth / windowHeight;
		// send matrix
		Launch.renderer.sendMat4(Util.projectionMatrix, Util.genProjectionMatrix(fov, aspectRatio), false);
		
		if(Launch.renderer.getClass() == Raytracer.class) {
			Raytracer tracer = (Raytracer) Launch.renderer;
			tracer.windowChanged(newWidth, newHeight);
		}
	}

	// this method updates the mouse's current rotation based on the x and y
	// difference from the cursor position (parameters)
	// WARNING: REQUIRES OPENGL CONTEXT TO ALREADY HAVE BEEN CREATED
	public void mouseMoved(float x, float y) {
		// 1 full window width represents pi/2 rads of rotation on the y axis
		// so 1 pixel means pi/(2 * windowWidth)
		float yConst = (float) Math.PI;
		yConst /= (2 * windowWidth);
		// 1 full window height represents fov rads of rotation of the x axis
		// so 1 pixel means fov/windowHeight
		float xConst = fov;
		xConst /= windowHeight;
		// now we add the rotation to the rotation variable
		// the reason for the negatives is because we are initially staring at the
		// negative z direction, so we are -pi/2 rad from the positive x axis,
		// which is where the euler angle y is measured from
		// so we need to multiply negatively to correctly update the mouse motion
		// same reason for x constant, except with positive z axis, and pi/2 rad from
		// looking direction
		rotation.add(new Vector3f(Util.mouseSensitivity * -xConst * y, Util.mouseSensitivity * -yConst * x, 0));
		// after the change has been applied, send the updated view matrix to the gpu
		Launch.renderer.sendMat4(Util.viewMatrix, genViewMatrix(), false);
	}

	// this method adds velocity to the current velocity variable based on inputs
	public void addVelocity(Vector3f change) {
		velocity.add(change);
	}

	// this method updates the position and velocity of the variable every iteration
	// in the update loop
	// *its an update*
	// WARNING: REQUIRES OPENGL CONTEXT TO ALREADY HAVE BEEN CREATED
	public void update() {
		// here the integration scheme used is explicit euler
		// for our purposes currently it is okay, since we are not dealing with large
		// forces in a tiny time step
		// this could be updated to implement verlet integration further down the line
		position.add(velocity);

		velocity.mul(0.8f);
	}

	public void sendState() {
		// send the updated view matrix to the gpu
		Launch.renderer.sendMat4(Util.viewMatrix, genViewMatrix(), false);
		//send the camera pos to gpu
		Launch.renderer.sendVec3(Util.cameraPos, position);
	}

	// this method generates the transformation represented by the camera's state
	// (position and rotation)
	// used in the vertex shader to transform based on camera state
	public Matrix4f genViewMatrix() {
		Matrix4f viewMatrix = new Matrix4f();
		// first we rotate everything by the negative of the angle
		// then translate by the negative position vector, bringing the camera back to 0
		// position and 0 rotation
		viewMatrix.rotate(-rotation.x, new Vector3f(1, 0, 0)).rotate(-rotation.y, new Vector3f(0, 1, 0))
				.rotate(-rotation.z, new Vector3f(0, 0, 1)).translate(new Vector3f(position).mul(-1.0f));
		// then we return the rotation matrix
		return viewMatrix;
	}
	
	public Matrix4f genCamToWorldMatrix() {
		/*
		return new Matrix4f().rotate(-rotation.x, new Vector3f(1, 0, 0)).rotate(-rotation.y, new Vector3f(0, 1, 0))
				.rotate(-rotation.z, new Vector3f(0, 0, 1)).translate(new Vector3f(position).mul(-1.0f));
		*/
		Matrix4f viewMatrix = new Matrix4f();
		
		viewMatrix.m30(position.x);
		viewMatrix.m31(position.y);
		viewMatrix.m32(position.z);
		viewMatrix.rotateZ(rotation.z);
		viewMatrix.rotateY(rotation.y);
		viewMatrix.rotateX(rotation.x);
		return viewMatrix;
	}

	// this method is a getter for the rotation variable
	// used in input velocity generation
	public Vector3f getRotation() {
		return rotation;
	}

	// getter for the position variable
	public Vector3f getPosition() {
		return new Vector3f(position);
	}

	// getter for vel
	public Vector3f getVelocity() {
		return velocity;
	}

	// getter for ove speed
	public float getMoveSpeed() {
		return moveSpeed;
	}

	public void setMoveSpeed(float moveSpeed) {
		this.moveSpeed = moveSpeed;
	}

	public void setPosition(Vector3f position) {
		this.position = position;
	}

	public void setVelocity(Vector3f velocity) {
		this.velocity = velocity;
	}

	public void addPosition(Vector3f other) {
		position.add(other);
	}
	
	public void setRotation(Vector3f rot) {
		rotation = rot;
	}
	
	public void addRotation(Vector3f rot) {
		rotation.add(rot);
	}
	
	public float getFOV() {
		return fov;
	}
	
	public void setFOV(float n) {
		fov = n;
		Launch.renderer.sendMat4(Util.projectionMatrix, Util.genProjectionMatrix(fov, Launch.window.getAspectRatio()), false);
	}
}
