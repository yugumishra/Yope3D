package visual;

import java.util.ArrayList;

//this class encapsulates the world, which contains many meshes
//this class is mainly used to consolidate the various meshes that exist in the world, acting as a getter for all universal meshes
public class World {
	// list that stores all meshes
	private ArrayList<Mesh> meshes;

	// constructor that initiates the mesh list
	public World() {
		meshes = new ArrayList<Mesh>();
	}

	// the getter for the number of meshes in the world
	public int getNumMeshes() {
		return meshes.size();
	}

	// the getter for each individual mesh
	public Mesh getMesh(int i) {
		// return the mesh at the point at i
		return meshes.get(i);
	}

	// method that adds a mesh to the world
	public void addMesh(Mesh m) {
		// add mesh to meshes list
		meshes.add(m);
	}
	
	//world initialization method
	public void init() {
		
		//create floor mesh
		Mesh floor = Util.readObjFile("Assets\\Models\\floor.obj");
		
		//set texture and add to world
		floor.setTexture("Assets\\Textures\\brick.jpg");
		addMesh(floor);
		
		//load the mesh
		floor.loadMesh();
		
	}

	// world cleanup method
	public void cleanup() {
		// iterate over each mesh
		for (Mesh m : meshes) {
			// cleanup each mesh
			m.cleanup();
		}
	}
}
