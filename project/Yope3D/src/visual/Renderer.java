package visual;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;

//this class manages the rendering of meshes, and everything related to rendering
public class Renderer {
	
	//initializes important things like the shader program and shader objects
	public void init() {
		//create shader object and read it
		int sid = GL20.glCreateShader(GL20.GL_VERTEX_SHADER);
		GL20.glShaderSource(sid, Util.readShader("Assets\\shaders\\vertex.vs"));
		
		//error check the shader
		//compile shader
		GL20.glCompileShader(sid);
		//check if compiled
		if(GL20.glGetShaderi(sid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			//shader failed to compile
			//print error message and log
			//then exit
			System.err.println("Vertex shader failed to compile, please try again.");
			System.err.println(GL20.glGetShaderInfoLog(sid, 1000));
			GL20.glDeleteShader(sid);
			System.exit(0);
		}
		
		//same for fragment shader
		int fid = GL20.glCreateShader(GL20.GL_FRAGMENT_SHADER);
		GL20.glShaderSource(fid, Util.readShader("Assets\\shaders\\fragment.fs"));
		
		//error check
		GL20.glCompileShader(fid);
		if(GL20.glGetShaderi(fid, GL20.GL_COMPILE_STATUS) == GL20.GL_FALSE) {
			//shader failed to compile
			//print error message and log
			//then exit
			System.err.println("Fragment shader failed to compile, please try again.");
			System.err.println(GL20.glGetShaderInfoLog(fid, 1000));
			GL20.glDeleteShader(fid);
			System.exit(0);
		}
		
		//create a shader program and link the 2 shaders together
		int program = GL20.glCreateProgram();
		GL20.glAttachShader(program, sid);
		GL20.glAttachShader(program, fid);
		
		//check for any link errors
		GL20.glLinkProgram(program);
		if(GL20.glGetProgrami(program, GL20.GL_LINK_STATUS) == GL20.GL_FALSE) {
			//the program did not link
			System.err.println("Shader program failed to link, please try again.");
			System.err.println(GL20.glGetProgramInfoLog(program, 1000));
			GL20.glDeleteProgram(program);
			GL20.glDeleteShader(sid);
			GL20.glDeleteShader(fid);
			System.exit(0);
		}
		//now we can delete the shader objects since they are stored in the program object now
		//first validate the program as fine
		GL20.glValidateProgram(program);
		//then delete the shaders
		GL20.glDeleteShader(sid);
		GL20.glDeleteShader(fid);
		//then use the shader program
		//with this in place, the draw calls will run through the vertex and fragment shaders stored in vertex.vs and fragment.fs
		GL20.glUseProgram(program);
	}
	
	//this method is what renders a mesh
	public void render(Mesh m) {
		//check if loaded yet
		if(m.loaded() == false) {
			m.loadMesh();
		}
		//bind to the specific vertex array object
		GL30.glBindVertexArray(m.getVao());
		
		//enable the formatting so the floats get formatted into 2d vectors each vertex
		GL30.glEnableVertexAttribArray(0);
		
		//bind to the vertex buffer object that holds the vertices in GPU memory
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, m.getVbo());
		
		//draw the vertices in memory using glDrawArrays
		//using the formatting, this will format the vertex buffer into the appropriate vertices and pass them into
		//the vertex and fragment shader
		//vertex shader runs every vertex, fragment every "pixel"
		GL11.glDrawArrays(GL11.GL_TRIANGLES, 0, m.getVertexCount());
		
		//unbind from the VBO and VAO
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL30.glBindVertexArray(0);
		//disable the formatting
		GL20.glDisableVertexAttribArray(0);
	}
}
