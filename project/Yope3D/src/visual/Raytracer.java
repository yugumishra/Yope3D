package visual;

import java.nio.FloatBuffer;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL40;
import org.lwjgl.opengl.GL46;
import org.lwjgl.system.MemoryUtil;

public class Raytracer extends Renderer {

	// handles to shader stuff

	// regular program that draws the 2 triangles onto which the image (computed
	// using raytracing) is drawn
	int program;

	// compute shader program that puts info into the texture that is drawn to the
	// screen (raytracer)
	int computeProgram;

	// texture handle for the image on which the raytracer raytraces and the regular
	// program uses for drawing
	int texture;

	// handle for the image variable in fragment shader (basically where the texture
	// will go in the shader)
	int image;

	// vao and vbo for the 2 triangles
	int vao;
	int vbo;
	int vertexCount;

	@Override
	public void init() {
		// init a basic image shader that draws an image
		int sid = GL20.glCreateShader(GL20.GL_VERTEX_SHADER);
		GL20.glShaderSource(sid,
				"" + "#version 460 core\n" + "" + "layout(location = 0) in vec2 Pos;\n"
						+ "layout(location = 1) in vec2 Tex;\n" + "" + "out vec2 tex;\n" + "" + "void main() {\n"
						+ "	gl_Position = vec4(Pos, 1.0, 1.0);\n" + "	tex = Tex;\n" + "}\n");

		GL20.glCompileShader(sid);
		if (GL20.glGetShaderi(sid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			System.err.println("vertex shader compile fail");
			System.err.println(GL20.glGetShaderInfoLog(sid, 1000));
			GL20.glDeleteShader(sid);
			System.exit(1);
		}

		int fid = GL20.glCreateShader(GL20.GL_FRAGMENT_SHADER);
		GL20.glShaderSource(fid, "" + "#version 460 core\n" + "" + "in vec2 tex;\n" + "" + "uniform sampler2D image;\n"
				+ "void main() {\n" + "	gl_FragColor = vec4(texture(image, tex));\n" + "}\n");
		GL20.glCompileShader(fid);
		if (GL20.glGetShaderi(fid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			System.err.println("fragment shader compile fail");
			System.err.println(GL20.glGetShaderInfoLog(fid, 1000));
			GL20.glDeleteShader(fid);
			System.exit(1);
		}

		program = GL20.glCreateProgram();
		GL20.glAttachShader(program, sid);
		GL20.glAttachShader(program, fid);

		GL20.glLinkProgram(program);
		if (GL20.glGetProgrami(program, GL20.GL_LINK_STATUS) == GL20.GL_FALSE) {
			System.err.println("shader link program fail");
			System.err.println(GL20.glGetProgramInfoLog(program, 1000));
			GL20.glDeleteProgram(program);
			GL20.glDeleteShader(sid);
			GL20.glDeleteShader(fid);
			System.exit(1);
		}
		GL20.glDeleteShader(fid);
		GL20.glDeleteShader(sid);
		GL20.glValidateProgram(program);

		// compute shader program
		int cid = GL40.glCreateShader(GL46.GL_COMPUTE_SHADER);
		GL20.glShaderSource(cid, Util.readShader("Assets\\Shaders\\raytracer.comp"));

		GL20.glCompileShader(cid);
		if (GL20.glGetShaderi(cid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			System.err.println("computer shader compile fail");
			System.err.println(GL20.glGetShaderInfoLog(cid, 1000));
			GL20.glDeleteShader(cid);
			System.exit(1);
		}

		computeProgram = GL20.glCreateProgram();
		GL20.glAttachShader(computeProgram, cid);

		GL20.glLinkProgram(computeProgram);

		if (GL20.glGetProgrami(computeProgram, GL20.GL_LINK_STATUS) == GL20.GL_FALSE) {
			System.err.println("shader link compute program fail");
			System.err.println(GL20.glGetProgramInfoLog(computeProgram, 1000));
			GL20.glDeleteProgram(computeProgram);
			GL20.glDeleteShader(cid);
			System.exit(1);
		}
		GL20.glDeleteShader(cid);

		GL20.glValidateProgram(computeProgram);
		
		GL30.glUseProgram(program);

		// add image uniform
		image = GL30.glGetUniformLocation(program, "image");
		if (image < 0) {
			// image doesn't exist
			System.err.println("fragment doesn't have image (fail)");
		}

		// init the 2 triangles
		float[] vertices = { -1.0f, 1.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,

				1.0f, 1.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f };
		FloatBuffer buff = MemoryUtil.memAllocFloat(vertices.length);
		buff.put(vertices).flip();

		vao = GL30.glGenVertexArrays();
		GL30.glBindVertexArray(vao);

		vbo = GL15.glGenBuffers();
		GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, vbo);
		GL15.glBufferData(GL15.GL_ARRAY_BUFFER, buff, GL15.GL_STATIC_DRAW);
		GL20.glVertexAttribPointer(0, 2, GL11.GL_FLOAT, false, Float.BYTES * (2 + 2), 0);
		GL20.glVertexAttribPointer(1, 2, GL11.GL_FLOAT, true, Float.BYTES * (2 + 2), Float.BYTES * (2));

		GL30.glBindVertexArray(0);
		GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER, 0);
		vertexCount = 6;
		MemoryUtil.memFree(buff);

		texture = initRGBA32F(Launch.window.getWidth(), Launch.window.getHeight());
		
		//add compute program uniforms
		addUniform(Util.viewMatrix);
		addUniform(Util.numLights);
	}

	// same as superclass but instead it looks for uniforms in the compute program
	// (since that's where the actual rendering happens)
	@Override
	public void addUniform(String name) {
		// get the relevant id from the shader program using the name
		int id = GL20.glGetUniformLocation(computeProgram, name);
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

	@Override
	public void render(World w) {
		raytrace(); 
		
		GL20.glUseProgram(program);
		GL30.glBindVertexArray(vao);
		GL20.glEnableVertexAttribArray(0);
		GL20.glEnableVertexAttribArray(1);

		GL20.glUniform1i(image, 1);
		GL20.glActiveTexture(GL20.GL_TEXTURE1);
		GL20.glBindTexture(GL30.GL_TEXTURE_2D, texture);

		GL20.glDrawArrays(GL11.GL_TRIANGLES, 0, vertexCount);

		GL30.glDisableVertexAttribArray(0);
		GL30.glDisableVertexAttribArray(1);
		GL30.glBindVertexArray(0);
	}

	@Override
	public void renderUI() {

	}

	public void raytrace() {
		Camera cam = Launch.window.getCamera();

		GL20.glUseProgram(computeProgram);
		
		//must pass all uniform data here because any calls to gluniform only work on the current program object (and that is the 2 triangle renderer, not the compute shader)
		GL20.glUniformMatrix4fv(4, false, cam.genCamToWorldMatrix().get(new float[16]));
		//perspective info (no matrix necessary lol)
		GL20.glUniform4f(5, cam.getFOV(), Launch.window.getAspectRatio(), 0.01f, 2000f);
		//number of lights in the scene
		GL20.glUniform1i(3, Launch.world.getNumLights());

		GL46.glBindImageTexture(0, texture, 0, false, 0, GL20.GL_WRITE_ONLY, GL46.GL_RGBA32F);

		GL46.glDispatchCompute((int) Math.ceil((float) (Launch.window.getWidth()) / 8.0f),
				(int) Math.ceil((Launch.window.getHeight()) / 4.0f), 1);

		GL46.glMemoryBarrier(GL46.GL_ALL_BARRIER_BITS);
	}

	public static int initRGBA32F(int w, int h) {
		int texture = GL20.glGenTextures();
		GL46.glBindTexture(GL20.GL_TEXTURE_2D, texture);
		GL46.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL13.GL_REPEAT);
		GL46.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL13.GL_REPEAT);
		GL46.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_NEAREST);
		GL46.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
		GL46.glTextureStorage2D(texture, 1, GL46.GL_RGBA32F, w, h);
		GL46.glBindTexture(GL20.GL_TEXTURE_2D, 0);
		return texture;
	}
	
	public void windowChanged(int newWidth, int newHeight) {
		texture = initRGBA32F(newWidth, newHeight);
	}

	@Override
	public void cleanup() {
		GL20.glBindTexture(GL20.GL_TEXTURE_2D, texture);
		GL20.glDeleteTextures(texture);
		GL20.glBindTexture(GL20.GL_TEXTURE_2D, 0);

		GL30.glDisableVertexAttribArray(vao);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glDeleteBuffers(vbo);
		GL30.glBindVertexArray(0);
	}
}
