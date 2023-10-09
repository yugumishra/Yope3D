package visual;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Scanner;

import org.joml.Matrix4f;

//this class contains utility methods
//that can't be contained anywhere else
//like file reading
public class Util {
	//constants for uniform names
	public static final String projectionMatrix = "projectionMatrix";
	public static final String viewMatrix = "viewMatrix";
	//constant for mouse sensitivity
	public static final float mouseSensitivity = 0.1f;
	// reads a shader from a source file and returns it as a string
	public static String readShader(String source) {
		// open scanner
		Scanner scan = null;
		// with error caught
		try {
			scan = new Scanner(new File(source));
		} catch (FileNotFoundException e) {
			System.err.println("The file " + source + " could not be found");
		}
		// then read shader
		String shader = "";
		while (scan.hasNextLine()) {
			shader += scan.nextLine() + "\n";
		}
		// close
		scan.close();
		// return
		return shader;
	}

	// creates a frustum projection matrix that clips 3D points onto 2D planes
	// this is done using the joml library and certain input values
	// like the fov of the camera, aspect ratio of the window, the near plane (the
	// closest the points can be to be projected), the far plane (the farthest
	// points can be relative to the camera to be projected)
	// etc
	// basic structure of the matrix is a 4x4 (to stack matrix multiplications in
	// the vertex shader)
	public static Matrix4f genProjectionMatrix(float fov, float aspectRatio) {
		Matrix4f projectionMatrix = new Matrix4f();
		// these 2 define how far in the z-direction the camera can see
		float near = 0.01f;
		float far = 1000.0f;
		// these 2 define how far in the y direction the camera can see (near plane)
		// here the fov variable is used to determine how much the camera can see
		// the fact that the fov is adjustable makes it so that its just not pixel
		// values that determines the top and bottom of the viewing frustum
		// here we use right triangle similar ratio
		//           *
		//       -   | top distance
		// -         |
		//* --near---
		// -         |
		//       -   | bottom distance
		//           *
		//the top triangle is defined by fov/2 because the full triangle is defined by fov
		float top = (float) Math.tan(fov / 2) * near;
		// the bottom is just the negative of the top
		float bottom = -top;
		// the next 2 variables define how far in the x direction the camera can see
		// (near plane)
		// here the aspect ratio comes into play because a wider monitor means a wider
		// view of the x-axis
		float right = top * aspectRatio;
		// again the left is just the negative of the right
		float left = -right;
		// the 4 variables above define the viewing plane for near, but given the far
		// value and near value, the frustum extends far outward than just the near
		// plane
		// its just that these 6 values define the frustum entirely, so there is no need
		// for additional values
		// now we use the JOML method to create the projection matrix
		projectionMatrix.frustum(left, right, bottom, top, near, far);
		return projectionMatrix;
	}
}
