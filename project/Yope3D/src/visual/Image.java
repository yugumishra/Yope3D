package visual;

import java.nio.ByteBuffer;

public class Image {
	public ByteBuffer buffer;
	public int width;
	public int height;
	
	public Image(ByteBuffer buffer, int width, int height) {
		this.buffer = buffer;
		this.width = width;
		this.height = height;
	}
}
