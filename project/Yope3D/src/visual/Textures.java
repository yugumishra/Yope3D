package visual;

import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.stb.STBImage;
import org.lwjgl.system.MemoryUtil;

public class Textures {
	//mapping of filepaths to their texture ids
	//this saves on generating repeat textures a lot
	//aka if 500 objects have the same texture
	private static Map<String, Integer> textures;
	
	//static initialization block for the textures
	static {
		textures = new HashMap<String, Integer>();
	}

	// texture loading method
	// loads the textures
	public static void loadTexture(String texture) {
		//if this texture is already contained, no need to generate it
		if(textures.containsKey(texture)) return;
		//is not contained, so we need to generate
		
		// bind to texture unit 0, since that is the unit we operate on
		GL13.glActiveTexture(GL13.GL_TEXTURE0);
		// generate the texture id
		int tid = GL11.glGenTextures();

		// bind to the generated texture object
		GL11.glBindTexture(GL30.GL_TEXTURE_2D, tid);

		// read the image datas
		// create a byte buffer to hold the data
		ByteBuffer imageBuffer;

		// load data using STBImage library
		// but first flip it vertically because image coordinates are different then uv
		// coordinates
		STBImage.stbi_set_flip_vertically_on_load(true);
		// create holders for width, height, and channel number
		int[] width = new int[1];
		int[] height = new int[1];
		int[] channels = new int[1];
		// load the image using STBImage load
		imageBuffer = STBImage.stbi_load(texture, width, height, channels, 4);

		// send to the gpu using texImage2D
		// first parameter defines format (2d)
		// second the storage format
		// next 2 the width and height of the texture
		// 5th the border (always 0)
		// 6th the format (components, not internal format)
		// 7th the data type (float, unsigned byte, double, etc)
		// 8th is the actual data
		GL30.glTexImage2D(GL20.GL_TEXTURE_2D, 0, GL20.GL_RGBA8, width[0], height[0], 0, GL20.GL_RGBA,
				GL20.GL_UNSIGNED_BYTE, imageBuffer);

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

		// free the image buffer
		MemoryUtil.memFree(imageBuffer);
		
		textures.put(texture, tid);
	}
	
	//getter for the texture id
	//used in renderer
	public static int getTexture(String path) {
		return textures.get(path);
	}
	
	//this method clears the texture object specified by the texture
    public void clearTexture(String texture) {
    	int tid = textures.get(texture);
    	//delete texture
    	GL20.glBindTexture(GL20.GL_TEXTURE_2D, tid);
    	GL20.glDeleteTextures(tid);
    	
    	//unbind
    	GL20.glBindTexture(GL20.GL_TEXTURE_2D, 0);
    	
    	textures.remove(texture, tid);
    }
}
