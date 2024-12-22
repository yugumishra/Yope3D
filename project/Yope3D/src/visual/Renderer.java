package visual;

import java.nio.FloatBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL43;
import org.lwjgl.system.MemoryUtil;

import ui.Label;

//this class manages the rendering of meshes, and everything related to rendering
public class Renderer {
	// this variable holds the program id that is associated with the vertex &
	// fragment shaders
	private int program;
	// this variable holds the mappings from uniform variable names to its integer
	// id in opengl
	public Map<String, Integer> uniforms;
	
	int lightSSBO;

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
		if(uniforms.get(name) == null) return;
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
		
		if(uniforms.get(name) == null) return;
		
		//send to gpu
		GL20.glUniformMatrix3fv(uniforms.get(name), transposed, buffer);
		
		//free buffer
		MemoryUtil.memFree(buffer);
	}

	// send vec3 using vector3f instance
	public void sendVec3(String name, Vector3f values) {
		// send to the gpu
		if(uniforms.get(name) == null) return;
		GL20.glUniform3f(uniforms.get(name), values.x, values.y, values.z);
	}

	// send a float using float
	public void sendFloat(String name, float value) {
		// buffer to hold the value
		FloatBuffer buffer = MemoryUtil.memAllocFloat(1);
		// put the single value into the buffer
		buffer.put(value);
		// send to the gpu
		if(uniforms.get(name) == null) return;
		GL20.glUniform1fv(uniforms.get(name), buffer);
		// free the buffer from memory
		MemoryUtil.memFree(buffer);
	}

	// send a integer using integer
	public void send1i(String name, int value) {
		if(uniforms.get(name) == null) return;
		GL20.glUniform1i(uniforms.get(name), value);
	}
	
	//uniform retrieval
	public int getUniform(String name) {
		return uniforms.get(name);
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
		addUniform(Util.cameraPos);
		addUniform(Util.image);
		addUniform(Util.modelMatrix);
		addUniform(Util.normalMatrix);
		addUniform(Util.state);
		addUniform(Util.col);
		addUniform(Util.numLights);

		// send an initial state of 0 (normal) here
		send1i(Util.state, 0);

		// reenable the drawing portion of the program
		GL20.glUseProgram(program);
	}
	
	//this method is what renders a whole world
	public void render(World w) {
		//enable depth testing because objects have depth
		GL11.glEnable(GL11.GL_DEPTH_TEST);
		for(int i = 0; i< w.getNumMeshes(); i++) {
			Mesh m = w.getMesh(i);
			if(m.draw()) renderMesh(m);
		}
	}

	// this method is what renders a mesh
	public void renderMesh(Mesh m) {
		if (m.draw() == false)
			return;
		// use the vertex.vs & fragment.fs program
		if (GL20.glGetInteger(GL20.GL_CURRENT_PROGRAM) != program)
			GL20.glUseProgram(program);

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
		GL11.glClear(GL11.GL_COLOR_BUFFER_BIT | GL20.GL_DEPTH_BUFFER_BIT);
	}
	
	//this method clears the depth buffer
	public void clearDepth() {
		GL11.glClear(GL20.GL_DEPTH_BUFFER_BIT);
	}

	// this method cleans up the renderer's stuff
	public void cleanup() {
		GL20.glDeleteProgram(program);
	}
	
	//this method renders a ui
	public void renderUI() {
		//disable depth testing (because ui components are flat)
		GL11.glDisable(GL11.GL_DEPTH_TEST);
		// draw the ui
		for (ArrayList<Label> layer : Launch.window.getUI()) {
			for (Label label : layer) {
				if (label.draw()) {
					label.render();
				}
				label.update();
			}
		}
	}

	public int getMainProgram() {
		return program;
	}
	
	//returns the int id for the light ssbo
	public int updateLightBuffer() {
		//create ssbo for light objects
		//we need 3 vec4s (max) for each type of light
		int size = 12 * Launch.world.getNumLights();
		float[] lightData = new float[size];
		for(int i = 0; i< Launch.world.getNumLights(); i++) {
			Light light = Launch.world.getLight(i);
			Vector3f lightColor = light.getColor();
			Vector3f characteristics = light.getLightCharacteristics();
			float theta = 0.0f, phi = 0.0f;
			if(light.getClass() == SpotLight.class && light.getClass() != FlashLight.class) {
				Vector3f direction = ((SpotLight) light).getDirection();
				//convert to theta phi
				theta = (float) Math.atan2(direction.z, direction.x);
				phi = (float) Math.atan2(direction.y, (float) Math.sqrt(direction.x * direction.x + direction.z * direction.z));
			}
			if(light.getClass() != DirectionalLight.class) {
				if(light.getClass() == PointLight.class) { 
					PointLight l = (PointLight) light;
					Vector3f position = l.getPosition();
					lightData[i * 12 + 0] = position.x;
					lightData[i * 12+ 1] = position.y;
					lightData[i * 12 + 2] = position.z;
				}else if(light.getClass() != FlashLight.class) {
					SpotLight l = (SpotLight) light;
					Vector3f position = l.getPosition();
					lightData[i * 12 + 0] = position.x;
					lightData[i * 12 + 1] = position.y;
					lightData[i * 12 + 2] = position.z;
				}else {
					//flashlight
					FlashLight l = (FlashLight) light;
					Vector3f d = l.getDirection();
					lightData[i * 12 + 0] = d.x;
					lightData[i * 12 + 1] = d.y;
					lightData[i * 12 + 2] = d.z;
				}
				lightData[i * 12 + 3] = Float.POSITIVE_INFINITY;
			}else {
				//init to dummy value
				lightData[i * 12 + 0] = Float.POSITIVE_INFINITY;
				lightData[i * 12 + 1] = Float.POSITIVE_INFINITY;
				lightData[i * 12 + 2] = Float.POSITIVE_INFINITY;
				lightData[i * 12 + 3] = Float.POSITIVE_INFINITY;
			}
			if(light.getClass() == SpotLight.class && light.getClass() != FlashLight.class) {
				lightData[i * 12 + 3] = theta;
			}
			
			
			lightData[i * 12 + 4] = lightColor.x;
			lightData[i * 12 + 5] = lightColor.y;
			lightData[i * 12 + 6] = lightColor.z;
			if(light.getClass() == SpotLight.class && light.getClass() != FlashLight.class) {
				lightData[i * 12 + 7] = phi;
			}
			
			if(light.getClass() != DirectionalLight.class) {
				lightData[i * 12 + 8] = characteristics.x;
				lightData[i * 12 + 9] = characteristics.y;
				lightData[i * 12 + 10] = characteristics.z;
			}else {
				DirectionalLight l = (DirectionalLight) light;
				Vector3f dir = l.getDirection();
				lightData[i * 12 + 8] = l.getLightCharacteristics().x;
				lightData[i * 12 + 9] = dir.x;
				lightData[i * 12 + 10] = dir.y;
				lightData[i * 12 + 11] = dir.z;
			}
			if(light.getClass() == SpotLight.class) {
				//cone angles
				//packem into one float
				SpotLight l = (SpotLight) light;
				float innerConeAngle = l.getInnerConeAngle();
				lightData[i*12 + 11] = innerConeAngle;
			}else if(light.getClass() == PointLight.class){
				//dummy value
				lightData[i*12 + 11] = Float.POSITIVE_INFINITY;
			}else if(light.getClass() == FlashLight.class) {
				FlashLight l = (FlashLight) light;
				lightData[i * 12 + 3] = l.getInnerConeAngle();
				lightData[i * 12 + 7] = l.getOuterConeAngle();
				lightData[i * 12 + 8]  = Float.POSITIVE_INFINITY;
				lightData[i * 12 + 9] = l.getLightCharacteristics().x;
				lightData[i * 12 + 10] = l.getLightCharacteristics().y;
				lightData[i * 12 + 11] = l.getLightCharacteristics().z;
			}
		}
		
		//populate buffer
		FloatBuffer lightBuffer = MemoryUtil.memAllocFloat(size);
		lightBuffer.put(lightData);
		
		//flip the buffer for reading
		lightBuffer.flip();
		
		GL30.glDeleteBuffers(lightSSBO);
		
		lightSSBO = GL30.glGenBuffers();
		//bind the buffer obj
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, lightSSBO);
		//buffer the data
		GL43.glBufferData(GL43.GL_SHADER_STORAGE_BUFFER, lightBuffer, GL43.GL_STATIC_READ);
		//set the binding index (always 1)
		GL43.glBindBufferBase(GL43.GL_SHADER_STORAGE_BUFFER, 1, lightSSBO);
		//unbind
		GL43.glBindBuffer(GL43.GL_SHADER_STORAGE_BUFFER, 0);
		
		//send the number of lights here as well
		send1i(Util.numLights, Launch.world.getNumLights());
		
		return lightSSBO;
	}
}
