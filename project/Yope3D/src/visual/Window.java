package visual;

import java.util.ArrayList;
import java.util.List;

import org.joml.Vector2f;
import org.lwjgl.PointerBuffer;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWImage;
import org.lwjgl.glfw.GLFWImage.Buffer;
import org.lwjgl.opengl.GL;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GLCapabilities;
import org.lwjgl.system.MemoryUtil;
import org.lwjgl.util.freetype.FreeType;

import audio.AudioManager;
import ui.Label;

public class Window {
	// window variables
	// GLFW long handle id for window
	// basically an window ID that lets us use all the window specific functions
	// GLFW has to offer
	private long window;
	// width and height of the specified window in pixels
	private int width;
	private int height;
	// name of the window in the taskbar window
	private String title;
	// camera instance to hold camera information
	private Camera camera;
	// boolean to hold whether or not fullscreen
	private boolean fullscreen;
	// max width and max height of the window
	private int maxWidth, maxHeight;
	// paused variable to keep track of whether or not the window is paused
	private boolean paused;
	// debugging variable to keep trakc whether or not the window is in debug mode
	public boolean debug;

	// handle to free type lib
	public PointerBuffer library;
	
	public int sourceID;

	// list of labels that is the UI
	// this list is sorted via depth
	// so the furthest back labels will be drawn first
	private List<ArrayList<Label>> UI;

	// reference to the GLCapabilities object instantiated by the winodow
	public GLCapabilities capabilities;
	
	//create the audio manager
	public AudioManager am;
	
	public Window(String title, int width, int height) {
		this.title = title;
		this.height = height;
		this.width = width;
		// the constructor is always passed max width and maximum height of the monitor
		// so we can set the max width and max height of the monitor right here
		maxWidth = width;
		maxHeight = height;
		paused = false;
		debug = false;

		// init ui
		UI = new ArrayList<ArrayList<Label>>();
	}

	// initialize this window instance
	// very important as this is needed to create the window and get the display
	// running
	public void init() {
		// initialize glfw and get the result (successful or not)
		boolean res = GLFW.glfwInit();
		// fail information
		if (res == false) {
			System.err.println("GLFW couldn't initialize. Please try again");
			System.exit(0);
		}

		// window hints
		// very important because they indicate certain behaviors that we want our
		// window to do to GLFW
		GLFW.glfwDefaultWindowHints();

		// whether or not the window should be visible
		// for now false (will enable later)
		GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GL11.GL_FALSE);
		// whether or not the window should be resizeable
		GLFW.glfwWindowHint(GLFW.GLFW_RESIZABLE, GL11.GL_TRUE);
		// what version of openGL will be running on the window (major and minor)
		GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR, 4);
		GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR, 3);
		// core profile of opengl
		// basically lets GLFW which opengl it should create the context for (so we can
		// run it)
		GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE, GLFW.GLFW_OPENGL_CORE_PROFILE);

		// now we create the window
		// first three specify width, height, and title
		// the 2nd to last parameter specifies which monitor to go on (if any)
		// the last memoryutil.null specifies that there is no share
		window = GLFW.glfwCreateWindow(width/2, height/2, title, MemoryUtil.NULL, MemoryUtil.NULL);
		// failcheck
		if (window == MemoryUtil.NULL) {
			System.err.println("Something went wrong with Window Creation. Please try again");
			System.exit(0);
		}
		// center the window in the middle of the screen
		// the over 4 is because you also subtract half of the window width and height
		// because of screen coordinates
		GLFW.glfwSetWindowPos(window, width/4, height/4);
		// center cursor position
		GLFW.glfwSetCursorPos(window, width/2, height/2);
		// this makes width and height half of what they are right now
		width /= 2;
		height /= 2;
		// get initial x and y position
		double[] x = new double[2];
		double[] y = new double[2];
		GLFW.glfwGetCursorPos(window, x, y);

		// set not full screen
		fullscreen = false;
		
		//set the icon for the application
		GLFW.glfwSetWindowIcon(window, loadIcons("Assets\\Textures\\tnail.png"));
		
		// key callback for all current keys
		// the 5 parameters are necessary in the lambda expression because that is what
		// the key callback receives
		GLFW.glfwSetKeyCallback(window, (window, key, scancode, action, mods) -> {
			// check if the key was escape and the action was release
			if (key == GLFW.GLFW_KEY_ESCAPE && action == GLFW.GLFW_RELEASE) {
				// then we should set that the window should close to true
				GLFW.glfwSetWindowShouldClose(window, true);
			}

			// full screen key
			// check if f11 was pressed
			if (key == GLFW.GLFW_KEY_F11 && action == GLFW.GLFW_PRESS) {
				// go from windowed to full screen if not full screen
				// or full screen to windowed if full screen
				if (fullscreen == false) {
					// maximize window so when it is returned to normal, the window is windowed full
					// screen
					GLFW.glfwMaximizeWindow(window);
					// set width and height to their right values
					width = maxWidth;
					height = maxHeight;
					// we need to go full screen
					GLFW.glfwSetWindowMonitor(window, GLFW.glfwGetPrimaryMonitor(), 0, 0, width, height,
							GLFW.GLFW_DONT_CARE);
					fullscreen = true;

				} else {
					// we need to go windowed
					GLFW.glfwSetWindowMonitor(window, MemoryUtil.NULL, 0, 0, width, height, GLFW.GLFW_DONT_CARE);
					// reset x and y
					GLFW.glfwGetCursorPos(window, x, y);
					fullscreen = false;
				}
			}

			// pause key
			// this will disable the mouse capturing and allow the user to use their mouse
			// (but no camera updates)
			if (key == GLFW.GLFW_KEY_TAB && action == GLFW.GLFW_PRESS) {
				// we need to pause or unpause
				if(paused) {
					unpause();
				}else {
					pause();
				}
				pauseChange();
			}
		});

		
		
		GLFW.glfwSetMouseButtonCallback(window, (window, button, action, mods) -> {
			for(ArrayList<Label> layer: UI) {
				for(Label l: layer) {
					double[] xx = new double[1];
					double[] yy = new double[1];
					GLFW.glfwGetCursorPos(window, xx, yy);
					l.clicked((int) xx[0], (int) yy[0], button, action);
				}
			}
		});

		// set up callback for the cursor changing virtual position
		// used to increment rotation
		GLFW.glfwSetCursorPosCallback(window, (window, xPos, yPos) -> {
			if (!paused) {
				// cast to float and subtract from initial
				float xDiff = (float) xPos - width;
				float yDiff = (float) yPos - height;
				// update rotation using camera method
				camera.mouseMoved(xDiff, yDiff);
				// reset position back to original
				GLFW.glfwSetCursorPos(window, width, height);
			}
		});

		// setup callback for scrolling
		// all scripts that implement scroll will be updated with this callback
		GLFW.glfwSetScrollCallback(window, (window, xOffset, yOffset) -> {
			for (int i = 0; i < Launch.world.getNumScripts(); i++) {
				Launch.world.getScript(i).scrolled(xOffset, yOffset);
			}
		});

		// make the context
		// this enables us to use opengl (because with the context we can use the opengl
		// library and it will write/affect the window)
		GLFW.glfwMakeContextCurrent(window);

		// but first we need to create the capabilities
		// just because it is affected doesn't mean we can use it just yet (with the
		// capabilities call, we can though)
		capabilities = GL.createCapabilities();
		
		
		//enable alpha testing
		GL11.glEnable(GL11.GL_BLEND);
		GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
		
		// set the background color to be black
		// this is also known as clear color
		// also the first use of a opengl library call
		// from now on we can use the opengl library
		GL11.glClearColor(0, 0, 0, 0);

		// reenable visibility
		GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE, GLFW.GLFW_TRUE);
		GLFW.glfwShowWindow(window);

		// enable video sync for better video results (at the cost of fps)
		GLFW.glfwSwapInterval(1);

		// free type setup
		library = MemoryUtil.memAllocPointer(1);
		int error = FreeType.FT_Init_FreeType(library);
		if (error != 0) {
			System.err.println("Could not initialize FreeType");
			cleanup();
			System.exit(0);
		}
		
		//init ui
		ui.UIInit.init();
		
		pauseChange();
		
		am = new AudioManager();
	}

	// this method represents the initialization of the camera
	// the reason is why the initialization is separate from the rest of the
	// initialization because it requires that the renderer has been initialized
	// something that can only happen after window initialization
	public void initCamera() {
		// create a camera instance
		camera = new Camera(width, height);

		// create a framebuffer (window) size callback to update when the window changes
		GLFW.glfwSetFramebufferSizeCallback(window, (window, width, height) -> {
			// update the viewport after the window change
			GL11.glViewport(0, 0, width, height);
			// update the camera with the new width and height
			camera.windowChanged(width, height);
			// reset width and height
			this.width = width;
			this.height = height;
		});

	}
	
	private static GLFWImage.Buffer loadIcons(String path) {
        //load icon using STB
		Image image = Util.readImage(path, false);

        //create GLFWImage object
        GLFWImage icon = GLFWImage.create();
        icon.set(image.width, image.height, image.buffer);

        //create GLFWImage.Buffer and add icon to it
        GLFWImage.Buffer icons = new Buffer(MemoryUtil.memAlloc(image.buffer.capacity()));
        icons.put(icon);
        icons.flip();

        return icons;
    }

	// getter for camera
	public Camera getCamera() {
		return camera;
	}

	// this method just sets the title of the window
	// mainly used for fps display
	public void setTitle(String newTitle) {
		GLFW.glfwSetWindowTitle(window, newTitle);
	}

	// returns the unaltered, parameter version of the title
	// used for fps display
	public String getTitle() {
		return title;
	}

	// swaps the read and write buffers
	// then looks for any action and processes it
	// essential to window updating
	public void update() {
		GLFW.glfwSwapBuffers(window);
		GLFW.glfwPollEvents();
	}

	// cleanup method
	// cleans up the window
	public void cleanup() {
		GLFW.glfwDestroyWindow(window);
		GLFW.glfwTerminate();
	}

	// getter for width
	public int getWidth() {
		return width;
	}

	// getter for height
	public int getHeight() {
		return height;
	}
	
	public float getAspectRatio() {
		return (float) width/ (float) height;
	}

	// returns if the window should close yet
	public boolean shouldClose() {
		return GLFW.glfwWindowShouldClose(window);
	}

	// getter for paused or not
	public boolean isPaused() {
		return paused;
	}
	
	public void pause() {
		paused = true;
		pauseChange();
		am.pauseAllMonoSources();
	}
	
	public void unpause() {
		paused = false;
		pauseChange();
		am.playAllMonoSources();
	}
	
	private void pauseChange() {
		if (paused) {
			// start the paused timer
			Launch.game.startPause();
			// enable mouse movement
			GLFW.glfwSetInputMode(window, GLFW.GLFW_CURSOR, GLFW.GLFW_CURSOR_NORMAL);
			// set the cursor to its position
			GLFW.glfwSetCursorPos(window, width / 2, height / 2);
		} else {
			// stop the pause timer
			Launch.game.stopPause();
			// set the cursor to the position
			GLFW.glfwSetCursorPos(window, width / 2, height / 2);
			// re disable the mouse movement
			GLFW.glfwSetInputMode(window, GLFW.GLFW_CURSOR, GLFW.GLFW_CURSOR_DISABLED);
		}
	}

	public boolean getLMB() {
		return GLFW.glfwGetMouseButton(window, GLFW.GLFW_MOUSE_BUTTON_1) == GLFW.GLFW_PRESS;
	}

	public boolean getRMB() {
		return GLFW.glfwGetMouseButton(window, GLFW.GLFW_MOUSE_BUTTON_2) == GLFW.GLFW_PRESS;
	}

	public boolean getMMB() {
		return GLFW.glfwGetMouseButton(window, GLFW.GLFW_MOUSE_BUTTON_3) == GLFW.GLFW_PRESS;
	}

	public boolean getBackwardMB() {
		return GLFW.glfwGetMouseButton(window, GLFW.GLFW_MOUSE_BUTTON_4) == GLFW.GLFW_PRESS;
	}

	public boolean getForwardMB() {
		return GLFW.glfwGetMouseButton(window, GLFW.GLFW_MOUSE_BUTTON_5) == GLFW.GLFW_PRESS;
	}

	public boolean getInput(int keycode) {
		return GLFW.glfwGetKey(window, keycode) == GLFW.GLFW_PRESS;
	}

	public long getWindow() {
		return window;
	}

	// pixel to window coordinate transformation
	public Vector2f pixelToWindow(int x, int y, int width, int height) {
		float X = 2 * (float) x / (float) width;
		float Y = 2 * (float) y / (float) height;

		return new Vector2f(X - 2, 2 - Y);
	}

	// same as above but vector2f format
	public Vector2f pixelToWindow(Vector2f vector, int width, int height) {
		float X = 2 * vector.x / (float) width;
		float Y = 2 * vector.y / (float) height;

		return new Vector2f(X - 1, 1 - Y);
	}

	// window to pixel coordinate transformation
	public Vector2f windowToPixel(float x, float y, int width, int height) {
		float X = x + 1;
		X *= (float) width / 2;

		float Y = 1 - y;
		Y *= height / 2;

		// round here to get rid of any floating point error
		return new Vector2f(Math.round(X), Math.round(Y));
	}

	// same as above but vector2f format
	public Vector2f windowToPixel(Vector2f vector, int width, int height) {
		float X = vector.x + 1;
		X *= (float) width / 2;

		float Y = 1 - vector.y;
		Y *= height / 2;

		return new Vector2f(Math.round(X), Math.round(Y));
	}

	public List<ArrayList<Label>> getUI() {
		return UI;
	}

	public void addLabel(Label l) {
		if (UI.isEmpty() || l.getDepth() >= UI.size()) {
			for(int i =0; i< l.getDepth() - UI.size(); i++) {
				UI.add(new ArrayList<Label>());
			}
			ArrayList<Label> finalLayer = new ArrayList<Label>();
			finalLayer.add(l);
			UI.add(finalLayer);
		}else if(l.getDepth() < UI.size()) {
			UI.get(l.getDepth()).add(l);
			
		}
	}
	
	public void clearUI() {
		UI = new ArrayList<ArrayList<Label>>();
	}
	
	public void closeWindow() {
		GLFW.glfwSetWindowShouldClose(window, true);
	}
}
