package visual;

public class Camera {
	//variables concerning the camera
	//fov to represent range of view
	private float fov;
	//width and height for window width and height
	private int windowWidth;
	private int windowHeight;
	//aspect ratio that is the ratio of width to height
	private float aspectRatio;
	
	//camera constructor
	//fov is initialized to a constant of 60 (in rad)
	//width and height are ported over
	//WARNING: REQUIRES OPENGL CONTEXT TO ALREADY HAVE BEEN CREATED
	public Camera(int width, int height) {
		//set fov to constant
		fov = (float) Math.toRadians(60);
		//port over width and height
		this.windowWidth = width;
		this.windowHeight = height;
		//divide the 2 to get aspectRatio
		aspectRatio = (float) windowWidth / windowHeight;
		
		//send the projection matrix here
		//the reason is that the projection matrix remains constant despite any meshes
		//so we don't want to be sending it unnecessarily
		//only when the window is created or its size is changed do we want to change it
		Launch.renderer.sendMat4(Util.projectionMatrix, Util.genProjectionMatrix(fov, aspectRatio));
	}
	
	//getter for fov
	public float getFov() {
		return fov;
	}
	
	//this method resends the projection matrix after a change in window width or height (due to user shifting)
	//WARNING: REQUIRES OPENGL CONTEXT TO ALREADY HAVE BEEN CREATED
	public void windowChanged(int newWidth, int newHeight) {
		//it is here we update the transformation matrix
		this.windowWidth = newWidth;
		this.windowHeight = newHeight;
		aspectRatio = (float) windowWidth/windowHeight;
		Launch.renderer.sendMat4(Util.projectionMatrix, Util.genProjectionMatrix(fov, aspectRatio));
	}
}
