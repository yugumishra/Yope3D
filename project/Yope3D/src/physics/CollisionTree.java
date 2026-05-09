package physics;

import java.util.ArrayList;

import org.joml.Vector3f;

//this class is the handler for the collision tree
//octree, fetches possible collisions with the indicated level of precision
public class CollisionTree {
	private int level;
	private Node root;

	public CollisionTree(Vector3f min, Vector3f max, int level) {
		this.level = level;
		root = new Node(min, max, level);
	}

	// this method adds an object to the tree
	public void addObject(Hull h) {
		root.addObject(h);
	}

	// this method gets the list of object associated bottom of the tree indicated
	// by that hull
	public ArrayList<Hull> getObjects(Hull h) {
		return root.getObjects(h);
	}

	public int getTreeDepth() {
		return level;
	}

	// node class
	// encapsulates a section of world space
	// defined by bounds, and a list of objects in the space
	// has references to its children nodes
	private class Node {
		ArrayList<Hull> objects;
		Vector3f min;
		Vector3f max;
		Node[] children;
		int level;

		Node(Vector3f min, Vector3f max, int level) {
			this.min = min;
			this.max = max;
			this.level = level;
			children = new Node[8];
			objects = new ArrayList<Hull>();
		}

		// this method adds an object to this node's list (if within bounds)
		// and has its children check it too
		void addObject(Hull h) {
			// if its within bounds we add it to this nodes list
			Vector3f diff = new Vector3f(max).sub(min);
			diff.mul(0.5f);
			Vector3f pos = new Vector3f(min).add(diff);
			AABB aabb = new AABB(pos, diff);
			if (Collider.colliding(h, aabb)) {
				objects.add(h);

				if (level == 0) {
					// we have reached the deepest level of the tree, no need to go any further
					return;
				}
				// because the deepest check has not yet been run, check the children
				// determine the appropriate child
				Vector3f midVector = new Vector3f(max).sub(min).mul(0.5f);
				midVector = new Vector3f(min).add(midVector);
				boolean[] alreadyVisited = new boolean[8];
				for(Vector3f v: vectorsToCheck) {
					Vector3f determinationVector = new Vector3f(v).sub(midVector);
					//appropriate child selection
					int index = determineIndex(determinationVector);
					if(alreadyVisited[index]) continue;
					Node appropriateChild = children[index];
					if (appropriateChild == null) {
						// child needs to be instantiated prior to checking
						// the min is always midVector
						Vector3f min = new Vector3f(midVector);
						// max is based on the sign of the determinationVector
						float minX = Math.min(this.min.x, this.max.x);
						float maxX = Math.max(this.min.x, this.max.x);
						float minY = Math.min(this.min.y, this.max.y);
						float maxY = Math.max(this.min.y, this.max.y);
						float minZ = Math.min(this.min.z, this.max.z);
						float maxZ = Math.max(this.min.z, this.max.z);
						Vector3f max = new Vector3f((determinationVector.x >= 0) ? (maxX) : (minX),
								(determinationVector.y >= 0) ? (maxY) : (minY),
								(determinationVector.z >= 0) ? (maxZ) : (minZ));
						appropriateChild = new Node(min, max, this.level - 1);
						children[index] = appropriateChild;
					}
					// have the child check it too
					appropriateChild.addObject(h);
					
					//signify this index has already been visited
					alreadyVisited[index] = true;
				}
			}
		}

		ArrayList<Hull> getObjects(Hull h) {
			//init the return array
			ArrayList<Hull> toReturn = new ArrayList<Hull>();
			// check if within bounds
			Vector3f[] vectorsToCheck = vectorsToCheck(h);
			if (withinBounds(vectorsToCheck, min, max)) {
				// check if we are the end of the tree
				if (level == 0) {
					// we are, return the list right now
					return objects;
				}
				// we are not, just check the children instead
				Vector3f midVector = new Vector3f(max).sub(min).mul(0.5f);
				midVector = new Vector3f(this.min).add(midVector);
				boolean[] alreadyVisited = new boolean[8];
				for(Vector3f v: vectorsToCheck) {
					Vector3f determinationVector = new Vector3f(v).sub(midVector);
					int index = determineIndex(determinationVector);
					if(alreadyVisited[index]) continue;
					Node appropriateChild = children[index];
					if(appropriateChild != null) {
						//System.out.println(appropriateChild.min + ", " + appropriateChild.max);
						//we have to add this child's objects to the to return list
						for(Hull hu: appropriateChild.getObjects(h))  toReturn.add(hu);
						alreadyVisited[index] = true;
					}
				}
			}
			return toReturn;
		}
	}

	private static int determineIndex(Vector3f vector) {
		// evaluate the index based on the sign of the vector
		boolean x = (vector.x >= 0) ? (true) : (false);
		boolean y = (vector.y >= 0) ? (true) : (false);
		boolean z = (vector.z >= 0) ? (true) : (false);

		// simple scheme
		if (x && y && z)
			return 0;
		if (x && y && !z)
			return 1;
		if (x && !y && z)
			return 2;
		if (x && !y && !z)
			return 3;
		if (!x && y && z)
			return 4;
		if (!x && y && !z)
			return 5;
		if (!x && !y && z)
			return 6;
		if (!x && !y && !z)
			return 7;

		return 0;
	}
}
