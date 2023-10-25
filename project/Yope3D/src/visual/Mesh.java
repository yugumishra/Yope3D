package visual;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.util.ArrayList;
import java.util.List;

import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL42;
import org.lwjgl.stb.STBImage;
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
	// id for the texture
	private int tid;
	//position variable
	private Vector3f position;
	//rotation variable
	private Vector3f rotation;

	// constructor
	public Mesh(float[] vertices, int[] indices) {
		//set values
		this.vertices = vertices;
		loaded = false;
		this.indices = indices;
		//since the default model mat has nothing, it is simply the identity
		position = new Vector3f();
		rotation = new Vector3f();
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
		GL20.glVertexAttribPointer(0, 3, GL11.GL_FLOAT, false, Float.BYTES * (3 + 3 + 3), 0);
		// same thing for normals and texture coordinates
		GL20.glVertexAttribPointer(1, 3, GL11.GL_FLOAT, true, Float.BYTES * (3 + 3 + 3), Float.BYTES * (3));
		GL20.glVertexAttribPointer(2, 3, GL11.GL_FLOAT, true, Float.BYTES * (3 + 3 + 3), Float.BYTES * (3 + 3));

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

		// load the textures
		loadTextures();
	}

	// texture loading method
	// loads the textures
    public void loadTextures() {
    	//for now use a constant filepath for testing
    	String[] filepaths = {"Assets\\Textures\\metal_texture.jpg"};
    	
		//bind to texture unit 0, since that is the unit we operate on
		GL13.glActiveTexture(GL13.GL_TEXTURE0);
		//generate the texture id
		tid = GL11.glGenTextures();

		// bind to the generated texture object
		GL11.glBindTexture(GL30.GL_TEXTURE_2D_ARRAY, tid);

		//read the image datas
		//create a list of bytebuffers which will hold each textures data
		List<ByteBuffer> imageBuffers = new ArrayList<ByteBuffer>();
		//create max width and max height variables to denote the maximum width and height to allocate for in storage
		int maxW = 0, maxH = 0;
		// load data using STBImage library
		//but first flip it vertically because image coordinates are different then uv coordinates
		STBImage.stbi_set_flip_vertically_on_load(true);
		//then iterate through the filepaths available
		for(int i = 0; i< filepaths.length; i++) {
			//create holders for width, height, and channel number
			int[] width = new int[1];
			int[] height = new int[1];
			int[] channels = new int[1];
			//load the image using STBImage load and add it to the buffers
			imageBuffers.add(STBImage.stbi_load(filepaths[i], width, height, channels, 4));
			//check for an increase in maxWidth or maxHeight
			//and set appropriately
			if(width[0] > maxW) {
				maxW = width[0];
			}
			if(height[0] > maxH) {
				maxH = height[0];
			}
		}

		//combine the buffers into one so it can be sent
		ByteBuffer imageBuffer = combine(imageBuffers);
		//calculate the number of mipmaps that will be generated
		int numMipmapLevels = (int) (Math.floor(Math.log(Math.max(maxW, maxH)) / Math.log(2)) + 1);
		//allocate storage for the array of textures
		//first parameter indicates the type of texture
		//the next parameter indicates the number of mipmap levels that will be created
		//the next indicates the type of data that must be allocated for storage
		//the next 2 indicate the texture size of each texture
		//the last parameter indicates the amount of textures
		GL42.glTexStorage3D(GL42.GL_TEXTURE_2D_ARRAY, numMipmapLevels, GL42.GL_RGBA8, maxW, maxH, filepaths.length);

		//send to the gpu using texsubimage3D
		//the first parameter indicates texture type
		//the next indicates level of detail that must be started from (0 for base image)
		//next 3 any texel offsets and texture index offsets
		//the next 3 indicate the length, width, and depth of the texture
		//the next parameter indicates storage type
		//the next indicates type of data being buffered
		//and the last is the actual buffer
		GL30.glTexSubImage3D(GL30.GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, maxW, maxH, filepaths.length, GL42.GL_RGBA, GL11.GL_UNSIGNED_BYTE, imageBuffer);

		// generate the mipmaps on the textures
		//generates automatically the number of mipmaps needed (using the calculation used for numMipmaplevels, which is why that calculation was used)
		GL30.glGenerateMipmap(GL30.GL_TEXTURE_2D_ARRAY);

		// indicate image processing parameters prior to unbinding from the current texture object
		//these 2 calls indicate the wrapping of uv coordinates into 0->1 s and t bounds
		//so the texture repeats
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D_ARRAY, GL11.GL_TEXTURE_WRAP_S, GL13.GL_REPEAT);
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D_ARRAY, GL11.GL_TEXTURE_WRAP_T, GL13.GL_REPEAT);
		//these 2 calls indicate how the pixels should be interpolated through mipmaps
		//this call indicates how mipmaps should be interpolated as the sample space is large relative to the pixel size (aka full texture being called for only a few pixels)
		//aka minimization
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D_ARRAY, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR_MIPMAP_LINEAR);
		//this call indicates how textures (not mipmaps) should be interpolated as the sample space is small relative to the pixel size (15, 16 actual texels being called for the whole screen of pixels)
		//aka magnification
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D_ARRAY, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);

		//then unbind from the texture object
		GL11.glBindTexture(GL30.GL_TEXTURE_2D_ARRAY, 0);

		// free the individual buffers and the big buffer
		for(int i = 0; i< imageBuffers.size(); i++) {
			MemoryUtil.memFree(imageBuffers.get(i));
		}
		MemoryUtil.memFree(imageBuffer);
    }
    
    //this method combines a list of bytebuffers into one
    //used to combine separate textures into one for textures
    private ByteBuffer combine(List<ByteBuffer> buffers) {
    	//create length variable to track how many bytes are in the aggregate
		int length = 0;
		//rewind each buffer and see how many bytes it has left (so from length -> 0 then see how many till length)
		//add it to length
		for(ByteBuffer bb: buffers) {
			bb.rewind();
			length += bb.remaining();
		}
		//create a big byte buffer with the length of all of the other byte buffers
		ByteBuffer buff = MemoryUtil.memAlloc(length);
		//store each buffer into the big one
		for(ByteBuffer bb: buffers) {
			bb.rewind();
			buff.put(bb);
		}
		//reset position back to 0 in the big buffer
		buff.rewind();
		//return it
		return buff;
	}
    
    //this method returns the model matrix that this mesh has
    public Matrix4f getMM() {
    	Matrix4f modelMat = new Matrix4f();
    	modelMat.translate(position)
    	.rotate(rotation.x, new Vector3f(1,0,0))
    	.rotate(rotation.y, new Vector3f(0,1,0))
    	.rotate(rotation.z, new Vector3f(0,0,1));
    	return modelMat;
    }
    
    //this method adds a translation (world) to the position
    public void translate(Vector3f translation) {
    	position.add(translation);
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
	public int getTexID() {
		return tid;
	}
}
