package visual;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL43;
import org.lwjgl.system.MemoryUtil;

import physics.Sphere;

//this class manages the rendering of meshes, and everything related to rendering
public class Renderer {
	// this variable holds the program id that is associated with the vertex &
	// fragment shaders
	private int program;
	// this variable holds the program id that is associated with the compute shader
	private int cProg;
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
		// send to the gpu
		GL20.glUniform3fv(uniforms.get(name), buffer);
		// free the buffer from memory
		MemoryUtil.memFree(buffer);
	}

	// send a integer using integer
	public void send1i(String name, int value) {
		GL20.glUniform1i(uniforms.get(name), value);
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

		// create a shader program and link the 3 shaders together
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
		GL20.glDeleteShader(fid);
		GL20.glDeleteShader(sid);
		// first validate the program as fine
		GL20.glValidateProgram(program);

		GL30.glUseProgram(program);

		// now we can begin the creation of the compute program

		// now we must create the mappings of uniforms in our shader programs
		// using addUniform
		addUniform(Util.projectionMatrix);
		addUniform(Util.viewMatrix);
		addUniform(Util.lightPos);
		addUniform(Util.cameraPos);
		addUniform(Util.image);
		addUniform(Util.modelMatrix);
		addUniform(Util.state);

		// send a light position here
		sendVec3(Util.lightPos, new Vector3f(0, 5000, 5000));

		// send an initial state of 0 (normal) here
		send1i(Util.state, 0);

		// now create the shader program
		// create shader object and read it
		int cid = GL20.glCreateShader(GL43.GL_COMPUTE_SHADER);
		GL20.glShaderSource(cid, Util.readShader("Assets\\shaders\\physicsStep.cs"));

		// error check the shader
		// compile shader
		GL20.glCompileShader(cid);
		// check if compiled
		if (GL20.glGetShaderi(cid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			// shader failed to compile
			// print error message and log
			// then exit
			System.err.println("Compute shader failed to compile, please try again.");
			System.err.println(GL20.glGetShaderInfoLog(cid, 1000));
			GL20.glDeleteShader(cid);
			System.exit(0);
		}

		// create a shader cProg and link the 3 shaders together
		cProg = GL20.glCreateProgram();
		GL20.glAttachShader(cProg, cid);

		// check for any link errors
		GL20.glLinkProgram(cProg);
		if (GL20.glGetProgrami(cProg, GL20.GL_LINK_STATUS) == GL20.GL_FALSE) {
			// the cProg did not link
			System.err.println("Shader cProg failed to link, please try again.");
			System.err.println(GL20.glGetProgramInfoLog(cProg, 1000));
			GL20.glDeleteProgram(cProg);
			GL20.glDeleteShader(cid);
			System.exit(0);
		}

		// now we can delete the shader objects since they are stored in the cProg
		// object now
		GL20.glDeleteShader(cid);

		// first validate the cProg as fine
		GL20.glValidateProgram(cProg);

		GL20.glUseProgram(cProg);
		// send the dt value
		GL20.glUniform1f(1, 1.0f / 300.0f);

		// reenable the drawing portion of the program
		GL20.glUseProgram(program);

		// generate the pipeline
		/*
		 * int pipeline = GL43.glGenProgramPipelines();
		 * 
		 * //specify the stages used GL43.glUseProgramStages(pipeline,
		 * GL43.GL_VERTEX_SHADER_BIT | GL43.GL_FRAGMENT_SHADER_BIT, program);
		 * GL43.glUseProgramStages(pipeline, GL43.GL_COMPUTE_SHADER_BIT, cProg);
		 * 
		 * //then bind GL43.glBindProgramPipeline(pipeline);
		 */
	}

	// this method is what runs the compute shader
	public void compute(World world, int iterations) {
		GL20.glUseProgram(cProg);
		// first get the sphere data from the world
		List<Float> data = new ArrayList<Float>();
		// iterate through meshes
		for (int i = 0; i < world.getNumMeshes(); i++) {
			Mesh m = world.getMesh(i);
			// determine if sphere
			if (m.getClass() == Sphere.class) {
				// this a sphere and needs to be added
				// cast to sphere
				Sphere s = (Sphere) m;
				// add the necessary data
				Vector3f pos = s.getPosition();
				Vector3f vel = s.getVelocity();
				float radius = s.getRadius();
				data.add(pos.x);
				data.add(pos.y);
				data.add(pos.z);
				data.add(radius);
				data.add(vel.x);
				data.add(vel.y);
				data.add(vel.z);
				// add one float of padding
				data.add(0.0f);
			}
		}
		// calculate the number of spheres
		int numSpheres = data.size() / 8;
		if (numSpheres == 0)
			return;

		// create the buffer to hold the values
		ByteBuffer buff = MemoryUtil.memAlloc(data.size() * Float.BYTES);

		// add the values to the buffer
		for (Float d : data) {
			buff.putFloat(d);
		}

		// flip the buffer
		// important in order to be able to be read
		buff.flip();

		// create the ssbo
		int ssbo = GL43.glGenBuffers();

		// bind to the ssbo
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, ssbo);

		// buffer the data
		GL43.glBufferData(GL43.GL_SHADER_STORAGE_BUFFER, buff, GL43.GL_DYNAMIC_DRAW);

		// set the binding index
		GL43.glBindBufferBase(GL43.GL_SHADER_STORAGE_BUFFER, 2, ssbo);

		// now run the actual compute shader
		//send the number of iterations
		GL20.glUniform1i(3, iterations);
		GL43.glDispatchCompute(numSpheres, 1, 1);
		GL43.glMemoryBarrier(GL43.GL_SHADER_STORAGE_BARRIER_BIT);

		// now map the buffer back
		long bufferPointer = GL43.nglMapBuffer(GL43.GL_SHADER_STORAGE_BUFFER, GL43.GL_READ_ONLY);

		// read the buffer to update the meshes
		if (bufferPointer != 0) {
			FloatBuffer floatBuffer = MemoryUtil.memFloatBuffer(bufferPointer, numSpheres * 8);
			// access the data using the float buffer
			//iterate through the world
			for (int i = 0; i < world.getNumMeshes(); i++) {
				Mesh m = world.getMesh(i);
				//check if sphere instance
				if (m.getClass() == Sphere.class) {
					// this sphere needs to be updated
					float x = floatBuffer.get();
					float y = floatBuffer.get();
					float z = floatBuffer.get();
					//flush the radius
					floatBuffer.get();
					float vx = floatBuffer.get();
					float vy = floatBuffer.get();
					float vz = floatBuffer.get();
					//flush the final value
					floatBuffer.get();
					//update
					m.setPosition(new Vector3f(x,y,z));
					m.setVelocity(new Vector3f(vx,vy,vz));
					
				}
			}
			// Unmap the buffer
			GL43.glUnmapBuffer(GL43.GL_SHADER_STORAGE_BUFFER);

			// unbind and delete the ssbo
			GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, 0);
			GL43.glDeleteBuffers(ssbo);
		} else {
			// handle mapping failure
			System.err.println("Failed to map buffer.");
		}
	}

	// this method is what renders a mesh
	public void render(Mesh m) {
		// use the vertex.vs & fragment.fs program
		GL20.glUseProgram(program);
		// check if loaded yet
		if (m.loaded() == false) {
			m.loadMesh();
		}

		// send the model matrix for this mesh
		sendMat4(Util.modelMatrix, m.getMM());

		// bind to the specific vertex array object
		GL30.glBindVertexArray(m.getVao());

		// enable the formatting so the floats get formatted into 3d vectors each vertex
		GL30.glEnableVertexAttribArray(0);
		GL30.glEnableVertexAttribArray(1);
		GL30.glEnableVertexAttribArray(2);

		// bind to the index buffer object that refers to the indices in GPU memory that
		// refers to the vertices
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, m.getIbo());

		// send which texture unit the texture object is bound to
		send1i(Util.image, 0);
		// bind to texture unit 0
		GL20.glActiveTexture(GL20.GL_TEXTURE0);
		// bind to the mesh's texture object, binding to texture unit 0 and allowing the
		// sampler2d array to access it
		GL20.glBindTexture(GL30.GL_TEXTURE_2D, Textures.getTexture(m.getTexture()));

		// draw the vertices in memory using glDrawElements
		// using the formatting, this will format the vertex buffer (referenced from the
		// indices) into the appropriate vertices and pass them into
		// the vertex and fragment shader
		// vertex shader runs every vertex, fragment every "pixel"
		GL11.glDrawElements(GL11.GL_TRIANGLES, m.getVertexCount(), GL11.GL_UNSIGNED_INT, 0);

		// unbind from the VBO and VAO
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);

		// disable the formatting
		GL20.glDisableVertexAttribArray(0);
		GL20.glDisableVertexAttribArray(1);
		GL20.glDisableVertexAttribArray(2);
		GL30.glBindVertexArray(0);

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
