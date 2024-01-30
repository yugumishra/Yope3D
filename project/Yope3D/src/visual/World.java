package visual;

import java.util.ArrayList;

import org.joml.Vector3f;

import physics.Barrier;
import scripts.Platformer;
import scripts.Script;

//this class encapsulates the world, which contains many meshes
//this class is mainly used to consolidate the various meshes that exist in the world, acting as a getter for all universal meshes
public class World {
	// list that stores all meshes
	private ArrayList<Mesh> meshes;
	
	// list that stores all barriers
	private ArrayList<Barrier> barriers;
	
	// list that stores all the scripts that run in the world
	private ArrayList<Script> scripts;
	
	//global light
	private Vector3f light;
	
	//global collision checker
	private int collisions;
	
	//global dt var
	private float dt;

	// constructor that initiates the mesh list
	public World() {
		meshes = new ArrayList<Mesh>();
		barriers = new ArrayList<Barrier>();
		scripts = new ArrayList<Script>();
		light = new Vector3f(0,0,0);
		collisions = 0;
		dt = 1.0f / 300.0f;
	}

	// the getter for the number of meshes in the world
	public int getNumMeshes() {
		return meshes.size();
	}
	
	// the same but for barriers
	public int getNumBarriers() {
		return barriers.size();
	}
	
	// the same but for scritpsh
	public int getNumScripts() {
		return scripts.size();
	}

	// the getter for each individual mesh
	public Mesh getMesh(int i) {
		// return the mesh at the point at i
		return meshes.get(i);
	}
	
	// the same as above but for barriers
	public Barrier getBarrier(int i) {
		// return the barrier @ i
		return barriers.get(i);
	}
	
	// the same as above but for scripts
	public Script getScript(int i) {
		// return the script @ i
		return scripts.get(i);
	}

	// method that adds a mesh to the world
	public void addMesh(Mesh m) {
		// add mesh to meshes list
		meshes.add(m);
	}
	
	// the same but for barriers
	public void addBarrier(Barrier b) {
		//add
		barriers.add(b);
	}
	
	// the same but for scripts
	public void addScript(Script s) {
		//add
		scripts.add(s);
	}
	
	//world initialization method
	public void init() {
		addScript(new Platformer());
		
		//run the init for each script
		for(Script s: scripts) {
			s.init();
		}
	}

	// world cleanup method
	public void cleanup() {
		// iterate over each mesh
		for (Mesh m : meshes) {
			// cleanup each mesh
			m.cleanup();
		}
	}
	
	public void removeMesh(int index) {
		Mesh m = meshes.remove(index);
		m.cleanup();
	}
	
	public void removeMesh(Mesh m) {
		meshes.remove(m);
		m.cleanup();
	}
	
	public Vector3f getLight() {
		return light;
	}
	
	public void setLight(Vector3f n) {
		light = new Vector3f(n);
	}
	
	public int getCollisions() {
		return collisions;
	}
	
	public void setCollisions(int col) {
		collisions = col;
	}
	
	public float getDT() {
		return dt;
	}
	
	public void setDT(float n) {
		dt = n;
	}
}
