package visual;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Scanner;

import org.joml.Matrix4f;
import org.joml.Vector3f;

//this class contains utility methods
//that can't be contained anywhere else
//like file reading
public class Util {
	// constants for uniform names
	public static final String projectionMatrix = "projectionMatrix";
	public static final String viewMatrix = "viewMatrix";
	public static final String lightPos = "lightPos";
	public static final String time = "time";
	public static final String cameraPos = "cameraPos";
	public static final String image = "image";
	public static final String modelMatrix = "modelMatrix";
	// constant for mouse sensitivity
	public static final float mouseSensitivity = 0.5f;

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
		// *
		// - | top distance
		// - |
		// * --near---
		// - |
		// - | bottom distance
		// *
		// the top triangle is defined by fov/2 because the full triangle is defined by
		// fov
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

	// this method reads an obj file at the location described in src
	// the obj file data is transcribed into a mesh instance
	public static Mesh readObjFile(String src) {
		// create file instance
		File f = new File(src);
		// create scanner
		Scanner scan = null;
		// try to initialize
		try {
			scan = new Scanner(f);
		} catch (FileNotFoundException e) {
			// error check
			System.err.println("The file " + src + " was not found");
			System.err.println(e.getMessage());
		}
		//lists of each attribute
		List<Float> positions = new ArrayList<Float>();
		List<Float> normals = new ArrayList<Float>();
		List<Float> textureCoordinates = new ArrayList<Float>();
		//list of floats representing the final vertex array (in list form)
		//the way the algo will work is it will interleave the above 3 float arrays into one using the face definitions
		List<Float> vertexes = new ArrayList<Float>();
		//list of indices that map each vertex to their triangles
		//this is done by mapping each string representation of an vertex to an index, and then referencing the map to see if a vertex exists
		//already
		List<Integer> indexes = new ArrayList<Integer>();
		//mapping of string representation of vertices to their indices
		Map<String, Integer> vertsToInds = new HashMap<String, Integer>();
		
		//smooth variable to determine whether or not to implement smooth shading (by averaging the normals)
		//the way this will be done is every time a vertex gets referenced, the one that would have been added has its normal added to the existing normal
		//this way the normals get summed up
		//then all that is needed is a normalization at the end when the vertices array is fully populated
		boolean smooth = true;
		//iterate through the file
		while(scan.hasNext()) {
			String token = scan.next();
			//position check
			if(token.equals("v")) {
				//add the positions on this line to the positions list
				positions.add(Float.valueOf(scan.next()));
				positions.add(Float.valueOf(scan.next()));
				positions.add(Float.valueOf(scan.next()));
			}
			//normal check
			if(token.equals("vn")) {
				//add the positions on this line to the positions list
				normals.add(Float.valueOf(scan.next()));
				normals.add(Float.valueOf(scan.next()));
				normals.add(Float.valueOf(scan.next()));
			}
			//texture check
			if(token.equals("vt")) {
				//add the positions on this line to the positions list
				textureCoordinates.add(Float.valueOf(scan.next()));
				textureCoordinates.add(Float.valueOf(scan.next()));
				//add 0 because of 3D texture referencing for now
				textureCoordinates.add(0.0f);
			}
			//smooth check
			if(token.equals("s")) {
				//smooth is true
				smooth = true;
				//skip the index
				scan.next();
				//continue
				continue;
			}
			//face check
			if(token.equals("f")) {
				//face definition
				//iterate over the 3 vertices
				for(int i =0; i < 3; i++) {
					//split each vertex into its indices that refer to the positions in the attribute lists
					String vertex = scan.next();
					
					//whether or not the model will be shaded smooth is determined by the key placement
					//if shaded smooth, the model will not have the same position but with a different normal
					//so the key placed in the map is different (only including position and texture coordinates)
					//otherwise the whole vertex representation gets placed
					if(smooth) {
						String realKey = vertex.substring(0, vertex.lastIndexOf("/"));
						boolean vertExists = vertsToInds.containsKey(realKey);
						if(vertExists) {
							//get the normal indexed by this vertex
							String[] components = vertex.split("/");
							int normalIndex = Integer.valueOf(components[2]);
							//substract index by one since obj indices are one based
							normalIndex--;
							//since we are doing smooth shading, we need to add this vertex's normals to the existing index
							int index = vertsToInds.get(realKey);
							//get current normal
							float curX = vertexes.get(index* 9 + 3);
							float curY = vertexes.get(index* 9 + 4);
							float curZ = vertexes.get(index* 9 + 5);
							//add this normal to it
							curX += normals.get(normalIndex*3);
							curY += normals.get(normalIndex*3+1);
							curZ += normals.get(normalIndex*3+2);
							//place it back into the vertexes list
							vertexes.set(index*9 + 3, curX);
							vertexes.set(index*9 + 4, curY);
							vertexes.set(index*9 + 5, curZ);
							//add to the indices
							indexes.add(index);
						}else {
							//we don't have an existing vertex for this vertex representation, so we just add this one to it
							//get the vertex
							String[] components = vertex.split("/");
							int positionIndex = Integer.valueOf(components[0]);
							int textureIndex = Integer.valueOf(components[1]);
							int normalIndex = Integer.valueOf(components[2]);
							//subtract one since obj indices are one based
							positionIndex--;
							textureIndex--;
							normalIndex--;
							//place the values into the vertexes list
							int prevSize = vertexes.size();
							//positions
							vertexes.add(positions.get(positionIndex*3));
							vertexes.add(positions.get(positionIndex*3+1));
							vertexes.add(positions.get(positionIndex*3+2));
							//normals
							vertexes.add(normals.get(normalIndex*3));
							vertexes.add(normals.get(normalIndex*3+1));
							vertexes.add(normals.get(normalIndex*3+2));
							//texture coordinate
							vertexes.add(textureCoordinates.get(textureIndex*3));
							vertexes.add(textureCoordinates.get(textureIndex*3+1));
							vertexes.add(textureCoordinates.get(textureIndex*3+2));
							//place this vertex into the indices array and the map
							int index = prevSize /9;
							indexes.add(index);
							//map adding
							vertsToInds.put(realKey, index);
						}
					}else {
						boolean vertExists = vertsToInds.containsKey(vertex);
						if(vertExists) {
							//already exists so no need to create an additional vertex
							indexes.add(vertsToInds.get(vertex));
						}else {
							//we don't have it in the map so it must be defined
							//get the vertex
							String[] components = vertex.split("/");
							int positionIndex = Integer.valueOf(components[0]);
							int textureIndex = Integer.valueOf(components[1]);
							int normalIndex = Integer.valueOf(components[2]);
							//subtract one since obj indices are one based
							positionIndex--;
							textureIndex--;
							normalIndex--;
							//place the values into the vertexes list
							int prevSize = vertexes.size();
							//positions
							vertexes.add(positions.get(positionIndex*3));
							vertexes.add(positions.get(positionIndex*3+1));
							vertexes.add(positions.get(positionIndex*3+2));
							//normals
							vertexes.add(normals.get(normalIndex*3));
							vertexes.add(normals.get(normalIndex*3+1));
							vertexes.add(normals.get(normalIndex*3+2));
							//texture coordinate
							vertexes.add(textureCoordinates.get(textureIndex*3));
							vertexes.add(textureCoordinates.get(textureIndex*3+1));
							vertexes.add(textureCoordinates.get(textureIndex*3+2));
							//place this vertex into the indices array and the map
							int index = prevSize /9;
							indexes.add(index);
							//map adding
							vertsToInds.put(vertex, index);
						}
					}					
				}
			}
		}
		// close to prevent resource leak
		scan.close();
		//create vertices and indices
		float[] vertices = new float[vertexes.size()];
		for(int i =0; i< vertices.length; i++) {
			vertices[i] = vertexes.get(i);
		}
		int[] indices = new int[indexes.size()];
		for(int i =0; i< indices.length; i++) {
			indices[i] = indexes.get(i);
		}
		//normalize the normals
		
		for(int i = 0; i< vertices.length; i+=9) {
			Vector3f normal = new Vector3f(vertices[i+3], vertices[i+4], vertices[i+5]);
			normal.normalize();
			vertices[i+3] = normal.x;
			vertices[i+4] = normal.y;
			vertices[i+5] = normal.z;
		}
		//create the mesh and return it
		Mesh m = new Mesh(vertices, indices);
		return m;
	}
}
