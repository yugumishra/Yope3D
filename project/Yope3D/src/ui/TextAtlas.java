package ui;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.HashMap;

import org.lwjgl.PointerBuffer;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;
import org.lwjgl.util.freetype.FT_Bitmap;
import org.lwjgl.util.freetype.FT_Face;
import org.lwjgl.util.freetype.FreeType;

import visual.Launch;
import visual.Textures;
import visual.Util;

public class TextAtlas {
	int texture;
	public static final int ATLAS_SIZE = 512;
	FT_Face face;
	int size;

	public HashMap<Character, Glyph> glyphMap;

	public TextAtlas(String fontFilePath, int size) {
		this(fontFilePath, size,
				"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$%^&*()[]/=-\\{}?+|_'\":;.,`~<>");
	}

	public TextAtlas(String fontFilePath, int size, String msg) {
		this.size = size;
		// read the font file into a buffer
		ByteBuffer data = null;
		File f = null;
		if (Util.isRunningFromJAR()) {

			// create a temp file and have STBI lib read from that
			// get image input stream
			InputStream image = Textures.class.getResourceAsStream("/src/" + fontFilePath.replace('\\', '/'));
			// split string into parts (used later)
			String[] parts = new String[2];
			// find location of the .
			int location = fontFilePath.indexOf(".");
			// split string
			parts[0] = fontFilePath.substring(0, location);
			parts[1] = fontFilePath.substring(location, fontFilePath.length());
			// apply post-processing bc .jar files use '/' separator and not '\\'
			parts[0] = parts[0].replace('\\', '/');
			// plus the initial '/' to signify absolute path
			parts[0] = "/" + parts[0];
			// create variable
			ByteArrayOutputStream os = new ByteArrayOutputStream();
			try {
				// try and read the input stream

				// create temp buffer
				byte[] buffer = new byte[1024];
				// bytes read variable to hold how many bytes read by the input stream currently
				int readBytes;
				// read bytes into output stream
				while ((readBytes = image.read(buffer)) != -1) {
					os.write(buffer, 0, readBytes);
				}

				// place data into array
				byte[] dat = os.toByteArray();

				System.out.println(dat.length);

				// place into buffer
				data = MemoryUtil.memAlloc(dat.length);
				data.put(dat);
				data.position(0);
				
			} catch (IOException e) {
				e.printStackTrace();
			} finally {
				// delete the stream
				try {

					if (image != null)
						image.close();
				} catch (IOException e) {
					System.err.println("streams failed to close");
					System.err.println(e.getMessage());
				}
			}
		} else {
			f = new File("src\\" + fontFilePath);
			try {
				FileInputStream fin = new FileInputStream(f);

				ByteArrayOutputStream os = new ByteArrayOutputStream();

				byte[] buff = new byte[1024];

				int readBytes;

				while ((readBytes = fin.read(buff)) != -1) {
					os.write(buff, 0, readBytes);
				}

				byte[] dat = os.toByteArray();

				data = MemoryUtil.memAlloc(dat.length);
				data.put(dat);
				data.position(0);

				fin.close();
				os.close();
			} catch (FileNotFoundException e) {
				System.err.println("The font file (" + fontFilePath + ") was not found");
				Launch.window.cleanup();
				System.exit(0);
			} catch (IOException e) {
				System.err.println("something went wrong reading the font file");
				System.err.println(e.getMessage());
				Launch.window.cleanup();
				System.exit(0);
			}
		}

		// create the font face
		PointerBuffer pointerToFace = MemoryUtil.memAllocPointer(1);
		int error = FreeType.FT_New_Memory_Face(Launch.window.library.get(), data, 0, pointerToFace);
		Launch.window.library.position(0);

		if (error != 0) {
			System.err.println("Font file could not be processed");
			System.err.println(error);
			Launch.window.cleanup();
			System.exit(0);
		}

		// create face instance
		face = FT_Face.create(pointerToFace.get());

		error = FreeType.FT_Set_Pixel_Sizes(face, 0, size);

		if (error != 0) {
			System.err.println("Fotn size could not be set");
			System.err.println(error);
			Launch.window.cleanup();
			System.exit(0);
		}

		int[][] indices = new int[ATLAS_SIZE][ATLAS_SIZE];
		// initialize indices default value to be -1, to indicate black
		for (int i = 0; i < indices.length; i++)
			for (int j = 0; j < indices[i].length; j++)
				indices[i][j] = -1;
		ByteBuffer[] buffers = new ByteBuffer[msg.length()];

		int x = 0, y = 0, maxHeight = 0;
		glyphMap = new HashMap<Character, Glyph>();

		for (int i = 0; i < msg.length(); i++) {
			FreeType.FT_Load_Char(face, Character.codePointAt(msg, i), FreeType.FT_LOAD_RENDER);

			FT_Bitmap bm = face.glyph().bitmap();
			
			buffers[i] = MemoryUtil.memAlloc(bm.rows() * bm.pitch());
			buffers[i].put(bm.buffer(bm.rows() * bm.pitch()));
			buffers[i].position(0);

			if (bm.width() + x > ATLAS_SIZE) {
				//add one pixel of spacing for insurance
				y += maxHeight + 1;
				x = 0;
				maxHeight = 0;
			}

			Glyph g = new Glyph(face.glyph().bitmap_left(), face.glyph().bitmap_top(), bm.rows(), bm.width(), x, y);

			glyphMap.put(msg.charAt(i), g);

			if (g.rows > maxHeight)
				maxHeight = g.rows;

			for (int yi = y; yi < g.rows + y; yi++) {
				for (int xi = x; xi < g.width + x; xi++) {
					indices[yi][xi] = i;
				}
			}
			//add one pixel of spacing for insurance
			x += g.width + 1;
		}

		ByteBuffer total = MemoryUtil.memAlloc(ATLAS_SIZE * ATLAS_SIZE);

		// now blit
		for (int yi = 0; yi < ATLAS_SIZE; yi++) {
			for (int xi = 0; xi < ATLAS_SIZE; xi++) {
				int index = indices[yi][xi];
				if (index != -1) {
					byte val = buffers[index].get();
					if(val < 1) {
						total.put(val);
					}else {
						total.put((byte) 0);
					}
				} else {
					total.put((byte) 0);
				}
			}
		}

		total.position(0);

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
		GL30.glTexImage2D(GL20.GL_TEXTURE_2D, 0, GL30.GL_R8, ATLAS_SIZE, ATLAS_SIZE, 0, GL20.GL_RED,
				GL20.GL_UNSIGNED_BYTE, total);

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

		MemoryUtil.memFree(data);
		MemoryUtil.memFree(total);
		for (ByteBuffer b : buffers)
			if (b != null)
				MemoryUtil.memFree(b);
	}

	// holder for glyph info
	public class Glyph {
		// numbers describing the positioning of the glyph relative to baseline
		public int left;
		public int top;

		// bitmap rows and width (width horizontal rows vertical)
		public int rows;
		public int width;

		// position on the bitmap
		public int x;
		public int y;

		public Glyph(int l, int t, int r, int w, int x, int y) {
			left = l;
			top = t;
			rows = r;
			width = w;
			this.x = x;
			this.y = y;
		}
	}
}
