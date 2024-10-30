package ui;

import java.nio.FloatBuffer;
import java.nio.IntBuffer;
import java.util.ArrayList;
import java.util.List;

import org.joml.Matrix3f;
import org.joml.Matrix4f;
import org.joml.Vector2f;
import org.joml.Vector3f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;

import ui.TextAtlas.Glyph;
import visual.Launch;
import visual.Util;

public class TextBox extends Label {
	protected String message;
	int size;
	protected float[] text;
	protected int[] indices;
	protected Background parent;
	protected int text_config;
	protected Matrix3f objMatrix;
	protected float totalWidth;
	protected float totalHeight;
	TextAtlas atlas;
	boolean draw;

	private Vector3f textColor;

	int vao;
	int vbo;
	int ebo;

	public TextBox(Background parent, String message, int size, int text_config, TextAtlas atlas, boolean draw) {
		this.atlas = atlas;
		this.parent = parent;
		this.text_config = text_config;

		this.message = message;
		this.size = size;

		super.depth = parent.depth + 1;
		load(parent.depth, text_config);

		textColor = new Vector3f(1.0f, 1.0f, 1.0f);
		
		this.draw = draw;

		// scripting init
		init();
	}

	public void generateTextMesh(int text_config) {
		int w = Launch.window.getWidth();
		int h = Launch.window.getHeight();

		List<Glyph> characters = new ArrayList<Glyph>();
		Vector2f origin = new Vector2f(-1.0f, 1.0f);
		Vector2f end = new Vector2f(1.0f, -1.0f);
		if (parent != null) {
			origin = parent.getMin();
			end = parent.getMax();
		}

		Vector2f boundary = new Vector2f(end).sub(origin);
		float baseline = 0;

		int counter = 0;
		for (int i = 0; i < message.length(); i++) {
			char c = message.charAt(i);
			if (c == ' ' || c == '\n') {
				continue;
			}
			characters.add(atlas.glyphMap.get(c));
			if (characters.get(counter).rows > baseline) {
				baseline = characters.get(counter).rows;
			}
			counter++;
		}
		counter = 0;

		// normalize
		baseline /= (float) h;
		
		int vertSize = Background.FLOATS_PER_VERTEX * 4;
		text = new float[vertSize * characters.size()];
		indices = new int[6 * characters.size()];

		float x = 0.0f;
		float y = 0.0f;

		float charIncrement = (float) size;
		charIncrement /= 16;
		charIncrement /= w;

		float rowIncrement = (float) size;
		rowIncrement /= 8;
		rowIncrement /= h;

		float maxHeight = size / (float) h;

		float totalWidth = 0.0f;
		float totalHeight = 0.0f;

		for (int i = 0; i < message.length(); i++) {
			char c = message.charAt(i);
			if (c == ' ') {
				if(x + 4 * charIncrement < boundary.x) {
					x += 4 * charIncrement;
				}else {
					x = 0.0f;
					y -= (maxHeight + rowIncrement);
					totalHeight += maxHeight + rowIncrement;
					maxHeight = size / (float) h;
					totalWidth = boundary.x;
				}
				continue;
			}
			if(c == '\n') {
				x = 0.0f;
				y -= (maxHeight + rowIncrement);
				totalHeight += maxHeight + rowIncrement;
				maxHeight = size / (float) h;
				totalWidth = boundary.x;
				continue;
			}

			Glyph g = characters.get(counter);
			float top = (float) g.top / (float) h;
			float rows = (float) g.rows / (float) h;
			float width = (float) g.width / (float) w;

			if (rows > maxHeight) {
				maxHeight = rows;
			}

			if (x + width + charIncrement > boundary.x) {
				x = 0.0f;
				y -= (maxHeight + rowIncrement);
				totalHeight += maxHeight + rowIncrement;
				maxHeight = size / (float) h;
				totalWidth = boundary.x;
			}

			if (y - rows < boundary.y) {
				// we need to stop adding chars
				totalHeight = boundary.y;
				break;
			}

			float yMin = y - baseline + top;
			float yMax = y - baseline - rows + top;
			float xMin = x;
			float xMax = x + width;

			float a = (float) TextAtlas.ATLAS_SIZE;

			float txMin = (float) g.x / a;
			float txMax = (float) (g.x + g.width) / a;
			float tyMin = (float) (g.y) / a;
			float tyMax = (float) (g.y + g.rows) / a;

			x += width + charIncrement;
			totalWidth += width + charIncrement;

			text[counter * vertSize + 0] = xMin;
			text[counter * vertSize + 1] = yMin;
			text[counter * vertSize + vertSize/4 - 2] = txMin;
			text[counter * vertSize + vertSize/4 - 1] = tyMin;

			text[counter * vertSize + vertSize/4] = xMax;
			text[counter * vertSize + vertSize/4 + 1] = yMin;
			text[counter * vertSize + vertSize/2 - 2] = txMax;
			text[counter * vertSize + vertSize/2 - 1] = tyMin;

			text[counter * vertSize + vertSize/2] = xMax;
			text[counter * vertSize + vertSize/2 + 1] = yMax;
			text[counter * vertSize + 3*vertSize/4 - 2] = txMax;
			text[counter * vertSize + 3*vertSize/4 - 1] = tyMax;

			text[counter * vertSize + 3*vertSize/4] = xMin;
			text[counter * vertSize + 3*vertSize/4 + 1] = yMax;
			text[counter * vertSize + vertSize - 2] = txMin;
			text[counter * vertSize + vertSize - 1] = tyMax;

			indices[counter * 6 + 0] = 0 + 4 * counter;
			indices[counter * 6 + 1] = 1 + 4 * counter;
			indices[counter * 6 + 2] = 2 + 4 * counter;
			indices[counter * 6 + 3] = 2 + 4 * counter;
			indices[counter * 6 + 4] = 0 + 4 * counter;
			indices[counter * 6 + 5] = 3 + 4 * counter;
			counter++;
		}
		
		objMatrix = new Matrix3f();
		if (text_config == TEXT_CONFIGURATIONS.DEFAULT) {
			
			objMatrix.m20(origin.x);
			objMatrix.m21(origin.y);
			
		} else if (text_config == TEXT_CONFIGURATIONS.CENTERED&& totalHeight > boundary.y) {
			if (totalWidth < boundary.x) {
				
				totalHeight += maxHeight;
				objMatrix.m20(origin.x + boundary.x/2 - totalWidth/2);
				objMatrix.m21(origin.y + boundary.y/2 + totalHeight/2);
				
			} else {
				objMatrix.m20(origin.x);
				objMatrix.m21(origin.y + boundary.y/2 + totalHeight/2);
			}
		}
		
		this.totalHeight = totalHeight;
		this.totalWidth = totalWidth;
		
		objMatrix.m22(1.0f);
	}
	
	public void align(int text_config) {
		Vector2f origin = parent.getMin();
		Vector2f boundary = new Vector2f(parent.getMax()).sub(origin);
		
		objMatrix = new Matrix3f();
		if (text_config == TEXT_CONFIGURATIONS.DEFAULT) {
			
			objMatrix.m20(origin.x);
			objMatrix.m21(origin.y);
			
		} else if (text_config == TEXT_CONFIGURATIONS.CENTERED && totalHeight > boundary.y) {
			if (totalWidth < boundary.x) {
				objMatrix.m20(origin.x + boundary.x/2 - totalWidth/2);
				objMatrix.m21(origin.y + boundary.y/2 + totalHeight/2);
			} else {
				objMatrix.m20(origin.x);
				objMatrix.m21(origin.y + boundary.y/2 + totalHeight/2);
			}
		}
		
		objMatrix.m22(1.0f);
		
		
	}

	private void load(int level, int text_config) {
		// create the mesh
		generateTextMesh(text_config);

		// load into buffers
		FloatBuffer vertices = MemoryUtil.memAllocFloat(text.length);
		vertices.put(text);
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
		GL20.glVertexAttribPointer(0, 3, GL20.GL_FLOAT, false, (Background.FLOATS_PER_VERTEX) * Float.BYTES, 0);
		GL20.glVertexAttribPointer(1, 3, GL20.GL_FLOAT, false, (Background.FLOATS_PER_VERTEX) * Float.BYTES, 3 * Float.BYTES);
		GL20.glVertexAttribPointer(2, 2, GL11.GL_FLOAT, true, (Background.FLOATS_PER_VERTEX) * Float.BYTES, (6) * Float.BYTES);

		// unbind and free
		GL30.glBindVertexArray(0);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		MemoryUtil.memFree(vertices);
		MemoryUtil.memFree(indices);
		
		depth = level+1;
	}

	public void reload() {
		cleanup();
		// load into buffers
		FloatBuffer vertices = MemoryUtil.memAllocFloat(text.length);
		vertices.put(text);
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
		GL20.glVertexAttribPointer(0, 3, GL20.GL_FLOAT, false, (Background.FLOATS_PER_VERTEX) * Float.BYTES, 0);
		GL20.glVertexAttribPointer(1, 3, GL20.GL_FLOAT, false, (Background.FLOATS_PER_VERTEX) * Float.BYTES, 3 * Float.BYTES);
		GL20.glVertexAttribPointer(2, 2, GL20.GL_FLOAT, false, (Background.FLOATS_PER_VERTEX) * Float.BYTES, 6 * Float.BYTES);
		// unbind and free
		GL30.glBindVertexArray(0);
		GL20.glBindBuffer(GL20.GL_ARRAY_BUFFER, 0);
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		MemoryUtil.memFree(vertices);
		MemoryUtil.memFree(indices);
	}

	@Override
	public void cleanup() {
		GL20.glDeleteBuffers(vbo);
		GL20.glDeleteBuffers(ebo);
		GL30.glDeleteVertexArrays(vao);
	}

	@Override
	public int getDepth() {
		return depth;
	}

	@Override
	public void render() {
		// bind vao and enable attribute
		GL30.glBindVertexArray(vao);

		// enable the formatting
		GL30.glEnableVertexAttribArray(0);
		GL30.glEnableVertexAttribArray(1);
		GL30.glEnableVertexAttribArray(2);

		// send uniforms
		Launch.renderer.send1i(Util.state, getTextured());
		Launch.renderer.sendVec3(Util.col, new org.joml.Vector3f(textColor));
		
		Matrix4f transform = new Matrix4f();
		transform.set3x3(new Matrix3f(objMatrix));
		Launch.renderer.sendMat4(Util.modelMatrix, transform, false);
		
		
		// bind to ebo
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, ebo);

		// send the texture unit
		GL20.glUniform1i(Launch.renderer.getUniform("image"), 0);

		// bind to texture unit and texture
		GL20.glActiveTexture(GL20.GL_TEXTURE0);

		// bind to the text texture atlas
		GL20.glBindTexture(GL20.GL_TEXTURE_2D, atlas.texture);

		GL11.glDrawElements(GL11.GL_TRIANGLES, indices.length, GL11.GL_UNSIGNED_INT, 0);

		// unbind and disable
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, 0);
		
		GL30.glDisableVertexAttribArray(2);
		GL30.glDisableVertexAttribArray(1);
		GL20.glDisableVertexAttribArray(0);
		GL30.glBindVertexArray(0);
	}

	public void updateColor(float r, float g, float b) {
		textColor.x = r;
		textColor.y = g;
		textColor.z = b;
	}

	public float getR() {
		return textColor.x;
	}

	public float getG() {
		return textColor.y;
	}

	public float getB() {
		return textColor.z;
	}

	public boolean draw() {
		return draw;
	}
	
	public void setVisibility(boolean n) {
		draw = n;
	}
	
	public int getTextured() {
		return Util.STATES.TEXT;
	}

	public static class TEXT_CONFIGURATIONS {
		public static int CENTERED = 1;
		public static int DEFAULT = 0;
	}
}
