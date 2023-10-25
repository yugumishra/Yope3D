package visual;

import java.util.HashMap;
import java.util.Map;

import org.joml.Vector3f;
import org.lwjgl.glfw.GLFW;

import physics.Sphere;

public class Loop {
	// time of the creation of the loop
	// will be used for time tracking (fps calculation)
	private long startTime;
	// window instance to update window
	private Window window;
	// world instance to access meshes
	private World world;
	// renderer instance to render meshes
	private Renderer renderer;
	// frame counter
	// we can use int here because the int storing capacity lasts well over a year
	// of runtime on 60fps
	// no need for extra storage with long
	private int frames;
	// we will display localized fps, not global fps
	// so the below time variables is used for that purpose
	private long lastTime;
	// camera instance to represent camera information
	private Camera camera;
	// mapping of keys to whether or not they are being held currently
	private Map<Integer, Boolean> keyMap;
	// array of all keys
	private int[] keys;
	// variable to hold the amount of time elapsed during the paused period
	private long pausedTime;
	//variable to hold the satrt time of the last pause
	private long lastPause;

	// fps variable
	// encapsulates the current frames per second of the application
	private float fps;

	// constructor
	public Loop(Window w, World world, Renderer renderer) {
		// initialize times
		startTime = System.currentTimeMillis();
		lastTime = System.currentTimeMillis();
		// port over references
		this.window = w;
		this.world = world;
		this.renderer = renderer;
		// initialize key map
		keyMap = new HashMap<Integer, Boolean>();
		// add initial entries for the keys that will be used
		int[] keys = { GLFW.GLFW_KEY_W, GLFW.GLFW_KEY_A, GLFW.GLFW_KEY_S, GLFW.GLFW_KEY_D, GLFW.GLFW_KEY_SPACE,
				GLFW.GLFW_KEY_LEFT_SHIFT, };
		this.keys = keys;
		for (Integer k : keys) {
			keyMap.put(k, false);
		}
		//initialize paused time to 0
		pausedTime = 0;
	}

	// how one starts the loop
	public void start() {
		// first initialize anything necessary
		init();
		// then run
		run();
	}

	// initializes all necessary variables
	public void init() {
		window.init();
		renderer.init();
		world.init();
		window.initCamera();
		camera = window.getCamera();
	}

	// run method
	// main loop of the window
	// basically runs forever until a signal is sent to stop (like hitting the
	// escape key)
	// then stops updating the window which closes it
	public void run() {
		// infinite loop
		while (true) {
			// increment the frames
			frames++;
			if (frames % 1000 == 0) {
				// reupdate the fps
				// calculate the difference in time from last update to now
				float timeDiff = (float) (System.currentTimeMillis() - lastTime);
				// reupdate last time
				lastTime = System.currentTimeMillis();
				// frame difference will always be 1000, and time diff is measured in
				// milliseconds
				// once you work out the math, the fps is just 1 million times the inverse in
				// difference
				fps = (1000 * 1000) / timeDiff;

				// now we update the title to indicate FPS
				window.setTitle(window.getTitle() + " FPS: " + (int) fps);
			}
			// render the current state
			render();
			// update the state
			update();
			// check for inputs
			input();

			// if the window should close, break out of the loop
			// this stops the updating and, by extension, closes the window
			if (window.shouldClose()) {
				break;
			}
		}
		cleanup();
	}

	// this method checks for any inputs and updates the camera based on that
	public void input() {
		//do a pause check
		if(window.isPaused()) return;
		// iterate over each key
		for (Integer key : keys) {
			// get whether or not it is being held/pressed
			boolean is = keyMap.get(key);
			if (is) {
				// then do whatever is necessary
				doInput(key);
			}
		}
	}

	// this method does what action the key needs to do
	// ex w means move forward
	public void doInput(Integer key) {
		float speed = 0.5f / fps;
		switch (key) {
		case GLFW.GLFW_KEY_SPACE:
			camera.addVelocity(new Vector3f(0, speed, 0));
			return;
		case GLFW.GLFW_KEY_LEFT_SHIFT:
			camera.addVelocity(new Vector3f(0, -speed, 0));
			return;
		}
		// the way the inputs are adjusted for rotation is thru polar coordinates
		// the reason for the constant -1 multiplier is in the way angles are measured
		// in the coordinate system we have defined
		float angle = camera.getRotation().y;
		angle *= -1;
		// all keys have radius of speed, but angles 90 degrees offset from each other
		// and all of them have a base angle of angle
		// ex: w = angle + 0
		// ex: a = angle + 90, etc
		// after adding the correct angle offset, we just convert back to rectangular
		// coordinates

		switch (key) {
		case GLFW.GLFW_KEY_W:
			angle -= (float) (Math.PI / 2);
			break;
		case GLFW.GLFW_KEY_A:
			angle += (float) (Math.PI);

			break;
		case GLFW.GLFW_KEY_S:
			angle += (float) (Math.PI / 2);
			break;
		}
		// convert back to rectangular
		float sin = (float) Math.sin(angle);
		float cos = (float) Math.cos(angle);
		// add relevant velocity
		camera.addVelocity(new Vector3f(cos * speed, 0, sin * speed));
	}

	// this method cleans up everything
	public void cleanup() {
		window.cleanup();
		renderer.cleanup();
		world.cleanup();
	}

	// encapsulates all of the updates in the loop in one function
	public void update() {
		// the window needs to update no matter what, so we will do a pause check only
		// on camera update
		window.update();
		if (window.isPaused() == false) {
			camera.update();
			//update light position
			float time = getTime();
			Vector3f newPosition = new Vector3f(10 * (float)Math.cos(time), 3, 10 * (float)Math.sin(time));
			renderer.sendVec3(Util.lightPos, newPosition);
		}
	}

	// encapsulates all of the renderings that are done in the loop
	// the world instance is used to access each mesh, which is then rendered using
	// the renderer
	public void render() {
		//no need for pause check for render loop because window changes are what
		//pause is most likely going to be used for, so it needs to be updating to show
		//the updated window
		//its just that the game state needs to remain constant, so no inputs or camera changes
		// clear the screen before drawing again
		renderer.clear();
		
		// iterate over each mesh
		for (int i = 0; i < world.getNumMeshes(); i++) {
			// grab the specific mesh
			Mesh m = world.getMesh(i);
			// render it
			renderer.render(m);
			
			//add a test rotation
			if(m.getClass().equals(Sphere.class)) {
				m.rotate(new Vector3f(0.0001f, 0.0001f,0.0001f));
			}
		}
	}

	// delta time getter
	// gets the time from the start
	public float getTime() {
		//get time difference
		long diff = System.currentTimeMillis() - startTime;
		//subtract paused time to keep the time accurate
		diff -= pausedTime;
		//convert to float and change to seconds
		float difference = (float) diff / 1000.0f;
		return difference;
	}

	// getter for fps
	public float getFPS() {
		return fps;
	}

	// accessor for key map
	// replaces the old entry with the new entry
	public void updateValue(Integer key, boolean value) {
		keyMap.replace(key, keyMap.get(key), value);
	}

	// accessor for keymap
	// indicates whether or not this key map has the specific key or no
	public boolean hasKey(Integer key) {
		return keyMap.containsKey(key);
	}
	
	//methods to start and stop paused time
	//used by window class to pause the time
	public void startPause() {
		lastPause = System.currentTimeMillis();
	}
	
	public void stopPause() {
		long time = System.currentTimeMillis() - lastPause;
		pausedTime = pausedTime + time;
		lastPause = 0;
	}
}
