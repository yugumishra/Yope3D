package visual;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;

import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;

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
	//string variable for the filepath of the texture
	private String texture;
	//position variable
	private Vector3f position;
	//previous position variable
	private Vector3f velocity;
	//rotation variable
	private Vector3f rotation;
	//scale variable
	//indicates how to scale the mesh
	private float scale;

	// constructor
	public Mesh(float[] vertices, int[] indices) {
		//set values
		this.vertices = vertices;
		loaded = false;
		this.indices = indices;
		//since the default model mat has nothing, it is simply the identity
		position = new Vector3f();
		rotation = new Vector3f();
		velocity = new Vector3f();
		//initialize scale to 1
		scale =1.0f;
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

		// enable the depth test
		// needed for culling faces that are behind other faces
		GL11.glEnable(GL11.GL_DEPTH_TEST);
		// set loaded to true to indicate that this mesh has been loaded
		loaded = true;

		// load the texture
		Textures.loadTexture(texture);
	}
	
	//this method sets the filepath for the texture
	public void setTexture(String path) {
		texture = path;
	}
    
    //this method returns the model matrix that this mesh has
    public Matrix4f getMM() {
    	Matrix4f modelMat = new Matrix4f();
    	modelMat.translate(position)
    	.rotate(rotation.x, new Vector3f(1,0,0))
    	.rotate(rotation.y, new Vector3f(0,1,0))
    	.rotate(rotation.z, new Vector3f(0,0,1))
    	.scale(scale);
    	return modelMat;
    }
    
    //this method adds a translation (world) to the position
    public void translate(Vector3f translation) {
    	//set the initial position
    	position.add(translation);
    }
    
    //this method sets the position variable to a new one
    public void setPosition(Vector3f n) {
    	position = n;
    }
    
    //this method adds a rotation (world) to the rotation
    public void rotate(Vector3f rotation) {
    	this.rotation.add(rotation);
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
	
	// getter for the position of this mesh (as represented by the model matrix)
	public Vector3f getPosition() {
		return new Vector3f(position);
	}
	
	//getter for velocity
	public Vector3f getVelocity() {
		return velocity;
	}
	
	//setter for velocity
	public void setVelocity(Vector3f n) {
		velocity = n;
	}
	
	//updater for velocity
	public void addVelocity(Vector3f n) {
		velocity.add(n);
	}
	
	//getter for scale
	public float getScale() {
		return scale;
	}
	
	//setter for scale
	public void setScale(float n) {
		scale = n;
	}
}
