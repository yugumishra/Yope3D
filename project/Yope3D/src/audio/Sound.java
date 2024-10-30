package audio;

import java.nio.ShortBuffer;

import org.lwjgl.system.MemoryUtil;

public class Sound {
	private ShortBuffer data;
	private int samplingRate;
	private int channels;
	
	public Sound(ShortBuffer data, int samplingRate, int channels) {
		this.data = data;
		this.samplingRate = samplingRate;
		this.channels = channels;
	}

	public ShortBuffer getData() {
		return data;
	}

	public int getSamplingRate() {
		return samplingRate;
	}

	public int getChannels() {
		return channels;
	}
	
	public static Sound genDefault() {
		int channels = 2;
		int samplingRate = 48000;
		int numSamples = (int) (samplingRate * 2 * 3.1415926535f);
		ShortBuffer data = MemoryUtil.memAllocShort(channels * numSamples);
		for(int i =0; i< numSamples; i++) {
			//calc the point in the sin wave
			float time = (float) i / (float) samplingRate;
			float value = (float) Math.sin(440 * 3.1415926535897f * time);
			int temp = (int) (value * Short.MAX_VALUE);
			short actualValue = (short) (temp);
			data.put((short) (actualValue));
			data.put((short) (actualValue));
		}
		data.flip();
		System.out.println("Hey");
		return new Sound(data, samplingRate, channels);
	}
}
