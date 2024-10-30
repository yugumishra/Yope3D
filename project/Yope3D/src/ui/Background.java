package ui;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;

import org.joml.Vector2f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;

import visual.Launch;
import visual.Util;

//a basic label that has a solid color, covers the screen, and is a background
public class Background extends Label {

	// handles to vao (holds vbo, ebo) and vbo (holds vertices) & ebo (holds
	// indices)
	int vao;
	int vbo;
	int ebo;

	// handle to mesh
	protected float[] mesh;
	protected int[] indices;

	// color
	float r;
	float g;
	float b;
	
	//visibility
	boolean visibility;
	
	//min and max
	//defines mesh
	protected Vector2f min;
	protected Vector2f max;

	// number to represent how many floats per vertex (changes if in game or nt)
	public static int FLOATS_PER_VERTEX = 8;

	public Background(float r, float g, float b, int level) {
		visibility = true;
		min = new Vector2f(-1.0f, 1.0f);
		max = new Vector2f(1.0f, -1.0f);
		
		// set colors
		this.r = r;
		this.g = g;
		this.b = b;

		// create the mesh
		redefineMesh();

		// load into buffers
		FloatBuffer vertices = MemoryUtil.memAllocFloat(mesh.length);
		vertices.put(mesh);
		vertices.flip();

		IntBuffer indices = MemoryUtil.memAllocInt(this.indices.length);
		indices.put(this.indices);
		indices.flip();

		// create vao and bind
		vao = GL30.glGenVertexArrays();

		GL30.glBindVertexArray(vao);

		// create vbo and bind and buffer the data
		vbo = GL30.glGenBuffers();
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, vbo);
		GL20.glBufferData(GL20.GL_ARRAY_BUFFER, vertices, GL20.GL_STATIC_DRAW);

		// create ebo and bind and buffer the data
		ebo = GL30.glGenBuffers();
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, ebo);
		GL20.glBufferData(GL20.GL_ELEMENT_ARRAY_BUFFER, indices, GL20.GL_STATIC_DRAW);

		// layout the buffer
		GL20.glVertexAttribPointer(0, 3, GL11.GL_FLOAT, false, (FLOATS_PER_VERTEX) * Float.BYTES, 0);
		GL20.glVertexAttribPointer(1, 3, GL11.GL_FLOAT, true, (FLOATS_PER_VERTEX) * Float.BYTES, 3 * Float.BYTES);
		GL20.glVertexAttribPointer(2, 2, GL11.GL_FLOAT, true, (FLOATS_PER_VERTEX) * Float.BYTES, 6 * Float.BYTES);

		// unbind and free
		GL30.glBindVertexArray(0);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		MemoryUtil.memFree(vertices);
		MemoryUtil.memFree(indices);

		depth = level;

		// scripting init
		init();
	}

	@Override
	public void render() {
		Launch.renderer.sendVec3(Util.col, new org.joml.Vector3f(r,g,b));
		Launch.renderer.send1i(Util.state, Util.STATES.UI);
		
		// bind to the vao that contains the vbo and ebo
		GL30.glBindVertexArray(vao);

		// enable the formatting
		GL30.glEnableVertexAttribArray(0);
		GL30.glEnableVertexAttribArray(1);
		GL30.glEnableVertexAttribArray(2);

		// bind to the ebo (to draw the indices)
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, ebo);
		
		// then draw the stuff
		GL11.glDrawElements(GL11.GL_TRIANGLES, indices.length, GL11.GL_UNSIGNED_INT, 0);

		// unbind
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		GL30.glDisableVertexAttribArray(2);
		GL30.glDisableVertexAttribArray(1);
		GL20.glDisableVertexAttribArray(0);
		GL30.glBindVertexArray(0);
	}

	@Override
	public void cleanup() {
		// delete
		GL20.glDeleteBuffers(vbo);
		GL20.glDeleteBuffers(ebo);
		GL30.glDeleteVertexArrays(vao);
	}

	public void reload() {
		// VERY IMPORTANT CALL TO CLEANUP TO PREVENT RECREATION OF BUFFERS
		cleanup();

		// generate new vao, vbo, ebo

		// load into buffers
		FloatBuffer vertices = MemoryUtil.memAllocFloat(mesh.length);
		vertices.put(mesh);
		vertices.flip();

		IntBuffer indices = MemoryUtil.memAllocInt(this.indices.length);
		indices.put(this.indices);
		indices.flip();

		// create vao and bind
		vao = GL30.glGenVertexArrays();

		GL30.glBindVertexArray(vao);

		// create vbo and bind and buffer the data
		vbo = GL30.glGenBuffers();
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, vbo);
		GL20.glBufferData(GL20.GL_ARRAY_BUFFER, vertices, GL20.GL_STATIC_DRAW);

		// create ebo and bind and buffer the data
		ebo = GL30.glGenBuffers();
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, ebo);
		GL20.glBufferData(GL20.GL_ELEMENT_ARRAY_BUFFER, indices, GL20.GL_STATIC_DRAW);

		// layout the buffer
		GL20.glVertexAttribPointer(0, 3, GL20.GL_FLOAT, false, (FLOATS_PER_VERTEX) * Float.BYTES, 0);
		GL20.glVertexAttribPointer(1, 3, GL20.GL_FLOAT, false, (FLOATS_PER_VERTEX) * Float.BYTES, 3 * Float.BYTES);
		GL20.glVertexAttribPointer(1, 2, GL20.GL_FLOAT, false, (FLOATS_PER_VERTEX) * Float.BYTES, 6 * Float.BYTES);
		
		// unbind and free
		GL30.glBindVertexArray(0);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		MemoryUtil.memFree(vertices);
		MemoryUtil.memFree(indices);
	}

	public float getR() {
		return r;
	}

	public float getG() {
		return g;
	}

	public float getB() {
		return b;
	}

	public void updateColor(float r, float g, float b) {
		this.r = r;
		this.g = g;
		this.b = b;
	}

	public int getDepth() {
		return depth;
	}
	
	public void setVisible(boolean n) {
		visibility = n;
	}
	
	public boolean draw() {
		return visibility;
	}
	
	public Vector2f getMin() {
		return min;
	}
	
	public Vector2f getMax() {
		return max;
	}
	
	//redefines mesh to be made of just min and max
	public void redefineMesh() {
		float[] newMesh = new float[FLOATS_PER_VERTEX * 4];
		newMesh[0] = min.x;
		newMesh[1] = min.y;
		
		newMesh[FLOATS_PER_VERTEX] = max.x;
		newMesh[FLOATS_PER_VERTEX + 1] = min.y;
		
		newMesh[2*FLOATS_PER_VERTEX] = max.x;
		newMesh[2*FLOATS_PER_VERTEX + 1] = max.y;
		
		newMesh[3*FLOATS_PER_VERTEX] = min.x;
		newMesh[3*FLOATS_PER_VERTEX + 1] = max.y;
			
		mesh = newMesh;
		
		int[] newIndices = {
				0, 1, 2,  2, 0, 3
		};
		indices = newIndices;
	}
	
	public int getTextured() {
		return Util.STATES.UI;
	}
}
