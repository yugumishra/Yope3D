package audio;

import org.joml.Vector3f;
import org.lwjgl.openal.AL10;

//encapsulates openal listener
//however there is only one listener, so everything is static
public class Listener {
	// position, velocity, orientation
	private static Vector3f position;
	private static Vector3f velocity;
	private static Vector3f forward;
	private static Vector3f up;

	// gain (for everything perceived by the listener)
	private static float gain;

	public static void init(Vector3f position, Vector3f velocity, Vector3f forward, Vector3f up, float gain) {
		Listener.position = position;
		Listener.velocity = velocity;
		Listener.forward = forward;
		Listener.up = up;
		Listener.gain = gain;

		AL10.alListener3f(AL10.AL_POSITION, position.x, position.y, position.z);
		AL10.alListener3f(AL10.AL_VELOCITY, velocity.x, velocity.y, velocity.z);
		float[] orientation = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
		AL10.alListenerfv(AL10.AL_ORIENTATION, orientation);
		AL10.alListenerf(AL10.AL_GAIN, gain);
	}

	public static void setPosition(Vector3f position) {
		AL10.alListener3f(AL10.AL_POSITION, position.x, position.y, position.z);
		Listener.position = position;
	}

	public static void setVelocity(Vector3f velocity) {
		AL10.alListener3f(AL10.AL_VELOCITY, velocity.x, velocity.y, velocity.z);
		Listener.velocity = velocity;
	}

	public static void setGain(float gain) {
		AL10.alListenerf(AL10.AL_GAIN, gain);
		Listener.gain = gain;
	}

	public static void setOrientation(Vector3f forward, Vector3f up) {
		float[] orientation = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
		AL10.alListenerfv(AL10.AL_ORIENTATION, orientation);
		Listener.forward = forward;
		Listener.up = up;
	}
	
	//getters
	public static Vector3f getListenerPosition() {
		return new Vector3f(Listener.position);
	}
	
	public static Vector3f getListenerVelocity() {
		return new Vector3f(Listener.velocity);
	}
	
	public static Vector3f getListenerForward() {
		return new Vector3f(Listener.forward);
	}
	
	public static Vector3f getListenerUp() {
		return new Vector3f(Listener.up);
	}
	
	public static float getGain() {
		return Listener.gain;
	}
}
