package visual;

import java.util.ArrayList;

import org.lwjgl.glfw.GLFW;

import scripts.Script;
import ui.Label;

public class Loop {
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
	// variable to hold the satrt time of the last pause
	private float lastPause;
	// variable to hold the time of the last frame
	private long lastFrame;
	// variable to hold the delta time for the most recent interval
	private float deltaTime;

	// fps variable
	// encapsulates the current frames per second of the application
	private float fps;

	// constructor
	public Loop(Window w, World world, Renderer renderer) {
		// port over references
		this.window = w;
		this.world = world;
		this.renderer = renderer;
		// initialize last frame
		lastFrame = System.currentTimeMillis();
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
		window.initCamera();
		camera = window.getCamera();
		world.init();
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
			if (frames % 100 == 0) {
				// reupdate the fps
				// the reason we use a separate method as opposed to delta time
				// is to create a smoother average (since delta time varies a lot and could be
				// susceptible to spikes)

				// calculate the difference in time from last update to now
				float timeDiff = (float) (System.currentTimeMillis() - lastTime);
				// reupdate last time
				lastTime = System.currentTimeMillis();
				// frame difference will always be 100, and time diff is measured in
				// milliseconds
				// once you work out the math, the fps is just 10^5 times the inverse in
				// difference
				fps = (100 * 1000) / timeDiff;
			}
			
			// render the current state
			render();
			// update the state
			update();

			// if the window should close, break out of the loop
			// this stops the updating and, by extension, closes the window
			if (window.shouldClose()) {
				break;
			}
		}
		cleanup();
	}

	// this method cleans up everything
	public void cleanup() {
		// cleanup the labels
		for (ArrayList<Label> layer : Launch.window.getUI()) {
			for (Label label : layer) {
				label.cleanup();
			}
		}
		Launch.window.am.cleanup();
		window.cleanup();
		renderer.cleanup();
		world.cleanup();
	}

	// encapsulates all of the updates in the loop in one function
	public void update() {
		reCalcDeltaTime();
		// the window needs to update no matter what, so we will do a pause check only
		// on camera update
		window.update();
		if (window.isPaused() == false) {

			// then we run each scripts update method
			for (int i = 0; i < world.getNumScripts(); i++) {
				Script s = world.getScript(i);
				s.update();
			}

		}
	}

	// encapsulates all of the renderings that are done in the loop
	// the world instance is used to access each mesh, which is then rendered using
	// the renderer
	public void render() {
		// no need for pause check for render loop because window changes are what
		// pause is most likely going to be used for, so it needs to be updating to show
		// the updated window
		// its just that the game state needs to remain constant, so no inputs or camera
		// changes
		// clear the screen before drawing again
		renderer.clear();

		//render the world
		renderer.render(world);
		
		//render ui after (so it appears on top)
		renderer.renderUI();
	}

	// gets the time from the start in seconds
	public float getTime() {
		return (float) GLFW.glfwGetTime();
	}

	// delta time method
	// returns the time from last frame to this one in seconds
	public float deltaTime() {
		return deltaTime;
	}

	// delta time calculator
	// calculates the time from last frame to now in seconds
	public void reCalcDeltaTime() {
		// get time difference
		long diff = System.currentTimeMillis() - lastFrame;
		// convert to seconds
		deltaTime = (float) diff / 1000.0f;
		// reupdate last frame
		lastFrame = System.currentTimeMillis();
	}

	// getter for fps
	public float getFPS() {
		return fps;
	}

	// methods to start and stop paused time
	// used by window class to pause the time
	public void startPause() {
		lastPause = (float) GLFW.glfwGetTime();
	}

	public void stopPause() {
		GLFW.glfwSetTime(lastPause);
	}

	public int frames() {
		return frames;
	}

	public Camera getCamera() {
		return camera;
	}

	public boolean getLMB() {
		return window.getLMB();
	}

	public boolean getRMB() {
		return window.getRMB();
	}

	public boolean getForwardMB() {
		return window.getForwardMB();
	}

	public boolean getBackwardMB() {
		return window.getBackwardMB();
	}

	public boolean getMMB() {
		return window.getMMB();
	}

	public boolean getKey(int key) {
		return window.getInput(key);
	}
}
