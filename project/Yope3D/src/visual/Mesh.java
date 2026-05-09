package visual;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.util.ArrayList;
import java.util.List;

import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;

import physics.Hull;

//this class encapsulates the data of a mesh and its associated functionality
public class Mesh {
	// represents the vertices that define the mesh
	// stored as one big array
	private float[] vertices;
	// represents the indices of vertices that create the triangles
	private int[] indices;
	// vertex array object id
	// think of it as a pointer to a bucket that contains information about this
	// mesh
	// the bucket contains the vbo
	private int vao;
	// vertex buffer object id
	// basically the id that refers to the vertices that we buffer to the gpu
	private int vbo;
	// index buffer object id
	// basically the id that refers to the indices that we also buffer to the gpu
	private int ibo;
	// represents how many vertices are in this mesh
	private int vertexCount;
	// represents whether or not this mesh is loaded or not
	private boolean loaded;
	// string variable for the filepath of the texture
	private String texture;
	// scale variable
	// indicates how to scale the mesh
	private float scale;
	// indicates the color of the mesh if not textured
	private Vector3f color;
	// a flag for whether or not to be drawn
	// debug mostly
	private boolean drawn;

	private int state;

	// physics hull
	private Hull hull;

	// constructor
	public Mesh(float[] vertices, int[] indices) {
		// set values
		this.vertices = vertices;
		loaded = false;
		this.indices = indices;
		// initialize scale to 1
		scale = 1.0f;
		// initialize color to null to indicate textured
		color = null;
		drawn = true;

		// init a hull (sphere of rad 1)
		hull = null;
	}

	// loads a mesh, using index based rendering
	// WARNING: REQUIRES OPENGL CONTEXT TO BE CREATED ALREADY
	public void loadMesh() {
		// create a buffer to hold the vertices
		FloatBuffer vertexBuffer = MemoryUtil.memAllocFloat(vertices.length);
		vertexBuffer.put(vertices).flip();

		// create a buffer to hold the indices
		IntBuffer indexBuffer = MemoryUtil.memAllocInt(indices.length);
		indexBuffer.put(indices).flip();

		// generate a vertex array object
		vao = GL30.glGenVertexArrays();
		// bind to it so we work on the current vertex array object
		GL30.glBindVertexArray(vao);

		// generate a vertex buffer object
		vbo = GL20.glGenBuffers();
		// bind to it as an array buffer (because we are buffering an array)
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, vbo);
		// buffer the data to the gpu
		// specify static draw to indicate that the vertex data doesn't change once
		// loaded
		GL20.glBufferData(GL20.GL_ARRAY_BUFFER, vertexBuffer, GL20.GL_STATIC_DRAW);

		// generate an index buffer object
		ibo = GL20.glGenBuffers();
		// bind to it as an element arry buffer (because we are buffering an array of
		// indices)
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, ibo);
		// buffer the index data to the gpu
		// specify static draw to indicate that the index data doesn't change once
		// loaded
		GL20.glBufferData(GL20.GL_ELEMENT_ARRAY_BUFFER, indexBuffer, GL20.GL_STATIC_DRAW);

		// gl vertex attrib pointer specifies the formatting of the vertices

		// positions, index 0
		// a 3d vector of floats
		// that is not normalized
		// total vertex size of 3 floats, and an offset of 0 bytes from the beginning of
		// the vertex
		// this ^ is what the below call formats
		GL20.glVertexAttribPointer(0, 3, GL11.GL_FLOAT, false, Float.BYTES * (3 + 3 + 2), 0);
		// same thing for normals and texture coordinates
		GL20.glVertexAttribPointer(1, 3, GL11.GL_FLOAT, true, Float.BYTES * (3 + 3 + 2), Float.BYTES * (3));
		GL20.glVertexAttribPointer(2, 2, GL11.GL_FLOAT, true, Float.BYTES * (3 + 3 + 2), Float.BYTES * (3 + 3));

		// unbind vertex array object and vertex buffer object to ensure we don't edit
		// the wrong bucket/object
		GL30.glBindVertexArray(0);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);

		// free the buffers from CPU mem
		// now its the GPU's hands, no need to hold it here
		// also good practice to manage memory wisely
		// when meshes get large, it becomes important because it can induce lag if
		// memory management is bad
		MemoryUtil.memFree(vertexBuffer);
		MemoryUtil.memFree(indexBuffer);

		// set the vertex count to be the length of the indices
		vertexCount = indices.length;

		// set loaded to true to indicate that this mesh has been loaded
		loaded = true;

		// load the texture
		Textures.loadTexture(texture);
	}

	// this method sets the filepath for the texture
	public void setTexture(String path) {
		texture = path;
	}

	// returns whether or not this specific mesh is loaded or not
	// important to not be repetitively loading the same mesh into memory
	public boolean loaded() {
		return loaded;
	}

	// used in the draw call for glDrawArrays
	public int getVertexCount() {
		return vertexCount;
	}

	// getter for vao
	// used to access the vertex array object "bucket" that contains the vbo (the
	// vertices)
	public int getVao() {
		return vao;
	}

	// getter for vbo
	// used to access the vertex buffer object that has the vertices
	public int getVbo() {
		return vbo;
	}

	// getter for ibo
	// used to acces the element array buffer object that has the indices
	public int getIbo() {
		return ibo;
	}

	// cleanup method to clear the vertex objects created for this mesh
	public void cleanup() {
		// disable the current vertex array object
		// so it is not being edited
		GL30.glDisableVertexAttribArray(vao);

		// disable the current vertex buffer object
		// so it is not being used
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);

		// then delete the buffers
		GL20.glDeleteBuffers(vbo);
		GL20.glDeleteBuffers(ibo);

		// then unbind the vertex array
		GL30.glBindVertexArray(0);
	}

	// getter for vertices array
	public float[] vertices() {
		return vertices;
	}

	// getter for indices array
	public int[] indices() {
		return indices;
	}

	// getter for texture id
	public String getTexture() {
		return texture;
	}

	// getter for scale
	public float getScale() {
		return scale;
	}

	// setter for scale
	public void setScale(float n) {
		scale = n;
	}

	// getter for color
	public Vector3f getColor() {
		return color;
	}

	// using this method indicates that the mesh is not to be textured and to be
	// colored using texture coordinates (mainly debug)
	public void setColor(float r, float g, float b) {
		this.color = new Vector3f(r, g, b);
	}

	// getter and setter for drawn
	public boolean draw() {
		return drawn;
	}

	public void setDraw(boolean draw) {
		this.drawn = draw;
	}

	public void setState(int s) {
		state = s;
	}

	public int getState() {
		return state;
	}

	public Matrix4f getMM() {
		return hull.getModelMatrix().scale(scale);
	}

	public Matrix3f getNormalMatrix() {
		return hull.genTransform().transpose().invert();
	}

	public Hull getHull() {
		return hull;
	}

	public void redefineHull(Hull n) {
		hull = n;
	}

	public static Mesh cube() {
		float[] nvertices = {-1.0f, 1.0f, 1.0f, -1.0f, -0.0f, -0.0f, 0.625f, 0.0f, -1.0f, -1.0f, -1.0f, -1.0f, -0.0f, -0.0f, 0.375f, 0.25f, -1.0f, -1.0f, 1.0f, -1.0f, -0.0f, -0.0f, 0.375f, 0.0f, -1.0f, 1.0f, -1.0f, -0.0f, -0.0f, -1.0f, 0.625f, 0.25f, 1.0f, -1.0f, -1.0f, -0.0f, -0.0f, -1.0f, 0.375f, 0.5f, -1.0f, -1.0f, -1.0f, -0.0f, -0.0f, -1.0f, 0.375f, 0.25f, 1.0f, 1.0f, -1.0f, 1.0f, -0.0f, -0.0f, 0.625f, 0.5f, 1.0f, -1.0f, 1.0f, 1.0f, -0.0f, -0.0f, 0.375f, 0.75f, 1.0f, -1.0f, -1.0f, 1.0f, -0.0f, -0.0f, 0.375f, 0.5f, 1.0f, 1.0f, 1.0f, -0.0f, -0.0f, 1.0f, 0.625f, 0.75f, -1.0f, -1.0f, 1.0f, -0.0f, -0.0f, 1.0f, 0.375f, 1.0f, 1.0f, -1.0f, 1.0f, -0.0f, -0.0f, 1.0f, 0.375f, 0.75f, 1.0f, -1.0f, -1.0f, -0.0f, -1.0f, -0.0f, 0.375f, 0.5f, -1.0f, -1.0f, 1.0f, -0.0f, -1.0f, -0.0f, 0.125f, 0.75f, -1.0f, -1.0f, -1.0f, -0.0f, -1.0f, -0.0f, 0.125f, 0.5f, -1.0f, 1.0f, -1.0f, -0.0f, 1.0f, -0.0f, 0.875f, 0.5f, 1.0f, 1.0f, 1.0f, -0.0f, 1.0f, -0.0f, 0.625f, 0.75f, 1.0f, 1.0f, -1.0f, -0.0f, 1.0f, -0.0f, 0.625f, 0.5f, -1.0f, 1.0f, -1.0f, -1.0f, -0.0f, -0.0f, 0.625f, 0.25f, 1.0f, 1.0f, -1.0f, -0.0f, -0.0f, -1.0f, 0.625f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, -0.0f, -0.0f, 0.625f, 0.75f, -1.0f, 1.0f, 1.0f, -0.0f, -0.0f, 1.0f, 0.625f, 1.0f, 1.0f, -1.0f, 1.0f, -0.0f, -1.0f, -0.0f, 0.375f, 0.75f, -1.0f, 1.0f, 1.0f, -0.0f, 1.0f, -0.0f, 0.875f, 0.75f};
		int[] nindices = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 0, 18, 1, 3, 19, 4, 6, 20, 7, 9, 21, 10, 12, 22, 13, 15, 23, 16};
		return new Mesh(nvertices, nindices);
	}

	public static Mesh box(Vector3f scale) {
		float[] nvertices = { -1.0f, 1.0f, -1.0f, -0.0f, 1.0f, -0.0f, 0.875f, 0.5f, 1.0f, 1.0f, 1.0f, 0.33333334f,
				0.6666667f, 0.6666667f, 0.625f, 0.75f, 1.0f, 1.0f, -1.0f, 0.81649655f, 0.40824828f, -0.40824828f,
				0.625f, 0.5f, -1.0f, -1.0f, 1.0f, -0.0f, -0.0f, 1.0f, 0.375f, 1.0f, 1.0f, -1.0f, 1.0f, 0.81649655f,
				-0.40824828f, 0.40824828f, 0.375f, 0.75f, -1.0f, 1.0f, 1.0f, -1.0f, -0.0f, -0.0f, 0.625f, 0.0f, -1.0f,
				-1.0f, -1.0f, -0.8944272f, -0.0f, -0.4472136f, 0.375f, 0.25f, -1.0f, -1.0f, 1.0f, -1.0f, -0.0f, -0.0f,
				0.375f, 0.0f, 1.0f, -1.0f, -1.0f, 0.33333334f, -0.6666667f, -0.6666667f, 0.375f, 0.5f, -1.0f, -1.0f,
				1.0f, -0.0f, -1.0f, -0.0f, 0.125f, 0.75f, -1.0f, -1.0f, -1.0f, -0.0f, -1.0f, -0.0f, 0.125f, 0.5f, -1.0f,
				1.0f, -1.0f, -0.4472136f, -0.0f, -0.8944272f, 0.625f, 0.25f, -1.0f, 1.0f, 1.0f, -0.0f, 1.0f, -0.0f,
				0.875f, 0.75f, -1.0f, 1.0f, 1.0f, -0.0f, -0.0f, 1.0f, 0.625f, 1.0f };
		int[] nindices = { 0, 1, 2, 1, 3, 4, 5, 6, 7, 8, 9, 10, 2, 4, 8, 11, 8, 6, 0, 12, 1, 1, 13, 3, 5, 11, 6, 8, 4,
				9, 2, 1, 4, 11, 2, 8 };
		for(int i = 0; i< nvertices.length; i++) {
			if(i % 8 == 0) nvertices[i] *= scale.x;
			if(i % 8 == 1) nvertices[i] *= scale.y;
			if(i % 8 == 2) nvertices[i] *= scale.z;
			
		}
		return new Mesh(nvertices, nindices);
	}

	// public constructor that creates mesh and indices based on input
	public static Mesh genSphere(int subdiv, float radius) {
		// this code will generate an icosphere
		// an icosphere is a sphere approximation generated by taking an icosahedron,
		// subdividing each triangle
		// into 4, then projecting the vertices onto the desired sphere size
		// first read the icosahedron
		// this is a mesh instance for now
		Mesh icosahedron = Util.readObjFile("Assets\\Models\\icosahedron.obj");
		// get vertices
		float[] vertices = icosahedron.vertices();
		for (int i = 0; i < vertices.length; i += 8) {
			// for normal data we can simply reuse point
			// because the normal vector is the gradient of the original curve
			// gradient of x^2 + y^2 + z^2 = r^2
			// gives partial x 2x, partial y 2y, partial z 2z
			// normalization cancels out the 2s and becomes x,y,z, the original point
			// essentially, no extra calculations are necessary to assign normals
			vertices[i + 3] = vertices[i];
			vertices[i + 4] = vertices[i + 1];
			vertices[i + 5] = vertices[i + 2];
		}

		// now subdivide
		// first get the indices for the icosahedron
		int[] indices = icosahedron.indices();
		// create a list to hold the new vertices and indices
		List<Float> newVertices = new ArrayList<Float>();
		List<Integer> newIndices = new ArrayList<Integer>();
		// populate both lists
		for (int i = 0; i < vertices.length; i++)
			newVertices.add(vertices[i]);
		for (int i = 0; i < indices.length; i++)
			newIndices.add(indices[i]);
		// subdivide
		for (int a = 0; a < subdiv; a++) {
			// create a new list to hold the subdivided indices
			List<Integer> subdivIndices = new ArrayList<Integer>();
			for (int i = 0; i < newIndices.size(); i += 3) {

				// grab the 3 indices
				int[] face = { newIndices.get(i), newIndices.get(i + 1), newIndices.get(i + 2) };
				// construct the vector3f instances
				Vector3f[] originalTriangle = new Vector3f[3];
				for (int b = 0; b < face.length; b++) {
					originalTriangle[b] = new Vector3f(newVertices.get(face[b] * 8), newVertices.get(face[b] * 8 + 1),
							newVertices.get(face[b] * 8 + 2));
				}

				// create the other 3 vector3f (midpoints of the 3 edges)
				// and the averaged texture coordinates
				Vector3f[] texCoords = new Vector3f[3];
				Vector3f[] newInstances = new Vector3f[3];
				for (int b = 0; b < originalTriangle.length; b++) {
					// get the 2 points that need to be midpointed
					Vector3f one = originalTriangle[b];
					// mod 3 to wrap back around to 0
					Vector3f two = originalTriangle[(b + 1) % 3];
					// take the difference and half it
					Vector3f diff = new Vector3f(two).sub(one);
					diff.mul(0.5f);
					// then add to one to get the point
					newInstances[b] = new Vector3f(one).add(diff);
					// then normalize
					newInstances[b].normalize();

					// now average the texture coordinates
					Vector3f t1 = new Vector3f(newVertices.get(face[b] * 8 + 6), newVertices.get(face[b] * 8 + 7), 0);
					int x = (b + 1) % 3;
					Vector3f t2 = new Vector3f(newVertices.get(face[x] * 8 + 6), newVertices.get(face[x] * 8 + 7), 0);
					texCoords[b] = new Vector3f(t1).add(t2);
					texCoords[b].mul(0.5f);
				}
				// now each new point has been created, now what is required to add to the
				// vertices list
				// and add the correct indices
				int[] correspondingIndices = new int[3];
				for (int b = 0; b < newInstances.length; b++) {
					// log the index
					correspondingIndices[b] = newVertices.size() / 8;
					// then add the vertex
					newVertices.add(newInstances[b].x);
					newVertices.add(newInstances[b].y);
					newVertices.add(newInstances[b].z);

					// see above for why surface point can be reused for normal
					newVertices.add(newInstances[b].x);
					newVertices.add(newInstances[b].y);
					newVertices.add(newInstances[b].z);

					newVertices.add(texCoords[b].x);
					newVertices.add(texCoords[b].y);
				}

				// now create the indices that connect the points
				// so from 0,1,2
				// to 0,n1,n3 & n1,1,n2 & n1,n2,n3 & n2,n3,2
				subdivIndices.add(face[0]);
				subdivIndices.add(correspondingIndices[0]);
				subdivIndices.add(correspondingIndices[2]);

				subdivIndices.add(correspondingIndices[0]);
				subdivIndices.add(face[1]);
				subdivIndices.add(correspondingIndices[1]);

				subdivIndices.add(correspondingIndices[0]);
				subdivIndices.add(correspondingIndices[1]);
				subdivIndices.add(correspondingIndices[2]);

				subdivIndices.add(correspondingIndices[1]);
				subdivIndices.add(correspondingIndices[2]);
				subdivIndices.add(face[2]);
			}
			// set the new indices to the indices created by the subdivision
			newIndices = subdivIndices;
		}

		// construct the new vertices and indices array to create the mesh instance
		float[] verts = new float[newVertices.size()];
		for (int i = 0; i < verts.length; i++) {
			verts[i] = newVertices.get(i);
		}
		int[] inds = new int[newIndices.size()];
		for (int i = 0; i < inds.length; i++) {
			inds[i] = newIndices.get(i);
		}
		Mesh sphere = new Mesh(verts, inds);
		sphere.setScale(Math.abs(radius));
		return sphere;
	}

	// uv sphere generation method
	// requires divisible by 2 slices parameter
	public static Mesh genSphere(int segments, int slices, float radius) {
		// this method will create rectangular faces around circles that increase and
		// decrease in radius
		// done through double for loop of angles

		// positions can be found through polar -> rectangular conversions
		// normals are just positions
		// texture coordinates are angles mapped to 0->1 (trust)

		// one issue is that due to the linear interpolation opengl does on textures,
		// the last segment (when joined with the first)
		// interpolates from a value close to 1 to 0, which creates this very bad
		// texture distortion
		// to fix this, a duplicate seam is created, so in reality segments + 1 segments
		// are created (that is why segments +1 is always used)

		// list of floats for vertices
		List<Float> vertices = new ArrayList<Float>();

		// generate angle steps
		float thetaStep = (float) Math.PI * 2;
		thetaStep /= segments;
		float phiStep = (float) Math.PI;
		phiStep /= slices;
		// run through the slices (vertical)
		for (int i = -slices / 2; i <= slices / 2; i++) {
			// calculate phi for this iteration
			float phi = i * phiStep;
			// calculate y
			float y = (float) Math.sin(phi);
			// calculate texture coordinate v by normalizing angle from -slices/2 ->
			// slices/2 to 0->1
			float v = (float) (i + slices / 2);
			v /= (float) (slices);
			// calculate the new radius, used for x and z calculations
			float newRadius = (float) Math.cos(phi);
			for (int j = 0; j < segments + 1; j++) {
				// calculate theta for this iteration
				float theta = j * thetaStep;
				// x is just the radius times sin
				float x = newRadius * (float) Math.cos(theta);
				// z is just -radius times cos
				float z = newRadius * (float) Math.sin(theta);

				// calculate texture coordinate u by normalizing angle from 0->segments to 0->1
				float u = (float) (j) / (float) segments;

				// add to vertices
				// positions
				vertices.add(x);
				vertices.add(y);
				vertices.add(z);

				// normals (see above why surface point can be reused for normal)
				vertices.add(x);
				vertices.add(y);
				vertices.add(z);

				// texture coordinates
				vertices.add(u);
				vertices.add(v);
			}
		}

		// list of integers for indices
		List<Integer> indices = new ArrayList<Integer>();
		// iterate through slices and segments
		for (int i = 0; i < slices; i++) {
			for (int j = 0; j < segments + 1; j++) {
				// index is given by i*(segments+1) + j
				int index = i * (segments + 1) + j;

				int plusOne = index + 1;

				// form the quad
				indices.add(index);
				indices.add(index + segments + 1);
				indices.add(plusOne);
				indices.add(plusOne + segments + 1);
				indices.add(index + segments + 1);
				indices.add(plusOne);
			}
		}
		float[] data = new float[vertices.size()];
		for (int i = 0; i < data.length; i++) {
			data[i] = vertices.get(i);
		}
		int[] indexes = new int[indices.size()];
		for (int i = 0; i < indexes.length; i++) {
			indexes[i] = indices.get(i);
		}
		Mesh sphere = new Mesh(data, indexes);
		sphere.setScale(Math.abs(radius));
		return sphere;
	}
}
