package visual;

import java.util.ArrayList;

import org.joml.Vector3f;

import physics.Barrier;
import physics.Collider;
import physics.CollisionTree;
import physics.Hull;
import physics.Spring;
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

	// list of all lights
	private ArrayList<Light> lights;
	
	//list of all current physics components (right now only springs)
	private ArrayList<Spring> springs;

	// global collision checker
	private int collisions;

	// global dt var
	private float dt;
	
	// collision tree for the world
	private CollisionTree tree;
	
	// constructor that initiates the mesh list
	public World() {
		meshes = new ArrayList<Mesh>();
		springs = new ArrayList<Spring>();
		barriers = new ArrayList<Barrier>();
		scripts = new ArrayList<Script>();
		lights = new ArrayList<Light>();
		collisions = 0;
		dt = 1.0f / 300.0f;
	}
	
	//instantiator for the tree (must be programmer defined min,max,level)
	public void instantiateCollisionTree(Vector3f min, Vector3f max, int level) {
		tree = new CollisionTree(min, max, level);
	}
	
	//accessor for tree method
	public void addObject(Hull h) {
		tree.addObject(h);
	}
	
	//another accessor for tree
	public ArrayList<Hull> getObjects(Hull h) {
		return tree.getObjects(h);
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
		if(m.getClass() == Spring.class) {
			Spring s = (Spring) m;
			if(s.draw()) {
				meshes.add(m);
			}
			springs.add(s);
			return;
		}
		meshes.add(m);
	}

	// the same but for barriers
	public void addBarrier(Barrier b) {
		// add
		barriers.add(b);
	}

	// the same but for scripts
	public void addScript(Script s) {
		// add
		scripts.add(s);
	}

	// world initialization method
	public void init() {
		Script script = null;
		try {
			script = (Script) Launch.toScript.getDeclaredConstructor().newInstance();
		}catch(Exception e) {
			e.printStackTrace();
		}
		if(script == null) {
			System.err.println("Provided a invalid starting script!");
			Launch.game.cleanup();
			System.exit(0);
		}
		addScript(script);

		// run the init for each script
		for (Script s : scripts) {
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

	public void addLight(Light l) {
		lights.add(l);
	}

	public Light getLight(int i) {
		return lights.get(i);
	}

	public void removeLight(Light l) {
		lights.remove(l);
	}

	public void removeLight(int i) {
		lights.remove(i);
	}

	// called if anything regarding the lights has changed
	// responsibility of the programmer to call this whenever they change light info
	public void lightChanged() {
		Launch.renderer.updateLightBuffer();
	}

	public int getNumLights() {
		return lights.size();
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
	
	public void addBarriers(Barrier[] barriers) {
		for(Barrier b: barriers) this.barriers.add(b);
	}

	public void advance() {
		
		// barrier physics
		for (int i = 0; i < barriers.size(); i++) {
			for (int j = 0; j < meshes.size(); j++) {
				Mesh mesh = meshes.get(j);
				if(mesh == null) continue;
				if(mesh.getHull() == null) continue;
				if (mesh.getHull().fixed() == false) {
					if(barriers.get(i) == null) continue;
					Collider.CCDBarrier(mesh.getHull(), barriers.get(i));
				}
			}
		}
		
		//use the tree
		if(tree != null) {
			for(int i =0; i< meshes.size(); i++) {
				Mesh mesh = meshes.get(i);
				if(mesh == null) continue;
				if(mesh.getHull() == null) continue;
				if(mesh.getHull().fixed() == false) {
					//use the tree to get the viable list of colliders
					ArrayList<Hull> possibles = tree.getObjects(mesh.getHull());
					if(possibles != null) {
						for(Hull h: possibles) {
							//do the collision
							Collider.CCD(mesh.getHull(), h);
						}
					}
				}
			}
		}

		// advance one time step
		for (int i = 0; i < meshes.size(); i++) {
			meshes.get(i).getHull().advance();
		}
		
		for(int i = 0; i< springs.size(); i++) {
			springs.get(i).update();
		}

	}
}
