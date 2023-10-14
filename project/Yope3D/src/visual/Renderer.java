package visual;

import java.nio.FloatBuffer;
import java.util.HashMap;
import java.util.Map;

import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;

//this class manages the rendering of meshes, and everything related to rendering
public class Renderer {
	// this variable holds the program id that is associated with the shaders
	private int program;
	// this variable holds the mappings from uniform variable names to its integer
	// id in opengl
	private Map<String, Integer> uniforms;

	// constructor for the renderer class
	public Renderer() {
		// initialization of the uniforms variable
		uniforms = new HashMap<String, Integer>();
	}

	// this method adds a uniform to the mappings
	// important because in order to update uniform values, we must have the opengl
	// id associated with it (which itself is associated with its name)
	// in order to access the uniforms in our shaders, we need to have the program
	// id that holds them, which is why the program is now a class variable
	public void addUniform(String name) {
		// get the relevant id from the shader program using the name
		int id = GL20.glGetUniformLocation(program, name);
		// check if invalid
		if (id < 0) {
			// this means an invalid id, meaning the uniform doesn't exist in the shaders
			// we cannot add this invalid id to the mappings
			System.err.println("Uniform " + name + " not found");
			return;
		}
		// now we add the id to the uniforms
		// now we can access each uniform using the map and its name
		uniforms.put(name, id);
	}

	// this method sends a matrix4f instance to the GPU using the uniform name and
	// the Matrix4f instance representing its values
	// important for sending transformation matrices and stuff like that
	public void sendMat4(String name, Matrix4f values) {
		// buffer to hold the values of the matrix
		FloatBuffer buffer = MemoryUtil.memAllocFloat(16);
		// load matrix entries into the buffer
		values.get(buffer);
		// send to the gpu using uniformmatrix4fv
		// the first parameter indicate the uniform id (gotten using map)
		// the second parameter indicates whether or not this matrix should be
		// transposed or not (false for now)
		// the third parameter is just the buffer hold the matrix
		GL20.glUniformMatrix4fv(uniforms.get(name), false, buffer);
		// free the buffer from memory
		MemoryUtil.memFree(buffer);
	}

	// send vec3 using vector3f instance
	public void sendVec3(String name, Vector3f values) {
		// send to the gpu
		GL20.glUniform3f(uniforms.get(name), values.x, values.y, values.z);
	}
	
	// send a float using float
	public void sendFloat(String name, float value) {
		// buffer to hold the value
		FloatBuffer buffer = MemoryUtil.memAllocFloat(1);
		// put the single value into the buffer
		buffer.put(value);
		//send to the gpu
		GL20.glUniform3fv(uniforms.get(name), buffer);
		// free the buffer from memory
		MemoryUtil.memFree(buffer);
	}

	// initializes important things like the shader program and shader objects
	public void init() {
		// create shader object and read it
		int sid = GL20.glCreateShader(GL20.GL_VERTEX_SHADER);
		GL20.glShaderSource(sid, Util.readShader("Assets\\shaders\\vertex.vs"));

		// error check the shader
		// compile shader
		GL20.glCompileShader(sid);
		// check if compiled
		if (GL20.glGetShaderi(sid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			// shader failed to compile
			// print error message and log
			// then exit
			System.err.println("Vertex shader failed to compile, please try again.");
			System.err.println(GL20.glGetShaderInfoLog(sid, 1000));
			GL20.glDeleteShader(sid);
			System.exit(0);
		}

		// same for fragment shader
		int fid = GL20.glCreateShader(GL20.GL_FRAGMENT_SHADER);
		GL20.glShaderSource(fid, Util.readShader("Assets\\shaders\\fragment.fs"));

		// error check
		GL20.glCompileShader(fid);
		if (GL20.glGetShaderi(fid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			// shader failed to compile
			// print error message and log
			// then exit
			System.err.println("Fragment shader failed to compile, please try again.");
			System.err.println(GL20.glGetShaderInfoLog(fid, 1000));
			GL20.glDeleteShader(fid);
			System.exit(0);
		}

		// create a shader program and link the 2 shaders together
		program = GL20.glCreateProgram();
		GL20.glAttachShader(program, sid);
		GL20.glAttachShader(program, fid);

		// check for any link errors
		GL20.glLinkProgram(program);
		if (GL20.glGetProgrami(program, GL20.GL_LINK_STATUS) == GL20.GL_FALSE) {
			// the program did not link
			System.err.println("Shader program failed to link, please try again.");
			System.err.println(GL20.glGetProgramInfoLog(program, 1000));
			GL20.glDeleteProgram(program);
			GL20.glDeleteShader(sid);
			GL20.glDeleteShader(fid);
			System.exit(0);
		}
		// now we can delete the shader objects since they are stored in the program
		// object now
		// first validate the program as fine
		GL20.glValidateProgram(program);
		// then delete the shaders
		GL20.glDeleteShader(sid);
		GL20.glDeleteShader(fid);
		// then use the shader program
		// with this in place, the draw calls will run through the vertex and fragment
		// shaders stored in vertex.vs and fragment.fs
		GL20.glUseProgram(program);

		// now we must create the mappings of uniforms in our shader programs
		// using addUniform
		addUniform(Util.projectionMatrix);
		addUniform(Util.viewMatrix);
		addUniform(Util.lightPos);
		
		//send a light position here, since it is constant
		sendVec3(Util.lightPos, new Vector3f(0, 3, 10));
	}

	// this method is what renders a mesh
	public void render(Mesh m) {
		// check if loaded yet
		if (m.loaded() == false) {
			m.loadMesh();
		}

		// bind to the specific vertex array object
		GL30.glBindVertexArray(m.getVao());

		// enable the formatting so the floats get formatted into 3d vectors each vertex
		GL30.glEnableVertexAttribArray(0);
		GL30.glEnableVertexAttribArray(1);
		GL30.glEnableVertexAttribArray(2);

		// bind to the index buffer object that refers to the indices in GPU memory that
		// refers to the vertices
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, m.getIbo());

		// draw the vertices in memory using glDrawElements
		// using the formatting, this will format the vertex buffer (referenced from the
		// indices) into the appropriate vertices and pass them into
		// the vertex and fragment shader
		// vertex shader runs every vertex, fragment every "pixel"
		GL11.glDrawElements(GL11.GL_TRIANGLES, m.getVertexCount(), GL11.GL_UNSIGNED_INT, 0);

		// unbind from the VBO and VAO
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		GL30.glBindVertexArray(0);
		// disable the formatting
		GL20.glDisableVertexAttribArray(0);
		GL20.glDisableVertexAttribArray(1);
		GL20.glDisableVertexAttribArray(2);
	}

	// this method clears the screen
	public void clear() {
		// clear the color buffer and depth buffer
		GL11.glClear(GL11.GL_COLOR_BUFFER_BIT | GL11.GL_DEPTH_BUFFER_BIT);
	}

	// this method cleans up the renderer's stuff
	public void cleanup() {
		GL20.glDeleteProgram(program);
	}
}
