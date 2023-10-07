package visual;

import java.util.ArrayList;

//this class encapsulates the world, which contains many meshes
//this class is mainly used to consolidate the various meshes that exist in the world, acting as a getter for all universal meshes
public class World {
	//list that stores all meshes
	private ArrayList<Mesh> meshes;
	
	//constructor that initiates the mesh list
	public World() {
		meshes = new ArrayList<Mesh>();
	}
	
	//the getter for the number of meshes in the world
	public int getNumMeshes() {
		return meshes.size();
	}
	
	//the getter for each individual mesh
	public Mesh getMesh(int i) {
		return meshes.get(i);
	}
	
	//method that adds a mesh to the world
	public void addMesh(Mesh m) {
		meshes.add(m);
	}
	
	public void init() {
		for(Mesh m: meshes) {
			m.loadMesh();
		}
	}
}
