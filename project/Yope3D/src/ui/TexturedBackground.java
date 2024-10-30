package ui;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;

import visual.*;

public class TexturedBackground extends Background {
	int texture;

	public TexturedBackground(int level, String textureFilePath) {
		super(1.0f, 1.0f, 1.0f, level);
		
		//
		Image image = Util.readImage(textureFilePath, false);

		// bind to texture unit 0, since that is the unit we operate on
		GL13.glActiveTexture(GL13.GL_TEXTURE0);
		// generate the texture id
		texture = GL11.glGenTextures();

		// bind to the generated texture object
		GL11.glBindTexture(GL30.GL_TEXTURE_2D, texture);

		// send to the gpu using texImage2D
		// first parameter defines format (2d)
		// second the storage format
		// next 2 the width and height of the texture
		// 5th the border (always 0)
		// 6th the format (components, not internal format)
		// 7th the data type (float, unsigned byte, double, etc)
		// 8th is the actual data
		GL30.glTexImage2D(GL20.GL_TEXTURE_2D, 0, GL20.GL_RGBA8, image.width, image.height, 0, GL20.GL_RGBA,
				GL20.GL_UNSIGNED_BYTE, image.buffer);

		// generate the mipmaps on the textures
		// generates automatically the number of mipmaps needed (using the calculation
		// used for numMipmaplevels, which is why that calculation was used)
		GL30.glGenerateMipmap(GL30.GL_TEXTURE_2D);

		// indicate image processing parameters prior to unbinding from the current
		// texture object
		// these 2 calls indicate the wrapping of uv coordinates into 0->1 s and t
		// bounds
		// so the texture repeats
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL13.GL_REPEAT);
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL13.GL_REPEAT);
		// these 2 calls indicate how the pixels should be interpolated through mipmaps
		// this call indicates how mipmaps should be interpolated as the sample space is
		// large relative to the pixel size (aka full texture being called for only a
		// few pixels)
		// aka minimization
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, GL11.GL_LINEAR_MIPMAP_LINEAR);
		// this call indicates how textures (not mipmaps) should be interpolated as the
		// sample space is small relative to the pixel size (15, 16 actual texels being
		// called for the whole screen of pixels)
		// aka magnification
		GL11.glTexParameteri(GL30.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);

		// then unbind from the texture object
		GL11.glBindTexture(GL30.GL_TEXTURE_2D, 0);
	}
	
	@Override
	//redefines mesh to be made of just min and max
	public void redefineMesh() {
		float[] newMesh = new float[FLOATS_PER_VERTEX * 4];
		newMesh[0] = min.x;
		newMesh[1] = min.y;
		
		newMesh[FLOATS_PER_VERTEX] = max.x;
		newMesh[FLOATS_PER_VERTEX + 1] = min.y;
		newMesh[FLOATS_PER_VERTEX + 2] = 1.0f;
		
		
		newMesh[2*FLOATS_PER_VERTEX] = max.x;
		newMesh[2*FLOATS_PER_VERTEX + 1] = max.y;
		newMesh[2*FLOATS_PER_VERTEX + 2] = 1.0f;
		newMesh[2*FLOATS_PER_VERTEX + 3] = 1.0f;
		
		
		newMesh[3*FLOATS_PER_VERTEX] = min.x;
		newMesh[3*FLOATS_PER_VERTEX + 1] = max.y;
		newMesh[3*FLOATS_PER_VERTEX + 3] = 1.0f;
		
			
		mesh = newMesh;
		
		int[] newIndices = {
				0, 1, 2,  2, 0, 3
		};
		indices = newIndices;
	}
	
	@Override
	public int getTextured() {
		return Util.STATES.UI_TEXTURED;
	}
	
	@Override
	public void render() {
		// bind to the vao that contains the vbo and ebo
		GL30.glBindVertexArray(vao);
		
		Launch.renderer.sendVec3(Util.col, new org.joml.Vector3f(r,g,b));
		Launch.renderer.send1i(Util.state, getTextured());
		// enable the formatting
		GL30.glEnableVertexAttribArray(0);
		GL30.glEnableVertexAttribArray(1);
		GL30.glEnableVertexAttribArray(2);
		// bind to the ebo (to draw the indices)
		GL20.glBindBuffer(GL20.GL_ELEMENT_ARRAY_BUFFER, ebo);
		
		// send the texture unit
		GL20.glUniform1i(Launch.renderer.getUniform("image"), 0);
		// bind to texture unit and texture
		GL20.glActiveTexture(GL20.GL_TEXTURE0);
		// bind to the text texture atlas
		GL20.glBindTexture(GL20.GL_TEXTURE_2D, texture);

		
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
		super.cleanup();
		GL20.glBindTexture(GL20.GL_TEXTURE_2D, 0);
		int[] textures = {texture};
		GL20.glDeleteTextures(textures);
	}
}
