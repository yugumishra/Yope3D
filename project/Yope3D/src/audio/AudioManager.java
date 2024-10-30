package audio;

import java.nio.ByteBuffer;
import java.nio.IntBuffer;
import java.util.ArrayList;

import org.lwjgl.openal.AL;
import org.lwjgl.openal.ALC;
import org.lwjgl.openal.ALC10;
import org.lwjgl.openal.ALCCapabilities;
import org.lwjgl.system.MemoryUtil;

//this class manages all of the audio related things
public class AudioManager {
	
	// al capabilities + context variables (can be queried later)
	public ALCCapabilities audioCapabilities;
	public long context;
	
	//list of all sound sources
	private ArrayList<Source> allSources;
	private ArrayList<Boolean> wasPlaying;
	
	public AudioManager() {
		//init openal
		long device = ALC10.alcOpenDevice((ByteBuffer)null);
		if(device == MemoryUtil.NULL) { 
			//uh oh
			System.err.println("Failed to open the OpenAL device");
			System.exit(0);
		}
		
		//create al context
		audioCapabilities = ALC.createCapabilities(device);
		context = ALC10.alcCreateContext(device, (IntBuffer) null);
		if(context == MemoryUtil.NULL) {
			System.err.println("Failed to create OpenAL context.");
			System.exit(0);
		}
		
		//make the context current
		ALC10.alcMakeContextCurrent(context);
		AL.createCapabilities(audioCapabilities);
		
		allSources = new ArrayList<Source>();
		wasPlaying = new ArrayList<Boolean>();
	}
	
	public void addSource(Source s) {
		allSources.add(s);
	}
	
	public Source getSource(int i) {
		return allSources.get(i);
	}
	
	public int getNumSources() {
		return allSources.size();
	}
	
	public void cleanup() {
		for(Source s: allSources) s.cleanup();
		ALC10.alcDestroyContext(context);
	}
	
	public void pauseAllMonoSources() {
		for(Source s: allSources) {
			if(s.getSound().getChannels() == 1) {
				wasPlaying.add(s.isPlaying());
				if(s.isPlaying()) s.pause();
			}
		}
	}
	
	public void playAllMonoSources() {
		for(int i =0; i< allSources.size(); i++) {
			Source s = allSources.get(i);
			if(wasPlaying.get(i) && s.getSound().getChannels() == 1) s.play(); 
		}
		wasPlaying = new ArrayList<Boolean>();
	}
}
