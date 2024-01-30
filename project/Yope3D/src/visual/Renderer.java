package visual;

import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL43;
import org.lwjgl.system.MemoryUtil;

import physics.Barrier;
import physics.Sphere;
import visual.Util.STATES;

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
	public void sendMat4(String name, Matrix4f values, boolean transposed) {
		// buffer to hold the values of the matrix
		FloatBuffer buffer = MemoryUtil.memAllocFloat(16);
		// load matrix entries into the buffer
		values.get(buffer);
		// send to the gpu using uniformmatrix4fv
		// the first parameter indicate the uniform id (gotten using map)
		// the second parameter indicates whether or not this matrix should be
		// transposed or not (false for now)
		// the third parameter is just the buffer hold the matrix
		GL20.glUniformMatrix4fv(uniforms.get(name), transposed, buffer);
		// free the buffer from memory
		MemoryUtil.memFree(buffer);
	}
	
	//same as above but for mat3
	public void sendMat3(String name, Matrix3f values, boolean transposed) {
		//buffer to hold the values
		FloatBuffer buffer = MemoryUtil.memAllocFloat(9);
		
		//place into buffer
		values.get(buffer);
		
		//send to gpu
		GL20.glUniformMatrix3fv(uniforms.get(name), transposed, buffer);
		
		//free buffer
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
		GL20.glUniform1fv(uniforms.get(name), buffer);
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
		GL20.glShaderSource(sid, Util.readShader("Assets\\Shaders\\vertex.vert"));

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
		GL20.glShaderSource(fid, Util.readShader("Assets\\Shaders\\fragment.frag"));

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
		addUniform(Util.normalMatrix);
		addUniform(Util.state);
		addUniform(Util.col);

		// send a light position here
		sendVec3(Util.lightPos, new Vector3f(0, 0, 0));

		// send an initial state of 0 (normal) here
		send1i(Util.state, 0);

		// now create the shader program
		// create shader object and read it
		int cid = GL20.glCreateShader(GL43.GL_COMPUTE_SHADER);
		String src = Util.readShader("Assets\\Shaders\\physicsStep.comp");

		GL20.glShaderSource(cid, src);

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
		GL20.glEnable(GL20.GL_CULL_FACE);

		// reenable the drawing portion of the program
		GL20.glUseProgram(program);

		// generate the pipeline

		int pipeline = GL43.glGenProgramPipelines();

		// specify the stages used
		GL43.glUseProgramStages(pipeline, GL43.GL_VERTEX_SHADER_BIT | GL43.GL_FRAGMENT_SHADER_BIT, program);
		GL43.glUseProgramStages(pipeline, GL43.GL_COMPUTE_SHADER_BIT, cProg);

		// then bind
		GL43.glBindProgramPipeline(pipeline);
	}

	// this method is what runs the compute shader
	public void compute(World world, int iterations) {
		if (world.getDT() < 0.0000001f)
			return;
		if (GL20.glGetInteger(GL20.GL_CURRENT_PROGRAM) != cProg)
			GL20.glUseProgram(cProg);
		// send if collisions enabled

		// first get the sphere data from the world
		List<Float> data = new ArrayList<Float>();
		// bbox data variable
		List<Float> bboxData = new ArrayList<Float>();
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
				// position & radius (first vec4)
				data.add(pos.x);
				data.add(pos.y);
				data.add(pos.z);
				data.add(radius);
				// velocity & mass (second vec4)
				data.add(vel.x);
				data.add(vel.y);
				data.add(vel.z);
				data.add(s.getMass());
			} else {
				// check if the extents exist
				// if not then we don't add to the bounding box list
				Vector4f extent = m.getExtent();
				if (extent != null) {
					Matrix4f transform = m.getMM();
					
					transform.m03(extent.x);
					transform.m13(extent.y);
					transform.m23(extent.z);
					
					

					Vector3f velocity = new Vector3f(m.getVelocity());
					Vector3f omega = new Vector3f(m.getAngularVelocity());

					float[] arr = new float[16];
					transform.get(arr);

					for (int j = 0; j < arr.length; j++) {
						bboxData.add(arr[j]);
					}
					bboxData.add(velocity.x);
					bboxData.add(velocity.y);
					bboxData.add(velocity.z);
					// mass
					bboxData.add(extent.w);
					
					//angular velo
					bboxData.add(omega.x);
					bboxData.add(omega.y);
					bboxData.add(omega.z);
					//padding
					bboxData.add(1.0f);
				}
			}
		}
		// calculate the number of spheres
		int numSpheres = data.size() / 8;
		int numBoxes = bboxData.size() / 24;
		if (numSpheres == 0 && numBoxes == 0) {
			return;
		}

		// now get the barrier data (for all possible barriers the spheres might collide
		// with in the world)
		float[] barrierData = new float[world.getNumBarriers() * 8];
		for (int i = 0; i < world.getNumBarriers(); i++) {
			Barrier b = world.getBarrier(i);
			Vector3f position = b.getPosition();
			Vector3f normal = b.getNormal();

			barrierData[i * 8 + 0] = position.x;
			barrierData[i * 8 + 1] = position.y;
			barrierData[i * 8 + 2] = position.z;
			// float of padding
			barrierData[i * 8 + 3] = 0.0f;

			barrierData[i * 8 + 4] = normal.x;
			barrierData[i * 8 + 5] = normal.y;
			barrierData[i * 8 + 6] = normal.z;
			// float of padding
			barrierData[i * 8 + 7] = 0.0f;
		}

		// create the buffer to hold the values
		ByteBuffer buff = MemoryUtil.memAlloc(data.size() * Float.BYTES);

		// create the barrier buffer to hold the barrier data
		ByteBuffer barrierBuff = MemoryUtil.memAlloc(barrierData.length * Float.BYTES);

		// create the bbox buffer to hold the bbox data
		ByteBuffer bboxDataBuffer = MemoryUtil.memAlloc(bboxData.size() * Float.BYTES);

		// add the values to the buffer
		for (Float d : data) {
			buff.putFloat(d);
		}

		// same for barrier data
		for (Float d : barrierData) {
			barrierBuff.putFloat(d);
		}

		// and bbox data
		for (Float d : bboxData) {
			bboxDataBuffer.putFloat(d);
		}

		// flip the buffer
		// important in order to be able to be read
		buff.flip();

		// same for barrier buffer
		barrierBuff.flip();

		// same for bbox data buffer
		bboxDataBuffer.flip();

		// create the ssbo
		int ssbo = GL43.glGenBuffers();

		// bind to the ssbo
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, ssbo);

		// buffer the data
		GL43.glBufferData(GL43.GL_SHADER_STORAGE_BUFFER, buff, GL43.GL_DYNAMIC_DRAW);

		// set the binding index
		GL43.glBindBufferBase(GL43.GL_SHADER_STORAGE_BUFFER, 2, ssbo);

		// unbind prior to barrier buffer operations
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, 0);

		// same for barrier
		int bssbo = GL43.glGenBuffers();
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, bssbo);
		GL43.glBufferData(GL43.GL_SHADER_STORAGE_BUFFER, barrierBuff, GL43.GL_DYNAMIC_DRAW);
		GL43.glBindBufferBase(GL43.GL_SHADER_STORAGE_BUFFER, 4, bssbo);

		// same for bbox
		int bboxSSBO = GL43.glGenBuffers();
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, bboxSSBO);
		GL43.glBufferData(GL43.GL_SHADER_STORAGE_BUFFER, bboxDataBuffer, GL43.GL_DYNAMIC_DRAW);
		GL43.glBindBufferBase(GL43.GL_SHADER_STORAGE_BUFFER, 6, bboxSSBO);

		// send the number of iterations
		GL20.glUniform1i(3, iterations);
		// send the new number of spheres
		GL20.glUniform1i(5, numSpheres);
		// send the number of barriers
		GL20.glUniform1i(7, barrierData.length / 8);
		// send if collisions are enabled
		GL20.glUniform1i(9, world.getCollisions());
		// send the number of bounding boxes
		GL20.glUniform1i(11, 1);
		// send the dt value
		GL20.glUniform1f(1, world.getDT());

		// now run the actual compute shader
		GL43.glDispatchCompute(numSpheres + numBoxes, 1, 1);
		GL43.glMemoryBarrier(GL43.GL_SHADER_STORAGE_BARRIER_BIT);

		// now map the buffer back
		// first bind to the data buffer
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, ssbo);
		long bufferPointer = GL43.nglMapBuffer(GL43.GL_SHADER_STORAGE_BUFFER, GL43.GL_READ_ONLY);
		// no need to map the barrier buffer because it is read only, nothing is written
		// to the barrier buffer

		// read the buffer to update the meshes
		if (bufferPointer != 0) {
			FloatBuffer floatBuffer = MemoryUtil.memFloatBuffer(bufferPointer, numSpheres * 8);
			// access the data using the float buffer
			// iterate through the world
			for (int i = 0; i < world.getNumMeshes(); i++) {
				Mesh m = world.getMesh(i);
				// check if sphere instance
				if (m.getClass() == Sphere.class) {
					// this sphere needs to be updated
					// get the positions
					float x = floatBuffer.get();
					float y = floatBuffer.get();
					float z = floatBuffer.get();
					// flush the radius (read only)
					floatBuffer.get();
					// the velocity
					float vx = floatBuffer.get();
					float vy = floatBuffer.get();
					float vz = floatBuffer.get();
					// flush the mass (read only)
					float mass = floatBuffer.get();

					if (mass == 0) {
						world.removeMesh(m);
					} else {
						// update
						m.setPosition(new Vector3f(x, y, z));
						m.setVelocity(new Vector3f(vx, vy, vz));
					}
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
		
		// same for bbox data
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, bboxSSBO);
		long bboxPointer = GL43.nglMapBuffer(GL43.GL_SHADER_STORAGE_BUFFER, GL43.GL_READ_ONLY);
		
		// read the buffer to update the meshes
		if (bboxPointer != 0) {
			FloatBuffer floatBuffer = MemoryUtil.memFloatBuffer(bboxPointer, numBoxes * 24);
			// access the data using the float buffer
			// iterate through the world
			for (int i = 0; i < world.getNumMeshes(); i++) {
				Mesh m = world.getMesh(i);
				// check if sphere instance
				if(m.getClass() != Sphere.class) {
					Vector4f extent = m.getExtent();
					if(extent != null) {
						//get the matrix
						float[] matrix = new float[16];
						for(int e = 0; e< 16; e++) {
							matrix[e] = floatBuffer.get();
						}
						//set
						Matrix4f mm = new Matrix4f();
						mm.set(matrix);
						m.setMM(mm);
						
						//get the pos
						Vector3f pos = new Vector3f(matrix[12], matrix[13], matrix[14]);
						
						//get vel
						Vector3f vel = new Vector3f(floatBuffer.get(), floatBuffer.get(), floatBuffer.get());
						//flush mass
						float mass = floatBuffer.get();
						
						if(mass != 1.0f && mass != STATES.FLOOR_MASS) System.out.println(mass);
						
						m.setPosition(pos);
						m.setVelocity(vel);
						
						//get angular velocity
						Vector3f angularVelocity = new Vector3f(floatBuffer.get(), floatBuffer.get(), floatBuffer.get());
						m.setAngularVelocity(angularVelocity);
						
						//flush the padding
						floatBuffer.get();
						
						//hehe integrate
						m.rotate(angularVelocity.mul(world.getDT()));
						
					}
					
				}
				
			}
			// Unmap the buffer
			GL43.glUnmapBuffer(GL43.GL_SHADER_STORAGE_BUFFER);

			// unbind and delete the ssbo
			GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, 0);
			GL43.glDeleteBuffers(bboxSSBO);
		} else {
			// handle mapping failure
			System.err.println("Failed to map buffer.");
		}

		// unbind and delete the bssbo
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, 0);
		GL43.glDeleteBuffers(bssbo);

		// free the buffer
		MemoryUtil.memFree(buff);
		MemoryUtil.memFree(barrierBuff);
		MemoryUtil.memFree(bboxDataBuffer);
	}

	// this method is what renders a mesh
	public void render(Mesh m) {
		if (m.draw() == false)
			return;
		// use the vertex.vs & fragment.fs program
		if (GL20.glGetInteger(GL20.GL_CURRENT_PROGRAM) != program)
			GL20.glUseProgram(program);
		
		sendVec3(Util.lightPos, Launch.world.getLight());

		// check if loaded yet
		if (m.loaded() == false) {
			m.loadMesh();
		}
		
		// send the model matrix for this mesh
		sendMat4(Util.modelMatrix, m.getMM(), false);
		// send the normal matrix for this mesh
		sendMat3(Util.normalMatrix, m.getNormalMatrix(), false);
		// send the color for this mesh (if not textured)
		Vector3f color = m.getColor();
		if (color != null) {
			// this model is not textured and the color uniform must be updated with the
			// mesh's color
			sendVec3(Util.col, m.getColor());
		} else {
			sendVec3(Util.col, new Vector3f(0, 0, 0));
		}
		send1i(Util.state, m.getState());

		// bind to the specific vertex array object
		GL30.glBindVertexArray(m.getVao());

		// enable the formatting so the floats get formatted into 3d vectors each vertex
		GL30.glEnableVertexAttribArray(0);
		GL30.glEnableVertexAttribArray(1);
		GL30.glEnableVertexAttribArray(2);

		// bind to the index buffer object that refers to the indices in GPU memory that
		// refers to the vertices
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, m.getIbo());

		if (m.getTexture() != null) {
			// send which texture unit the texture object is bound to
			send1i(Util.image, 0);
			// bind to texture unit 0
			GL20.glActiveTexture(GL20.GL_TEXTURE0);
			// bind to the mesh's texture object, binding to texture unit 0 and allowing the
			// sampler2d array to access it
			GL20.glBindTexture(GL30.GL_TEXTURE_2D, Textures.getTexture(m.getTexture()));
		} else {
			GL20.glBindTexture(GL30.GL_TEXTURE_2D, 0);
		}

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

	public int getMainProgram() {
		return program;
	}
}
