package audio;

import org.joml.Vector3f;
import org.lwjgl.openal.AL10;
import org.lwjgl.openal.AL11;

//encapsulates the OpenAL definition of a source
//makes it easier to interact with sources
public class Source {
	// openal source id
	private int sourceID;
	// openal buffer id (id of the buffer of sound)
	private int bufferID;
	// world position & velocity of this source
	private Vector3f position;
	private Vector3f velocity;
	// sound object associated with this source
	private Sound sound;

	public Source(Sound sound, Vector3f position, Vector3f velocity) {
		this.position = position;
		this.velocity = velocity;
		this.sound = sound;

		// gen a dummy source
		sourceID = AL10.alGenSources();
		bufferID = AL10.alGenBuffers();

		// load audio data into a buffer
		AL10.alBufferData(bufferID, (sound.getChannels() == 2) ? (AL10.AL_FORMAT_STEREO16): (AL10.AL_FORMAT_MONO16),
				sound.getData(), sound.getSamplingRate());

		// associate the buffer we just put with the audio source
		AL10.alSourcei(sourceID, AL10.AL_BUFFER, bufferID);

		// set some source properties
		AL10.alSourcef(sourceID, AL10.AL_GAIN, 1.0f);
		AL10.alSourcef(sourceID, AL10.AL_PITCH, 1.0f);

		AL10.alSource3f(sourceID, AL10.AL_POSITION, position.x, position.y, position.z);
		AL10.alSource3f(sourceID, AL10.AL_VELOCITY, velocity.x, velocity.y, velocity.z);
		
		//set the distance model
		AL10.alDistanceModel(AL11.AL_INVERSE_DISTANCE_CLAMPED);
		float globalReferenceDistance = 1;
		float globalMaxDistance = Float.MAX_VALUE;
		
		AL10.alSourcef(sourceID, AL10.AL_REFERENCE_DISTANCE, globalReferenceDistance);
		AL10.alSourcef(sourceID, AL10.AL_MAX_DISTANCE, globalMaxDistance);
	}
	
	public void play() {
		AL10.alSourcePlay(sourceID);
	}
	
	public void stop() {
		AL10.alSourceStop(sourceID);
	}
	
	public void pause() {
		AL10.alSourcePause(sourceID);
	}
	
	public void enableLooping() {
		AL10.alSourcei(sourceID, AL11.AL_LOOPING, AL10.AL_TRUE);
	}
	
	public void offset(int sec) {
		AL10.alSourcef(sourceID, AL11.AL_SEC_OFFSET, sec);
	}
	
	public void setGain(float f) {
		AL10.alSourcef(sourceID, AL10.AL_GAIN, f);
	}
	
	public void setPitch(float f) {
		AL10.alSourcef(sourceID, AL10.AL_PITCH, f);
	}
	
	public float getPitch() {
		return AL10.alGetSourcef(sourceID, AL10.AL_PITCH);
	}
	
	public float getGain() {
		return AL10.alGetSourcef(sourceID, AL10.AL_GAIN);
	}
	
	public void setPosition(Vector3f position) {
		AL10.alSource3f(sourceID, AL10.AL_POSITION, position.x, position.y, position.z);
		this.position = position;
	}
	
	public void setVelocity(Vector3f velocity) {
		AL10.alSource3f(sourceID, AL10.AL_VELOCITY, velocity.x, velocity.y, velocity.z);
		this.velocity = velocity;
	}
	
	public Sound getSound() {
		return sound;
	}
	
	public Vector3f getPosition() {
		return position;
	}
	
	public Vector3f getVelocity() {
		return velocity;
	}
	
	public void rewind() {
		AL10.alSourceRewind(sourceID);
	}
	
	public boolean isPlaying() {
		return AL10.alGetSourcei(sourceID, AL10.AL_SOURCE_STATE) == AL10.AL_PLAYING; 
	}
	
	public void cleanup() {
		AL10.alDeleteSources(sourceID);
		AL10.alDeleteBuffers(bufferID);
	}
}
