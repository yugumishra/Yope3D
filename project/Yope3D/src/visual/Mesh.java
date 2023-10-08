package visual;

import java.nio.FloatBuffer;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;

//this class encapsulates the data of a mesh and its associated functionality
public class Mesh {
	//represents the vertices that define the mesh
	//stored as one big array
	private float[] vertices;
	//vertex array object id
	//think of it as a pointer to a bucket that contains information about this mesh
	//the bucket contains the vbo
	private int vao;
	//vertex buffer object id
	//basically the id that refers to the vertices that we buffer to the gpu
	private int vbo;
	//represents how many vertices are in this mesh
	private int vertexCount;
	//represents whether or not this mesh is loaded or not
	private boolean loaded;
	
	//constructor
	public Mesh(float[] vertices) {
		this.vertices = vertices;
		loaded = false;
	}
	
	//loads a mesh
	//WARNING: REQUIRES OPENGL CONTEXT TO BE CREATED ALREADY
	public void loadMesh() {
		//create a buffer to hold the vertices
		FloatBuffer vertexBuffer = MemoryUtil.memAllocFloat(vertices.length);
		vertexBuffer.put(vertices).flip();
		
		//generate a vertex array object
		vao = GL30.glGenVertexArrays();
		//bind to it so we work on the current vertex array object
		GL30.glBindVertexArray(vao);
		
		//generate a vertex buffer object
		vbo = GL20.glGenBuffers();
		//bind to it as an array buffer (because we are buffering an array)
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, vbo);
		//buffer the data to the gpu
		//specify static draw to indicate that the vertex data doesn't change once loaded
		GL20.glBufferData(GL20.GL_ARRAY_BUFFER, vertexBuffer, GL20.GL_STATIC_DRAW);
		
		//gl vertex attrib pointer specifies the formatting of the vertices
		
		//positions, index 0
		//a 3d vector of floats
		//that is not normalized
		//total vertex size of 3 floats, and an offset of 0 bytes from the beginning of the vertex
		//this ^ is what the below call formats
		GL20.glVertexAttribPointer(0, 3, GL11.GL_FLOAT, false, Float.BYTES * (3), 0);
		
		//unbind vertex array object and vertex buffer object to ensure we don't edit the wrong bucket/object
		GL30.glBindVertexArray(0);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		
		//free the buffer from CPU mem
		//now its the GPU's hands, no need to hold it here
		//also good practice to manage memory wisely
		//when meshes get large, it becomes important because it can induce lag if memory management is bad
		MemoryUtil.memFree(vertexBuffer);
		
		//set the vertex count to be half of the vertices array size
		vertexCount = vertices.length/2;
		//set loaded to true to indicate that this mesh has been loaded
		loaded = true;
	}
	
	//returns whether or not this specific mesh is loaded or not
	//important to not be repetitively loading the same mesh into memory
	public boolean loaded() {
		return loaded;
	}
	
	//used in the draw call for glDrawArrays
	public int getVertexCount() {
		return vertexCount;
	}
	
	//getter for vao
	//used to access the vertex array object "bucket" that contains the vbo (the vertices
	public int getVao() {
		return vao;
	}
	
	//getter for vbo
	//used to access the vertex buffer object that has the vertices
	public int getVbo() {
		return vbo;
	}
	
	//cleanup method to clear the vertex objects created for this mesh
	public void cleanup() {
		//disable the current vertex array object
		//so it is not being edited
		GL30.glDisableVertexAttribArray(vao);
		
		//disable the current vertex buffer object
		//so it is not being used
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		
		//then delete the buffers
		GL20.glDeleteBuffers(vbo);
		
		//then unbind the vertex array
		GL30.glBindVertexArray(0);
	}
}
